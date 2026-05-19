#include "openscadparser.h"
#include "expression.h"

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
    QHash<QString, qreal> variableValues;
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

// Matches any top-level call: <ident>(<args>);
static bool isModuleCallLine(const QString &line)
{
    static const QRegularExpression regex("^([A-Za-z_][A-Za-z0-9_]*)\\s*\\([^)]*\\)\\s*;\\s*$");
    const QRegularExpressionMatch match = regex.match(line);
    if (!match.hasMatch())
        return false;

    static const QStringList reservedCalls = {
        QStringLiteral("cube"),
        QStringLiteral("sphere"),
        QStringLiteral("cylinder")
    };
    return !reservedCalls.contains(match.captured(1));
}

static bool parseOperationLine(const QString &line,
                               SceneDocument::TreeNode::Operation *operation,
                               QVector3D *vector,
                               const QHash<QString, qreal> &varValues,
                               QStringList *expressions = nullptr,
                               QString *loopVariable = nullptr,
                               QString *loopRangeExpression = nullptr)
{
    static const QRegularExpression unionRegex("^union\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression differenceRegex("^difference\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression intersectionRegex("^intersection\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression translateRegex("^translate\\s*\\(\\s*\\[([^\\]]+)\\]\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression rotateRegex("^rotate\\s*\\(\\s*\\[([^\\]]+)\\]\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression scaleRegex("^scale\\s*\\(\\s*\\[([^\\]]+)\\]\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression forRegex("^for\\s*\\(\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*(\\[[^\\]]+\\])\\s*\\)\\s*\\{\\s*$");

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

    QRegularExpressionMatch forMatch = forRegex.match(line);
    if (forMatch.hasMatch()) {
        *operation = SceneDocument::TreeNode::For;
        if (loopVariable)     *loopVariable = forMatch.captured(1);
        if (loopRangeExpression) *loopRangeExpression = forMatch.captured(2).trimmed();
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
        const QRegularExpressionMatch m = vecRe.match(argsStr);
        if (!m.hasMatch()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("cube on line %1: expected [x, y, z]");
            return false;
        }
        const QStringList parts = splitAtTopLevelCommas(m.captured(1));
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

    // ── Cylinder ────────────────────────────────────────────────────────────
    if (extractCallArgs(line, "cylinder", &argsStr) && line.endsWith(';')) {
        // Accept h and r in any order, center optional
        // Positional order: cylinder(h, r) or cylinder(h, r, center)
        const auto args = parseNamedArgs(argsStr, {"h", "r", "center"});
        if (!args.contains("h") || !args.contains("r")) {
            if (errorMessage)
                *errorMessage = QStringLiteral("cylinder on line %1: missing h or r");
            return false;
        }
        qreal h = 0.0, r = 0.0;
        QString he, re;
        if (!parseParamExpression(args["h"], state->variableValues, &h, &he)
            || !parseParamExpression(args["r"], state->variableValues, &r, &re)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("cylinder on line %1: could not parse h or r");
            return false;
        }
        shape->id = state->nextShapeId++;
        shape->type = ShapeNode::Cylinder;
        shape->name = QStringLiteral("Cylinder %1").arg(shape->id);
        shape->height = h;
        shape->radius = r;
        // parameterExpressions order: R=index 0, H=index 1
        shape->parameterExpressions = QStringList({re, he});
        return true;
    }

    return false; // not a primitive line at all
}

// Returns true if the line starts with a keyword we know how to parse.
// Used to distinguish "known-but-invalid" from "completely unknown".
static bool startsWithKnownKeyword(const QString &line)
{
    static const QStringList known = {
        "translate", "rotate", "scale",
        "union", "difference", "intersection", "for",
        "cube", "sphere", "cylinder"
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

        // Skip module call lines (e.g. "scene_model();" or "my_module();").
        if (isModuleCallLine(line))
            continue;

        // ── Variable assignment ───────────────────────────────────────────
        QString variableName, variableExpression, variableError;
        qreal variableValue = 0.0;
        if (parseVariableLine(line, &variableName, &variableExpression, &variableValue, &variableError)) {
            if (parent->type == SceneDocument::TreeNode::Group
                && parent->operation != SceneDocument::TreeNode::Module) {
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
        if (parseOperationLine(line, &operation, &transformVector, state->variableValues,
                               &transformExpressions, &loopVariable, &loopRangeExpression)) {
            SceneDocument::TreeNode group = makeGroupNode(operation, state);
            if (operation == SceneDocument::TreeNode::Translate)
                group.position = transformVector;
            else if (operation == SceneDocument::TreeNode::Rotate)
                group.rotation = transformVector;
            else if (operation == SceneDocument::TreeNode::Scale)
                group.scale = transformVector;
            else if (operation == SceneDocument::TreeNode::For) {
                group.loopVariable = loopVariable.isEmpty() ? QStringLiteral("i") : loopVariable;
                group.loopRangeExpression = loopRangeExpression.isEmpty() ? QStringLiteral("[0 : 1 : 3]") : loopRangeExpression;
            }
            if (operation == SceneDocument::TreeNode::Translate
                || operation == SceneDocument::TreeNode::Rotate
                || operation == SceneDocument::TreeNode::Scale)
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
            if (errorMessage)
                *errorMessage = QStringLiteral("Module parameters must use name = expression syntax: %1").arg(part.trimmed());
            return false;
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

bool OpenScadParser::parseScene(const QString &code, SceneDocument::Snapshot *snapshot, QString *errorMessage, int *errorLine)
{
    if (!snapshot)
        return false;

    ParserState state;
    state.lines = normalizedLines(code);

    // Root is an implicit top-level container (operation=Module, never emitted directly).
    SceneDocument::TreeNode root = makeGroupNode(SceneDocument::TreeNode::Module, &state);

    while (state.index < state.lines.size()) {
        const ParsedLine current = state.lines[state.index];
        const QString &line = current.text;

        // Skip module call lines at any point in the top-level.
        if (isModuleCallLine(line)) {
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

            // The generated legacy/main scene_model wrapper is the implicit document root.
            // Keeping it implicit preserves UI -> code -> UI tree structure.
            if (modName == QStringLiteral("scene_model") && modParams.trimmed().isEmpty()) {
                if (!parseBlock(&state, &root, true, errorMessage)) {
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

        // Variable at top level (global scope).
        QString variableName, variableExpression, variableError;
        qreal variableValue = 0.0;
        if (parseVariableLine(line, &variableName, &variableExpression, &variableValue, &variableError)) {
            ++state.index;
            qreal evaluatedValue = 0.0;
            if (ExpressionSyntax::evaluate(variableExpression, state.variableValues, &evaluatedValue))
                variableValue = evaluatedValue;
            state.variableValues[variableName] = variableValue;
            root.children.append(makeVariableNode(variableName, variableExpression, variableValue, &state));
            continue;
        }
        if (!variableError.isEmpty()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Invalid expression on line %1: %2").arg(current.number).arg(variableError);
            if (errorLine) *errorLine = current.number;
            return false;
        }

        // Anything else at top level is an error.
        if (errorMessage)
            *errorMessage = QStringLiteral("Unexpected statement at top level on line %1: %2").arg(current.number).arg(line);
        if (errorLine) *errorLine = current.number;
        return false;
    }

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
