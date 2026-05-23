#include "scenecontroller.h"

#include "expression.h"
#include "openscadparser.h"
#include "scenecommands.h"
#include "scenetreegraphicshelpers.h"

#include <QApplication>
#include <QUndoStack>
#include <QAction>

#include <functional>
#include <cmath>

// ────────────────────────────────────────────────────────────────────────────
// File-local helpers (moved here from mainwindow.cpp)
// ────────────────────────────────────────────────────────────────────────────

static constexpr int ModuleParameterInsertSentinel = -100000;

static bool decodeModuleParameterInsertIndex(int *insertIndex)
{
    if (!insertIndex || *insertIndex > ModuleParameterInsertSentinel)
        return false;
    *insertIndex = -ModuleParameterInsertSentinel - *insertIndex;
    return true;
}

static QStringList splitAtTopLevelCommas(const QString &text)
{
    QStringList result;
    int depth = 0, start = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text[i];
        if (ch == QLatin1Char('(') || ch == QLatin1Char('['))      ++depth;
        else if (ch == QLatin1Char(')') || ch == QLatin1Char(']')) --depth;
        else if (ch == QLatin1Char(',') && depth == 0) {
            const QString part = text.mid(start, i - start).trimmed();
            if (!part.isEmpty()) result.append(part);
            start = i + 1;
        }
    }
    const QString tail = text.mid(start).trimmed();
    if (!tail.isEmpty()) result.append(tail);
    return result;
}

static QHash<QString, QString> parseNamedArgumentExpressions(const QString &arguments)
{
    QHash<QString, QString> result;
    for (const QString &part : splitAtTopLevelCommas(arguments)) {
        const int equal = part.indexOf(QLatin1Char('='));
        if (equal <= 0) continue;
        const QString name = part.left(equal).trimmed();
        const QString expr = part.mid(equal + 1).trimmed();
        if (!name.isEmpty() && !expr.isEmpty())
            result[name] = expr;
    }
    return result;
}

static QHash<QString, QString> resolveModuleArguments(
    const QString &callArguments,
    const SceneDocument::TreeNode &moduleNode)
{
    QStringList paramOrder;
    for (const SceneDocument::TreeNode &child : moduleNode.children)
        if (child.type == SceneDocument::TreeNode::Variable && child.isParameter)
            paramOrder.append(child.variableName);

    QHash<QString, QString> result;
    int positionalIndex = 0;
    for (const QString &part : splitAtTopLevelCommas(callArguments)) {
        const int equal = part.indexOf(QLatin1Char('='));
        if (equal > 0) {
            const QString name = part.left(equal).trimmed();
            const QString expr = part.mid(equal + 1).trimmed();
            if (!name.isEmpty() && !expr.isEmpty())
                result[name] = expr;
        } else {
            const QString expr = part.trimmed();
            if (!expr.isEmpty() && positionalIndex < paramOrder.size())
                result[paramOrder[positionalIndex]] = expr;
            ++positionalIndex;
        }
    }
    return result;
}

static float normalizedRotationDegrees(float value)
{
    while (value >  180.0f) value -= 360.0f;
    while (value < -180.0f) value += 360.0f;
    return value;
}

static QVector3D normalizedRotation(const QVector3D &r)
{
    return QVector3D(normalizedRotationDegrees(r.x()),
                     normalizedRotationDegrees(r.y()),
                     normalizedRotationDegrees(r.z()));
}

static bool isStandaloneNumericToken(const QString &expression, int start, int length)
{
    if (start < 0 || length <= 0 || start + length > expression.size())
        return false;
    return expression.mid(start, length) == expression.trimmed();
}

static QString adjustedNumericToken(const QString &expression,
                                    int    start,
                                    int    length,
                                    qreal  delta,
                                    qreal  step,
                                    qreal  minimumValue,
                                    bool   clampMagnitude)
{
    const QString numberText = expression.mid(start, length);
    bool ok = false;
    const qreal value = numberText.toDouble(&ok);
    if (!ok) return QString();

    const int   decimalPoint = numberText.indexOf(QLatin1Char('.'));
    const int   precision    = decimalPoint >= 0
                                   ? qMin(3, numberText.size() - decimalPoint - 1)
                                   : 0;
    const qreal adjusted  = value + delta * step;
    const qreal newValue  = clampMagnitude ? qMax(minimumValue, adjusted) : adjusted;
    QString replacement   = QString::number(newValue, 'f', precision);
    if (precision == 0 && replacement == QStringLiteral("-0"))
        replacement = QStringLiteral("0");
    return replacement;
}

