#include "openscadparser.h"
#include "expression.h"
#include "scenestringutils.h"

#include <QColor>
#include <QRegularExpression>
#include <QStringList>

namespace {

struct ParsedLine
{
    QString text;
    int number = 0;
};

struct ParserState
{
    QVector<ParsedLine> lines;
    int index = 0;
    int errorLine = -1;
    int nextShapeId = 1;
    int nextTreeNodeId = 1;
    bool insideModuleBody = false;
    QHash<QString, qreal>    variableValues;
    QHash<QString, QVector3D> vectorVariableValues; // e.g. body_size = [28, 28, 4]
    QVector<ShapeNode> shapes;
};

static bool parseFloat(const QString &text, float *value)
{
    bool ok = false;
    const float parsedValue = text.trimmed().toFloat(&ok);
    if (!ok) return false;
    *value = parsedValue;
    return true;
}

static bool parseReal(const QString &text, qreal *value)
{
    bool ok = false;
    const qreal parsedValue = text.trimmed().toDouble(&ok);
    if (!ok) return false;
    *value = parsedValue;
    return true;
}

static bool parseVector3Value(const QString &text, QVector3D *vector)
{
    const QStringList parts = text.split(',');
    if (parts.size() != 3 || !vector)
        return false;

    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (!parseFloat(parts[0], &x) || !parseFloat(parts[1], &y) || !parseFloat(parts[2], &z))
        return false;

    *vector = QVector3D(x, y, z);
    return true;
}

// Splits "a+1, b, max(c,d)" on commas at parenthesis/bracket depth 0.

static bool parseVector3WithExpressions(const QString &text, const QHash<QString, qreal> &varValues, QVector3D *vector, QStringList *expressions = nullptr)
{
    if (!vector)
        return false;
    const QStringList parts = splitAtTopLevelCommas(text);
    if (parts.size() != 3)
        return false;

    qreal vals[3] = {0.0, 0.0, 0.0};
    QStringList exprs;
    exprs.reserve(3);
    for (int i = 0; i < 3; ++i) {
        const QString trimmed = parts[i].trimmed();
        qreal floatVal = 0.0;
        if (parseReal(trimmed, &floatVal)) {
            vals[i] = floatVal;
            exprs.append(trimmed);
        } else {
            qreal exprVal = 0.0;
            if (!ExpressionSyntax::evaluate(trimmed, varValues, &exprVal))
                return false;
            vals[i] = exprVal;
            exprs.append(trimmed);
        }
    }
    *vector = QVector3D(static_cast<float>(vals[0]), static_cast<float>(vals[1]), static_cast<float>(vals[2]));
    if (expressions)
        *expressions = exprs;
    return true;
}

static bool parseColorArgument(const QString &text, QColor *color)
{
    if (!color)
        return false;

    const QString firstArg = splitAtTopLevelCommas(text).value(0).trimmed();
    if (firstArg.isEmpty())
        return false;

    QString literal = firstArg;
    if ((literal.startsWith('"') && literal.endsWith('"'))
        || (literal.startsWith('\'') && literal.endsWith('\''))) {
        literal = literal.mid(1, literal.size() - 2).trimmed();
    }

    QColor parsed(literal);
    if (parsed.isValid()) {
        *color = parsed;
        return true;
    }

    if (firstArg.startsWith('[') && firstArg.endsWith(']')) {
        const QStringList parts = splitAtTopLevelCommas(firstArg.mid(1, firstArg.size() - 2));
        if (parts.size() < 3)
            return false;

        qreal channels[3] = {0.0, 0.0, 0.0};
        for (int i = 0; i < 3; ++i) {
            if (!parseReal(parts[i].trimmed(), &channels[i]))
                return false;
        }

        *color = QColor(qBound(0, qRound(channels[0] * 255.0), 255),
                        qBound(0, qRound(channels[1] * 255.0), 255),
                        qBound(0, qRound(channels[2] * 255.0), 255));
        return true;
    }

    return false;
}

// Parses named and/or positional arguments from an arg-list string.
// E.g. "h=10, r=5, center=true" → {h:"10", r:"5", center:"true"}
//      "10, 5, true" with positionalNames={"h","r","center"} → same result
static QHash<QString, QString> parseNamedArgs(const QString &argsStr,
                                               const QStringList &positionalNames = {})
{
    QHash<QString, QString> result;
    const QStringList parts = splitAtTopLevelCommas(argsStr);
    int positionalIndex = 0;
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (trimmed.isEmpty())
            continue;
        const int eq = trimmed.indexOf('=');
        if (eq > 0) {
            result[trimmed.left(eq).trimmed()] = trimmed.mid(eq + 1).trimmed();
        } else if (positionalIndex < positionalNames.size()) {
            result[positionalNames[positionalIndex++]] = trimmed;
        }
    }
    return result;
}

// Extracts the argument string from "keyword(...);", handling nested parens.
static bool extractCallArgs(const QString &line, const QString &keyword, QString *argsOut)
{
    if (!line.startsWith(keyword))
        return false;
    int i = keyword.size();
    while (i < line.size() && line[i].isSpace()) ++i;
    if (i >= line.size() || line[i] != '(')
        return false;
    // find matching closing paren
    int depth = 0, start = i;
    for (; i < line.size(); ++i) {
        if (line[i] == '(') ++depth;
        else if (line[i] == ')') { if (--depth == 0) break; }
    }
    if (depth != 0)
        return false;
    *argsOut = line.mid(start + 1, i - start - 1).trimmed();
    return true;
}

static SceneDocument::TreeNode makeGroupNode(SceneDocument::TreeNode::Operation operation, ParserState *state)
{
    SceneDocument::TreeNode node;
    node.id = state->nextTreeNodeId++;
    node.type = SceneDocument::TreeNode::Group;
    node.operation = operation;
    return node;
}

static SceneDocument::TreeNode makeVariableNode(const QString &name, const QString &expression, qreal value, ParserState *state)
{
    SceneDocument::TreeNode node;
    node.id = state->nextTreeNodeId++;
    node.type = SceneDocument::TreeNode::Variable;
    node.variableName = name;
    node.variableExpression = expression;
    node.variableValue = value;
    return node;
}

static SceneDocument::TreeNode makeModuleCallNode(int moduleGroupId,
                                                  const QString &moduleName,
                                                  const QString &arguments,
                                                  ParserState *state)
{
    SceneDocument::TreeNode node;
    node.id = state->nextTreeNodeId++;
    node.type = SceneDocument::TreeNode::ModuleCall;
    node.shapeId = moduleGroupId;
    node.moduleName = moduleName;
    node.moduleCallArguments = arguments.trimmed();
    return node;
}

static SceneDocument::TreeNode makePrimitiveNode(const ShapeNode &shape, ParserState *state)
{
    SceneDocument::TreeNode node;
    node.id = state->nextTreeNodeId++;
    node.type = SceneDocument::TreeNode::Primitive;
    node.shapeId = shape.id;
    return node;
}

// Matches: module <name>(<optional params>) {
// Captures: [1]=name, [2]=params content (may be empty)
static bool isModuleDefinitionLine(const QString &line, QString *name, QString *params)
{
    static const QRegularExpression regex(
        "^module\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\(([^)]*)\\)\\s*\\{\\s*$");
    const auto m = regex.match(line);
    if (!m.hasMatch())
        return false;
    if (name)   *name   = m.captured(1).trimmed();
    if (params) *params = m.captured(2).trimmed();
    return true;
}

// Matches any module-style call: <ident>(<args>);
static bool parseModuleCallLine(const QString &line, QString *name = nullptr, QString *args = nullptr)
{
    static const QRegularExpression regex("^([A-Za-z_][A-Za-z0-9_]*)\\s*\\(([^)]*)\\)\\s*;\\s*$");
    const QRegularExpressionMatch match = regex.match(line);
    if (!match.hasMatch())
        return false;

    static const QStringList reservedCalls = {
        QStringLiteral("cube"),
        QStringLiteral("sphere"),
        QStringLiteral("cylinder"),
        QStringLiteral("circle"),
        QStringLiteral("square"),
        QStringLiteral("polygon"),
        QStringLiteral("linear_extrude"),
        QStringLiteral("polyhedron")
    };
    if (reservedCalls.contains(match.captured(1)))
        return false;

    if (name)
        *name = match.captured(1).trimmed();
    if (args)
        *args = match.captured(2).trimmed();
    return true;
}

static bool parseParamExpression(const QString &text, const QHash<QString, qreal> &varValues, qreal *value, QString *expression);

