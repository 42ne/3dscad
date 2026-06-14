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
    QHash<QString, ExpressionSyntax::FunctionDef> functionDefs;
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

static bool parseVector3WithExpressions(const QString &text, const QHash<QString, qreal> &varValues, QVector3D *vector, QStringList *expressions = nullptr,
                                        const QHash<QString, ExpressionSyntax::FunctionDef> *fns = nullptr)
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
            if (!ExpressionSyntax::evaluate(trimmed, varValues, &exprVal, nullptr, fns))
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

        qreal channels[4] = {0.0, 0.0, 0.0, 1.0};
        const int nChannels = qMin(parts.size(), 4);
        for (int i = 0; i < nChannels; ++i) {
            if (!parseReal(parts[i].trimmed(), &channels[i]))
                return false;
        }

        QColor c(qBound(0, qRound(channels[0] * 255.0), 255),
                 qBound(0, qRound(channels[1] * 255.0), 255),
                 qBound(0, qRound(channels[2] * 255.0), 255));
        if (parts.size() >= 4)
            c.setAlphaF(qBound(0.0, channels[3], 1.0));
        *color = c;
        return true;
    }

    // color("name", alpha) — named color with optional second alpha argument
    {
        const QStringList allArgs = splitAtTopLevelCommas(text);
        if (allArgs.size() >= 2) {
            QString nameLit = allArgs[0].trimmed();
            if ((nameLit.startsWith('"') && nameLit.endsWith('"'))
                || (nameLit.startsWith('\'') && nameLit.endsWith('\'')))
                nameLit = nameLit.mid(1, nameLit.size() - 2).trimmed();
            QColor nc(nameLit);
            if (nc.isValid()) {
                qreal alpha = 1.0;
                parseReal(allArgs[1].trimmed(), &alpha);
                nc.setAlphaF(qBound(0.0, alpha, 1.0));
                *color = nc;
                return true;
            }
        }
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
// Uses balanced-paren matching so nested calls like part(sin(45), r) are handled.
static bool parseModuleCallLine(const QString &line, QString *name = nullptr, QString *args = nullptr)
{
    // Quick identifier check before the expensive scan.
    static const QRegularExpression identRe("^([A-Za-z_][A-Za-z0-9_]*)\\s*\\(");
    const QRegularExpressionMatch identMatch = identRe.match(line);
    if (!identMatch.hasMatch())
        return false;
    const QString callName = identMatch.captured(1).trimmed();

    static const QStringList reservedCalls = {
        QStringLiteral("cube"),
        QStringLiteral("sphere"),
        QStringLiteral("cylinder"),
        QStringLiteral("circle"),
        QStringLiteral("square"),
        QStringLiteral("polygon"),
        QStringLiteral("linear_extrude"),
        QStringLiteral("rotate_extrude"),
        QStringLiteral("polyhedron"),
        QStringLiteral("echo"),
        QStringLiteral("assert"),
        QStringLiteral("assign")
    };
    if (reservedCalls.contains(callName))
        return false;

    // Find the balanced closing paren.
    QString argsOut;
    if (!extractCallArgs(line, callName, &argsOut))
        return false;

    // After the closing paren must be optional whitespace + ';'
    const int parenEnd = line.indexOf('(', callName.size());
    int depth = 0, closePos = -1;
    for (int i = parenEnd; i < line.size(); ++i) {
        if (line[i] == '(') ++depth;
        else if (line[i] == ')') { if (--depth == 0) { closePos = i; break; } }
    }
    if (closePos < 0)
        return false;
    const QString tail = line.mid(closePos + 1).trimmed();
    if (tail != QStringLiteral(";"))
        return false;

    if (name) *name = callName;
    if (args) *args = argsOut.trimmed();
    return true;
}

static bool parseParamExpression(const QString &text, const QHash<QString, qreal> &varValues, qreal *value, QString *expression,
                                  const QHash<QString, ExpressionSyntax::FunctionDef> *fns = nullptr);

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
                               int *extraLinearExtrudeSlices = nullptr,
                               const QHash<QString, ExpressionSyntax::FunctionDef> *fns = nullptr)
{
    static const QRegularExpression unionRegex("^union\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression differenceRegex("^difference\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression intersectionRegex("^intersection\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression hullRegex("^hull\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression minkowskiRegex("^minkowski\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression translateRegex("^translate\\s*\\((?:v\\s*=\\s*)?\\[([^\\]]+)\\]\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression rotateRegex("^rotate\\s*\\((?:a\\s*=\\s*)?\\[([^\\]]+)\\]\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression scalarRotateBlockRegex("^rotate\\s*\\((?:a\\s*=\\s*)?([^\\[\\]]+?)\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression scaleRegex("^scale\\s*\\((?:v\\s*=\\s*)?\\[([^\\]]+)\\]\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression mirrorRegex("^mirror\\s*\\((?:v\\s*=\\s*)?\\[([^\\]]+)\\]\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression forRegex("^for\\s*\\((.+?)\\)\\s*\\{\\s*$");
    static const QRegularExpression intersectionForRegex("^intersection_for\\s*\\((.+?)\\)\\s*\\{\\s*$");
    static const QRegularExpression colorRegex("^color\\s*\\((.*)\\)\\s*\\{\\s*$");
    static const QRegularExpression linearExtrudeRegex("^linear_extrude\\s*\\((.*)\\)\\s*\\{\\s*$");
    static const QRegularExpression rotateExtrudeRegex("^rotate_extrude\\s*\\((.*)\\)\\s*\\{\\s*$");
    static const QRegularExpression resizeRegex("^resize\\s*\\((.*)\\)\\s*\\{\\s*$");
    static const QRegularExpression offsetRegex("^offset\\s*\\((.*)\\)\\s*\\{\\s*$");

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
        if (!parseParamExpression(heightExpr, varValues, &height, &heightExprStr, fns))
            return false;

        const QString centerExpr = args.value(QStringLiteral("center"), QStringLiteral("false"));
        const bool center = (centerExpr.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);

        const QString twistExpr = args.value(QStringLiteral("twist"), QStringLiteral("0"));
        qreal twist = 0.0;
        QString twistExprStr;
        if (!parseParamExpression(twistExpr, varValues, &twist, &twistExprStr, fns))
            return false;

        const QString slicesExpr = args.value(QStringLiteral("slices"), QStringLiteral("0"));
        qreal slices = 0.0;
        QString slicesExprStr;
        if (!parseParamExpression(slicesExpr, varValues, &slices, &slicesExprStr, fns))
            return false;

        const QString scaleExpr = args.value(QStringLiteral("scale"), QStringLiteral("1.0"));
        qreal scaleVal = 1.0;
        QString scaleExprStr;
        if (!parseParamExpression(scaleExpr, varValues, &scaleVal, &scaleExprStr, fns))
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

    QRegularExpressionMatch rotExtrudeMatch = rotateExtrudeRegex.match(line);
    if (rotExtrudeMatch.hasMatch()) {
        const auto args = parseNamedArgs(rotExtrudeMatch.captured(1), {"angle"});
        const QString angleExpr = args.value(QStringLiteral("angle"), QStringLiteral("360"));
        qreal angle = 360.0;
        QString angleExprStr;
        if (!parseParamExpression(angleExpr, varValues, &angle, &angleExprStr, fns))
            return false;
        *operation = SceneDocument::TreeNode::RotateExtrude;
        if (vector)
            *vector = QVector3D(float(angle), 0.0f, 0.0f);
        if (expressions)
            *expressions = QStringList({angleExprStr});
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
        if (!parseVector3WithExpressions(vectorStr, varValues, vector, expressions, fns))
            return false;
        if (expressions) {
            while (expressions->size() < 4)
                expressions->append(QString());
            (*expressions)[3] = autoStr;
        }
        return true;
    }

    QRegularExpressionMatch offsetMatch = offsetRegex.match(line);
    if (offsetMatch.hasMatch()) {
        const auto args = parseNamedArgs(offsetMatch.captured(1), {"r", "delta", "chamfer"});
        const QString deltaStr = args.value(QStringLiteral("delta"));
        const QString rStr = args.value(QStringLiteral("r"));
        const QString chamferStr = args.value(QStringLiteral("chamfer"));
        const bool useDelta = !deltaStr.trimmed().isEmpty();
        const QString amtSrc = useDelta
                                   ? deltaStr
                                   : (rStr.trimmed().isEmpty() ? QStringLiteral("1") : rStr);
        qreal amount = 1.0;
        QString amtExpr;
        if (!parseParamExpression(amtSrc, varValues, &amount, &amtExpr, fns))
            return false;
        const bool chamfer = (chamferStr.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
        *operation = SceneDocument::TreeNode::Offset;
        if (vector)
            *vector = QVector3D(float(amount), useDelta ? 1.0f : 0.0f, chamfer ? 1.0f : 0.0f);
        return true;
    }

    QRegularExpressionMatch match = translateRegex.match(line);
    if (match.hasMatch()) {
        *operation = SceneDocument::TreeNode::Translate;
        return parseVector3WithExpressions(match.captured(1), varValues, vector, expressions, fns);
    }
    match = rotateRegex.match(line);
    if (match.hasMatch()) {
        *operation = SceneDocument::TreeNode::Rotate;
        return parseVector3WithExpressions(match.captured(1), varValues, vector, expressions, fns);
    }
    match = scalarRotateBlockRegex.match(line);
    if (match.hasMatch()) {
        qreal angle = 0.0;
        const QString angleExpr = match.captured(1).trimmed();
        if (!ExpressionSyntax::evaluate(angleExpr, varValues, &angle, nullptr, fns))
            return false;
        *operation = SceneDocument::TreeNode::Rotate;
        *vector = QVector3D(0, 0, static_cast<float>(angle));
        if (expressions) *expressions = QStringList() << QStringLiteral("0") << QStringLiteral("0") << angleExpr;
        return true;
    }
    match = scaleRegex.match(line);
    if (match.hasMatch()) {
        *operation = SceneDocument::TreeNode::Scale;
        return parseVector3WithExpressions(match.captured(1), varValues, vector, expressions, fns);
    }
    match = mirrorRegex.match(line);
    if (match.hasMatch()) {
        *operation = SceneDocument::TreeNode::Mirror;
        return parseVector3WithExpressions(match.captured(1), varValues, vector, expressions, fns);
    }

    return false;
}

static bool parseVariableLine(const QString &line, QString *name, QString *expression, qreal *value, QString *errorMessage)
{
    static const QRegularExpression regex("^(\\$?[A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*(.+)\\s*;\\s*$");
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

static bool parseParamExpression(const QString &text, const QHash<QString, qreal> &varValues, qreal *value, QString *expression,
                                  const QHash<QString, ExpressionSyntax::FunctionDef> *fns)
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
    if (!ExpressionSyntax::evaluate(trimmed, varValues, &exprVal, nullptr, fns))
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

// ── Vector/Scalar unified expression evaluator ────────────────────────────
// Evaluates expressions that may produce either a scalar or a 3-vector.
// Handles: number literals, scalar vars, vector vars, [a,b,c] literals,
//          +, -, *, / operators, unary -, and parentheses.
struct VecOrScalar {
    bool isVec = false;
    qreal s = 0.0;
    QVector3D v;
    static VecOrScalar fromScalar(qreal x) { VecOrScalar r; r.s = x; return r; }
    static VecOrScalar fromVec(float x, float y, float z) { VecOrScalar r; r.isVec = true; r.v = QVector3D(x,y,z); return r; }
    static VecOrScalar fromVec(QVector3D u) { VecOrScalar r; r.isVec = true; r.v = u; return r; }
};

class VecExprEvaluator {
public:
    VecExprEvaluator(const QString &text,
                     const QHash<QString, qreal> &sVars,
                     const QHash<QString, QVector3D> &vVars,
                     const QHash<QString, ExpressionSyntax::FunctionDef> *fns)
        : m_text(text), m_pos(0), m_sVars(sVars), m_vVars(vVars), m_fns(fns) {}

    bool eval(VecOrScalar *result) {
        if (!evalAddSub(result)) return false;
        skipSpaces();
        return atEnd();
    }

    QString error() const { return m_error; }

private:
    QString m_text;
    int m_pos;
    const QHash<QString, qreal> &m_sVars;
    const QHash<QString, QVector3D> &m_vVars;
    const QHash<QString, ExpressionSyntax::FunctionDef> *m_fns;
    QString m_error;

    bool atEnd() const { return m_pos >= m_text.size(); }
    void skipSpaces() { while (!atEnd() && m_text[m_pos].isSpace()) ++m_pos; }
    bool consume(QChar c) {
        skipSpaces();
        if (atEnd() || m_text[m_pos] != c) return false;
        ++m_pos; return true;
    }

    VecOrScalar addVOS(const VecOrScalar &a, const VecOrScalar &b, bool sub) const {
        if (!a.isVec && !b.isVec) return VecOrScalar::fromScalar(a.s + (sub ? -b.s : b.s));
        QVector3D av = a.isVec ? a.v : QVector3D(a.s, a.s, a.s);
        QVector3D bv = b.isVec ? b.v : QVector3D(b.s, b.s, b.s);
        return VecOrScalar::fromVec(sub ? av - bv : av + bv);
    }
    VecOrScalar mulVOS(const VecOrScalar &a, const VecOrScalar &b) const {
        if (!a.isVec && !b.isVec) return VecOrScalar::fromScalar(a.s * b.s);
        if (a.isVec && !b.isVec)  return VecOrScalar::fromVec(a.v * static_cast<float>(b.s));
        if (!a.isVec && b.isVec)  return VecOrScalar::fromVec(b.v * static_cast<float>(a.s));
        return VecOrScalar::fromVec(a.v.x()*b.v.x(), a.v.y()*b.v.y(), a.v.z()*b.v.z());
    }

    bool evalAddSub(VecOrScalar *result) {
        VecOrScalar left;
        if (!evalMul(&left)) return false;
        while (true) {
            skipSpaces();
            if (consume(QLatin1Char('+'))) {
                VecOrScalar right; if (!evalMul(&right)) return false;
                left = addVOS(left, right, false);
            } else if (consume(QLatin1Char('-'))) {
                VecOrScalar right; if (!evalMul(&right)) return false;
                left = addVOS(left, right, true);
            } else break;
        }
        *result = left; return true;
    }

    bool evalMul(VecOrScalar *result) {
        VecOrScalar left;
        if (!evalUnary(&left)) return false;
        while (true) {
            skipSpaces();
            if (consume(QLatin1Char('*'))) {
                VecOrScalar right; if (!evalUnary(&right)) return false;
                left = mulVOS(left, right);
            } else if (consume(QLatin1Char('/'))) {
                VecOrScalar right; if (!evalUnary(&right)) return false;
                if (!right.isVec && right.s == 0.0) { m_error = QStringLiteral("Division by zero"); return false; }
                if (right.isVec) { m_error = QStringLiteral("Cannot divide by a vector"); return false; }
                left = left.isVec ? VecOrScalar::fromVec(left.v / static_cast<float>(right.s))
                                  : VecOrScalar::fromScalar(left.s / right.s);
            } else break;
        }
        *result = left; return true;
    }

    bool evalUnary(VecOrScalar *result) {
        skipSpaces();
        bool neg = false;
        while (!atEnd() && m_text[m_pos] == QLatin1Char('-')) { neg = !neg; ++m_pos; skipSpaces(); }
        VecOrScalar val; if (!evalPrimary(&val)) return false;
        if (neg) { val = val.isVec ? VecOrScalar::fromVec(-val.v) : VecOrScalar::fromScalar(-val.s); }
        *result = val; return true;
    }

    bool evalPrimary(VecOrScalar *result) {
        skipSpaces();

        if (consume(QLatin1Char('('))) {
            if (!evalAddSub(result)) return false;
            if (!consume(QLatin1Char(')'))) { m_error = QStringLiteral("Expected ')'"); return false; }
            return true;
        }

        if (!atEnd() && m_text[m_pos] == QLatin1Char('[')) {
            ++m_pos;
            QStringList parts;
            int depth = 1, start = m_pos;
            for (; m_pos < m_text.size(); ++m_pos) {
                if (m_text[m_pos] == QLatin1Char('['))      ++depth;
                else if (m_text[m_pos] == QLatin1Char(']')) { if (--depth == 0) break; }
                else if (m_text[m_pos] == QLatin1Char(',') && depth == 1) {
                    parts.append(m_text.mid(start, m_pos - start).trimmed());
                    start = m_pos + 1;
                }
            }
            if (depth != 0) { m_error = QStringLiteral("Unclosed '['"); return false; }
            parts.append(m_text.mid(start, m_pos - start).trimmed());
            ++m_pos;
            if (parts.size() != 3) { m_error = QStringLiteral("Vector must have 3 components"); return false; }
            qreal x = 0, y = 0, z = 0;
            if (!ExpressionSyntax::evaluate(parts[0], m_sVars, &x, nullptr, m_fns)
                || !ExpressionSyntax::evaluate(parts[1], m_sVars, &y, nullptr, m_fns)
                || !ExpressionSyntax::evaluate(parts[2], m_sVars, &z, nullptr, m_fns))
            { m_error = QStringLiteral("Bad vector component"); return false; }
            *result = VecOrScalar::fromVec(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
            return true;
        }

        // Number literal
        {
            const int start = m_pos;
            bool hasDigit = false;
            while (!atEnd() && m_text[m_pos].isDigit()) { hasDigit = true; ++m_pos; }
            if (!atEnd() && m_text[m_pos] == QLatin1Char('.')) {
                ++m_pos;
                while (!atEnd() && m_text[m_pos].isDigit()) { hasDigit = true; ++m_pos; }
            }
            if (hasDigit) {
                bool ok = false;
                qreal val = m_text.mid(start, m_pos - start).toDouble(&ok);
                if (!ok) { m_pos = start; } else { *result = VecOrScalar::fromScalar(val); return true; }
            }
        }

        // Identifier: variable or function call
        if (!atEnd() && (m_text[m_pos].isLetter() || m_text[m_pos] == QLatin1Char('_') || m_text[m_pos] == QLatin1Char('$'))) {
            const int start = m_pos++;
            while (!atEnd() && (m_text[m_pos].isLetterOrNumber() || m_text[m_pos] == QLatin1Char('_') || m_text[m_pos] == QLatin1Char('$')))
                ++m_pos;
            const QString name = m_text.mid(start, m_pos - start);
            skipSpaces();
            if (!atEnd() && m_text[m_pos] == QLatin1Char('(')) {
                // function call: delegate fully to scalar evaluator
                int depth = 0, callStart = start;
                int i = m_pos;
                for (; i < m_text.size(); ++i) {
                    if (m_text[i] == QLatin1Char('(')) ++depth;
                    else if (m_text[i] == QLatin1Char(')')) { if (--depth == 0) { ++i; break; } }
                }
                const QString callStr = m_text.mid(callStart, i - callStart);
                qreal val = 0.0;
                if (!ExpressionSyntax::evaluate(callStr, m_sVars, &val, nullptr, m_fns))
                { m_error = QStringLiteral("Unknown function '%1'").arg(name); return false; }
                m_pos = i;
                *result = VecOrScalar::fromScalar(val);
                return true;
            }
            if (m_vVars.contains(name)) { *result = VecOrScalar::fromVec(m_vVars[name]); return true; }
            qreal val = 0.0;
            if (!ExpressionSyntax::evaluate(name, m_sVars, &val, nullptr, m_fns))
            { m_error = QStringLiteral("Unknown identifier '%1'").arg(name); return false; }
            *result = VecOrScalar::fromScalar(val);
            return true;
        }

        m_error = QStringLiteral("Unexpected token at position %1").arg(m_pos + 1);
        return false;
    }
};

// Evaluates expr as a 3-vector. Returns false if it can't be resolved to a vector.
static bool evaluateVectorExpression(const QString &expr,
                                     const QHash<QString, qreal> &sVars,
                                     const QHash<QString, QVector3D> &vVars,
                                     QVector3D *result,
                                     const QHash<QString, ExpressionSyntax::FunctionDef> *fns = nullptr)
{
    VecExprEvaluator ev(expr, sVars, vVars, fns);
    VecOrScalar r;
    if (!ev.eval(&r) || !r.isVec) return false;
    *result = r.v;
    return true;
}

// If line = "transform(...)child..." (no '{' suffix), splits into *tfPart and *childPart.
static bool trySplitInlineTransformChild(const QString &line, QString *tfPart, QString *childPart)
{
    static const QStringList kws = {
        QStringLiteral("translate"), QStringLiteral("rotate"),
        QStringLiteral("scale"),     QStringLiteral("mirror"),
        QStringLiteral("color")
    };
    for (const QString &kw : kws) {
        if (!line.startsWith(kw)) continue;
        int i = kw.size();
        while (i < line.size() && line[i].isSpace()) ++i;
        if (i >= line.size() || line[i] != QLatin1Char('(')) continue;
        int depth = 0;
        for (; i < line.size(); ++i) {
            if (line[i] == QLatin1Char('('))      ++depth;
            else if (line[i] == QLatin1Char(')')) { if (--depth == 0) break; }
        }
        if (depth != 0 || i >= line.size()) continue;
        ++i; // past ')'
        int j = i;
        while (j < line.size() && line[j].isSpace()) ++j;
        if (j >= line.size() || line[j] == QLatin1Char('{')) continue; // block form, not inline
        *tfPart   = line.left(i).trimmed();
        *childPart = line.mid(j);
        return true;
    }
    return false;
}

// Splits "if(cond)child_stmt" into condition and child (brace-free form).
// Returns false for block-form "if(...){" or lines that don't start with "if".
static bool splitBraceFreeIfStatement(const QString &line, QString *condStr, QString *childStr)
{
    if (!line.startsWith(QStringLiteral("if"))) return false;
    int i = 2;
    while (i < line.size() && line[i].isSpace()) ++i;
    if (i >= line.size() || line[i] != QLatin1Char('(')) return false;
    int depth = 0, condStart = i + 1;
    for (; i < line.size(); ++i) {
        if (line[i] == QLatin1Char('('))      ++depth;
        else if (line[i] == QLatin1Char(')')) { if (--depth == 0) break; }
    }
    if (depth != 0 || i >= line.size()) return false;
    *condStr = line.mid(condStart, i - condStart).trimmed();
    int j = i + 1;
    while (j < line.size() && line[j].isSpace()) ++j;
    if (j < line.size() && line[j] == QLatin1Char('{')) return false; // block form
    *childStr = (j < line.size()) ? line.mid(j).trimmed() : QString();
    return !condStr->isEmpty();
}

// Forward declarations needed by parseSingleStatementString
static bool parsePrimitiveLine(const QString &, ShapeNode *, ParserState *, QString *);
static bool parseModuleCallLine(const QString &, QString *, QString *);
static SceneDocument::TreeNode makeGroupNode(SceneDocument::TreeNode::Operation, ParserState *);
static SceneDocument::TreeNode makeModuleCallNode(int, const QString &, const QString &, ParserState *);
static SceneDocument::TreeNode makePrimitiveNode(const ShapeNode &, ParserState *);
static bool parseBraceFreeOperationLine(const QString &, SceneDocument::TreeNode::Operation *,
                                        QVector3D *, const QHash<QString, qreal> &,
                                        QStringList *, const QHash<QString, ExpressionSyntax::FunctionDef> *);
static bool trySplitInlineTransformChild(const QString &, QString *, QString *);

// Parses a single statement string (module call, inline transform chain, or primitive).
// Used for brace-free if/else bodies.
static void parseSingleStatementString(const QString &stmt,
                                        SceneDocument::TreeNode *parent,
                                        ParserState *state,
                                        int lineNumber,
                                        QString *errorMessage)
{
    if (stmt.isEmpty()) return;

    QString ifCond, ifChild;
    if (splitBraceFreeIfStatement(stmt, &ifCond, &ifChild)) {
        SceneDocument::TreeNode condNode = makeGroupNode(SceneDocument::TreeNode::Conditional, state);
        condNode.conditionExpression = ifCond;
        SceneDocument::TreeNode trueBr = makeGroupNode(SceneDocument::TreeNode::Union, state);
        if (!ifChild.isEmpty())
            parseSingleStatementString(ifChild, &trueBr, state, lineNumber, errorMessage);
        condNode.children.append(trueBr);
        parent->children.append(condNode);
        return;
    }

    QString tfPart, remaining;
    if (trySplitInlineTransformChild(stmt, &tfPart, &remaining)) {
        SceneDocument::TreeNode::Operation op = SceneDocument::TreeNode::Union;
        QVector3D vec; QStringList exprs;
        if (parseBraceFreeOperationLine(tfPart, &op, &vec, state->variableValues, &exprs, &state->functionDefs)) {
            SceneDocument::TreeNode grp = makeGroupNode(op, state);
            if (op == SceneDocument::TreeNode::Translate) grp.position = vec;
            else if (op == SceneDocument::TreeNode::Rotate) grp.rotation = vec;
            else if (op == SceneDocument::TreeNode::Scale)  grp.scale    = vec;
            grp.transformExpressions = exprs;
            parent->children.append(grp);
            SceneDocument::TreeNode *inner = &parent->children.last();
            while (true) {
                QString nextTf, nextChild;
                if (!trySplitInlineTransformChild(remaining, &nextTf, &nextChild)) break;
                SceneDocument::TreeNode::Operation cop = SceneDocument::TreeNode::Union;
                QVector3D cvec; QStringList cexprs;
                if (!parseBraceFreeOperationLine(nextTf, &cop, &cvec, state->variableValues, &cexprs, &state->functionDefs)) break;
                SceneDocument::TreeNode cgrp = makeGroupNode(cop, state);
                if (cop == SceneDocument::TreeNode::Translate) cgrp.position = cvec;
                else if (cop == SceneDocument::TreeNode::Rotate) cgrp.rotation = cvec;
                else if (cop == SceneDocument::TreeNode::Scale)  cgrp.scale    = cvec;
                cgrp.transformExpressions = cexprs;
                inner->children.append(cgrp);
                inner = &inner->children.last();
                remaining = nextChild;
            }
            QString cc, ca;
            if (parseModuleCallLine(remaining, &cc, &ca)) {
                inner->children.append(makeModuleCallNode(0, cc, ca, state));
            } else {
                ShapeNode cs; QString cpe;
                if (parsePrimitiveLine(remaining, &cs, state, &cpe)) {
                    state->shapes.append(cs);
                    inner->children.append(makePrimitiveNode(cs, state));
                } else if (!cpe.isEmpty() && errorMessage && errorMessage->isEmpty()) {
                    *errorMessage = cpe.arg(lineNumber);
                }
            }
            return;
        }
    }
    QString cc, ca;
    if (parseModuleCallLine(stmt, &cc, &ca)) {
        parent->children.append(makeModuleCallNode(0, cc, ca, state));
        return;
    }
    ShapeNode cs; QString cpe;
    if (parsePrimitiveLine(stmt, &cs, state, &cpe)) {
        state->shapes.append(cs);
        parent->children.append(makePrimitiveNode(cs, state));
    }
}

// Returns true and fills shape if this line is a cube/sphere/cylinder call (flexible args).
// Returns false if the line is not a primitive call at all.
// Sets *errorMessage and returns false if the line IS a primitive call but has invalid args.
static bool parsePrimitiveLine(const QString &line, ShapeNode *shape, ParserState *state,
                               QString *errorMessage)
{
    QString argsStr;
    const QHash<QString, ExpressionSyntax::FunctionDef> *fns = &state->functionDefs;

    // ── Cube ────────────────────────────────────────────────────────────────
    if (extractCallArgs(line, "cube", &argsStr) && line.endsWith(';')) {
        // Accept cube([x, y, z]), cube(size=[x,y,z]), cube(N), cube(N, center=...)
        const QStringList allArgs = splitAtTopLevelCommas(argsStr);
        // If named arg "size=" is present, rewrite argsStr to just the vector part.
        const auto namedCubeArgs = parseNamedArgs(argsStr, {"size", "center"});
        QString effectiveArgsStr = argsStr;
        if (namedCubeArgs.contains(QStringLiteral("size")))
            effectiveArgsStr = namedCubeArgs[QStringLiteral("size")];
        const QString firstArg = allArgs.value(0).trimmed();
        static const QRegularExpression vecRe("^\\[([^\\]]+)\\]");
        QRegularExpressionMatch m = vecRe.match(effectiveArgsStr);
        if (m.hasMatch()) {
            const QString vecInner = m.captured(1); // inner content of [...]
            const QStringList parts = splitAtTopLevelCommas(vecInner);
            if (parts.size() != 3) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("cube on line %1: expected 3 components");
                return false;
            }
            qreal x = 0.0, y = 0.0, z = 0.0;
            QString xe, ye, ze;
            if (!parseParamExpression(parts[0], state->variableValues, &x, &xe, fns)
                || !parseParamExpression(parts[1], state->variableValues, &y, &ye, fns)
                || !parseParamExpression(parts[2], state->variableValues, &z, &ze, fns)) {
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
        if (parseParamExpression(firstArg, state->variableValues, &scalar, &scalarExpr, fns)) {
            shape->id = state->nextShapeId++;
            shape->type = ShapeNode::Cube;
            shape->name = QStringLiteral("Cube %1").arg(shape->id);
            shape->size = QVector3D(scalar, scalar, scalar);
            shape->parameterExpressions = QStringList({scalarExpr, scalarExpr, scalarExpr});
            centerFromArgs(argsStr, &shape->center);
            return true;
        }
        // Vector arithmetic expression: size-2*[d,d,0], v1+v2, etc.
        {
            QVector3D vecResult;
            if (evaluateVectorExpression(effectiveArgsStr, state->variableValues, state->vectorVariableValues, &vecResult, fns)) {
                shape->id   = state->nextShapeId++;
                shape->type = ShapeNode::Cube;
                shape->name = QStringLiteral("Cube %1").arg(shape->id);
                shape->size = vecResult;
                shape->parameterExpressions = QStringList({
                    QString::number(vecResult.x()),
                    QString::number(vecResult.y()),
                    QString::number(vecResult.z())});
                centerFromArgs(argsStr, &shape->center);
                return true;
            }
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
        if (!parseParamExpression(paramStr, state->variableValues, &val, &valExpr, fns)) {
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
        if (!parseParamExpression(paramStr, state->variableValues, &val, &valExpr, fns)) {
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
        if (!parseParamExpression(parts[0], state->variableValues, &x, &xe, fns)
            || !parseParamExpression(parts[1], state->variableValues, &y, &ye, fns)) {
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
        // Helper: find matching ']' for an '[' at position start.
        auto findMatchingBracket = [](const QString &s, int start) -> int {
            int depth = 0;
            for (int i = start; i < s.size(); ++i) {
                if (s[i] == QLatin1Char('[')) ++depth;
                else if (s[i] == QLatin1Char(']')) { if (--depth == 0) return i; }
            }
            return -1;
        };

        const auto polygonArgs = parseNamedArgs(argsStr, {"points"});
        const QString pointsStr = polygonArgs.value(QStringLiteral("points"));
        const QString pathsStr  = polygonArgs.value(QStringLiteral("paths"));

        // ── Parse points ──────────────────────────────────────────────────
        const int ptsStart = pointsStr.isEmpty()
            ? argsStr.indexOf(QLatin1Char('['))
            : pointsStr.indexOf(QLatin1Char('['));
        const int ptsEnd = ptsStart >= 0 ? findMatchingBracket(
            pointsStr.isEmpty() ? argsStr : pointsStr, ptsStart) : -1;
        if (ptsStart < 0 || ptsEnd <= ptsStart) {
            if (errorMessage)
                *errorMessage = QStringLiteral("polygon on line %1: missing or malformed points");
            return false;
        }
        const QString &ptsSource = pointsStr.isEmpty() ? argsStr : pointsStr;
        const QString ptsInner = ptsSource.mid(ptsStart + 1, ptsEnd - ptsStart - 1);
        QVector<QVector3D> points;
        int pos = 0;
        while (pos < ptsInner.size()) {
            const int start = ptsInner.indexOf(QLatin1Char('['), pos);
            if (start < 0) break;
            const int end = findMatchingBracket(ptsInner, start);
            if (end < 0) break;
            const QStringList coords = splitAtTopLevelCommas(ptsInner.mid(start + 1, end - start - 1));
            if (coords.size() != 2) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("polygon on line %1: expected [x, y] points");
                return false;
            }
            qreal x = 0.0, y = 0.0;
            QString xe, ye;
            if (!parseParamExpression(coords[0], state->variableValues, &x, &xe, fns)
                || !parseParamExpression(coords[1], state->variableValues, &y, &ye, fns)) {
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

        // ── Parse paths (optional) ────────────────────────────────────────
        // paths=[[i0,i1,...], [j0,j1,...]] where first path = outer, rest = holes.
        QVector<QVector<int>> paths;
        if (!pathsStr.isEmpty()) {
            const int outerStart = pathsStr.indexOf(QLatin1Char('['));
            const int outerEnd   = outerStart >= 0 ? findMatchingBracket(pathsStr, outerStart) : -1;
            if (outerStart >= 0 && outerEnd > outerStart) {
                const QString outerInner = pathsStr.mid(outerStart + 1, outerEnd - outerStart - 1);
                int p = 0;
                while (p < outerInner.size()) {
                    const int s = outerInner.indexOf(QLatin1Char('['), p);
                    if (s < 0) break;
                    const int e = findMatchingBracket(outerInner, s);
                    if (e < 0) break;
                    const QStringList idxStrs = splitAtTopLevelCommas(outerInner.mid(s + 1, e - s - 1));
                    QVector<int> path;
                    for (const QString &is : idxStrs) {
                        bool ok = false;
                        const int idx = is.trimmed().toInt(&ok);
                        if (ok) path.append(idx);
                    }
                    if (path.size() >= 3) paths.append(path);
                    p = e + 1;
                }
            }
        }

        shape->id = state->nextShapeId++;
        shape->type = ShapeNode::Polygon2D;
        shape->name = QStringLiteral("Polygon %1").arg(shape->id);
        shape->polyhedronPoints = points;
        if (!paths.isEmpty())
            shape->polyhedronFaces = paths; // paths[0]=outer, paths[1..]=holes
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
        if (!parseParamExpression(args["h"], state->variableValues, &h, &he, fns)) {
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
            if (!parseParamExpression(p1Str, state->variableValues, &v1, &e1, fns)
                || !parseParamExpression(p2Str, state->variableValues, &v2, &e2, fns)) {
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
        if (!parseParamExpression(rStr, state->variableValues, &rVal, &rExpr, fns)) {
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

static bool parsePolyhedron(const QString &line, PolyhedronData *data, QString *errorMessage,
                            const QHash<QString, qreal> *varValues = nullptr,
                            const QHash<QString, ExpressionSyntax::FunctionDef> *fns = nullptr)
{
    QString argsStr;
    if (!extractCallArgs(line, "polyhedron", &argsStr) || !line.endsWith(';'))
        return false;

    const auto args = parseNamedArgs(argsStr, {"points", "faces"});
    // "convexity" is a rendering hint only — parsed and silently ignored.
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
        if (varValues) {
            static const QHash<QString, qreal> emptyVars;
            qreal rv = 0.0; QString re;
            for (int ci = 0; ci < 3; ++ci) {
                if (!parseParamExpression(coords[ci].trimmed(), *varValues, &rv, &re, fns))
                    return false;
                f[ci] = static_cast<float>(rv);
            }
        } else {
            if (!parseFloat(coords[0], &f[0]) || !parseFloat(coords[1], &f[1]) || !parseFloat(coords[2], &f[2]))
                return false;
        }
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
                                        QStringList *expressions,
                                        const QHash<QString, ExpressionSyntax::FunctionDef> *fns = nullptr)
{
    static const QRegularExpression translateRe("^translate\\s*\\((?:v\\s*=\\s*)?\\[([^\\]]+)\\]\\s*\\)\\s*$");
    static const QRegularExpression rotateRe("^rotate\\s*\\((?:a\\s*=\\s*)?\\[([^\\]]+)\\]\\s*\\)\\s*$");
    static const QRegularExpression scaleRe("^scale\\s*\\((?:v\\s*=\\s*)?\\[([^\\]]+)\\]\\s*\\)\\s*$");
    static const QRegularExpression mirrorRe("^mirror\\s*\\((?:v\\s*=\\s*)?\\[([^\\]]+)\\]\\s*\\)\\s*$");
    // scalar rotate: rotate(expr) or rotate(a=expr) — rotates around Z
    static const QRegularExpression scalarRotateRe("^rotate\\s*\\((?:a\\s*=\\s*)?([^\\[\\]]+?)\\s*\\)\\s*$");

    QRegularExpressionMatch m = translateRe.match(line);
    if (m.hasMatch()) {
        *operation = SceneDocument::TreeNode::Translate;
        return parseVector3WithExpressions(m.captured(1), varValues, vector, expressions, fns);
    }
    m = rotateRe.match(line);
    if (m.hasMatch()) {
        *operation = SceneDocument::TreeNode::Rotate;
        return parseVector3WithExpressions(m.captured(1), varValues, vector, expressions, fns);
    }
    m = scalarRotateRe.match(line);
    if (m.hasMatch()) {
        qreal angle = 0.0;
        const QString angleExpr = m.captured(1).trimmed();
        if (!ExpressionSyntax::evaluate(angleExpr, varValues, &angle, nullptr, fns))
            return false;
        *operation = SceneDocument::TreeNode::Rotate;
        *vector = QVector3D(0, 0, static_cast<float>(angle));
        if (expressions) *expressions = QStringList() << QStringLiteral("0") << QStringLiteral("0") << angleExpr;
        return true;
    }
    m = scaleRe.match(line);
    if (m.hasMatch()) {
        *operation = SceneDocument::TreeNode::Scale;
        return parseVector3WithExpressions(m.captured(1), varValues, vector, expressions, fns);
    }
    m = mirrorRe.match(line);
    if (m.hasMatch()) {
        *operation = SceneDocument::TreeNode::Mirror;
        return parseVector3WithExpressions(m.captured(1), varValues, vector, expressions, fns);
    }
    return false;
}

// Returns true if the line starts with a keyword we know how to parse.
// Used to distinguish "known-but-invalid" from "completely unknown".
static bool startsWithKnownKeyword(const QString &line)
{
    static const QStringList known = {
        "translate", "rotate", "scale", "mirror",
        "union", "difference", "intersection", "hull", "minkowski", "for", "intersection_for", "let", "assign", "if", "else", "color", "linear_extrude", "rotate_extrude",
        "resize", "offset",
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

        // ── echo(...); / assert(...); — no geometry, skip silently ─────────
        if (line.startsWith(QLatin1String("echo")) || line.startsWith(QLatin1String("assert"))) {
            continue;
        }

        // ── function name(params) = expr; — store for evaluator ─────────────
        {
            static const QRegularExpression funcRe(
                "^function\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\(([^)]*)\\)\\s*=\\s*(.+)\\s*;\\s*$");
            static const QRegularExpression funcStartRe(
                "^function\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\(([^)]*)\\)\\s*=");
            QString funcLine = line;
            if (!funcRe.match(funcLine).hasMatch() && funcStartRe.match(funcLine).hasMatch()) {
                while (state->index < state->lines.size()) {
                    const QString &next = state->lines[state->index].text;
                    ++state->index;
                    funcLine += QLatin1Char(' ') + next;
                    if (next.trimmed().endsWith(QLatin1Char(';')))
                        break;
                }
            }
            const QRegularExpressionMatch fm = funcRe.match(funcLine);
            if (fm.hasMatch()) {
                ExpressionSyntax::FunctionDef def;
                const QStringList rawParams = splitAtTopLevelCommas(fm.captured(2));
                for (const QString &p : rawParams) {
                    const QString pt = p.trimmed();
                    if (!pt.isEmpty()) def.params.append(pt);
                }
                def.body = fm.captured(3).trimmed();
                state->functionDefs[fm.captured(1)] = def;
                continue;
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

        // ── Brace-free assign/let: strip "assign(x=expr)..." prefix, bind vars ──
        {
            bool stripped = true;
            while (stripped) {
                stripped = false;
                const QString kw = line.startsWith(QStringLiteral("assign")) ? QStringLiteral("assign")
                                 : line.startsWith(QStringLiteral("let"))    ? QStringLiteral("let")
                                 : QString();
                if (kw.isEmpty() || line.trimmed().endsWith(QLatin1Char('{'))) break;
                QString assignArgs;
                if (!extractCallArgs(line, kw, &assignArgs)) break;
                // Find the end position of "kw(...)" in line
                int pos = kw.size();
                while (pos < line.size() && line[pos].isSpace()) ++pos;
                int depth = 0;
                for (; pos < line.size(); ++pos) {
                    if (line[pos] == QLatin1Char('('))      ++depth;
                    else if (line[pos] == QLatin1Char(')')) { if (--depth == 0) { ++pos; break; } }
                }
                for (const QString &pair : splitAtTopLevelCommas(assignArgs)) {
                    const int eq = pair.indexOf(QLatin1Char('='));
                    if (eq <= 0) continue;
                    const QString vn = pair.left(eq).trimmed();
                    const QString ve = pair.mid(eq + 1).trimmed();
                    qreal val = 0.0;
                    ExpressionSyntax::evaluate(ve, state->variableValues, &val, nullptr, &state->functionDefs);
                    state->variableValues[vn] = val;
                }
                const QString rest = line.mid(pos).trimmed();
                if (rest.isEmpty()) {
                    if (state->index >= state->lines.size()) break;
                    line = state->lines[state->index++].text;
                } else {
                    line = rest;
                }
                stripped = true;
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
            if (ExpressionSyntax::evaluate(variableExpression, state->variableValues, &evaluatedValue, nullptr, &state->functionDefs))
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

        // ── let/assign (x = expr) { ... } ── scoped variables, no tree node ─
        {
            static const QRegularExpression letRe("^(?:let|assign)\\s*\\((.+)\\)\\s*\\{\\s*$");
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
                    if (ExpressionSyntax::evaluate(expr, state->variableValues, &val, nullptr, &state->functionDefs)) {
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

        // ── if (cond) { ... } else { ... } ── tree-preserving ──────────────
        {
            static const QRegularExpression ifRe("^if\\s*\\((.+)\\)\\s*\\{\\s*$");
            static const QRegularExpression elseRe("^else\\s*\\{\\s*$");
            static const QRegularExpression elseIfRe("^else\\s+if\\s*\\((.+)\\)\\s*\\{\\s*$");
            QRegularExpressionMatch im = ifRe.match(line);
            if (im.hasMatch()) {
                // Collect all if / else-if / else clauses, then assemble nested Conditional nodes.
                struct IfClause { QString cond; SceneDocument::TreeNode trueBlock; };
                QVector<IfClause> clauses;
                SceneDocument::TreeNode elseBlock;
                bool hasElse = false;

                IfClause initClause;
                initClause.cond = im.captured(1).trimmed();
                initClause.trueBlock = makeGroupNode(SceneDocument::TreeNode::Union, state);
                if (!parseBlock(state, &initClause.trueBlock, true, errorMessage))
                    return false;
                clauses.append(initClause);

                while (state->index < state->lines.size()) {
                    const QString nxt = state->lines[state->index].text;
                    const QRegularExpressionMatch eif = elseIfRe.match(nxt);
                    if (eif.hasMatch()) {
                        ++state->index;
                        IfClause c;
                        c.cond = eif.captured(1).trimmed();
                        c.trueBlock = makeGroupNode(SceneDocument::TreeNode::Union, state);
                        if (!parseBlock(state, &c.trueBlock, true, errorMessage))
                            return false;
                        clauses.append(c);
                        continue;
                    }
                    if (elseRe.match(nxt).hasMatch()) {
                        ++state->index;
                        elseBlock = makeGroupNode(SceneDocument::TreeNode::Union, state);
                        elseBlock.isElseBranch = true;
                        if (!parseBlock(state, &elseBlock, true, errorMessage))
                            return false;
                        hasElse = true;
                    }
                    break;
                }

                // Assemble nested Conditional nodes bottom-up.
                // if(a){A} else if(b){B} else {C}
                // → Cond(a, trueBr(A), elseBr( Cond(b, trueBr(B), elseBr(C)) ))
                SceneDocument::TreeNode assembled;
                for (int ci = clauses.size() - 1; ci >= 0; --ci) {
                    SceneDocument::TreeNode cond = makeGroupNode(SceneDocument::TreeNode::Conditional, state);
                    cond.conditionExpression = clauses[ci].cond;
                    cond.children.append(clauses[ci].trueBlock);
                    if (ci == clauses.size() - 1) {
                        if (hasElse)
                            cond.children.append(elseBlock);
                    } else {
                        SceneDocument::TreeNode elseBr = makeGroupNode(SceneDocument::TreeNode::Union, state);
                        elseBr.isElseBranch = true;
                        elseBr.children.append(assembled);
                        cond.children.append(elseBr);
                    }
                    assembled = cond;
                }
                parent->children.append(assembled);
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
                               &extrudeCenter, &extrudeSlices, &state->functionDefs)) {
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
                group.scale = transformVector;            // x=height, y=twist, z=scaleVal
                group.linearExtrudeCenter = extrudeCenter;
                group.linearExtrudeSlices = extrudeSlices;
                group.linearExtrudeTwist   = transformVector.y();
                group.linearExtrudeScaleVal = transformVector.z() > 0.001f ? transformVector.z() : 1.0f;
            } else if (operation == SceneDocument::TreeNode::RotateExtrude) {
                group.scale = transformVector; // scale.x() = angle
            } else if (operation == SceneDocument::TreeNode::Resize) {
                group.scale = transformVector;
            } else if (operation == SceneDocument::TreeNode::Offset) {
                group.offsetAmount = transformVector.x();
                group.offsetUseDelta = transformVector.y() > 0.5f;
                group.offsetChamfer = transformVector.z() > 0.5f;
            }
            if (operation == SceneDocument::TreeNode::Translate
                || operation == SceneDocument::TreeNode::Rotate
                || operation == SceneDocument::TreeNode::Scale
                || operation == SceneDocument::TreeNode::Mirror
                || operation == SceneDocument::TreeNode::LinearExtrude
                || operation == SceneDocument::TreeNode::RotateExtrude
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
            if (!parsePolyhedron(polyLine, &polyData, &polyError, &state->variableValues, &state->functionDefs)) {
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

        // ── Brace-free if(cond)statement ────────────────────────────────────
        {
            QString bfCond, bfChild;
            if (splitBraceFreeIfStatement(line, &bfCond, &bfChild)) {
                SceneDocument::TreeNode condNode = makeGroupNode(SceneDocument::TreeNode::Conditional, state);
                condNode.conditionExpression = bfCond;
                SceneDocument::TreeNode trueBr = makeGroupNode(SceneDocument::TreeNode::Union, state);
                if (!bfChild.isEmpty())
                    parseSingleStatementString(bfChild, &trueBr, state, current.number, errorMessage);
                condNode.children.append(trueBr);

                // Look ahead for else / else-if (brace-free or block form)
                while (state->index < state->lines.size()) {
                    const QString nextLine = state->lines[state->index].text;
                    if (!nextLine.startsWith(QStringLiteral("else"))) break;
                    ++state->index;
                    const QString afterElse = nextLine.mid(4).trimmed();

                    SceneDocument::TreeNode elseBr = makeGroupNode(SceneDocument::TreeNode::Union, state);
                    elseBr.isElseBranch = true;
                    static const QRegularExpression inlineElseIfBlockRe("^if\\s*\\((.+)\\)\\s*\\{\\s*$");
                    const QRegularExpressionMatch inlineElseIfBlock = inlineElseIfBlockRe.match(afterElse);
                    if (inlineElseIfBlock.hasMatch()) {
                        SceneDocument::TreeNode nestedCond = makeGroupNode(SceneDocument::TreeNode::Conditional, state);
                        nestedCond.conditionExpression = inlineElseIfBlock.captured(1).trimmed();
                        SceneDocument::TreeNode nestedTrue = makeGroupNode(SceneDocument::TreeNode::Union, state);
                        if (!parseBlock(state, &nestedTrue, true, errorMessage)) return false;
                        nestedCond.children.append(nestedTrue);
                        elseBr.children.append(nestedCond);
                    } else if (afterElse.endsWith(QLatin1Char('{'))) {
                        if (!parseBlock(state, &elseBr, true, errorMessage)) return false;
                    } else if (!afterElse.isEmpty()) {
                        parseSingleStatementString(afterElse, &elseBr, state, current.number, errorMessage);
                    }
                    condNode.children.append(elseBr);
                    break;
                }
                parent->children.append(condNode);
                continue;
            }
        }

        // ── Brace-free single-child transform (OpenSCAD shorthand) ──────────
        // e.g.  translate([x, y, 0])
        //         cylinder(h=1, r=2, center=true);
        // Also handles inline: translate([x,y,0])cube(...);
        {
            // Inline chain: transform(...)transform(...)...primitive(...) on one line
            {
                QString tfPart, childPart;
                if (trySplitInlineTransformChild(line, &tfPart, &childPart)) {
                    SceneDocument::TreeNode::Operation inlineOp = SceneDocument::TreeNode::Union;
                    QVector3D inlineVec;
                    QStringList inlineExprs;
                    if (parseBraceFreeOperationLine(tfPart, &inlineOp, &inlineVec, state->variableValues, &inlineExprs, &state->functionDefs)) {
                        SceneDocument::TreeNode inlineGroup = makeGroupNode(inlineOp, state);
                        if (inlineOp == SceneDocument::TreeNode::Translate) inlineGroup.position = inlineVec;
                        else if (inlineOp == SceneDocument::TreeNode::Rotate) inlineGroup.rotation = inlineVec;
                        else if (inlineOp == SceneDocument::TreeNode::Scale)  inlineGroup.scale    = inlineVec;
                        inlineGroup.transformExpressions = inlineExprs;
                        parent->children.append(inlineGroup);
                        SceneDocument::TreeNode *inlineInner = &parent->children.last();

                        // Consume additional transforms chained inline on the same line
                        QString remaining = childPart;
                        while (true) {
                            QString nextTf, nextChild;
                            if (!trySplitInlineTransformChild(remaining, &nextTf, &nextChild)) break;
                            SceneDocument::TreeNode::Operation chainOp = SceneDocument::TreeNode::Union;
                            QVector3D chainVec;
                            QStringList chainExprs;
                            if (!parseBraceFreeOperationLine(nextTf, &chainOp, &chainVec, state->variableValues, &chainExprs, &state->functionDefs))
                                break;
                            SceneDocument::TreeNode chainGroup = makeGroupNode(chainOp, state);
                            if (chainOp == SceneDocument::TreeNode::Translate) chainGroup.position = chainVec;
                            else if (chainOp == SceneDocument::TreeNode::Rotate) chainGroup.rotation = chainVec;
                            else if (chainOp == SceneDocument::TreeNode::Scale)  chainGroup.scale    = chainVec;
                            chainGroup.transformExpressions = chainExprs;
                            inlineInner->children.append(chainGroup);
                            inlineInner = &inlineInner->children.last();
                            remaining = nextChild;
                        }

                        QString cc, ca;
                        if (parseModuleCallLine(remaining, &cc, &ca)) {
                            inlineInner->children.append(makeModuleCallNode(0, cc, ca, state));
                        } else {
                            ShapeNode cs;
                            QString cpe;
                            if (parsePrimitiveLine(remaining, &cs, state, &cpe)) {
                                state->shapes.append(cs);
                                inlineInner->children.append(makePrimitiveNode(cs, state));
                            } else if (!cpe.isEmpty()) {
                                if (errorMessage) *errorMessage = cpe.arg(current.number);
                                state->errorLine = current.number;
                                return false;
                            }
                        }
                        continue;
                    }
                }
            }

            SceneDocument::TreeNode::Operation bfOp = SceneDocument::TreeNode::Union;
            QVector3D bfVec;
            QStringList bfExprs;
            if (parseBraceFreeOperationLine(line, &bfOp, &bfVec, state->variableValues, &bfExprs, &state->functionDefs)) {
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
                    if (!parseBraceFreeOperationLine(peek.text, &chainOp, &chainVec, state->variableValues, &chainExprs, &state->functionDefs))
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
                                                   nullptr, &childExtrudeCenter, &childExtrudeSlices, &state->functionDefs)) {
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
                                    childGroup.linearExtrudeTwist    = childVec.y();
                                    childGroup.linearExtrudeScaleVal = childVec.z() > 0.001f ? childVec.z() : 1.0f;
                                } else if (childOp == SceneDocument::TreeNode::RotateExtrude) {
                                    childGroup.scale = childVec;
                                } else if (childOp == SceneDocument::TreeNode::Resize) {
                                    childGroup.scale = childVec;
                                }
                                if (childOp == SceneDocument::TreeNode::Translate
                                    || childOp == SceneDocument::TreeNode::Rotate
                                    || childOp == SceneDocument::TreeNode::Scale
                                    || childOp == SceneDocument::TreeNode::Mirror
                                    || childOp == SceneDocument::TreeNode::LinearExtrude
                                    || childOp == SceneDocument::TreeNode::RotateExtrude
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

// Splits a single line on { } ; that appear outside of () and [].
// Handles compact OpenSCAD like: translate([0,0,0]) { sphere(r=3); }
// → produces: "translate([0,0,0]) {", "sphere(r=3);", "}"
static QVector<QString> expandInlineBlocks(const QString &line)
{
    QVector<QString> parts;
    QString current;
    int parenDepth   = 0;
    int bracketDepth = 0;

    for (const QChar ch : line) {
        if (ch == QLatin1Char('(')) {
            ++parenDepth;
            current += ch;
        } else if (ch == QLatin1Char(')')) {
            --parenDepth;
            current += ch;
        } else if (ch == QLatin1Char('[')) {
            ++bracketDepth;
            current += ch;
        } else if (ch == QLatin1Char(']')) {
            --bracketDepth;
            current += ch;
        } else if (parenDepth == 0 && bracketDepth == 0 && ch == QLatin1Char('{')) {
            const QString seg = current.trimmed();
            parts.append(seg.isEmpty() ? QStringLiteral("{") : seg + QStringLiteral(" {"));
            current.clear();
        } else if (parenDepth == 0 && bracketDepth == 0 && ch == QLatin1Char('}')) {
            const QString seg = current.trimmed();
            if (!seg.isEmpty())
                parts.append(seg);
            parts.append(QStringLiteral("}"));
            current.clear();
        } else if (parenDepth == 0 && bracketDepth == 0 && ch == QLatin1Char(';')) {
            const QString seg = current.trimmed();
            if (!seg.isEmpty())
                parts.append(seg + QLatin1Char(';'));
            current.clear();
        } else {
            current += ch;
        }
    }

    const QString tail = current.trimmed();
    if (!tail.isEmpty())
        parts.append(tail);

    return parts;
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

    // Pass 1: join continuation lines (unbalanced parens/brackets at end of line).
    // This turns multi-line expressions like:
    //   translate([cos(a)*r,
    //              sin(a)*r,
    //              0]) {
    // into a single text line before further processing.
    QVector<QPair<QString, int>> joined; // (text, originalLineNumber)
    {
        QString acc;
        int accLine = -1;
        int pd = 0, bd = 0; // paren / bracket depth
        const QStringList rawLines = cleaned.split(QLatin1Char('\n'));
        for (int i = 0; i < rawLines.size(); ++i) {
            QString line = rawLines[i].trimmed();
            const int ci = line.indexOf(QStringLiteral("//"));
            if (ci >= 0) line = line.left(ci).trimmed();
            if (line.isEmpty()) continue;

            if (acc.isEmpty()) accLine = i + 1;
            acc += line;

            for (const QChar ch : line) {
                if      (ch == QLatin1Char('(')) ++pd;
                else if (ch == QLatin1Char(')')) --pd;
                else if (ch == QLatin1Char('[')) ++bd;
                else if (ch == QLatin1Char(']')) --bd;
            }

            if (pd <= 0 && bd <= 0) {
                pd = 0; bd = 0;
                joined.append({acc, accLine});
                acc.clear();
            }
        }
        if (!acc.isEmpty())
            joined.append({acc, accLine});
    }

    // Pass 2: split each joined line on { } ; at depth 0 (expandInlineBlocks).
    for (const auto &entry : joined) {
        for (const QString &part : expandInlineBlocks(entry.first)) {
            if (!part.isEmpty())
                result.append({part, entry.second});
        }
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
        if (!ExpressionSyntax::evaluate(expr, *varValues, &value, nullptr, &state->functionDefs)) {
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
            if (ExpressionSyntax::evaluate(variableExpression, state.variableValues, &evaluatedValue, nullptr, &state.functionDefs))
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
            static const QRegularExpression letRe("^(?:let|assign)\\s*\\((.+)\\)\\s*\\{\\s*$");
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
                    if (ExpressionSyntax::evaluate(expr, state.variableValues, &val, nullptr, &state.functionDefs)) {
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

        // ── if (cond) { ... } else { ... } at top scope — tree-preserving ──
        {
            static const QRegularExpression ifRe("^if\\s*\\((.+)\\)\\s*\\{\\s*$");
            static const QRegularExpression elseRe("^else\\s*\\{\\s*$");
            static const QRegularExpression elseIfRe("^else\\s+if\\s*\\((.+)\\)\\s*\\{\\s*$");
            QRegularExpressionMatch im = ifRe.match(line);
            if (im.hasMatch()) {
                ++state.index;
                struct IfClause { QString cond; SceneDocument::TreeNode trueBlock; };
                QVector<IfClause> clauses;
                SceneDocument::TreeNode elseBlock;
                bool hasElse = false;

                IfClause initClause;
                initClause.cond = im.captured(1).trimmed();
                initClause.trueBlock = makeGroupNode(SceneDocument::TreeNode::Union, &state);
                if (!parseBlock(&state, &initClause.trueBlock, true, errorMessage)) {
                    if (errorLine) *errorLine = state.errorLine;
                    return false;
                }
                clauses.append(initClause);

                while (state.index < state.lines.size()) {
                    const QString nxt = state.lines[state.index].text;
                    const QRegularExpressionMatch eif = elseIfRe.match(nxt);
                    if (eif.hasMatch()) {
                        ++state.index;
                        IfClause c;
                        c.cond = eif.captured(1).trimmed();
                        c.trueBlock = makeGroupNode(SceneDocument::TreeNode::Union, &state);
                        if (!parseBlock(&state, &c.trueBlock, true, errorMessage)) {
                            if (errorLine) *errorLine = state.errorLine;
                            return false;
                        }
                        clauses.append(c);
                        continue;
                    }
                    if (elseRe.match(nxt).hasMatch()) {
                        ++state.index;
                        elseBlock = makeGroupNode(SceneDocument::TreeNode::Union, &state);
                        elseBlock.isElseBranch = true;
                        if (!parseBlock(&state, &elseBlock, true, errorMessage)) {
                            if (errorLine) *errorLine = state.errorLine;
                            return false;
                        }
                        hasElse = true;
                    }
                    break;
                }

                SceneDocument::TreeNode assembled;
                for (int ci = clauses.size() - 1; ci >= 0; --ci) {
                    SceneDocument::TreeNode cond = makeGroupNode(SceneDocument::TreeNode::Conditional, &state);
                    cond.conditionExpression = clauses[ci].cond;
                    cond.children.append(clauses[ci].trueBlock);
                    if (ci == clauses.size() - 1) {
                        if (hasElse) cond.children.append(elseBlock);
                    } else {
                        SceneDocument::TreeNode elseBr = makeGroupNode(SceneDocument::TreeNode::Union, &state);
                        elseBr.isElseBranch = true;
                        elseBr.children.append(assembled);
                        cond.children.append(elseBr);
                    }
                    assembled = cond;
                }
                sceneNode.children.append(assembled);
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
                               &extrudeCenter, &extrudeSlices, &state.functionDefs)) {
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
                group.linearExtrudeTwist    = transformVector.y();
                group.linearExtrudeScaleVal = transformVector.z() > 0.001f ? transformVector.z() : 1.0f;
            } else if (operation == SceneDocument::TreeNode::RotateExtrude) {
                group.scale = transformVector;
            } else if (operation == SceneDocument::TreeNode::Resize) {
                group.scale = transformVector;
            } else if (operation == SceneDocument::TreeNode::Offset) {
                group.offsetAmount = transformVector.x();
                group.offsetUseDelta = transformVector.y() > 0.5f;
                group.offsetChamfer = transformVector.z() > 0.5f;
            }
            if (operation == SceneDocument::TreeNode::Translate
                || operation == SceneDocument::TreeNode::Rotate
                || operation == SceneDocument::TreeNode::Scale
                || operation == SceneDocument::TreeNode::Mirror
                || operation == SceneDocument::TreeNode::LinearExtrude
                || operation == SceneDocument::TreeNode::RotateExtrude
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
            if (!parsePolyhedron(polyLine, &polyData, &polyError, &state.variableValues, &state.functionDefs)) {
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

        // ── echo(...); / assert(...); at top level — skip silently ──────────
        if (line.startsWith(QLatin1String("echo")) || line.startsWith(QLatin1String("assert"))) {
            ++state.index;
            continue;
        }

        // ── function name(params) = expr; at top level — store for evaluator ─
        {
            static const QRegularExpression funcRe(
                "^function\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\(([^)]*)\\)\\s*=\\s*(.+)\\s*;\\s*$");
            static const QRegularExpression funcStartRe(
                "^function\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\(([^)]*)\\)\\s*=");
            ++state.index;
            QString funcLine = line;
            if (!funcRe.match(funcLine).hasMatch() && funcStartRe.match(funcLine).hasMatch()) {
                while (state.index < state.lines.size()) {
                    const QString &next = state.lines[state.index].text;
                    ++state.index;
                    funcLine += QLatin1Char(' ') + next;
                    if (next.trimmed().endsWith(QLatin1Char(';')))
                        break;
                }
            }
            const QRegularExpressionMatch fm = funcRe.match(funcLine);
            if (fm.hasMatch()) {
                ExpressionSyntax::FunctionDef def;
                const QStringList rawParams = splitAtTopLevelCommas(fm.captured(2));
                for (const QString &p : rawParams) {
                    const QString pt = p.trimmed();
                    if (!pt.isEmpty()) def.params.append(pt);
                }
                def.body = fm.captured(3).trimmed();
                state.functionDefs[fm.captured(1)] = def;
                continue;
            }
            --state.index; // not a function line, put index back
        }

        // Brace-free single-child transform at top level.
        // Also handles inline: translate([x,y,0])cube(...);
        {
            // Inline chain: transform(...)transform(...)...primitive(...) on one line
            {
                QString tfPart, childPart;
                if (trySplitInlineTransformChild(line, &tfPart, &childPart)) {
                    SceneDocument::TreeNode::Operation inlineOp = SceneDocument::TreeNode::Union;
                    QVector3D inlineVec;
                    QStringList inlineExprs;
                    if (parseBraceFreeOperationLine(tfPart, &inlineOp, &inlineVec, state.variableValues, &inlineExprs, &state.functionDefs)) {
                        ++state.index;
                        SceneDocument::TreeNode inlineGroup = makeGroupNode(inlineOp, &state);
                        if (inlineOp == SceneDocument::TreeNode::Translate) inlineGroup.position = inlineVec;
                        else if (inlineOp == SceneDocument::TreeNode::Rotate) inlineGroup.rotation = inlineVec;
                        else if (inlineOp == SceneDocument::TreeNode::Scale)  inlineGroup.scale    = inlineVec;
                        inlineGroup.transformExpressions = inlineExprs;
                        sceneNode.children.append(inlineGroup);
                        SceneDocument::TreeNode *inlineInner = &sceneNode.children.last();

                        // Consume additional transforms chained inline on the same line
                        QString remaining = childPart;
                        while (true) {
                            QString nextTf, nextChild;
                            if (!trySplitInlineTransformChild(remaining, &nextTf, &nextChild)) break;
                            SceneDocument::TreeNode::Operation chainOp = SceneDocument::TreeNode::Union;
                            QVector3D chainVec;
                            QStringList chainExprs;
                            if (!parseBraceFreeOperationLine(nextTf, &chainOp, &chainVec, state.variableValues, &chainExprs, &state.functionDefs))
                                break;
                            SceneDocument::TreeNode chainGroup = makeGroupNode(chainOp, &state);
                            if (chainOp == SceneDocument::TreeNode::Translate) chainGroup.position = chainVec;
                            else if (chainOp == SceneDocument::TreeNode::Rotate) chainGroup.rotation = chainVec;
                            else if (chainOp == SceneDocument::TreeNode::Scale)  chainGroup.scale    = chainVec;
                            chainGroup.transformExpressions = chainExprs;
                            inlineInner->children.append(chainGroup);
                            inlineInner = &inlineInner->children.last();
                            remaining = nextChild;
                        }

                        QString cc, ca;
                        if (parseModuleCallLine(remaining, &cc, &ca)) {
                            inlineInner->children.append(makeModuleCallNode(0, cc, ca, &state));
                        } else {
                            ShapeNode cs;
                            QString cpe;
                            if (parsePrimitiveLine(remaining, &cs, &state, &cpe)) {
                                state.shapes.append(cs);
                                inlineInner->children.append(makePrimitiveNode(cs, &state));
                            } else if (!cpe.isEmpty()) {
                                if (errorMessage) *errorMessage = cpe.arg(current.number);
                                if (errorLine) *errorLine = current.number;
                                return false;
                            }
                        }
                        continue;
                    }
                }
            }

            SceneDocument::TreeNode::Operation bfOp = SceneDocument::TreeNode::Union;
            QVector3D bfVec;
            QStringList bfExprs;
            if (parseBraceFreeOperationLine(line, &bfOp, &bfVec, state.variableValues, &bfExprs, &state.functionDefs)) {
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
                    if (!parseBraceFreeOperationLine(peek.text, &chainOp, &chainVec, state.variableValues, &chainExprs, &state.functionDefs))
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
                                                   nullptr, &childExtrudeCenter, &childExtrudeSlices, &state.functionDefs)) {
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
                                    childGroup.linearExtrudeTwist    = childVec.y();
                                    childGroup.linearExtrudeScaleVal = childVec.z() > 0.001f ? childVec.z() : 1.0f;
                                } else if (childOp == SceneDocument::TreeNode::RotateExtrude) {
                                    childGroup.scale = childVec;
                                } else if (childOp == SceneDocument::TreeNode::Resize) {
                                    childGroup.scale = childVec;
                                }
                                if (childOp == SceneDocument::TreeNode::Translate
                                    || childOp == SceneDocument::TreeNode::Rotate
                                    || childOp == SceneDocument::TreeNode::Scale
                                    || childOp == SceneDocument::TreeNode::Mirror
                                    || childOp == SceneDocument::TreeNode::LinearExtrude
                                    || childOp == SceneDocument::TreeNode::RotateExtrude
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