static ShapeNode makeShapeForTool(const QString &toolName, int shapeNumber)
{
    ShapeNode shape;
    shape.name = QString("%1 %2")
        .arg(toolName.left(1).toUpper() + toolName.mid(1))
        .arg(shapeNumber);

    if (toolName == QStringLiteral("sphere")) {
        shape.type   = ShapeNode::Sphere;
        shape.radius = 10.0f;
    } else if (toolName == QStringLiteral("cylinder")) {
        shape.type   = ShapeNode::Cylinder;
        shape.radius = 10.0f;
        shape.height = 30.0f;
    } else {
        shape.type = ShapeNode::Cube;
        shape.size = QVector3D(20, 20, 20);
    }
    return shape;
}

static bool operationForTool(const QString &toolName,
                              SceneDocument::TreeNode::Operation *operation)
{
    if (!operation) return false;
    if (toolName == QStringLiteral("module"))       { *operation = SceneDocument::TreeNode::Module;       return true; }
    if (toolName == QStringLiteral("union"))        { *operation = SceneDocument::TreeNode::Union;        return true; }
    if (toolName == QStringLiteral("difference"))   { *operation = SceneDocument::TreeNode::Difference;   return true; }
    if (toolName == QStringLiteral("intersection")) { *operation = SceneDocument::TreeNode::Intersection; return true; }
    if (toolName == QStringLiteral("translate"))    { *operation = SceneDocument::TreeNode::Translate;    return true; }
    if (toolName == QStringLiteral("rotate"))       { *operation = SceneDocument::TreeNode::Rotate;       return true; }
    if (toolName == QStringLiteral("scale"))        { *operation = SceneDocument::TreeNode::Scale;        return true; }
    if (toolName == QStringLiteral("mirror"))       { *operation = SceneDocument::TreeNode::Mirror;       return true; }
    if (toolName == QStringLiteral("hull"))         { *operation = SceneDocument::TreeNode::Hull;         return true; }
    if (toolName == QStringLiteral("for"))          { *operation = SceneDocument::TreeNode::For;          return true; }
    return false;
}

static bool isVariableTool(const QString &toolName)
{
    return toolName == QStringLiteral("var") || toolName == QStringLiteral("variable");
}

// ────────────────────────────────────────────────────────────────────────────
// SceneController
// ────────────────────────────────────────────────────────────────────────────