static bool parseOperationLine(const QString &line,
                               SceneDocument::TreeNode::Operation *operation,
                               QVector3D *vector,
                               const QHash<QString, qreal> &varValues,
                               QStringList *expressions = nullptr,
                               QString *loopVariable = nullptr,
                               QString *loopRangeExpression = nullptr,
                               QColor *color = nullptr,
                               QStringList *forExtraPairs = nullptr,
                               bool *extraLinearExtrudeCenter = nullptr,
                               int *extraLinearExtrudeSlices = nullptr)
{
    static const QRegularExpression unionRegex("^union\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression differenceRegex("^difference\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression intersectionRegex("^intersection\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression hullRegex("^hull\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression minkowskiRegex("^minkowski\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression translateRegex("^translate\\s*\\((?:v\\s*=\\s*)?\\[([^\\]]+)\\]\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression rotateRegex("^rotate\\s*\\((?:a\\s*=\\s*)?\\[([^\\]]+)\\]\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression scaleRegex("^scale\\s*\\((?:v\\s*=\\s*)?\\[([^\\]]+)\\]\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression mirrorRegex("^mirror\\s*\\((?:v\\s*=\\s*)?\\[([^\\]]+)\\]\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression forRegex("^for\\s*\\((.+?)\\)\\s*\\{\\s*$");
    static const QRegularExpression intersectionForRegex("^intersection_for\\s*\\((.+?)\\)\\s*\\{\\s*$");
    static const QRegularExpression colorRegex("^color\\s*\\((.*)\\)\\s*\\{\\s*$");
    static const QRegularExpression linearExtrudeRegex("^linear_extrude\\s*\\((.*)\\)\\s*\\{\\s*$");
    static const QRegularExpression resizeRegex("^resize\\s*\\((.*)\\)\\s*\\{\\s*$");

    // intersection_for → intersection() { for() { ... } }
    {
        QRegularExpressionMatch im = intersectionForRegex.match(line);
        if (im.hasMatch()) {
            *operation = SceneDocument::TreeNode::Intersection;
            if (loopVariable || loopRangeExpression || forExtraPairs) {
                const QStringList pairs = splitAtTopLevelCommas(im.captured(1));
                const QString first = pairs.value(0).trimmed();
                const int eq = first.indexOf('=');
                if (eq > 0) {
                    if (loopVariable) *loopVariable = first.left(eq).trimmed();
                    if (loopRangeExpression) *loopRangeExpression = first.mid(eq + 1).trimmed();
                }
                if (forExtraPairs && pairs.size() > 1)
                    *forExtraPairs = pairs.mid(1);
            }
            return true;
        }
    }

    if (unionRegex.match(line).hasMatch()) {
        *operation = SceneDocument::TreeNode::Union;
        return true;
    }
    if (differenceRegex.match(line).hasMatch()) {
        *operation = SceneDocument::TreeNode::Difference;
        return true;
    }
    if (intersectionRegex.match(line).hasMatch()) {
        *operation = SceneDocument::TreeNode::Intersection;
        return true;
    }
    if (hullRegex.match(line).hasMatch()) {
        *operation = SceneDocument::TreeNode::Hull;
        return true;
    }
    if (minkowskiRegex.match(line).hasMatch()) {
        *operation = SceneDocument::TreeNode::Minkowski;
        return true;
    }
    QRegularExpressionMatch colorMatch = colorRegex.match(line);
    if (colorMatch.hasMatch()) {
        *operation = SceneDocument::TreeNode::Color;
        QColor parsedColor(79, 163, 255);
        if (!parseColorArgument(colorMatch.captured(1), &parsedColor))
            return false;
        if (color)
            *color = parsedColor;
        return true;
    }

    QRegularExpressionMatch forMatch = forRegex.match(line);
    if (forMatch.hasMatch()) {
        *operation = SceneDocument::TreeNode::For;
        if (loopVariable || loopRangeExpression || forExtraPairs) {
            const QStringList pairs = splitAtTopLevelCommas(forMatch.captured(1));
            const QString first = pairs.value(0).trimmed();
            const int eq = first.indexOf('=');
            if (eq > 0) {
                if (loopVariable)     *loopVariable     = first.left(eq).trimmed();
                if (loopRangeExpression) *loopRangeExpression = first.mid(eq + 1).trimmed();
            }
            if (forExtraPairs && pairs.size() > 1)
                *forExtraPairs = pairs.mid(1);
        }
        return true;
    }

    QRegularExpressionMatch extrudeMatch = linearExtrudeRegex.match(line);
    if (extrudeMatch.hasMatch()) {
        const auto args = parseNamedArgs(extrudeMatch.captured(1), {"height", "center", "twist", "slices", "scale"});
        const QString heightExpr = args.value(QStringLiteral("height"), QStringLiteral("20"));
        qreal height = 20.0;
        QString heightExprStr;
        if (!parseParamExpression(heightExpr, varValues, &height, &heightExprStr))
            return false;

        const QString centerExpr = args.value(QStringLiteral("center"), QStringLiteral("false"));
        const bool center = (centerExpr.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);

        const QString twistExpr = args.value(QStringLiteral("twist"), QStringLiteral("0"));
        qreal twist = 0.0;
        QString twistExprStr;
        if (!parseParamExpression(twistExpr, varValues, &twist, &twistExprStr))
            return false;

        const QString slicesExpr = args.value(QStringLiteral("slices"), QStringLiteral("0"));
        qreal slices = 0.0;
        QString slicesExprStr;
        if (!parseParamExpression(slicesExpr, varValues, &slices, &slicesExprStr))
            return false;

        const QString scaleExpr = args.value(QStringLiteral("scale"), QStringLiteral("1.0"));
        qreal scaleVal = 1.0;
        QString scaleExprStr;
        if (!parseParamExpression(scaleExpr, varValues, &scaleVal, &scaleExprStr))
            return false;

        *operation = SceneDocument::TreeNode::LinearExtrude;
        if (vector)
            *vector = QVector3D(float(height), float(twist), float(scaleVal));
        if (expressions)
            *expressions = QStringList({heightExprStr, twistExprStr, slicesExprStr, scaleExprStr});

        if (extraLinearExtrudeCenter)
            *extraLinearExtrudeCenter = center;
        if (extraLinearExtrudeSlices)
            *extraLinearExtrudeSlices = int(qRound(slices));
        return true;
    }

    QRegularExpressionMatch resizeMatch = resizeRegex.match(line);
    if (resizeMatch.hasMatch()) {
        *operation = SceneDocument::TreeNode::Resize;
        const QString inner = resizeMatch.captured(1).trimmed();
        const QStringList parts = splitAtTopLevelCommas(inner);
        QString vectorStr = parts.value(0).trimmed();
        QString autoStr;
        if (parts.size() >= 2 && parts[1].trimmed().startsWith(QStringLiteral("auto="), Qt::CaseInsensitive))
            autoStr = parts[1].trimmed().mid(5).trimmed();
        if (!parseVector3WithExpressions(vectorStr, varValues, vector, expressions))
            return false;
        if (expressions) {
            while (expressions->size() < 4)
                expressions->append(QString());
            (*expressions)[3] = autoStr;
        }
        return true;
    }

    QRegularExpressionMatch match = translateRegex.match(line);
    if (match.hasMatch()) {
        *operation = SceneDocument::TreeNode::Translate;
        return parseVector3WithExpressions(match.captured(1), varValues, vector, expressions);
    }
    match = rotateRegex.match(line);
    if (match.hasMatch()) {
        *operation = SceneDocument::TreeNode::Rotate;
        return parseVector3WithExpressions(match.captured(1), varValues, vector, expressions);
    }
    match = scaleRegex.match(line);
    if (match.hasMatch()) {
        *operation = SceneDocument::TreeNode::Scale;
        return parseVector3WithExpressions(match.captured(1), varValues, vector, expressions);
    }
    match = mirrorRegex.match(line);
    if (match.hasMatch()) {
        *operation = SceneDocument::TreeNode::Mirror;
        return parseVector3WithExpressions(match.captured(1), varValues, vector, expressions);
    }

    return false;
}

static bool parseVariableLine(const QString &line, QString *name, QString *expression, qreal *value, QString *errorMessage)
{
    static const QRegularExpression regex("^([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*(.+)\\s*;\\s*$");
    const QRegularExpressionMatch match = regex.match(line);
    if (!match.hasMatch())
        return false;

    const QString expressionText = match.captured(2).trimmed();
    QString expressionError;
    if (!ExpressionSyntax::validate(expressionText, &expressionError)) {
        if (errorMessage) *errorMessage = expressionError;
        return false;
    }

    qreal parsedValue = 0.0;
    parseReal(expressionText, &parsedValue);

    if (name)       *name = match.captured(1);
    if (expression) *expression = expressionText;
    if (value)      *value = parsedValue;
    return true;
}

static bool parseParamExpression(const QString &text, const QHash<QString, qreal> &varValues, qreal *value, QString *expression)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return false;

    qreal floatVal = 0.0;
    if (parseReal(trimmed, &floatVal)) {
        *value = floatVal;
        *expression = trimmed;
        return true;
    }

    qreal exprVal = 0.0;
    if (!ExpressionSyntax::evaluate(trimmed, varValues, &exprVal))
        return false;

    *value = exprVal;
    *expression = trimmed;
    return true;
}

static bool centerFromArgs(const QString &argsStr, bool *centerOut)
{
    // Extract center=true/false from named arguments.
    static const QRegularExpression centerRe("\\bcenter\\s*=\\s*(true|false)\\b");
    QRegularExpressionMatch m = centerRe.match(argsStr);
    if (m.hasMatch()) {
        *centerOut = (m.captured(1) == QLatin1String("true"));
        return true;
    }
    // No center parameter found — default to false (per OpenSCAD spec)
    *centerOut = false;
    return false;
}

// Returns true and fills shape if this line is a cube/sphere/cylinder call (flexible args).
// Returns false if the line is not a primitive call at all.
// Sets *errorMessage and returns false if the line IS a primitive call but has invalid args.
static bool parsePrimitiveLine(const QString &line, ShapeNode *shape, ParserState *state,
                               QString *errorMessage)
{
    QString argsStr;

    // ── Cube ────────────────────────────────────────────────────────────────
    if (extractCallArgs(line, "cube", &argsStr) && line.endsWith(';')) {
        // Accept cube([x, y, z]), cube([x, y, z], center=...), cube(N), cube(N, center=...)
        const QStringList allArgs = splitAtTopLevelCommas(argsStr);
        const QString firstArg = allArgs.value(0).trimmed();
        static const QRegularExpression vecRe("^\\[([^\\]]+)\\]");
        QRegularExpressionMatch m = vecRe.match(argsStr);
        if (m.hasMatch()) {
            const QString vecInner = m.captured(1);
            const QStringList parts = splitAtTopLevelCommas(vecInner);
            if (parts.size() != 3) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("cube on line %1: expected 3 components");
                return false;
            }
            qreal x = 0.0, y = 0.0, z = 0.0;
            QString xe, ye, ze;
            if (!parseParamExpression(parts[0], state->variableValues, &x, &xe)
                || !parseParamExpression(parts[1], state->variableValues, &y, &ye)
                || !parseParamExpression(parts[2], state->variableValues, &z, &ze)) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("cube on line %1: could not parse dimensions");
                return false;
            }
            shape->id = state->nextShapeId++;
            shape->type = ShapeNode::Cube;
            shape->name = QStringLiteral("Cube %1").arg(shape->id);
            shape->size = QVector3D(x, y, z);
            shape->parameterExpressions = QStringList({xe, ye, ze});
            centerFromArgs(argsStr, &shape->center);
            return true;
        }
        // Vector variable: cube(body_size, center=true)
        if (state->vectorVariableValues.contains(firstArg)) {
            const QVector3D v = state->vectorVariableValues[firstArg];
            shape->id = state->nextShapeId++;
            shape->type = ShapeNode::Cube;
            shape->name = QStringLiteral("Cube %1").arg(shape->id);
            shape->size = v;
            shape->parameterExpressions = QStringList({
                QString::number(v.x()), QString::number(v.y()), QString::number(v.z())});
            centerFromArgs(argsStr, &shape->center);
            return true;
        }
        // Single scalar: cube(10) → 10×10×10
        qreal scalar = 0.0;
        QString scalarExpr;
        if (parseParamExpression(firstArg, state->variableValues, &scalar, &scalarExpr)) {
            shape->id = state->nextShapeId++;
            shape->type = ShapeNode::Cube;
            shape->name = QStringLiteral("Cube %1").arg(shape->id);
            shape->size = QVector3D(scalar, scalar, scalar);
            shape->parameterExpressions = QStringList({scalarExpr, scalarExpr, scalarExpr});
            centerFromArgs(argsStr, &shape->center);
            return true;
        }
        if (errorMessage)
            *errorMessage = QStringLiteral("cube on line %1: expected [x, y, z], size variable, or number");
        return false;
    }

    // ── Sphere ──────────────────────────────────────────────────────────────
    if (extractCallArgs(line, "sphere", &argsStr) && line.endsWith(';')) {
        // Accept sphere(r=5), sphere(5), sphere(d=10)
        const auto args = parseNamedArgs(argsStr, {"r"});
        const bool useDiameter = args.value("r").isEmpty() && args.contains("d");
        const QString paramStr = useDiameter ? args["d"] : args.value("r");
        if (paramStr.isEmpty()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("sphere on line %1: missing radius");
            return false;
        }
        qreal val = 0.0;
        QString valExpr;
        if (!parseParamExpression(paramStr, state->variableValues, &val, &valExpr)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("sphere on line %1: could not parse radius");
            return false;
        }
        shape->id = state->nextShapeId++;
        shape->type = ShapeNode::Sphere;
        shape->name = QStringLiteral("Sphere %1").arg(shape->id);
        shape->usesDiameter = useDiameter;
        shape->radius = useDiameter ? static_cast<float>(val / 2.0) : static_cast<float>(val);
        shape->parameterExpressions = QStringList({valExpr});
        return true;
    }

    if (extractCallArgs(line, "circle", &argsStr) && line.endsWith(';')) {
        const auto args = parseNamedArgs(argsStr, {"r"});
        const bool useDiameter = args.value("r").isEmpty() && args.contains("d");
        const QString paramStr = useDiameter ? args["d"] : args.value("r");
        if (paramStr.isEmpty()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("circle on line %1: missing radius");
            return false;
        }
        qreal val = 0.0;
        QString valExpr;
        if (!parseParamExpression(paramStr, state->variableValues, &val, &valExpr)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("circle on line %1: could not parse radius");
            return false;
        }
        shape->id = state->nextShapeId++;
        shape->type = ShapeNode::Circle;
        shape->name = QStringLiteral("Circle %1").arg(shape->id);
        shape->usesDiameter = useDiameter;
        shape->radius = useDiameter ? static_cast<float>(val / 2.0) : static_cast<float>(val);
        shape->parameterExpressions = QStringList({valExpr});
        return true;
    }

    // ── Cylinder / Cone ─────────────────────────────────────────────────────
    if (extractCallArgs(line, "square", &argsStr) && line.endsWith(';')) {
        const auto args = parseNamedArgs(argsStr, {"size", "center"});
        QString sizeStr = args.value(QStringLiteral("size"));
        if (sizeStr.isEmpty())
            sizeStr = splitAtTopLevelCommas(argsStr).value(0).trimmed();
        QStringList parts;
        if (sizeStr.startsWith('[') && sizeStr.endsWith(']'))
            parts = splitAtTopLevelCommas(sizeStr.mid(1, sizeStr.size() - 2));
        else if (!sizeStr.isEmpty())
            parts = QStringList() << sizeStr << sizeStr;
        if (parts.size() != 2) {
            if (errorMessage)
                *errorMessage = QStringLiteral("square on line %1: expected size or [x, y]");
            return false;
        }
        qreal x = 0.0, y = 0.0;
        QString xe, ye;
        if (!parseParamExpression(parts[0], state->variableValues, &x, &xe)
            || !parseParamExpression(parts[1], state->variableValues, &y, &ye)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("square on line %1: could not parse size");
            return false;
        }
        shape->id = state->nextShapeId++;
        shape->type = ShapeNode::Square;
        shape->name = QStringLiteral("Square %1").arg(shape->id);
        shape->size = QVector3D(x, y, 0.1f);
        shape->parameterExpressions = QStringList({xe, ye});
        centerFromArgs(argsStr, &shape->center);
        return true;
    }

    if (extractCallArgs(line, "polygon", &argsStr) && line.endsWith(';')) {
        const QString normalized = argsStr.trimmed();
        const int pointsKey = normalized.indexOf(QStringLiteral("points"));
        const int listStart = normalized.indexOf(QLatin1Char('['), pointsKey >= 0 ? pointsKey : 0);
        int depth = 0;
        int listEnd = -1;
        for (int i = listStart; i >= 0 && i < normalized.size(); ++i) {
            if (normalized[i] == QLatin1Char('[')) ++depth;
            else if (normalized[i] == QLatin1Char(']')) {
                --depth;
                if (depth == 0) { listEnd = i; break; }
            }
        }
        if (listStart < 0 || listEnd <= listStart) {
            if (errorMessage)
                *errorMessage = QStringLiteral("polygon on line %1: missing or malformed points");
            return false;
        }
        const QString listText = normalized.mid(listStart + 1, listEnd - listStart - 1);
        QVector<QVector3D> points;
        int pos = 0;
        while (pos < listText.size()) {
            const int start = listText.indexOf(QLatin1Char('['), pos);
            if (start < 0) break;
            const int end = listText.indexOf(QLatin1Char(']'), start + 1);
            if (end < 0) break;
            const QStringList parts = splitAtTopLevelCommas(listText.mid(start + 1, end - start - 1));
            if (parts.size() != 2) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("polygon on line %1: expected [x, y] points");
                return false;
            }
            qreal x = 0.0, y = 0.0;
            QString xe, ye;
            if (!parseParamExpression(parts[0], state->variableValues, &x, &xe)
                || !parseParamExpression(parts[1], state->variableValues, &y, &ye)) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("polygon on line %1: could not parse point");
                return false;
            }
            points.append(QVector3D(x, y, 0.0f));
            pos = end + 1;
        }
        if (points.size() < 3) {
            if (errorMessage)
                *errorMessage = QStringLiteral("polygon on line %1: expected at least 3 points");
            return false;
        }
        shape->id = state->nextShapeId++;
        shape->type = ShapeNode::Polygon2D;
        shape->name = QStringLiteral("Polygon %1").arg(shape->id);
        shape->polyhedronPoints = points;
        return true;
    }

    if (extractCallArgs(line, "cylinder", &argsStr) && line.endsWith(';')) {
        const auto args = parseNamedArgs(argsStr, {"h", "r", "r1", "r2", "center"});
        if (!args.contains("h")) {
            if (errorMessage)
                *errorMessage = QStringLiteral("cylinder on line %1: missing h");
            return false;
        }
        qreal h = 0.0;
        QString he;
        if (!parseParamExpression(args["h"], state->variableValues, &h, &he)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("cylinder on line %1: could not parse h");
            return false;
        }

        // Cone / frustum — r1/r2 or d1/d2
        const bool hasConeR = args.contains("r1") || args.contains("r2");
        const bool hasConeD = args.contains("d1") || args.contains("d2");
        if (hasConeR || hasConeD) {
            const bool useD1 = !args.contains("r1") && args.contains("d1");
            const bool useD2 = !args.contains("r2") && args.contains("d2");
            const QString p1Str = useD1 ? args.value("d1", QStringLiteral("0"))
                                        : args.value("r1", QStringLiteral("0"));
            const QString p2Str = useD2 ? args.value("d2", QStringLiteral("0"))
                                        : args.value("r2", QStringLiteral("0"));
            qreal v1 = 0.0, v2 = 0.0;
            QString e1, e2;
            if (!parseParamExpression(p1Str, state->variableValues, &v1, &e1)
                || !parseParamExpression(p2Str, state->variableValues, &v2, &e2)) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("cylinder on line %1: could not parse r1/d1 or r2/d2");
                return false;
            }
            shape->id = state->nextShapeId++;
            shape->type = ShapeNode::Cone;
            shape->name = QStringLiteral("Cone %1").arg(shape->id);
            shape->height  = static_cast<float>(h);
            shape->usesD1  = useD1;
            shape->usesD2  = useD2;
            shape->radius  = useD1 ? static_cast<float>(v1 / 2.0) : static_cast<float>(v1);
            shape->radius2 = useD2 ? static_cast<float>(v2 / 2.0) : static_cast<float>(v2);
            // parameterExpressions order: R1/D1=0, R2/D2=1, H=2
            shape->parameterExpressions = QStringList({e1, e2, he});
            centerFromArgs(argsStr, &shape->center);
            return true;
        }

        // Regular cylinder: cylinder(h=..., r=...) or cylinder(h=..., d=...)
        const bool useDiameter = !args.contains("r") && args.contains("d");
        const QString rStr = useDiameter ? args.value("d") : args.value("r");
        if (rStr.isEmpty()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("cylinder on line %1: missing r (or d, r1/r2, d1/d2)");
            return false;
        }
        qreal rVal = 0.0;
        QString rExpr;
        if (!parseParamExpression(rStr, state->variableValues, &rVal, &rExpr)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("cylinder on line %1: could not parse r or d");
            return false;
        }
        shape->id = state->nextShapeId++;
        shape->type = ShapeNode::Cylinder;
        shape->name = QStringLiteral("Cylinder %1").arg(shape->id);
        shape->height = static_cast<float>(h);
        shape->usesDiameter = useDiameter;
        shape->radius = useDiameter ? static_cast<float>(rVal / 2.0) : static_cast<float>(rVal);
        // parameterExpressions order: R/D=index 0, H=index 1
        shape->parameterExpressions = QStringList({rExpr, he});
        centerFromArgs(argsStr, &shape->center);
        return true;
    }

    return false; // not a primitive line at all
}

