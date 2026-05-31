#include "openscadparser.h"
#include "expression.h"

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

static bool isValidIdentifier(const QString &name)
{
    if (name.isEmpty())
        return false;

    const QChar first = name.front();
    if (!(first == QLatin1Char('_') || first.isLetter()))
        return false;

    for (const QChar ch : name) {
        if (!(ch == QLatin1Char('_') || ch.isLetterOrNumber()))
            return false;
    }

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
static QStringList splitAtTopLevelCommas(const QString &text)
{
    QStringList result;
    int depth = 0;
    int start = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c == '(' || c == '[') ++depth;
        else if (c == ')' || c == ']') --depth;
        else if (c == ',' && depth == 0) {
            result.append(text.mid(start, i - start));
            start = i + 1;
        }
    }
    result.append(text.mid(start));
    return result;
}

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
                               QColor *color = nullptr)
{
    static const QRegularExpression unionRegex("^union\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression differenceRegex("^difference\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression intersectionRegex("^intersection\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression hullRegex("^hull\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression minkowskiRegex("^minkowski\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression translateRegex("^translate\\s*\\(\\s*\\[([^\\]]+)\\]\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression rotateRegex("^rotate\\s*\\(\\s*\\[([^\\]]+)\\]\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression scaleRegex("^scale\\s*\\(\\s*\\[([^\\]]+)\\]\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression mirrorRegex("^mirror\\s*\\(\\s*\\[([^\\]]+)\\]\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression forRegex("^for\\s*\\(\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*(\\[[^\\]]+\\])\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression colorRegex("^color\\s*\\((.*)\\)\\s*\\{\\s*$");
    static const QRegularExpression linearExtrudeRegex("^linear_extrude\\s*\\((.*)\\)\\s*\\{\\s*$");

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
        if (loopVariable)     *loopVariable = forMatch.captured(1);
        if (loopRangeExpression) *loopRangeExpression = forMatch.captured(2).trimmed();
        return true;
    }

    QRegularExpressionMatch extrudeMatch = linearExtrudeRegex.match(line);
    if (extrudeMatch.hasMatch()) {
        const auto args = parseNamedArgs(extrudeMatch.captured(1), {"height"});
        const QString heightExpr = args.value(QStringLiteral("height"), QStringLiteral("20"));
        qreal height = 20.0;
        QString he;
        if (!parseParamExpression(heightExpr, varValues, &height, &he))
            return false;
        *operation = SceneDocument::TreeNode::LinearExtrude;
        if (vector)
            *vector = QVector3D(height, 1.0f, 1.0f);
        if (expressions)
            *expressions = QStringList({he});
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

// Returns true and fills shape if this line is a cube/sphere/cylinder call (flexible args).
// Returns false if the line is not a primitive call at all.
// Sets *errorMessage and returns false if the line IS a primitive call but has invalid args.
static bool parsePrimitiveLine(const QString &line, ShapeNode *shape, ParserState *state,
                               QString *errorMessage)
{
    QString argsStr;

    // ── Cube ────────────────────────────────────────────────────────────────
    if (extractCallArgs(line, "cube", &argsStr) && line.endsWith(';')) {
        // Accept cube([x, y, z]) and cube([x, y, z], center=...)
        static const QRegularExpression vecRe("^\\[([^\\]]+)\\]");
        QRegularExpressionMatch m = vecRe.match(argsStr);
        QString vecInner;
        if (m.hasMatch()) {
            vecInner = m.captured(1);
        } else {
            // Might be a vector variable: cube(body_size, center=true)
            const QString firstArg = splitAtTopLevelCommas(argsStr).value(0).trimmed();
            if (state->vectorVariableValues.contains(firstArg)) {
                const QVector3D v = state->vectorVariableValues[firstArg];
                shape->id = state->nextShapeId++;
                shape->type = ShapeNode::Cube;
                shape->name = QStringLiteral("Cube %1").arg(shape->id);
                shape->size = v;
                shape->parameterExpressions = QStringList({
                    QString::number(v.x()), QString::number(v.y()), QString::number(v.z())});
                return true;
            }
            if (errorMessage)
                *errorMessage = QStringLiteral("cube on line %1: expected [x, y, z]");
            return false;
        }
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
        return true;
    }

    // ── Sphere ──────────────────────────────────────────────────────────────
    if (extractCallArgs(line, "sphere", &argsStr) && line.endsWith(';')) {
        // Accept sphere(r=5), sphere(5), sphere(d=10)
        const auto args = parseNamedArgs(argsStr, {"r"});
        QString radiusStr = args.value("r");
        if (radiusStr.isEmpty() && args.contains("d"))
            radiusStr = args["d"] + "/2"; // diameter → radius expression
        if (radiusStr.isEmpty()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("sphere on line %1: missing radius");
            return false;
        }
        qreal r = 0.0;
        QString re;
        if (!parseParamExpression(radiusStr, state->variableValues, &r, &re)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("sphere on line %1: could not parse radius");
            return false;
        }
        shape->id = state->nextShapeId++;
        shape->type = ShapeNode::Sphere;
        shape->name = QStringLiteral("Sphere %1").arg(shape->id);
        shape->radius = r;
        shape->parameterExpressions = QStringList({re});
        return true;
    }

    if (extractCallArgs(line, "circle", &argsStr) && line.endsWith(';')) {
        const auto args = parseNamedArgs(argsStr, {"r"});
        QString radiusStr = args.value("r");
        if (radiusStr.isEmpty() && args.contains("d"))
            radiusStr = args["d"] + "/2";
        if (radiusStr.isEmpty()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("circle on line %1: missing radius");
            return false;
        }
        qreal r = 0.0;
        QString re;
        if (!parseParamExpression(radiusStr, state->variableValues, &r, &re)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("circle on line %1: could not parse radius");
            return false;
        }
        shape->id = state->nextShapeId++;
        shape->type = ShapeNode::Circle;
        shape->name = QStringLiteral("Circle %1").arg(shape->id);
        shape->radius = r;
        shape->parameterExpressions = QStringList({re});
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

        if (args.contains("r1") || args.contains("r2")) {
            // Cone / frustum: cylinder(h=..., r1=..., r2=..., center=true)
            const QString r1Str = args.value("r1", QStringLiteral("0"));
            const QString r2Str = args.value("r2", QStringLiteral("0"));
            qreal r1 = 0.0, r2 = 0.0;
            QString r1e, r2e;
            if (!parseParamExpression(r1Str, state->variableValues, &r1, &r1e)
                || !parseParamExpression(r2Str, state->variableValues, &r2, &r2e)) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("cylinder on line %1: could not parse r1 or r2");
                return false;
            }
            shape->id = state->nextShapeId++;
            shape->type = ShapeNode::Cone;
            shape->name = QStringLiteral("Cone %1").arg(shape->id);
            shape->height  = static_cast<float>(h);
            shape->radius  = static_cast<float>(r1);
            shape->radius2 = static_cast<float>(r2);
            // parameterExpressions order: R1=0, R2=1, H=2
            shape->parameterExpressions = QStringList({r1e, r2e, he});
            return true;
        }

        // Regular cylinder: cylinder(h=..., r=..., center=true)
        if (!args.contains("r")) {
            if (errorMessage)
                *errorMessage = QStringLiteral("cylinder on line %1: missing r (or r1/r2)");
            return false;
        }
        qreal r = 0.0;
        QString re;
        if (!parseParamExpression(args["r"], state->variableValues, &r, &re)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("cylinder on line %1: could not parse r");
            return false;
        }
        shape->id = state->nextShapeId++;
        shape->type = ShapeNode::Cylinder;
        shape->name = QStringLiteral("Cylinder %1").arg(shape->id);
        shape->height = static_cast<float>(h);
        shape->radius = static_cast<float>(r);
        // parameterExpressions order: R=index 0, H=index 1
        shape->parameterExpressions = QStringList({re, he});
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
    static const QRegularExpression translateRe("^translate\\s*\\(\\s*\\[([^\\]]+)\\]\\s*\\)\\s*$");
    static const QRegularExpression rotateRe("^rotate\\s*\\(\\s*\\[([^\\]]+)\\]\\s*\\)\\s*$");
    static const QRegularExpression scaleRe("^scale\\s*\\(\\s*\\[([^\\]]+)\\]\\s*\\)\\s*$");
    static const QRegularExpression mirrorRe("^mirror\\s*\\(\\s*\\[([^\\]]+)\\]\\s*\\)\\s*$");

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
        "union", "difference", "intersection", "hull", "minkowski", "for", "color", "linear_extrude",
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
        const QString &line = current.text;

        if (line == QStringLiteral("}"))
            return stopAtBrace;

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

        // ── Group / transform / for ───────────────────────────────────────
        SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
        QVector3D transformVector;
        QStringList transformExpressions;
        QString loopVariable, loopRangeExpression;
        QColor operationColor;
        if (parseOperationLine(line, &operation, &transformVector, state->variableValues,
                               &transformExpressions, &loopVariable, &loopRangeExpression, &operationColor)) {
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
            }
            if (operation == SceneDocument::TreeNode::Translate
                || operation == SceneDocument::TreeNode::Rotate
                || operation == SceneDocument::TreeNode::Scale
                || operation == SceneDocument::TreeNode::Mirror
                || operation == SceneDocument::TreeNode::LinearExtrude)
                group.transformExpressions = transformExpressions;

            parent->children.append(group);
            SceneDocument::TreeNode &child = parent->children.last();
            const bool scopedLoop = (operation == SceneDocument::TreeNode::For);
            const QString scopedVar = child.loopVariable;
            const bool hadPrev = scopedLoop && state->variableValues.contains(scopedVar);
            const qreal prevVal = hadPrev ? state->variableValues.value(scopedVar) : 0.0;
            if (scopedLoop)
                state->variableValues[scopedVar] = 0.0;

            const bool ok = parseBlock(state, &child, true, errorMessage);

            if (scopedLoop) {
                if (hadPrev) state->variableValues[scopedVar] = prevVal;
                else         state->variableValues.remove(scopedVar);
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
                            if (parseOperationLine(ct, &childOp, &childVec, state->variableValues,
                                                   &childExprs, &childLoopVar, &childLoopRange, &childColor)) {
                                SceneDocument::TreeNode childGroup = makeGroupNode(childOp, state);
                                if (childOp == SceneDocument::TreeNode::Translate)
                                    childGroup.position = childVec;
                                else if (childOp == SceneDocument::TreeNode::Rotate)
                                    childGroup.rotation = childVec;
                                else if (childOp == SceneDocument::TreeNode::Scale)
                                    childGroup.scale = childVec;
                                else if (childOp == SceneDocument::TreeNode::For) {
                                    childGroup.loopVariable = childLoopVar.isEmpty() ? QStringLiteral("i") : childLoopVar;
                                    childGroup.loopRangeExpression = childLoopRange.isEmpty() ? QStringLiteral("[0 : 1 : 3]") : childLoopRange;
                                } else if (childOp == SceneDocument::TreeNode::Color) {
                                    childGroup.color = childColor;
                                }
                                if (childOp == SceneDocument::TreeNode::Translate
                                    || childOp == SceneDocument::TreeNode::Rotate
                                    || childOp == SceneDocument::TreeNode::Scale)
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
    const QStringList lines = code.split('\n');
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
        const QString &line = current.text;

        // Collect top-level module call statements — they become ModuleCall nodes in Scene.
        QString callName, callArgs;
        if (parseModuleCallLine(line, &callName, &callArgs)) {
            moduleCalls.append({callName, callArgs});
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

            if (!parseBlock(&state, &moduleNode, true, errorMessage)) {
                if (errorLine) *errorLine = state.errorLine;
                state.variableValues = outerVariables;
                return false;
            }

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

        // Direct top-level group / transform / for (no module wrapper).
        SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
        QVector3D transformVector;
        QStringList transformExpressions;
        QString loopVariable, loopRangeExpression;
        QColor operationColor;
        if (parseOperationLine(line, &operation, &transformVector, state.variableValues,
                               &transformExpressions, &loopVariable, &loopRangeExpression, &operationColor)) {
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
            }
            if (operation == SceneDocument::TreeNode::Translate
                || operation == SceneDocument::TreeNode::Rotate
                || operation == SceneDocument::TreeNode::Scale
                || operation == SceneDocument::TreeNode::Mirror
                || operation == SceneDocument::TreeNode::LinearExtrude)
                group.transformExpressions = transformExpressions;
            sceneNode.children.append(group);
            SceneDocument::TreeNode &child = sceneNode.children.last();
            const bool scopedLoop = operation == SceneDocument::TreeNode::For;
            const QString scopedVar = child.loopVariable;
            const bool hadPreviousValue = scopedLoop && state.variableValues.contains(scopedVar);
            const qreal previousValue = hadPreviousValue ? state.variableValues.value(scopedVar) : 0.0;
            if (scopedLoop)
                state.variableValues[scopedVar] = 0.0;

            const bool parsedBody = parseBlock(&state, &child, true, errorMessage);

            if (scopedLoop) {
                if (hadPreviousValue)
                    state.variableValues[scopedVar] = previousValue;
                else
                    state.variableValues.remove(scopedVar);
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
                            if (parseOperationLine(ct, &childOp, &childVec, state.variableValues,
                                                   &childExprs, &childLoopVar, &childLoopRange, &childColor)) {
                                SceneDocument::TreeNode childGroup = makeGroupNode(childOp, &state);
                                if (childOp == SceneDocument::TreeNode::Translate)
                                    childGroup.position = childVec;
                                else if (childOp == SceneDocument::TreeNode::Rotate)
                                    childGroup.rotation = childVec;
                                else if (childOp == SceneDocument::TreeNode::Scale)
                                    childGroup.scale = childVec;
                                else if (childOp == SceneDocument::TreeNode::For) {
                                    childGroup.loopVariable = childLoopVar.isEmpty() ? QStringLiteral("i") : childLoopVar;
                                    childGroup.loopRangeExpression = childLoopRange.isEmpty() ? QStringLiteral("[0 : 1 : 3]") : childLoopRange;
                                } else if (childOp == SceneDocument::TreeNode::Color) {
                                    childGroup.color = childColor;
                                }
                                if (childOp == SceneDocument::TreeNode::Translate
                                    || childOp == SceneDocument::TreeNode::Rotate
                                    || childOp == SceneDocument::TreeNode::Scale)
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