SceneController::SceneController(QObject *parent)
    : QObject(parent)
{
    m_undoStack  = new QUndoStack(this);
    m_undoAction = m_undoStack->createUndoAction(this, QStringLiteral("Undo"));
    m_redoAction = m_undoStack->createRedoAction(this, QStringLiteral("Redo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction->setShortcut(QKeySequence::Redo);
}

// ── Helpers ──────────────────────────────────────────────────────────────────

void SceneController::pushCommandIfValid(QUndoCommand *command)
{
    // Commands with isValid() == false are discarded to avoid touching the stack.
    // Commands that don't implement isValid() are always pushed.
    m_undoStack->push(command);
}

void SceneController::selectShapeInternal(int shapeId)
{
    m_scene.setSelectedShapeId(shapeId);

    int nodeId = 0;
    if (shapeId >= 0) {
        std::function<int(const SceneDocument::TreeNode &)> find =
            [&](const SceneDocument::TreeNode &n) -> int {
            if (n.type == SceneDocument::TreeNode::Primitive && n.shapeId == shapeId)
                return n.id;
            for (const auto &child : n.children) {
                const int found = find(child);
                if (found > 0) return found;
            }
            return 0;
        };
        nodeId = find(m_scene.treeRoot());
    }
    m_selectedTreeNodeId = nodeId;
    emit selectionChanged(nodeId);
}

int SceneController::selectedTreeGroupId() const
{
    if (m_selectedTreeNodeId <= 0)
        return 0;

    const SceneDocument::TreeNode *node = m_scene.treeNodeById(m_selectedTreeNodeId);
    if (!node) return 0;

    if (node->type == SceneDocument::TreeNode::Group)
        return m_selectedTreeNodeId;

    std::function<int(const SceneDocument::TreeNode &)> findParent =
        [&](const SceneDocument::TreeNode &parent) -> int {
        for (const auto &child : parent.children) {
            if (child.id == m_selectedTreeNodeId) return parent.id;
            const int found = findParent(child);
            if (found > 0) return found;
        }
        return 0;
    };
    return findParent(m_scene.treeRoot());
}

int SceneController::selectedDirectGroupId() const
{
    if (m_selectedTreeNodeId <= 0) return 0;
    const SceneDocument::TreeNode *node = m_scene.treeNodeById(m_selectedTreeNodeId);
    if (!node || node->type != SceneDocument::TreeNode::Group) return 0;
    return m_selectedTreeNodeId;
}

// ── Selection ─────────────────────────────────────────────────────────────────

void SceneController::selectShape(int shapeId)
{
    selectShapeInternal(shapeId);
}

void SceneController::selectTreeNode(int treeNodeId)
{
    m_selectedTreeNodeId = treeNodeId;
    emit selectionChanged(treeNodeId);
}

void SceneController::clearSelection()
{
    m_scene.setSelectedShapeId(-1);
    m_ctrlHighlight = CtrlParamHighlight();
    m_selectedTreeNodeId = 0;
    emit selectionChanged(0);
    emit ctrlHighlightChanged();
}

// ── Scene mutations ───────────────────────────────────────────────────────────

void SceneController::addCube()
{
    ShapeNode s;
    s.type = ShapeNode::Cube;
    s.name = QString("Cube %1").arg(m_scene.shapeCount() + 1);
    s.size = QVector3D(20, 20, 20);
    m_undoStack->push(new AddShapeCommand(&m_scene, s, [this]() { emit sceneChanged(); }));
}

void SceneController::addSphere()
{
    ShapeNode s;
    s.type   = ShapeNode::Sphere;
    s.name   = QString("Sphere %1").arg(m_scene.shapeCount() + 1);
    s.radius = 10;
    m_undoStack->push(new AddShapeCommand(&m_scene, s, [this]() { emit sceneChanged(); }));
}

void SceneController::addCylinder()
{
    ShapeNode s;
    s.type   = ShapeNode::Cylinder;
    s.name   = QString("Cylinder %1").arg(m_scene.shapeCount() + 1);
    s.radius = 10;
    s.height = 30;
    m_undoStack->push(new AddShapeCommand(&m_scene, s, [this]() { emit sceneChanged(); }));
}

void SceneController::addGroup(SceneDocument::TreeNode::Operation operation)
{
    auto *command = new AddGroupCommand(&m_scene, operation,
                                        selectedTreeGroupId(), -1,
                                        [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

void SceneController::moveTreeNodeToGroup(int nodeId, int parentGroupId, int insertIndex)
{
    const bool moduleParameterZone = decodeModuleParameterInsertIndex(&insertIndex);
    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);

    if (node && node->type == SceneDocument::TreeNode::Variable && parentGroupId > 0) {
        const SceneDocument::TreeNode *parentNode = m_scene.treeNodeById(parentGroupId);
        const bool targetIsRoot   = (parentGroupId == m_scene.treeRoot().id);
        const bool targetIsModule = parentNode
                                    && parentNode->type == SceneDocument::TreeNode::Group
                                    && parentNode->operation == SceneDocument::TreeNode::Module;
        if (!targetIsRoot && !targetIsModule) return;
    }
    if (node && node->type == SceneDocument::TreeNode::ModuleCall) {
        const SceneDocument::TreeNode *parentNode = m_scene.treeNodeById(parentGroupId);
        if (!parentNode || parentNode->type != SceneDocument::TreeNode::Group) return;
    }

    auto *command = new MoveTreeNodeCommand(&m_scene, nodeId, parentGroupId, insertIndex,
                                             [this]() { emit sceneChanged(); },
                                             moduleParameterZone);
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

bool SceneController::applyCode(const QString &code,
                                 QString *errorMessage,
                                 int     *errorLine)
{
    SceneDocument::Snapshot snapshot;
    QString localError;
    int     localLine = -1;

    if (!OpenScadParser::parseScene(code, &snapshot, &localError, &localLine)) {
        if (errorMessage) *errorMessage = localError;
        if (errorLine)    *errorLine    = localLine;
        return false;
    }

    auto *command = new ReplaceSceneCommand(&m_scene, snapshot,
                                             [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return true; }
    m_undoStack->push(command);
    return true;
}

// ── Viewport drag: shape ──────────────────────────────────────────────────────

void SceneController::handleShapeDragStarted(int index)
{
    m_scene.setSelectedIndex(index);
    selectShapeInternal(m_scene.selectedShapeId());

    const ShapeNode *shape = m_scene.selectedShape();
    if (!shape) return;
    m_shapeDragStartShape = *shape;
    m_shapeDragActive = true;
}

void SceneController::handleShapeDragged(int index, const QVector3D &delta)
{
    if (!m_shapeDragActive || m_scene.selectedIndex() != index) return;
    ShapeNode *shape = m_scene.selectedShape();
    if (!shape) return;
    *shape = m_shapeDragStartShape;
    shape->position = m_shapeDragStartShape.position + delta;
    emit liveViewportUpdate();
}

void SceneController::handleShapeDragFinished(int index)
{
    if (!m_shapeDragActive || m_scene.selectedIndex() != index) return;
    m_shapeDragActive = false;

    const ShapeNode *shape = m_scene.selectedShape();
    if (!shape) return;

    auto *command = new UpdateShapeCommand(&m_scene,
                                           m_shapeDragStartShape, *shape,
                                           [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

void SceneController::handleShapeRotationDragStarted(int index)
{
    handleShapeDragStarted(index);
}

void SceneController::handleShapeRotated(int index, const QVector3D &deltaDegrees)
{
    if (!m_shapeDragActive || m_scene.selectedIndex() != index) return;
    ShapeNode *shape = m_scene.selectedShape();
    if (!shape) return;
    *shape = m_shapeDragStartShape;
    shape->rotation = normalizedRotation(m_shapeDragStartShape.rotation + deltaDegrees);
    emit liveViewportUpdate();
}

void SceneController::handleShapeRotationDragFinished(int index)
{
    handleShapeDragFinished(index);
}

// ── Viewport drag: group ─────────────────────────────────────────────────────

void SceneController::handleGroupDragStarted(int groupId)
{
    selectTreeNode(groupId);

    const SceneDocument::TreeNode *group = m_scene.treeNodeById(groupId);
    if (!group || group->type != SceneDocument::TreeNode::Group) return;

    m_groupDragId         = groupId;
    m_groupDragStartPos   = group->position;
    m_groupDragStartRot   = group->rotation;
    m_groupDragStartScale = group->scale;
    m_groupDragActive     = true;
}

void SceneController::handleGroupDragged(int groupId, const QVector3D &delta)
{
    if (!m_groupDragActive || m_groupDragId != groupId) return;
    m_scene.updateGroupTransform(groupId,
                                 m_groupDragStartPos + delta,
                                 m_groupDragStartRot,
                                 m_groupDragStartScale);
    emit liveViewportUpdate();
}

void SceneController::handleGroupDragFinished(int groupId)
{
    if (!m_groupDragActive || m_groupDragId != groupId) return;
    m_groupDragActive = false;

    const SceneDocument::TreeNode *group = m_scene.treeNodeById(groupId);
    if (!group || group->type != SceneDocument::TreeNode::Group) return;

    const QVector3D finalPos   = group->position;
    const QVector3D finalRot   = group->rotation;
    const QVector3D finalScale = group->scale;
    if (finalPos == m_groupDragStartPos
        && finalRot == m_groupDragStartRot
        && finalScale == m_groupDragStartScale)
        return;

    const SceneDocument::Snapshot newSnapshot = m_scene.snapshot();
    m_scene.updateGroupTransform(groupId,
                                 m_groupDragStartPos,
                                 m_groupDragStartRot,
                                 m_groupDragStartScale);
    const SceneDocument::Snapshot oldSnapshot = m_scene.snapshot();
    m_scene.restoreSnapshot(newSnapshot);

    auto *command = new UpdateGroupTransformCommand(&m_scene, oldSnapshot, newSnapshot,
                                                    [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

void SceneController::handleGroupRotationDragStarted(int groupId)
{
    handleGroupDragStarted(groupId);
}

void SceneController::handleGroupRotated(int groupId, const QVector3D &deltaDegrees)
{
    if (!m_groupDragActive || m_groupDragId != groupId) return;
    m_scene.updateGroupTransform(groupId,
                                 m_groupDragStartPos,
                                 normalizedRotation(m_groupDragStartRot + deltaDegrees),
                                 m_groupDragStartScale);
    emit liveViewportUpdate();
}

void SceneController::handleGroupRotationDragFinished(int groupId)
{
    handleGroupDragFinished(groupId);
}

// ── Graphics-tree: tool drop ──────────────────────────────────────────────────

void SceneController::handleToolDrop(const QString &toolName, int parentGroupId, int insertIndex)
{
    const bool moduleParameterZone = decodeModuleParameterInsertIndex(&insertIndex);

    if (isVariableTool(toolName)) {
        const int rootId  = m_scene.treeRoot().id;
        const int sceneId = m_scene.sceneNodeId();

        const SceneDocument::TreeNode *parentNode =
            parentGroupId > 0 ? m_scene.treeNodeById(parentGroupId) : nullptr;
        const bool inModule = parentNode
                              && parentNode->type == SceneDocument::TreeNode::Group
                              && parentNode->operation == SceneDocument::TreeNode::Module;

        if (parentGroupId > 0 && !inModule && parentGroupId != rootId && parentGroupId != sceneId)
            return;

        if (inModule) {
            struct AddModuleParamCommand : public QUndoCommand {
                SceneDocument *scene; int moduleId; int insertIdx; bool parameter;
                std::function<void()> refresh; int addedId = 0;
                AddModuleParamCommand(SceneDocument *s, int mid, int idx, bool isParam,
                                      std::function<void()> r)
                    : scene(s), moduleId(mid), insertIdx(idx), parameter(isParam), refresh(r) {}
                void redo() override { addedId = scene->addVariableToModule(moduleId, parameter, insertIdx); if (refresh) refresh(); }
                void undo() override { if (addedId > 0) { scene->removeVariableById(addedId); if (refresh) refresh(); } }
                bool isValid() const { return scene && moduleId > 0; }
            };
            auto *cmd = new AddModuleParamCommand(&m_scene, parentGroupId, insertIndex,
                                                   moduleParameterZone,
                                                   [this]() { emit sceneChanged(); });
            if (!cmd->isValid()) { delete cmd; return; }
            m_undoStack->push(cmd);
            return;
        }

        auto *command = new AddVariableCommand(&m_scene, insertIndex,
                                               [this]() { emit sceneChanged(); });
        if (!command->isValid()) { delete command; return; }
        m_undoStack->push(command);
        return;
    }

    SceneDocument::TreeNode::Operation operation;
    if (operationForTool(toolName, &operation)) {
        if (operation == SceneDocument::TreeNode::Module) {
            if (parentGroupId > 0 && parentGroupId != m_scene.treeRoot().id)
                return;
            parentGroupId = 0;
        } else if (parentGroupId <= 0 || parentGroupId == m_scene.treeRoot().id) {
            parentGroupId = m_scene.sceneNodeId();
        }

        auto *command = new AddGroupCommand(&m_scene, operation, parentGroupId, insertIndex,
                                            [this]() { emit sceneChanged(); });
        if (!command->isValid()) { delete command; return; }
        m_undoStack->push(command);
        return;
    }

    if (toolName != QStringLiteral("cube")
        && toolName != QStringLiteral("sphere")
        && toolName != QStringLiteral("cylinder"))
        return;

    ShapeNode shape = makeShapeForTool(toolName, m_scene.shapeCount() + 1);
    m_undoStack->push(new AddShapeCommand(&m_scene, shape, parentGroupId, insertIndex,
                                          [this]() { emit sceneChanged(); }));
}

// ── Graphics-tree: module call drop ──────────────────────────────────────────

void SceneController::handleModuleCallDrop(int moduleGroupId, int parentGroupId, int insertIndex)
{
    auto *command = new AddModuleCallCommand(&m_scene, moduleGroupId, parentGroupId, insertIndex,
                                             [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

// ── Graphics-tree: node selected ─────────────────────────────────────────────

void SceneController::handleNodeSelected(int nodeId)
{
    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node) {
        clearSelection();
        return;
    }

    if (node->type == SceneDocument::TreeNode::Primitive) {
        m_scene.setSelectedShapeId(node->shapeId);
        selectShapeInternal(node->shapeId);
    } else {
        m_scene.setSelectedShapeId(-1);
        m_selectedTreeNodeId = node->id;
        emit selectionChanged(node->id);
    }
}

// ── Graphics-tree: node delete ────────────────────────────────────────────────

void SceneController::handleNodeDeleteRequested(int nodeId)
{
    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node) return;

    if (node->type == SceneDocument::TreeNode::ModuleCall) {
        auto *command = new RemoveModuleCallCommand(&m_scene, node->id,
                                                    [this]() { emit sceneChanged(); });
        if (!command->isValid()) { delete command; return; }
        m_undoStack->push(command);
        return;
    }
    if (node->type == SceneDocument::TreeNode::Primitive) {
        auto *command = new DeleteShapeCommand(&m_scene, node->shapeId,
                                               [this]() { emit sceneChanged(); });
        if (!command->isValid()) { delete command; return; }
        m_undoStack->push(command);
        return;
    }
    if (node->type == SceneDocument::TreeNode::Variable) {
        auto *command = new RemoveVariableCommand(&m_scene, node->id,
                                                  [this]() { emit sceneChanged(); });
        if (!command->isValid()) { delete command; return; }
        m_undoStack->push(command);
        return;
    }
    if (node->id == m_scene.treeRoot().id) return;

    auto *command = new RemoveGroupCommand(&m_scene, node->id,
                                           [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

// ── Graphics-tree: transform value adjusted ───────────────────────────────────

void SceneController::handleTransformValueAdjusted(int groupId, int axis,
                                                    int numberStart, int numberLength,
                                                    qreal delta)
{
    if (axis < 0 || axis > 2 || qFuzzyIsNull(delta)) return;

    const SceneDocument::TreeNode *group = m_scene.treeNodeById(groupId);
    if (!group || group->type != SceneDocument::TreeNode::Group) return;

    const QString currentExpr = SceneTreeGraphics::transformAxisExpression(*group, axis);
    QStringList newExpressions = group->transformExpressions;
    while (newExpressions.size() < 3) newExpressions.append(QString());

    QVector3D position = group->position;
    QVector3D rotation = group->rotation;
    QVector3D scale    = group->scale;

    if (numberStart >= 0 && numberLength > 0
        && numberStart + numberLength <= currentExpr.size()) {

        const bool  isScale     = (group->operation == SceneDocument::TreeNode::Scale);
        const qreal minVal      = isScale ? 0.01 : -1e9;
        const QString numberText = currentExpr.mid(numberStart, numberLength);
        const int   decimalPoint = numberText.indexOf(QLatin1Char('.'));
        const int   precision    = decimalPoint >= 0
                                       ? qMin(3, numberText.size() - decimalPoint - 1) : 0;
        const qreal step         = precision > 0 ? 0.1 : 1.0;
        const bool  standalone   = isStandaloneNumericToken(currentExpr, numberStart, numberLength);
        const qreal tokenMin     = isScale ? 0.01 : (standalone ? -1e9 : 0.0);
        QString replacement = adjustedNumericToken(currentExpr, numberStart, numberLength,
                                                   delta, step, tokenMin,
                                                   isScale || !standalone);
        if (replacement.isEmpty()) return;
        if (isScale && precision == 0 && !replacement.contains(QLatin1Char('.')))
            replacement = QString::number(replacement.toDouble(), 'f', 1);

        const QString newExpr = currentExpr.left(numberStart)
                                + replacement
                                + currentExpr.mid(numberStart + numberLength);
        newExpressions[axis] = newExpr;

        if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
            QString contextPrefix = QStringLiteral("[");
            for (int i = 0; i < axis; ++i)
                contextPrefix += SceneTreeGraphics::transformAxisExpression(*group, i)
                                 + QStringLiteral(", ");
            m_ctrlHighlight.active        = true;
            m_ctrlHighlight.nodeId        = groupId;
            m_ctrlHighlight.contextPrefix = contextPrefix;
            m_ctrlHighlight.expression    = newExpr;
            m_ctrlHighlight.numberStart   = numberStart;
            m_ctrlHighlight.numberLength  = int(replacement.size());
        } else {
            m_ctrlHighlight.active = false;
        }

        QHash<QString, qreal> varValues;
        for (const SceneDocument::TreeNode &child : m_scene.treeRoot().children)
            if (child.type == SceneDocument::TreeNode::Variable)
                varValues[child.variableName] = child.variableValue;

        qreal newNumeric = replacement.toDouble();
        ExpressionSyntax::evaluate(newExpr, varValues, &newNumeric);
        newNumeric = qMax(minVal, newNumeric);

        if (axis == 0) {
            if (group->operation == SceneDocument::TreeNode::Translate) position.setX(float(newNumeric));
            else if (group->operation == SceneDocument::TreeNode::Rotate) rotation.setX(float(newNumeric));
            else scale.setX(float(newNumeric));
        } else if (axis == 1) {
            if (group->operation == SceneDocument::TreeNode::Translate) position.setY(float(newNumeric));
            else if (group->operation == SceneDocument::TreeNode::Rotate) rotation.setY(float(newNumeric));
            else scale.setY(float(newNumeric));
        } else {
            if (group->operation == SceneDocument::TreeNode::Translate) position.setZ(float(newNumeric));
            else if (group->operation == SceneDocument::TreeNode::Rotate) rotation.setZ(float(newNumeric));
            else scale.setZ(float(newNumeric));
        }
    } else {
        const bool  isScale = (group->operation == SceneDocument::TreeNode::Scale);
        const qreal step    = group->operation == SceneDocument::TreeNode::Rotate ? 5.0
                            : isScale ? 0.1 : 1.0;
        QVector3D *target   = group->operation == SceneDocument::TreeNode::Translate ? &position
                            : group->operation == SceneDocument::TreeNode::Rotate    ? &rotation
                                                                                     : &scale;
        auto adjustAxis = [&](float current) -> float {
            return float(isScale ? qMax(0.01, double(current) + delta * step)
                                 : double(current) + delta * step);
        };
        if (axis == 0)      target->setX(adjustAxis(target->x()));
        else if (axis == 1) target->setY(adjustAxis(target->y()));
        else                target->setZ(adjustAxis(target->z()));
        newExpressions[axis].clear();
        m_ctrlHighlight.active = false;
    }

    const SceneDocument::Snapshot oldSnapshot = m_scene.snapshot();
    if (!m_scene.updateGroupTransform(groupId, position, rotation, scale, newExpressions))
        return;
    const SceneDocument::Snapshot newSnapshot = m_scene.snapshot();
    m_scene.restoreSnapshot(oldSnapshot);

    auto *command = new UpdateGroupTransformCommand(&m_scene, oldSnapshot, newSnapshot,
                                                    [this]() { emit sceneChanged(); });
    m_undoStack->push(command);
    emit ctrlHighlightChanged();
}

// ── Graphics-tree: module rename ──────────────────────────────────────────────

void SceneController::handleModuleRenameRequested(int groupId, const QString &newName)
{
    auto *command = new RenameModuleCommand(&m_scene, groupId, newName,
                                            [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

// ── Graphics-tree: variable rename ───────────────────────────────────────────

void SceneController::handleVariableRenameRequested(int variableId, const QString &newName)
{
    auto *command = new RenameVariableCommand(&m_scene, variableId, newName,
                                              [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

// ── Graphics-tree: shape parameter adjusted ───────────────────────────────────

void SceneController::handleShapeParameterAdjusted(int nodeId, int paramIndex,
                                                    int numberStart, int numberLength,
                                                    qreal delta)
{
    if (nodeId <= 0 || paramIndex < 0 || numberStart < 0 || numberLength <= 0
        || qFuzzyIsNull(delta))
        return;

    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node || node->type != SceneDocument::TreeNode::Primitive) return;

    const ShapeNode *shape = m_scene.shapeById(node->shapeId);
    if (!shape) return;

    const QVector<SceneTreeGraphics::ShapeParameterControl> controls =
        SceneTreeGraphics::shapeParameterControls(*shape);
    if (paramIndex >= controls.size()) return;

    const QString &currentExpr = controls[paramIndex].expression;
    if (numberStart + numberLength > currentExpr.size()) return;

    const QString numberText  = currentExpr.mid(numberStart, numberLength);
    const int   decimalPoint  = numberText.indexOf(QLatin1Char('.'));
    const int   precision     = decimalPoint >= 0
                                    ? qMin(3, numberText.size() - decimalPoint - 1) : 0;
    const qreal step          = precision > 0 ? 0.1 : 1.0;
    const bool  standalone    = isStandaloneNumericToken(currentExpr, numberStart, numberLength);
    const QString replacement = adjustedNumericToken(currentExpr, numberStart, numberLength,
                                                     delta, step, 0.1, !standalone);
    if (replacement.isEmpty()) return;

    const QString newExpr = currentExpr.left(numberStart)
                            + replacement
                            + currentExpr.mid(numberStart + numberLength);

    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        QString contextPrefix;
        if (shape->type == ShapeNode::Cylinder) {
            contextPrefix = (paramIndex == 0) ? QStringLiteral(", r=") : QStringLiteral("h=");
        } else if (shape->type == ShapeNode::Sphere) {
            contextPrefix = QStringLiteral("r=");
        } else {
            contextPrefix = QStringLiteral("[");
            for (int i = 0; i < paramIndex && i < controls.size(); ++i)
                contextPrefix += controls[i].expression + QStringLiteral(", ");
        }
        m_ctrlHighlight.active        = true;
        m_ctrlHighlight.nodeId        = nodeId;
        m_ctrlHighlight.contextPrefix = contextPrefix;
        m_ctrlHighlight.expression    = newExpr;
        m_ctrlHighlight.numberStart   = numberStart;
        m_ctrlHighlight.numberLength  = int(replacement.size());
    } else {
        m_ctrlHighlight.active = false;
    }

    QHash<QString, qreal> varValues;
    for (const SceneDocument::TreeNode &child : m_scene.treeRoot().children)
        if (child.type == SceneDocument::TreeNode::Variable)
            varValues[child.variableName] = child.variableValue;

    qreal newNumericValue = replacement.toDouble();
    ExpressionSyntax::evaluate(newExpr, varValues, &newNumericValue);
    newNumericValue = qMax(0.1, newNumericValue);

    ShapeNode updatedShape = *shape;
    while (updatedShape.parameterExpressions.size() < controls.size())
        updatedShape.parameterExpressions.append(QString());
    updatedShape.parameterExpressions[paramIndex] = newExpr;

    if (updatedShape.type == ShapeNode::Cube) {
        QVector3D size = updatedShape.size;
        if (paramIndex == 0)      size.setX(newNumericValue);
        else if (paramIndex == 1) size.setY(newNumericValue);
        else if (paramIndex == 2) size.setZ(newNumericValue);
        updatedShape.size = size;
    } else if (updatedShape.type == ShapeNode::Sphere) {
        if (paramIndex == 0) updatedShape.radius = newNumericValue;
    } else if (updatedShape.type == ShapeNode::Cylinder) {
        if (paramIndex == 0)      updatedShape.radius = newNumericValue;
        else if (paramIndex == 1) updatedShape.height = newNumericValue;
    }

    auto *command = new UpdateShapeCommand(&m_scene, *shape, updatedShape,
                                           [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
    emit ctrlHighlightChanged();
}

// ── Graphics-tree: variable number adjusted ───────────────────────────────────

void SceneController::handleVariableNumberAdjusted(int nodeId, int start, int length,
                                                    qreal delta)
{
    if (nodeId <= 0 || start < 0 || length <= 0 || qFuzzyIsNull(delta)) return;

    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node || node->type != SceneDocument::TreeNode::Variable) return;

    QString expression = node->variableExpression;
    if (start + length > expression.size()) return;

    const QString numberText  = expression.mid(start, length);
    const int   decimalPoint  = numberText.indexOf(QLatin1Char('.'));
    const int   precision     = decimalPoint >= 0
                                    ? qMin(3, numberText.size() - decimalPoint - 1) : 0;
    const qreal step          = precision > 0 ? 0.1 : 1.0;
    const bool  standalone    = isStandaloneNumericToken(expression, start, length);
    QString replacement = adjustedNumericToken(expression, start, length,
                                               delta, step, 0.0, !standalone);
    if (replacement.isEmpty()) return;

    expression.replace(start, length, replacement);

    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        m_ctrlHighlight.active        = true;
        m_ctrlHighlight.nodeId        = nodeId;
        m_ctrlHighlight.contextPrefix = QString();
        m_ctrlHighlight.expression    = expression;
        m_ctrlHighlight.numberStart   = start;
        m_ctrlHighlight.numberLength  = int(replacement.size());
    } else {
        m_ctrlHighlight.active = false;
    }

    auto *command = new UpdateVariableExpressionCommand(&m_scene, nodeId, expression,
                                                        [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
    emit ctrlHighlightChanged();
}

// ── Graphics-tree: module call argument adjusted ──────────────────────────────

void SceneController::handleModuleCallArgumentAdjusted(int moduleCallId,
                                                        int parameterVariableId,
                                                        int start, int length,
                                                        qreal delta)
{
    if (moduleCallId <= 0 || parameterVariableId <= 0
        || start < 0 || length <= 0 || qFuzzyIsNull(delta))
        return;

    const SceneDocument::TreeNode *callNode      = m_scene.treeNodeById(moduleCallId);
    const SceneDocument::TreeNode *parameterNode = m_scene.treeNodeById(parameterVariableId);
    if (!callNode || callNode->type != SceneDocument::TreeNode::ModuleCall
        || !parameterNode || parameterNode->type != SceneDocument::TreeNode::Variable
        || !parameterNode->isParameter)
        return;

    const SceneDocument::TreeNode *moduleGroupNode = m_scene.treeNodeById(callNode->shapeId);
    const QHash<QString, QString> overrides = moduleGroupNode
        ? resolveModuleArguments(callNode->moduleCallArguments, *moduleGroupNode)
        : parseNamedArgumentExpressions(callNode->moduleCallArguments);
    QString expression = overrides.value(
        parameterNode->variableName,
        parameterNode->variableExpression.trimmed().isEmpty()
            ? QString::number(parameterNode->variableValue)
            : parameterNode->variableExpression.trimmed());

    if (start + length > expression.size()) return;

    const QString numberText  = expression.mid(start, length);
    const int   decimalPoint  = numberText.indexOf(QLatin1Char('.'));
    const int   precision     = decimalPoint >= 0
                                    ? qMin(3, numberText.size() - decimalPoint - 1) : 0;
    const qreal step          = precision > 0 ? 0.1 : 1.0;
    const bool  standalone    = isStandaloneNumericToken(expression, start, length);
    QString replacement = adjustedNumericToken(expression, start, length,
                                               delta, step, 0.0, !standalone);
    if (replacement.isEmpty()) return;

    expression.replace(start, length, replacement);

    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        m_ctrlHighlight.active        = true;
        m_ctrlHighlight.nodeId        = moduleCallId;
        m_ctrlHighlight.contextPrefix = QString();
        m_ctrlHighlight.expression    = expression;
        m_ctrlHighlight.numberStart   = start;
        m_ctrlHighlight.numberLength  = int(replacement.size());
    } else {
        m_ctrlHighlight.active = false;
    }

    auto *command = new UpdateModuleCallArgumentCommand(&m_scene,
                                                        moduleCallId,
                                                        parameterNode->variableName,
                                                        expression,
                                                        [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
    emit ctrlHighlightChanged();
}

// ── Graphics-tree: for loop range adjusted ────────────────────────────────────

void SceneController::handleForLoopRangeAdjusted(int nodeId, int start, int length,
                                                   qreal delta)
{
    if (nodeId <= 0 || start < 0 || length <= 0 || qFuzzyIsNull(delta)) return;

    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node || node->type != SceneDocument::TreeNode::Group
        || node->operation != SceneDocument::TreeNode::For)
        return;

    QString expression = SceneTreeGraphics::forLoopRangeExpression(*node);
    if (start + length > expression.size()) return;

    const QString numberText  = expression.mid(start, length);
    const int   decimalPoint  = numberText.indexOf(QLatin1Char('.'));
    const int   precision     = decimalPoint >= 0
                                    ? qMin(3, numberText.size() - decimalPoint - 1) : 0;
    const qreal step          = precision > 0 ? 0.1 : 1.0;
    const bool  standalone    = isStandaloneNumericToken(expression, start, length);
    const QString replacement = adjustedNumericToken(expression, start, length,
                                                     delta, step, 0.0, !standalone);
    if (replacement.isEmpty()) return;

    expression.replace(start, length, replacement);

    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        m_ctrlHighlight.active        = true;
        m_ctrlHighlight.nodeId        = nodeId;
        m_ctrlHighlight.contextPrefix = QString();
        m_ctrlHighlight.expression    = expression;
        m_ctrlHighlight.numberStart   = start;
        m_ctrlHighlight.numberLength  = int(replacement.size());
    } else {
        m_ctrlHighlight.active = false;
    }

    auto *command = new UpdateForLoopCommand(&m_scene, nodeId,
                                             SceneTreeGraphics::forLoopVariableName(*node),
                                             expression,
                                             [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
    emit ctrlHighlightChanged();
}

// ── Graphics-tree: ctrl released ─────────────────────────────────────────────

void SceneController::handleCtrlReleased()
{
    m_ctrlHighlight.active = false;
    emit ctrlHighlightChanged();
}