struct PolyhedronData
{
    QVector<QVector3D> points;
    QVector<QVector<int>> faces;
};

static bool parsePolyhedron(const QString &line, PolyhedronData *data, QString *errorMessage)
{
    QString argsStr;
    if (!extractCallArgs(line, "polyhedron", &argsStr) || !line.endsWith(';'))
        return false;

    const auto args = parseNamedArgs(argsStr, {"points", "faces"});
    const QString pointsStr = args.value(QStringLiteral("points"));
    const QString facesStr  = args.value(QStringLiteral("faces"));
    if (pointsStr.isEmpty() || facesStr.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("polyhedron requires both points and faces arguments");
        return false;
    }

    // Strip outer brackets of the array
    auto stripBrackets = [](const QString &s) -> QString {
        QString t = s.trimmed();
        if (t.size() >= 2 && t.startsWith('[') && t.endsWith(']'))
            return t.mid(1, t.size() - 2).trimmed();
        return t;
    };

    // ── Parse points ───────────────────────────────────────────────────
    const QString ptsInner = stripBrackets(pointsStr);
    const QStringList ptTokens = splitAtTopLevelCommas(ptsInner);
    QVector<QVector3D> points;
    for (const QString &token : ptTokens) {
        const QString inner = stripBrackets(token);
        if (inner.isEmpty()) continue;
        const QStringList coords = splitAtTopLevelCommas(inner);
        if (coords.size() != 3) {
            if (errorMessage)
                *errorMessage = QStringLiteral("polyhedron: each point must have 3 coordinates");
            return false;
        }
        float f[3];
        if (!parseFloat(coords[0], &f[0]) || !parseFloat(coords[1], &f[1]) || !parseFloat(coords[2], &f[2]))
            return false;
        points.append(QVector3D(f[0], f[1], f[2]));
    }

    // ── Parse faces ────────────────────────────────────────────────────
    const QString fcsInner = stripBrackets(facesStr);
    const QStringList fcTokens = splitAtTopLevelCommas(fcsInner);
    QVector<QVector<int>> faces;
    for (const QString &token : fcTokens) {
        const QString inner = stripBrackets(token);
        if (inner.isEmpty()) continue;
        const QStringList idxStrs = splitAtTopLevelCommas(inner);
        QVector<int> face;
        for (const QString &is : idxStrs) {
            bool ok = false;
            const int idx = is.trimmed().toInt(&ok);
            if (!ok) return false;
            face.append(idx);
        }
        if (face.size() >= 3)
            faces.append(face);
    }

    if (data) {
        data->points = points;
        data->faces  = faces;
    }
    return true;
}

