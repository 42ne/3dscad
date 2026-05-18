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
    int nextShapeId = 1;
    int nextTreeNodeId = 1;
    int cubeCount = 0;
    int sphereCount = 0;
    int cylinderCount = 0;
    QVector<ShapeNode> shapes;
};

static bool parseFloat(const QString &text, float *value)
{
    bool ok = false;
    const float parsedValue = text.trimmed().toFloat(&ok);

    if (!ok)
        return false;

    *value = parsedValue;
    return true;
}

static bool parseReal(const QString &text, qreal *value)
{
    bool ok = false;
    const qreal parsedValue = text.trimmed().toDouble(&ok);

    if (!ok)
        return false;

    *value = parsedValue;
    return true;
}

static bool parseVector3Value(const QString &text, QVector3D *vector)
{
    const QStringList parts = text.split(',');
    if (parts.size() != 3 || !vector)
        return false;

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    if (!parseFloat(parts[0], &x) || !parseFloat(parts[1], &y) || !parseFloat(parts[2], &z))
        return false;

    *vector = QVector3D(x, y, z);
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

static bool isRootModuleLine(const QString &line)
{
    static const QRegularExpression regex("^module\\s+scene_model\\s*\\(\\s*\\)\\s*\\{\\s*$");
    return regex.match(line).hasMatch();
}

static bool isSceneModelCallLine(const QString &line)
{
    static const QRegularExpression regex("^scene_model\\s*\\(\\s*\\)\\s*;\\s*$");
    return regex.match(line).hasMatch();
}

static bool parseOperationLine(const QString &line, SceneDocument::TreeNode::Operation *operation, QVector3D *vector)
{
    static const QRegularExpression unionRegex("^union\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression differenceRegex("^difference\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression intersectionRegex("^intersection\\s*\\(\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression translateRegex("^translate\\s*\\(\\s*\\[([^\\]]+)\\]\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression rotateRegex("^rotate\\s*\\(\\s*\\[([^\\]]+)\\]\\s*\\)\\s*\\{\\s*$");
    static const QRegularExpression scaleRegex("^scale\\s*\\(\\s*\\[([^\\]]+)\\]\\s*\\)\\s*\\{\\s*$");

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

    QRegularExpressionMatch match = translateRegex.match(line);
    if (match.hasMatch()) {
        *operation = SceneDocument::TreeNode::Translate;
        return parseVector3Value(match.captured(1), vector);
    }

    match = rotateRegex.match(line);
    if (match.hasMatch()) {
        *operation = SceneDocument::TreeNode::Rotate;
        return parseVector3Value(match.captured(1), vector);
    }

    match = scaleRegex.match(line);
    if (match.hasMatch()) {
        *operation = SceneDocument::TreeNode::Scale;
        return parseVector3Value(match.captured(1), vector);
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
        if (errorMessage)
            *errorMessage = expressionError;
        return false;
    }

    qreal parsedValue = 0.0;
    parseReal(expressionText, &parsedValue);

    if (name)
        *name = match.captured(1);
    if (expression)
        *expression = expressionText;
    if (value)
        *value = parsedValue;
    return true;
}

static bool parsePrimitiveLine(const QString &line, ShapeNode *shape, ParserState *state)
{
    static const QRegularExpression cubeRegex("^cube\\s*\\(\\s*\\[([^\\]]+)\\]\\s*,\\s*center\\s*=\\s*true\\s*\\)\\s*;\\s*$");
    static const QRegularExpression sphereRegex("^sphere\\s*\\(\\s*r\\s*=\\s*([^\\)]+)\\)\\s*;\\s*$");
    static const QRegularExpression cylinderRegex("^cylinder\\s*\\(\\s*h\\s*=\\s*([^,]+)\\s*,\\s*r\\s*=\\s*([^,\\)]+)\\s*,\\s*center\\s*=\\s*true\\s*\\)\\s*;\\s*$");

    QRegularExpressionMatch match = cubeRegex.match(line);
    if (match.hasMatch()) {
        shape->id = state->nextShapeId++;
        shape->type = ShapeNode::Cube;
        shape->name = QStringLiteral("Cube %1").arg(++state->cubeCount);
        return parseVector3Value(match.captured(1), &shape->size);
    }

    match = sphereRegex.match(line);
    if (match.hasMatch()) {
        shape->id = state->nextShapeId++;
        shape->type = ShapeNode::Sphere;
        shape->name = QStringLiteral("Sphere %1").arg(++state->sphereCount);
        return parseFloat(match.captured(1), &shape->radius);
    }

    match = cylinderRegex.match(line);
    if (match.hasMatch()) {
        shape->id = state->nextShapeId++;
        shape->type = ShapeNode::Cylinder;
        shape->name = QStringLiteral("Cylinder %1").arg(++state->cylinderCount);
        return parseFloat(match.captured(1), &shape->height)
               && parseFloat(match.captured(2), &shape->radius);
    }

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

        if (isSceneModelCallLine(line))
            continue;

        QString variableName;
        QString variableExpression;
        qreal variableValue = 0.0;
        QString variableError;
        if (parseVariableLine(line, &variableName, &variableExpression, &variableValue, &variableError)) {
            if (parent->operation != SceneDocument::TreeNode::Module) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("Variables are currently supported only in scene_model() on line %1.").arg(current.number);
                return false;
            }

            parent->children.append(makeVariableNode(variableName, variableExpression, variableValue, state));
            continue;
        } else if (!variableError.isEmpty()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Invalid expression on line %1: %2").arg(current.number).arg(variableError);
            return false;
        }

        SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
        QVector3D transformVector;
        if (parseOperationLine(line, &operation, &transformVector)) {
            SceneDocument::TreeNode group = makeGroupNode(operation, state);
            if (operation == SceneDocument::TreeNode::Translate)
                group.position = transformVector;
            else if (operation == SceneDocument::TreeNode::Rotate)
                group.rotation = transformVector;
            else if (operation == SceneDocument::TreeNode::Scale)
                group.scale = transformVector;

            parent->children.append(group);
            SceneDocument::TreeNode &child = parent->children.last();
            if (!parseBlock(state, &child, true, errorMessage))
                return false;
            continue;
        }

        ShapeNode shape;
        if (parsePrimitiveLine(line, &shape, state)) {
            state->shapes.append(shape);
            parent->children.append(makePrimitiveNode(shape, state));
            continue;
        }

        if (errorMessage)
            *errorMessage = QStringLiteral("Unsupported OpenSCAD syntax on line %1: %2").arg(current.number).arg(line);
        return false;
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

bool OpenScadParser::parse(const QString &code, QVector<ShapeNode> *shapes, QString *errorMessage)
{
    if (!shapes)
        return false;

    SceneDocument::Snapshot snapshot;
    if (!parseScene(code, &snapshot, errorMessage))
        return false;

    *shapes = snapshot.shapes;
    return true;
}

bool OpenScadParser::parseScene(const QString &code, SceneDocument::Snapshot *snapshot, QString *errorMessage)
{
    if (!snapshot)
        return false;

    ParserState state;
    state.lines = normalizedLines(code);

    SceneDocument::TreeNode root = makeGroupNode(SceneDocument::TreeNode::Module, &state);
    bool hasModuleWrapper = false;

    if (!state.lines.isEmpty() && isRootModuleLine(state.lines.first().text)) {
        hasModuleWrapper = true;
        state.index = 1;
    }

    if (!parseBlock(&state, &root, hasModuleWrapper, errorMessage))
        return false;

    while (state.index < state.lines.size()) {
        const ParsedLine current = state.lines[state.index++];
        if (!isSceneModelCallLine(current.text)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Unsupported OpenSCAD syntax on line %1: %2").arg(current.number).arg(current.text);
            return false;
        }
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