static void buildPolyhedronGroup(const PolyhedronData &data, ParserState *state,
                                  SceneDocument::TreeNode *parent)
{
    SceneDocument::TreeNode group = makeGroupNode(SceneDocument::TreeNode::Polyhedron, state);
    parent->children.append(group);
    SceneDocument::TreeNode &grp = parent->children.last();

    for (const QVector3D &pt : data.points) {
        ShapeNode shape;
        shape.id = state->nextShapeId++;
        shape.type = ShapeNode::Point3D;
        shape.name = QStringLiteral("Point %1").arg(shape.id);
        shape.position = pt;
        shape.parameterExpressions = QStringList()
            << QString::number(pt.x(), 'g') << QString::number(pt.y(), 'g') << QString::number(pt.z(), 'g');
        state->shapes.append(shape);
        grp.children.append(makePrimitiveNode(shape, state));
    }

    for (const QVector<int> &face : data.faces) {
        ShapeNode shape;
        shape.id = state->nextShapeId++;
        shape.type = ShapeNode::Face3D;
        shape.name = QStringLiteral("Face %1").arg(shape.id);
        shape.polyhedronFaces.append(face);
        QStringList exprs;
        exprs.append(QString::number(face.size()));
        for (int idx : face)
            exprs.append(QString::number(idx));
        shape.parameterExpressions = exprs;
        state->shapes.append(shape);
        grp.children.append(makePrimitiveNode(shape, state));
    }
}

// Like parseOperationLine but matches translate/rotate/scale WITHOUT a trailing "{".
// Used to handle the OpenSCAD brace-free single-child shorthand:
//   translate([x, y, 0])
//     sphere(r=5);
static bool parseBraceFreeOperationLine(const QString &line,
                                        SceneDocument::TreeNode::Operation *operation,
                                        QVector3D *vector,
                                        const QHash<QString, qreal> &varValues,
                                        QStringList *expressions)
{
    static const QRegularExpression translateRe("^translate\\s*\\((?:v\\s*=\\s*)?\\[([^\\]]+)\\]\\s*\\)\\s*$");
    static const QRegularExpression rotateRe("^rotate\\s*\\((?:a\\s*=\\s*)?\\[([^\\]]+)\\]\\s*\\)\\s*$");
    static const QRegularExpression scaleRe("^scale\\s*\\((?:v\\s*=\\s*)?\\[([^\\]]+)\\]\\s*\\)\\s*$");
    static const QRegularExpression mirrorRe("^mirror\\s*\\((?:v\\s*=\\s*)?\\[([^\\]]+)\\]\\s*\\)\\s*$");

    QRegularExpressionMatch m = translateRe.match(line);
    if (m.hasMatch()) {
        *operation = SceneDocument::TreeNode::Translate;
        return parseVector3WithExpressions(m.captured(1), varValues, vector, expressions);
    }
    m = rotateRe.match(line);
    if (m.hasMatch()) {
        *operation = SceneDocument::TreeNode::Rotate;
        return parseVector3WithExpressions(m.captured(1), varValues, vector, expressions);
    }
    m = scaleRe.match(line);
    if (m.hasMatch()) {
        *operation = SceneDocument::TreeNode::Scale;
        return parseVector3WithExpressions(m.captured(1), varValues, vector, expressions);
    }
    m = mirrorRe.match(line);
    if (m.hasMatch()) {
        *operation = SceneDocument::TreeNode::Mirror;
        return parseVector3WithExpressions(m.captured(1), varValues, vector, expressions);
    }
    return false;
}

// Returns true if the line starts with a keyword we know how to parse.
// Used to distinguish "known-but-invalid" from "completely unknown".
static bool startsWithKnownKeyword(const QString &line)
{
    static const QStringList known = {
        "translate", "rotate", "scale", "mirror",
        "union", "difference", "intersection", "hull", "minkowski", "for", "intersection_for", "let", "if", "else", "color", "linear_extrude",
        "cube", "sphere", "cylinder", "circle", "polyhedron"
    };
    for (const QString &kw : known)
        if (line.startsWith(kw))
            return true;
    return false;
}

static ShapeNode::BooleanMode booleanModeForChild(const SceneDocument::TreeNode &parent, int childIndex)
{
    if (parent.operation == SceneDocument::TreeNode::Difference && childIndex > 0)
        return ShapeNode::Subtract;
    if (parent.operation == SceneDocument::TreeNode::Intersection)
        return ShapeNode::Intersect;
    return ShapeNode::Add;
}

static void applyBooleanModes(SceneDocument::TreeNode *node, QVector<ShapeNode> *shapes, ShapeNode::BooleanMode inheritedMode)
{
    if (!node || !shapes)
        return;

    if (node->type == SceneDocument::TreeNode::Primitive) {
        for (ShapeNode &shape : *shapes) {
            if (shape.id == node->shapeId) {
                shape.booleanMode = inheritedMode;
                break;
            }
        }
        return;
    }

    if (node->type == SceneDocument::TreeNode::Variable)
        return;

    for (int i = 0; i < node->children.size(); ++i)
        applyBooleanModes(&node->children[i], shapes, booleanModeForChild(*node, i));
}

static bool parseBlock(ParserState *state,
                       SceneDocument::TreeNode *parent,
                       bool stopAtBrace,
                       QString *errorMessage)
{
    while (state->index < state->lines.size()) {
        const ParsedLine current = state->lines[state->index++];
        QString line = current.text;

        // OpenSCAD * modifier → skip disabled statement entirely
        if (!line.isEmpty() && line[0] == QLatin1Char('*')) {
            if (line.trimmed().endsWith(QLatin1Char('{'))) {
                SceneDocument::TreeNode _dummy;
                _dummy.type = SceneDocument::TreeNode::Group;
                _dummy.operation = SceneDocument::TreeNode::Union;
                parseBlock(state, &_dummy, true, nullptr);
            }
            continue;
        }

        // Strip OpenSCAD debug modifiers (#, %, !)
        while (!line.isEmpty() && (line[0] == QLatin1Char('#') || line[0] == QLatin1Char('%') || line[0] == QLatin1Char('!')))
            line = line.mid(1).trimmed();

        if (line == QStringLiteral("}"))
            return stopAtBrace;

        // ── Multi-line module call accumulator ────────────────────────────
        // If a line starts with identifier+( but doesn't end with ; or {,
        // accumulate subsequent lines until ); is found.
        {
            static const QRegularExpression mlRe("^[A-Za-z_][A-Za-z0-9_]*\\s*\\(");
            if (line.contains(mlRe)
                && !line.trimmed().endsWith(QLatin1Char(';'))
                && !line.trimmed().endsWith(QLatin1Char('{'))) {
                QString accLine = line;
                while (state->index < state->lines.size()) {
                    const ParsedLine &next = state->lines[state->index];
                    ++state->index;
                    accLine += next.text;
                    if (next.text.indexOf(QStringLiteral(");")) >= 0) {
                        line = accLine.trimmed();
                        break;
                    }
                }
            }
        }

        QString callName, callArgs;
        if (parseModuleCallLine(line, &callName, &callArgs)) {
            parent->children.append(makeModuleCallNode(0, callName, callArgs, state));
            continue;
        }

        // ── Vector variable (e.g. dims = [10, 5, 2]) — skip silently but store ──
        {
            static const QRegularExpression vecVarRe(
                "^([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*\\[([^\\]]+)\\]\\s*;\\s*$");
            const auto vm = vecVarRe.match(line);
            if (vm.hasMatch()) {
                const QStringList vparts = splitAtTopLevelCommas(vm.captured(2));
                if (vparts.size() == 3) {
                    qreal vx = 0, vy = 0, vz = 0;
                    QString vxe, vye, vze;
                    if (parseParamExpression(vparts[0].trimmed(), state->variableValues, &vx, &vxe)
                        && parseParamExpression(vparts[1].trimmed(), state->variableValues, &vy, &vye)
                        && parseParamExpression(vparts[2].trimmed(), state->variableValues, &vz, &vze))
                        state->vectorVariableValues[vm.captured(1)] = QVector3D(vx, vy, vz);
                }
                continue;
            }
        }

        // ── Variable assignment ───────────────────────────────────────────
        QString variableName, variableExpression, variableError;
        qreal variableValue = 0.0;
        if (parseVariableLine(line, &variableName, &variableExpression, &variableValue, &variableError)) {
            const bool allowedScope = parent->type == SceneDocument::TreeNode::Group
                                      && (parent->operation == SceneDocument::TreeNode::Module
                                          || parent->operation == SceneDocument::TreeNode::Scene);
            if (!allowedScope) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("Variables must be at top scope or directly inside a module on line %1.").arg(current.number);
                state->errorLine = current.number;
                return false;
            }
            qreal evaluatedValue = 0.0;
            if (ExpressionSyntax::evaluate(variableExpression, state->variableValues, &evaluatedValue))
                variableValue = evaluatedValue;
            state->variableValues[variableName] = variableValue;
            parent->children.append(makeVariableNode(variableName, variableExpression, variableValue, state));
            continue;
        } else if (!variableError.isEmpty()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Invalid expression on line %1: %2").arg(current.number).arg(variableError);
            state->errorLine = current.number;
            return false;
        }

        // ── let (x = expr) { ... } ── scoped variables, no tree node ──────
        {
            static const QRegularExpression letRe("^let\\s*\\((.+)\\)\\s*\\{\\s*$");
            QRegularExpressionMatch lm = letRe.match(line);
            if (lm.hasMatch()) {
                const QStringList pairs = splitAtTopLevelCommas(lm.captured(1));
                QVector<QPair<QString, qreal>> letScope;
                for (const QString &pair : pairs) {
                    const int eq = pair.indexOf('=');
                    if (eq <= 0) continue;
                    const QString var = pair.left(eq).trimmed();
                    const QString expr = pair.mid(eq + 1).trimmed();
                    qreal val = 0.0;
                    if (ExpressionSyntax::evaluate(expr, state->variableValues, &val)) {
                        const bool hadPrev = state->variableValues.contains(var);
                        letScope.append({var, hadPrev ? state->variableValues[var] : qQNaN()});
                        state->variableValues[var] = val;
                    }
                }
                const bool ok = parseBlock(state, parent, true, errorMessage);
                for (int li = letScope.size() - 1; li >= 0; --li) {
                    const auto &ls = letScope[li];
                    if (std::isnan(ls.second))
                        state->variableValues.remove(ls.first);
                    else
                        state->variableValues[ls.first] = ls.second;
                }
                if (!ok) return false;
                continue;
            }
        }

        // ── if (cond) { ... } else { ... } ── parse-time evaluation ──────
        {
            static const QRegularExpression ifRe("^if\\s*\\((.+)\\)\\s*\\{\\s*$");
            static const QRegularExpression elseRe("^else\\s*\\{\\s*$");
            static const QRegularExpression elseIfRe("^else\\s+if\\s*\\((.+)\\)\\s*\\{\\s*$");
            QRegularExpressionMatch im = ifRe.match(line);
            if (im.hasMatch()) {
                qreal condVal = 0.0;
                bool condTrue = state->insideModuleBody
                    || (ExpressionSyntax::evaluate(im.captured(1), state->variableValues, &condVal) && (condVal != 0.0));

                auto skipBlock = [state](QString *errMsg) -> bool {
                    SceneDocument::TreeNode _d;
                    _d.type = SceneDocument::TreeNode::Group;
                    _d.operation = SceneDocument::TreeNode::Union;
                    return parseBlock(state, &_d, true, errMsg);
                };

                if (condTrue) {
                    if (!parseBlock(state, parent, true, errorMessage))
                        return false;
                } else {
                    if (!skipBlock(nullptr))
                        return false;
                }

                // Check for else / else-if chain
                while (state->index < state->lines.size()) {
                    const QString next = state->lines[state->index].text;
                    QRegularExpressionMatch eif = elseIfRe.match(next);
                    if (eif.hasMatch()) {
                        ++state->index;
                        qreal eifVal = 0.0;
                        bool eifTrue = ExpressionSyntax::evaluate(eif.captured(1), state->variableValues, &eifVal) && (eifVal != 0.0);
                        if (!condTrue && eifTrue) {
                            if (!parseBlock(state, parent, true, errorMessage))
                                return false;
                            condTrue = true; // prevents further else branches
                        } else {
                            if (!skipBlock(nullptr))
                                return false;
                        }
                        continue;
                    }
                    if (elseRe.match(next).hasMatch()) {
                        ++state->index;
                        if (!condTrue) {
                            if (!parseBlock(state, parent, true, errorMessage))
                                return false;
                        } else {
                            if (!skipBlock(nullptr))
                                return false;
                        }
                    }
                    break;
                }
                continue;
            }
        }

        // ── Group / transform / for ───────────────────────────────────────
        SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
        QVector3D transformVector;
        QStringList transformExpressions;
        QString loopVariable, loopRangeExpression;
        QStringList forExtraPairs;
        QColor operationColor;
        bool extrudeCenter = false;
        int extrudeSlices = 0;
        if (parseOperationLine(line, &operation, &transformVector, state->variableValues,
                               &transformExpressions, &loopVariable, &loopRangeExpression, &operationColor, &forExtraPairs,
                               &extrudeCenter, &extrudeSlices)) {
            SceneDocument::TreeNode group = makeGroupNode(operation, state);
            if (operation == SceneDocument::TreeNode::Translate)
                group.position = transformVector;
            else if (operation == SceneDocument::TreeNode::Rotate)
                group.rotation = transformVector;
            else if (operation == SceneDocument::TreeNode::Scale)
                group.scale = transformVector;
            else if (operation == SceneDocument::TreeNode::Mirror)
                group.position = transformVector;
            else if (operation == SceneDocument::TreeNode::For) {
                group.loopVariable = loopVariable.isEmpty() ? QStringLiteral("i") : loopVariable;
                group.loopRangeExpression = loopRangeExpression.isEmpty() ? QStringLiteral("[0 : 1 : 3]") : loopRangeExpression;
            } else if (operation == SceneDocument::TreeNode::Color) {
                group.color = operationColor;
            } else if (operation == SceneDocument::TreeNode::LinearExtrude) {
                group.scale = transformVector;
                group.linearExtrudeCenter = extrudeCenter;
                group.linearExtrudeSlices = extrudeSlices;
            } else if (operation == SceneDocument::TreeNode::Resize) {
                group.scale = transformVector;
            }
            if (operation == SceneDocument::TreeNode::Translate
                || operation == SceneDocument::TreeNode::Rotate
                || operation == SceneDocument::TreeNode::Scale
                || operation == SceneDocument::TreeNode::Mirror
                || operation == SceneDocument::TreeNode::LinearExtrude
                || operation == SceneDocument::TreeNode::Resize)
                group.transformExpressions = transformExpressions;

            parent->children.append(group);
            SceneDocument::TreeNode *bodyParent = &parent->children.last();

            // ── Build nested For chain for multi-variable for or intersection_for ──
            struct VarScope { QString name; bool hadPrev; qreal prevVal; };
            QVector<VarScope> loopScopes;

            if (operation == SceneDocument::TreeNode::For) {
                for (const QString &pair : forExtraPairs) {
                    const int eq = pair.indexOf('=');
                    if (eq <= 0) continue;
                    SceneDocument::TreeNode subFor = makeGroupNode(SceneDocument::TreeNode::For, state);
                    subFor.loopVariable = pair.left(eq).trimmed();
                    subFor.loopRangeExpression = pair.mid(eq + 1).trimmed();
                    bodyParent->children.append(subFor);
                    bodyParent = &bodyParent->children.last();
                }
            }

            if (operation == SceneDocument::TreeNode::Intersection && !loopVariable.isEmpty()) {
                SceneDocument::TreeNode subFor = makeGroupNode(SceneDocument::TreeNode::For, state);
                subFor.loopVariable = loopVariable;
                subFor.loopRangeExpression = loopRangeExpression;
                bodyParent->children.append(subFor);
                bodyParent = &bodyParent->children.last();
                for (const QString &pair : forExtraPairs) {
                    const int eq = pair.indexOf('=');
                    if (eq <= 0) continue;
                    SceneDocument::TreeNode extraFor = makeGroupNode(SceneDocument::TreeNode::For, state);
                    extraFor.loopVariable = pair.left(eq).trimmed();
                    extraFor.loopRangeExpression = pair.mid(eq + 1).trimmed();
                    bodyParent->children.append(extraFor);
                    bodyParent = &bodyParent->children.last();
                }
            }

            // ── Push for-loop variable scopes ──
            bool isForLike = (operation == SceneDocument::TreeNode::For || operation == SceneDocument::TreeNode::Intersection);
            if (isForLike) {
                SceneDocument::TreeNode *walk = &parent->children.last();
                while (walk) {
                    if (walk->operation == SceneDocument::TreeNode::For && !walk->loopVariable.isEmpty()) {
                        const bool hp = state->variableValues.contains(walk->loopVariable);
                        loopScopes.append({walk->loopVariable, hp, hp ? state->variableValues[walk->loopVariable] : 0.0});
                        state->variableValues[walk->loopVariable] = 0.0;
                    }
                    if (walk == bodyParent) break;
                    walk = walk->children.isEmpty() ? nullptr : &walk->children.last();
                }
            }

            const bool ok = parseBlock(state, bodyParent, true, errorMessage);

            // ── Pop for-loop variable scopes ──
            for (int si = loopScopes.size() - 1; si >= 0; --si) {
                const VarScope &vs = loopScopes[si];
                if (vs.hadPrev) state->variableValues[vs.name] = vs.prevVal;
                else            state->variableValues.remove(vs.name);
            }

            if (!ok)
                return false;
            continue;
        }

        // ── Primitive ─────────────────────────────────────────────────────
        ShapeNode shape;
        QString primitiveError;
        const bool isPrimitive = parsePrimitiveLine(line, &shape, state, &primitiveError);
        if (isPrimitive) {
            state->shapes.append(shape);
            parent->children.append(makePrimitiveNode(shape, state));
            continue;
        }
        if (!primitiveError.isEmpty()) {
            // Recognized primitive keyword but invalid args — hard error with line number.
            const QString msg = primitiveError.arg(current.number);
            if (errorMessage) *errorMessage = msg;
            state->errorLine = current.number;
            return false;
        }

        // ── Polyhedron ──────────────────────────────────────────────────
        if (line.startsWith(QStringLiteral("polyhedron("))) {
            // Combine multi-line polyhedron statement until ); is found
            QString polyLine = line;
            if (!polyLine.endsWith(QLatin1Char(';'))) {
                while (state->index < state->lines.size()) {
                    const ParsedLine &next = state->lines[state->index];
                    polyLine += next.text;
                    ++state->index;
                    if (next.text.endsWith(QLatin1Char(';')))
                        break;
                }
            }

            PolyhedronData polyData;
            QString polyError;
            if (!parsePolyhedron(polyLine, &polyData, &polyError)) {
                if (polyError.isEmpty())
                    polyError = QStringLiteral("malformed polyhedron call");
                if (errorMessage)
                    *errorMessage = polyError + QStringLiteral(" on line %1").arg(current.number);
                state->errorLine = current.number;
                return false;
            }
            if (polyData.points.isEmpty() || polyData.faces.isEmpty()) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("polyhedron on line %1 has no points or faces").arg(current.number);
                state->errorLine = current.number;
                return false;
            }
            buildPolyhedronGroup(polyData, state, parent);
            continue;
        }

        // ── Brace-free single-child transform (OpenSCAD shorthand) ──────────
        // e.g.  translate([x, y, 0])
        //         cylinder(h=1, r=2, center=true);
        {
            SceneDocument::TreeNode::Operation bfOp = SceneDocument::TreeNode::Union;
            QVector3D bfVec;
            QStringList bfExprs;
            if (parseBraceFreeOperationLine(line, &bfOp, &bfVec, state->variableValues, &bfExprs)) {
                SceneDocument::TreeNode bfGroup = makeGroupNode(bfOp, state);
                if (bfOp == SceneDocument::TreeNode::Translate) bfGroup.position = bfVec;
                else if (bfOp == SceneDocument::TreeNode::Rotate)  bfGroup.rotation = bfVec;
                else if (bfOp == SceneDocument::TreeNode::Scale)   bfGroup.scale    = bfVec;
                bfGroup.transformExpressions = bfExprs;
                parent->children.append(bfGroup);
                SceneDocument::TreeNode *innermost = &parent->children.last();

                // Chain additional consecutive brace-free transforms
                while (state->index < state->lines.size()) {
                    const ParsedLine &peek = state->lines[state->index];
                    SceneDocument::TreeNode::Operation chainOp = SceneDocument::TreeNode::Union;
                    QVector3D chainVec;
                    QStringList chainExprs;
                    if (!parseBraceFreeOperationLine(peek.text, &chainOp, &chainVec, state->variableValues, &chainExprs))
                        break;
                    ++state->index;
                    SceneDocument::TreeNode chainGroup = makeGroupNode(chainOp, state);
                    if (chainOp == SceneDocument::TreeNode::Translate) chainGroup.position = chainVec;
                    else if (chainOp == SceneDocument::TreeNode::Rotate)  chainGroup.rotation = chainVec;
                    else if (chainOp == SceneDocument::TreeNode::Scale)   chainGroup.scale    = chainVec;
                    chainGroup.transformExpressions = chainExprs;
                    innermost->children.append(chainGroup);
                    innermost = &innermost->children.last();
                }

                // Parse the single following child statement into the innermost group
                if (state->index < state->lines.size()) {
                    const ParsedLine childLine = state->lines[state->index++];
                    const QString &ct = childLine.text;
                    QString cc, ca;
                    if (parseModuleCallLine(ct, &cc, &ca)) {
                        innermost->children.append(makeModuleCallNode(0, cc, ca, state));
                    } else {
                        ShapeNode cs;
                        QString cpe;
                        if (parsePrimitiveLine(ct, &cs, state, &cpe)) {
                            state->shapes.append(cs);
                            innermost->children.append(makePrimitiveNode(cs, state));
                        } else if (!cpe.isEmpty()) {
                            if (errorMessage) *errorMessage = cpe.arg(childLine.number);
                            state->errorLine = childLine.number;
                            return false;
                        } else if (ct.endsWith('{')) {
                            // If the child line is a recognized operation/transform with block,
                            // create a proper group node (e.g. translate([x,y,z]) { ... })
                            SceneDocument::TreeNode::Operation childOp = SceneDocument::TreeNode::Union;
                            QVector3D childVec;
                            QStringList childExprs;
                            QString childLoopVar, childLoopRange;
                            QColor childColor;
                            bool childExtrudeCenter = false;
                            int childExtrudeSlices = 0;
                            if (parseOperationLine(ct, &childOp, &childVec, state->variableValues,
                                                   &childExprs, &childLoopVar, &childLoopRange, &childColor,
                                                   nullptr, &childExtrudeCenter, &childExtrudeSlices)) {
                                SceneDocument::TreeNode childGroup = makeGroupNode(childOp, state);
                                if (childOp == SceneDocument::TreeNode::Translate)
                                    childGroup.position = childVec;
                                else if (childOp == SceneDocument::TreeNode::Rotate)
                                    childGroup.rotation = childVec;
                                else if (childOp == SceneDocument::TreeNode::Scale)
                                    childGroup.scale = childVec;
                                else if (childOp == SceneDocument::TreeNode::Mirror)
                                    childGroup.position = childVec;
                                else if (childOp == SceneDocument::TreeNode::For) {
                                    childGroup.loopVariable = childLoopVar.isEmpty() ? QStringLiteral("i") : childLoopVar;
                                    childGroup.loopRangeExpression = childLoopRange.isEmpty() ? QStringLiteral("[0 : 1 : 3]") : childLoopRange;
                                } else if (childOp == SceneDocument::TreeNode::Color) {
                                    childGroup.color = childColor;
                                } else if (childOp == SceneDocument::TreeNode::LinearExtrude) {
                                    childGroup.scale = childVec;
                                    childGroup.linearExtrudeCenter = childExtrudeCenter;
                                    childGroup.linearExtrudeSlices = childExtrudeSlices;
                                } else if (childOp == SceneDocument::TreeNode::Resize) {
                                    childGroup.scale = childVec;
                                }
                                if (childOp == SceneDocument::TreeNode::Translate
                                    || childOp == SceneDocument::TreeNode::Rotate
                                    || childOp == SceneDocument::TreeNode::Scale
                                    || childOp == SceneDocument::TreeNode::Mirror
                                    || childOp == SceneDocument::TreeNode::LinearExtrude
                                    || childOp == SceneDocument::TreeNode::Resize)
                                    childGroup.transformExpressions = childExprs;
                                innermost->children.append(childGroup);
                                if (!parseBlock(state, &innermost->children.last(), true, errorMessage))
                                    return false;
                            } else {
                                // Unknown block wrapper — flatten its contents into innermost
                                if (!parseBlock(state, innermost, true, errorMessage))
                                    return false;
                            }
                        }
                        // else: unknown single-line child, skip
                    }
                }
                continue;
            }
        }

        // ── Known keyword that none of the above caught — hard error ──────
        if (startsWithKnownKeyword(line)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Unsupported or malformed syntax on line %1: %2").arg(current.number).arg(line);
            state->errorLine = current.number;
            return false;
        }

        // ── Completely unknown statement — skip transparently ─────────────
        // If it opens a block, parse children into the current parent (flatten),
        // so primitives inside unsupported wrappers (e.g. color(){...}) are preserved.
        if (line.endsWith('{')) {
            if (!parseBlock(state, parent, true, errorMessage))
                return false;
        }
        // Single-line unknown statement: silently skip.
    }

    if (stopAtBrace) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Missing closing brace.");
        return false;
    }

    return true;
}

static QVector<ParsedLine> normalizedLines(const QString &code)
{
    QVector<ParsedLine> result;

    QString cleaned = code;
    QRegularExpression blockCommentRe(QStringLiteral("/\\*.*?\\*/"),
                                      QRegularExpression::DotMatchesEverythingOption);
    cleaned.replace(blockCommentRe, QString());
    const int unclosed = cleaned.indexOf(QStringLiteral("/*"));
    if (unclosed >= 0)
        cleaned = cleaned.left(unclosed);

    const QStringList lines = cleaned.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i].trimmed();
        const int commentIndex = line.indexOf(QStringLiteral("//"));
        if (commentIndex >= 0)
            line = line.left(commentIndex).trimmed();
        if (line.isEmpty())
            continue;
        result.append({line, i + 1});
    }
    return result;
}

} // namespace

bool OpenScadParser::parse(const QString &code, QVector<ShapeNode> *shapes, QString *errorMessage, int *errorLine)
{
    if (!shapes)
        return false;

    SceneDocument::Snapshot snapshot;
    if (!parseScene(code, &snapshot, errorMessage, errorLine))
        return false;

    *shapes = snapshot.shapes;
    return true;
}

static bool parseModuleParams(const QString &paramsStr,
                              SceneDocument::TreeNode *moduleNode,
                              ParserState *state,
                              QHash<QString, qreal> *varValues,
                              QString *errorMessage)
{
    if (paramsStr.trimmed().isEmpty())
        return true;

    const QStringList parts = splitAtTopLevelCommas(paramsStr);
    for (const QString &part : parts) {
        const int eq = part.indexOf('=');
        if (eq < 0) {
            // Parameter without default value (e.g. module foo(x, y)) — default to 0.
            const QString name = part.trimmed();
            if (!isValidIdentifier(name)) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("Invalid module parameter name: %1").arg(name);
                return false;
            }
            SceneDocument::TreeNode paramNode = makeVariableNode(name, QStringLiteral("0"), 0.0, state);
            paramNode.isParameter = true;
            (*varValues)[name] = 0.0;
            moduleNode->children.append(paramNode);
            continue;
        }

        const QString name = part.left(eq).trimmed();
        const QString expr = part.mid(eq + 1).trimmed();
        if (!isValidIdentifier(name) || expr.isEmpty()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Invalid module parameter: %1").arg(part.trimmed());
            return false;
        }

        QString expressionError;
        if (!ExpressionSyntax::validate(expr, &expressionError)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Invalid module parameter expression for %1: %2").arg(name, expressionError);
            return false;
        }

        qreal value = 0.0;
        if (!ExpressionSyntax::evaluate(expr, *varValues, &value)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Could not evaluate module parameter %1 = %2").arg(name, expr);
            return false;
        }

        SceneDocument::TreeNode paramNode = makeVariableNode(name, expr, value, state);
        paramNode.isParameter = true;
        (*varValues)[name] = value;
        moduleNode->children.append(paramNode);
    }

    return true;
}

static void resolveModuleCallReferences(SceneDocument::TreeNode *node,
                                        const QHash<QString, SceneDocument::TreeNode *> &moduleByName)
{
    if (!node)
        return;

    if (node->type == SceneDocument::TreeNode::ModuleCall) {
        if (SceneDocument::TreeNode *module = moduleByName.value(node->moduleName, nullptr))
            node->shapeId = module->id;
        return;
    }

    for (SceneDocument::TreeNode &child : node->children)
        resolveModuleCallReferences(&child, moduleByName);
}

bool OpenScadParser::parseScene(const QString &code, SceneDocument::Snapshot *snapshot, QString *errorMessage, int *errorLine)
{
    if (!snapshot)
        return false;

    ParserState state;
    state.lines = normalizedLines(code);

    // Root is an implicit top-level container (operation=Module, never emitted directly).
    SceneDocument::TreeNode root = makeGroupNode(SceneDocument::TreeNode::Module, &state);
    // Scene holds top-level variables and standalone primitives.
    SceneDocument::TreeNode sceneNode = makeGroupNode(SceneDocument::TreeNode::Scene, &state);
    QVector<QPair<QString, QString>> moduleCalls; // preserves explicit call order: name, args

    while (state.index < state.lines.size()) {
        const ParsedLine current = state.lines[state.index];
        QString line = current.text;

        // OpenSCAD * modifier → skip disabled statement entirely
        if (!line.isEmpty() && line[0] == QLatin1Char('*')) {
            ++state.index;
            if (line.trimmed().endsWith(QLatin1Char('{'))) {
                SceneDocument::TreeNode _dummy;
                _dummy.type = SceneDocument::TreeNode::Group;
                _dummy.operation = SceneDocument::TreeNode::Union;
                parseBlock(&state, &_dummy, true, nullptr);
            }
            continue;
        }

        // Strip OpenSCAD debug modifiers (#, %, !)
        while (!line.isEmpty() && (line[0] == QLatin1Char('#') || line[0] == QLatin1Char('%') || line[0] == QLatin1Char('!')))
            line = line.mid(1).trimmed();

        // Collect top-level module call statements — they become ModuleCall nodes in Scene.
        // ── Multi-line module call accumulator ────────────────────────────
        bool callWasAccumulated = false;
        {
            static const QRegularExpression mlRe("^[A-Za-z_][A-Za-z0-9_]*\\s*\\(");
            if (line.contains(mlRe)
                && !line.trimmed().endsWith(QLatin1Char(';'))
                && !line.trimmed().endsWith(QLatin1Char('{'))) {
                QString accLine = line;
                while (state.index < state.lines.size()) {
                    const ParsedLine &next = state.lines[state.index];
                    ++state.index;
                    accLine += next.text;
                    if (next.text.indexOf(QStringLiteral(");")) >= 0) {
                        line = accLine.trimmed();
                        callWasAccumulated = true;
                        break;
                    }
                }
            }
        }

        QString callName, callArgs;
        if (parseModuleCallLine(line, &callName, &callArgs)) {
            moduleCalls.append({callName, callArgs});
            if (!callWasAccumulated)
                ++state.index;
            continue;
        }

        // Module definition: module name(params) {
        QString modName, modParams;
        if (isModuleDefinitionLine(line, &modName, &modParams)) {
            ++state.index;

            if (!isValidIdentifier(modName)) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("Invalid module name on line %1: %2").arg(current.number).arg(modName);
                if (errorLine) *errorLine = current.number;
                return false;
            }

            // The generated legacy/main scene_model wrapper is flattened into the Scene container.
            if (modName == QStringLiteral("scene_model") && modParams.trimmed().isEmpty()) {
                if (!parseBlock(&state, &sceneNode, true, errorMessage)) {
                    if (errorLine) *errorLine = state.errorLine;
                    return false;
                }
                continue;
            }

            const QHash<QString, qreal> outerVariables = state.variableValues;
            SceneDocument::TreeNode moduleNode = makeGroupNode(SceneDocument::TreeNode::Module, &state);
            moduleNode.moduleName = modName;

            if (!parseModuleParams(modParams, &moduleNode, &state, &state.variableValues, errorMessage)) {
                if (errorLine) *errorLine = current.number;
                state.variableValues = outerVariables;
                return false;
            }

            state.insideModuleBody = true;
            if (!parseBlock(&state, &moduleNode, true, errorMessage)) {
                if (errorLine) *errorLine = state.errorLine;
                state.variableValues = outerVariables;
                state.insideModuleBody = false;
                return false;
            }
            state.insideModuleBody = false;

            state.variableValues = outerVariables;
            root.children.append(moduleNode);
            continue;
        }

        // Vector variable at top level: body_size = [28, 28, 4];
        // Store in vectorVariableValues so primitives can reference them by name.
        {
            static const QRegularExpression vecVarRe(
                "^([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*\\[([^\\]]+)\\]\\s*;\\s*$");
            const auto vm = vecVarRe.match(line);
            if (vm.hasMatch()) {
                ++state.index;
                const QStringList vparts = splitAtTopLevelCommas(vm.captured(2));
                if (vparts.size() == 3) {
                    qreal vx = 0, vy = 0, vz = 0;
                    QString vxe, vye, vze;
                    if (parseParamExpression(vparts[0].trimmed(), state.variableValues, &vx, &vxe)
                        && parseParamExpression(vparts[1].trimmed(), state.variableValues, &vy, &vye)
                        && parseParamExpression(vparts[2].trimmed(), state.variableValues, &vz, &vze))
                        state.vectorVariableValues[vm.captured(1)] = QVector3D(vx, vy, vz);
                }
                continue;
            }
        }

        // Variable at top level (global scope) → goes into the Scene container.
        QString variableName, variableExpression, variableError;
        qreal variableValue = 0.0;
        if (parseVariableLine(line, &variableName, &variableExpression, &variableValue, &variableError)) {
            ++state.index;
            qreal evaluatedValue = 0.0;
            if (ExpressionSyntax::evaluate(variableExpression, state.variableValues, &evaluatedValue))
                variableValue = evaluatedValue;
            state.variableValues[variableName] = variableValue;
            sceneNode.children.append(makeVariableNode(variableName, variableExpression, variableValue, &state));
            continue;
        }
        if (!variableError.isEmpty()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Invalid expression on line %1: %2").arg(current.number).arg(variableError);
            if (errorLine) *errorLine = current.number;
            return false;
        }

        // ── let (x = expr) { ... } at top scope ──────────────────────────
        {
            static const QRegularExpression letRe("^let\\s*\\((.+)\\)\\s*\\{\\s*$");
            QRegularExpressionMatch lm = letRe.match(line);
            if (lm.hasMatch()) {
                ++state.index;
                const QStringList pairs = splitAtTopLevelCommas(lm.captured(1));
                QVector<QPair<QString, qreal>> letScope;
                for (const QString &pair : pairs) {
                    const int eq = pair.indexOf('=');
                    if (eq <= 0) continue;
                    const QString var = pair.left(eq).trimmed();
                    const QString expr = pair.mid(eq + 1).trimmed();
                    qreal val = 0.0;
                    if (ExpressionSyntax::evaluate(expr, state.variableValues, &val)) {
                        const bool hadPrev = state.variableValues.contains(var);
                        letScope.append({var, hadPrev ? state.variableValues[var] : qQNaN()});
                        state.variableValues[var] = val;
                    }
                }
                if (!parseBlock(&state, &sceneNode, true, errorMessage)) {
                    for (int li = letScope.size() - 1; li >= 0; --li) {
                        const auto &ls = letScope[li];
                        if (std::isnan(ls.second))
                            state.variableValues.remove(ls.first);
                        else
                            state.variableValues[ls.first] = ls.second;
                    }
                    if (errorLine) *errorLine = state.errorLine;
                    return false;
                }
                for (int li = letScope.size() - 1; li >= 0; --li) {
                    const auto &ls = letScope[li];
                    if (std::isnan(ls.second))
                        state.variableValues.remove(ls.first);
                    else
                        state.variableValues[ls.first] = ls.second;
                }
                continue;
            }
        }

        // ── if (cond) { ... } else { ... } at top scope ──────────────────
        {
            static const QRegularExpression ifRe("^if\\s*\\((.+)\\)\\s*\\{\\s*$");
            static const QRegularExpression elseRe("^else\\s*\\{\\s*$");
            static const QRegularExpression elseIfRe("^else\\s+if\\s*\\((.+)\\)\\s*\\{\\s*$");
            QRegularExpressionMatch im = ifRe.match(line);
            if (im.hasMatch()) {
                ++state.index;
                qreal condVal = 0.0;
                bool condTrue = ExpressionSyntax::evaluate(im.captured(1), state.variableValues, &condVal) && (condVal != 0.0);

                auto skipBlock = [&state](QString *errMsg) -> bool {
                    SceneDocument::TreeNode _d;
                    _d.type = SceneDocument::TreeNode::Group;
                    _d.operation = SceneDocument::TreeNode::Union;
                    return parseBlock(&state, &_d, true, errMsg);
                };

                if (condTrue) {
                    if (!parseBlock(&state, &sceneNode, true, errorMessage)) {
                        if (errorLine) *errorLine = state.errorLine;
                        return false;
                    }
                } else {
                    if (!skipBlock(nullptr))
                        return false;
                }

                while (state.index < state.lines.size()) {
                    const QString next = state.lines[state.index].text;
                    QRegularExpressionMatch eif = elseIfRe.match(next);
                    if (eif.hasMatch()) {
                        ++state.index;
                        qreal eifVal = 0.0;
                        bool eifTrue = ExpressionSyntax::evaluate(eif.captured(1), state.variableValues, &eifVal) && (eifVal != 0.0);
                        if (!condTrue && eifTrue) {
                            if (!parseBlock(&state, &sceneNode, true, errorMessage)) {
                                if (errorLine) *errorLine = state.errorLine;
                                return false;
                            }
                            condTrue = true;
                        } else {
                            if (!skipBlock(nullptr))
                                return false;
                        }
                        continue;
                    }
                    if (elseRe.match(next).hasMatch()) {
                        ++state.index;
                        if (!condTrue) {
                            if (!parseBlock(&state, &sceneNode, true, errorMessage)) {
                                if (errorLine) *errorLine = state.errorLine;
                                return false;
                            }
                        } else {
                            if (!skipBlock(nullptr))
                                return false;
                        }
                    }
                    break;
                }
                continue;
            }
        }

        // Direct top-level group / transform / for (no module wrapper).
        SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
        QVector3D transformVector;
        QStringList transformExpressions;
        QString loopVariable, loopRangeExpression;
        QStringList forExtraPairs;
        QColor operationColor;
        bool extrudeCenter = false;
        int extrudeSlices = 0;
        if (parseOperationLine(line, &operation, &transformVector, state.variableValues,
                               &transformExpressions, &loopVariable, &loopRangeExpression, &operationColor, &forExtraPairs,
                               &extrudeCenter, &extrudeSlices)) {
            ++state.index;
            SceneDocument::TreeNode group = makeGroupNode(operation, &state);
            if (operation == SceneDocument::TreeNode::Translate)
                group.position = transformVector;
            else if (operation == SceneDocument::TreeNode::Rotate)
                group.rotation = transformVector;
            else if (operation == SceneDocument::TreeNode::Scale)
                group.scale = transformVector;
            else if (operation == SceneDocument::TreeNode::Mirror)
                group.position = transformVector;
            else if (operation == SceneDocument::TreeNode::For) {
                group.loopVariable = loopVariable.isEmpty() ? QStringLiteral("i") : loopVariable;
                group.loopRangeExpression = loopRangeExpression.isEmpty() ? QStringLiteral("[0 : 1 : 3]") : loopRangeExpression;
            } else if (operation == SceneDocument::TreeNode::Color) {
                group.color = operationColor;
            } else if (operation == SceneDocument::TreeNode::LinearExtrude) {
                group.scale = transformVector;
                group.linearExtrudeCenter = extrudeCenter;
                group.linearExtrudeSlices = extrudeSlices;
            } else if (operation == SceneDocument::TreeNode::Resize) {
                group.scale = transformVector;
            }
            if (operation == SceneDocument::TreeNode::Translate
                || operation == SceneDocument::TreeNode::Rotate
                || operation == SceneDocument::TreeNode::Scale
                || operation == SceneDocument::TreeNode::Mirror
                || operation == SceneDocument::TreeNode::LinearExtrude
                || operation == SceneDocument::TreeNode::Resize)
                group.transformExpressions = transformExpressions;
            sceneNode.children.append(group);
            SceneDocument::TreeNode *bodyParent = &sceneNode.children.last();

            // ── Build nested For chain for multi-variable for or intersection_for ──
            struct VarScope { QString name; bool hadPrev; qreal prevVal; };
            QVector<VarScope> loopScopes;

            if (operation == SceneDocument::TreeNode::For) {
                for (const QString &pair : forExtraPairs) {
                    const int eq = pair.indexOf('=');
                    if (eq <= 0) continue;
                    SceneDocument::TreeNode subFor = makeGroupNode(SceneDocument::TreeNode::For, &state);
                    subFor.loopVariable = pair.left(eq).trimmed();
                    subFor.loopRangeExpression = pair.mid(eq + 1).trimmed();
                    bodyParent->children.append(subFor);
                    bodyParent = &bodyParent->children.last();
                }
            }

            if (operation == SceneDocument::TreeNode::Intersection && !loopVariable.isEmpty()) {
                SceneDocument::TreeNode subFor = makeGroupNode(SceneDocument::TreeNode::For, &state);
                subFor.loopVariable = loopVariable;
                subFor.loopRangeExpression = loopRangeExpression;
                bodyParent->children.append(subFor);
                bodyParent = &bodyParent->children.last();
                for (const QString &pair : forExtraPairs) {
                    const int eq = pair.indexOf('=');
                    if (eq <= 0) continue;
                    SceneDocument::TreeNode extraFor = makeGroupNode(SceneDocument::TreeNode::For, &state);
                    extraFor.loopVariable = pair.left(eq).trimmed();
                    extraFor.loopRangeExpression = pair.mid(eq + 1).trimmed();
                    bodyParent->children.append(extraFor);
                    bodyParent = &bodyParent->children.last();
                }
            }

            // ── Push for-loop variable scopes ──
            bool isForLike = (operation == SceneDocument::TreeNode::For || operation == SceneDocument::TreeNode::Intersection);
            if (isForLike) {
                SceneDocument::TreeNode *walk = &sceneNode.children.last();
                while (walk) {
                    if (walk->operation == SceneDocument::TreeNode::For && !walk->loopVariable.isEmpty()) {
                        const bool hp = state.variableValues.contains(walk->loopVariable);
                        loopScopes.append({walk->loopVariable, hp, hp ? state.variableValues[walk->loopVariable] : 0.0});
                        state.variableValues[walk->loopVariable] = 0.0;
                    }
                    if (walk == bodyParent) break;
                    walk = walk->children.isEmpty() ? nullptr : &walk->children.last();
                }
            }

            const bool parsedBody = parseBlock(&state, bodyParent, true, errorMessage);

            // ── Pop for-loop variable scopes ──
            for (int si = loopScopes.size() - 1; si >= 0; --si) {
                const auto &vs = loopScopes[si];
                if (vs.hadPrev) state.variableValues[vs.name] = vs.prevVal;
                else            state.variableValues.remove(vs.name);
            }

            if (!parsedBody) {
                if (errorLine) *errorLine = state.errorLine;
                return false;
            }
            continue;
        }

        // Direct top-level primitive (cube / sphere / cylinder) → goes into Scene container.
        ShapeNode shape;
        QString primitiveError;
        if (parsePrimitiveLine(line, &shape, &state, &primitiveError)) {
            ++state.index;
            state.shapes.append(shape);
            sceneNode.children.append(makePrimitiveNode(shape, &state));
            continue;
        }
        if (!primitiveError.isEmpty()) {
            if (errorMessage)
                *errorMessage = primitiveError.arg(current.number);
            if (errorLine) *errorLine = current.number;
            return false;
        }

        // ── Top-level polyhedron ──────────────────────────────────────────
        if (line.startsWith(QStringLiteral("polyhedron("))) {
            ++state.index; // consume first line
            QString polyLine = line;
            if (!polyLine.endsWith(QLatin1Char(';'))) {
                while (state.index < state.lines.size()) {
                    const ParsedLine &next = state.lines[state.index];
                    polyLine += next.text;
                    ++state.index;
                    if (next.text.endsWith(QLatin1Char(';')))
                        break;
                }
            }

            PolyhedronData polyData;
            QString polyError;
            if (!parsePolyhedron(polyLine, &polyData, &polyError)) {
                if (polyError.isEmpty())
                    polyError = QStringLiteral("malformed polyhedron call");
                if (errorMessage)
                    *errorMessage = polyError + QStringLiteral(" on line %1").arg(current.number);
                if (errorLine) *errorLine = current.number;
                return false;
            }
            if (polyData.points.isEmpty() || polyData.faces.isEmpty()) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("polyhedron on line %1 has no points or faces").arg(current.number);
                if (errorLine) *errorLine = current.number;
                return false;
            }
            buildPolyhedronGroup(polyData, &state, &sceneNode);
            continue;
        }

        // Brace-free single-child transform at top level.
        {
            SceneDocument::TreeNode::Operation bfOp = SceneDocument::TreeNode::Union;
            QVector3D bfVec;
            QStringList bfExprs;
            if (parseBraceFreeOperationLine(line, &bfOp, &bfVec, state.variableValues, &bfExprs)) {
                ++state.index;
                SceneDocument::TreeNode bfGroup = makeGroupNode(bfOp, &state);
                if (bfOp == SceneDocument::TreeNode::Translate) bfGroup.position = bfVec;
                else if (bfOp == SceneDocument::TreeNode::Rotate)  bfGroup.rotation = bfVec;
                else if (bfOp == SceneDocument::TreeNode::Scale)   bfGroup.scale    = bfVec;
                bfGroup.transformExpressions = bfExprs;
                sceneNode.children.append(bfGroup);
                SceneDocument::TreeNode *innermost = &sceneNode.children.last();

                while (state.index < state.lines.size()) {
                    const ParsedLine &peek = state.lines[state.index];
                    SceneDocument::TreeNode::Operation chainOp = SceneDocument::TreeNode::Union;
                    QVector3D chainVec;
                    QStringList chainExprs;
                    if (!parseBraceFreeOperationLine(peek.text, &chainOp, &chainVec, state.variableValues, &chainExprs))
                        break;
                    ++state.index;
                    SceneDocument::TreeNode chainGroup = makeGroupNode(chainOp, &state);
                    if (chainOp == SceneDocument::TreeNode::Translate) chainGroup.position = chainVec;
                    else if (chainOp == SceneDocument::TreeNode::Rotate)  chainGroup.rotation = chainVec;
                    else if (chainOp == SceneDocument::TreeNode::Scale)   chainGroup.scale    = chainVec;
                    chainGroup.transformExpressions = chainExprs;
                    innermost->children.append(chainGroup);
                    innermost = &innermost->children.last();
                }

                if (state.index < state.lines.size()) {
                    const ParsedLine childLine = state.lines[state.index++];
                    const QString &ct = childLine.text;
                    QString cc, ca;
                    if (parseModuleCallLine(ct, &cc, &ca)) {
                        innermost->children.append(makeModuleCallNode(0, cc, ca, &state));
                    } else {
                        ShapeNode cs;
                        QString cpe;
                        if (parsePrimitiveLine(ct, &cs, &state, &cpe)) {
                            state.shapes.append(cs);
                            innermost->children.append(makePrimitiveNode(cs, &state));
                        } else if (!cpe.isEmpty()) {
                            if (errorMessage) *errorMessage = cpe.arg(childLine.number);
                            if (errorLine) *errorLine = childLine.number;
                            return false;
                        } else if (ct.endsWith('{')) {
                            SceneDocument::TreeNode::Operation childOp = SceneDocument::TreeNode::Union;
                            QVector3D childVec;
                            QStringList childExprs;
                            QString childLoopVar, childLoopRange;
                            QColor childColor;
                            bool childExtrudeCenter = false;
                            int childExtrudeSlices = 0;
                            if (parseOperationLine(ct, &childOp, &childVec, state.variableValues,
                                                   &childExprs, &childLoopVar, &childLoopRange, &childColor,
                                                   nullptr, &childExtrudeCenter, &childExtrudeSlices)) {
                                SceneDocument::TreeNode childGroup = makeGroupNode(childOp, &state);
                                if (childOp == SceneDocument::TreeNode::Translate)
                                    childGroup.position = childVec;
                                else if (childOp == SceneDocument::TreeNode::Rotate)
                                    childGroup.rotation = childVec;
                                else if (childOp == SceneDocument::TreeNode::Scale)
                                    childGroup.scale = childVec;
                                else if (childOp == SceneDocument::TreeNode::Mirror)
                                    childGroup.position = childVec;
                                else if (childOp == SceneDocument::TreeNode::For) {
                                    childGroup.loopVariable = childLoopVar.isEmpty() ? QStringLiteral("i") : childLoopVar;
                                    childGroup.loopRangeExpression = childLoopRange.isEmpty() ? QStringLiteral("[0 : 1 : 3]") : childLoopRange;
                                } else if (childOp == SceneDocument::TreeNode::Color) {
                                    childGroup.color = childColor;
                                } else if (childOp == SceneDocument::TreeNode::LinearExtrude) {
                                    childGroup.scale = childVec;
                                    childGroup.linearExtrudeCenter = childExtrudeCenter;
                                    childGroup.linearExtrudeSlices = childExtrudeSlices;
                                } else if (childOp == SceneDocument::TreeNode::Resize) {
                                    childGroup.scale = childVec;
                                }
                                if (childOp == SceneDocument::TreeNode::Translate
                                    || childOp == SceneDocument::TreeNode::Rotate
                                    || childOp == SceneDocument::TreeNode::Scale
                                    || childOp == SceneDocument::TreeNode::Mirror
                                    || childOp == SceneDocument::TreeNode::LinearExtrude
                                    || childOp == SceneDocument::TreeNode::Resize)
                                    childGroup.transformExpressions = childExprs;
                                innermost->children.append(childGroup);
                                if (!parseBlock(&state, &innermost->children.last(), true, errorMessage)) {
                                    if (errorLine) *errorLine = state.errorLine;
                                    return false;
                                }
                            } else {
                                if (!parseBlock(&state, innermost, true, errorMessage)) {
                                    if (errorLine) *errorLine = state.errorLine;
                                    return false;
                                }
                            }
                        }
                    }
                }
                continue;
            }
        }

        // Known keyword that none of the above matched — hard error.
        if (startsWithKnownKeyword(line)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Unsupported or malformed syntax on line %1: %2").arg(current.number).arg(line);
            if (errorLine) *errorLine = current.number;
            return false;
        }

        // Completely unknown at top level — skip transparently (same as parseBlock).
        ++state.index;
        if (line.endsWith('{')) {
            SceneDocument::TreeNode dummy = makeGroupNode(SceneDocument::TreeNode::Union, &state);
            parseBlock(&state, &dummy, true, errorMessage);
        }
    }

    // Build a name→moduleNode map for fast lookup.
    QHash<QString, SceneDocument::TreeNode *> moduleByName;
    for (SceneDocument::TreeNode &child : root.children) {
        if (child.type == SceneDocument::TreeNode::Group
            && child.operation == SceneDocument::TreeNode::Module)
            moduleByName[child.moduleName] = &child;
    }

    // Create ModuleCall nodes in sceneNode in the order the calls appeared.
    // First, emit calls that were explicitly present (in call order).
    for (const auto &call : moduleCalls) {
        const QString &callName = call.first;
        if (SceneDocument::TreeNode *mod = moduleByName.value(callName, nullptr))
            sceneNode.children.append(makeModuleCallNode(mod->id, callName, call.second, &state));
    }

    resolveModuleCallReferences(&root, moduleByName);
    resolveModuleCallReferences(&sceneNode, moduleByName);

    // Insert the Scene container as the first root child.
    root.children.prepend(sceneNode);

    applyBooleanModes(&root, &state.shapes, ShapeNode::Add);

    snapshot->shapes = state.shapes;
    snapshot->treeRoot = root;
    snapshot->treeSnapshot = SceneTree::Snapshot{root, state.nextTreeNodeId};
    snapshot->selectedShapeId = state.shapes.isEmpty() ? -1 : state.shapes.first().id;
    snapshot->nextShapeId = state.nextShapeId;
    snapshot->nextTreeNodeId = state.nextTreeNodeId;
    return true;
}

bool OpenScadParser::parseVector3(const QString &text, QVector3D *vector)
{
    return parseVector3Value(text, vector);
}
