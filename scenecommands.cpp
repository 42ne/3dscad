#include "scenecommands.h"

// ── SnapshotCommand ──────────────────────────────────────────────────────────

SnapshotCommand::SnapshotCommand(const QString &text,
                                 SceneDocument *scene,
                                 std::function<void()> onChanged)
    : QUndoCommand(text)
    , m_scene(scene)
    , m_onChanged(std::move(onChanged))
{
}

bool SnapshotCommand::isValid() const
{
    return m_valid;
}

void SnapshotCommand::undo()
{
    if (!m_scene || !m_valid)
        return;

    m_scene->restoreSnapshot(m_oldSnapshot);

    if (m_onChanged)
        m_onChanged();
}

void SnapshotCommand::redo()
{
    if (!m_scene || !m_valid)
        return;

    m_scene->restoreSnapshot(m_newSnapshot);

    if (m_onChanged)
        m_onChanged();
}

// ── AddShapeCommand ──────────────────────────────────────────────────────────

AddShapeCommand::AddShapeCommand(SceneDocument *scene, const ShapeNode &shape, std::function<void()> onChanged)
    : AddShapeCommand(scene, shape, 0, onChanged)
{
}

AddShapeCommand::AddShapeCommand(SceneDocument *scene, const ShapeNode &shape, int parentGroupId, std::function<void()> onChanged)
    : AddShapeCommand(scene, shape, parentGroupId, -1, onChanged)
{
}

AddShapeCommand::AddShapeCommand(SceneDocument *scene, const ShapeNode &shape, int parentGroupId, int treeInsertIndex, std::function<void()> onChanged)
    : QUndoCommand("Add shape")
    , m_scene(scene)
    , m_shape(shape)
    , m_parentGroupId(parentGroupId)
    , m_treeInsertIndex(treeInsertIndex)
    , m_onChanged(onChanged)
{
}

void AddShapeCommand::undo()
{
    if (!m_scene)
        return;

    m_scene->removeShapeById(m_shape.id);

    if (m_onChanged)
        m_onChanged();
}

void AddShapeCommand::redo()
{
    if (!m_scene)
        return;

    if (m_firstRedo) {
        const int id = m_scene->addShape(m_shape, m_parentGroupId, m_treeInsertIndex);
        const ShapeNode *insertedShape = m_scene->shapeById(id);
        if (insertedShape)
            m_shape = *insertedShape;

        m_index = m_scene->indexOfShapeId(id);
        m_firstRedo = false;
    } else {
        m_scene->insertShape(m_index, m_shape, m_parentGroupId, m_treeInsertIndex);
    }

    m_scene->setSelectedShapeId(m_shape.id);

    if (m_onChanged)
        m_onChanged();
}

DeleteShapeCommand::DeleteShapeCommand(SceneDocument *scene, int shapeId, std::function<void()> onChanged)
    : QUndoCommand("Delete shape")
    , m_scene(scene)
    , m_onChanged(onChanged)
{
    if (!m_scene)
        return;

    const ShapeNode *shape = m_scene->shapeById(shapeId);
    if (!shape)
        return;

    m_shape = *shape;
    m_index = m_scene->indexOfShapeId(shapeId);
    m_valid = true;
}

bool DeleteShapeCommand::isValid() const
{
    return m_valid;
}

void DeleteShapeCommand::undo()
{
    if (!m_scene || !m_valid)
        return;

    m_scene->insertShape(m_index, m_shape);
    m_scene->setSelectedShapeId(m_shape.id);

    if (m_onChanged)
        m_onChanged();
}

void DeleteShapeCommand::redo()
{
    if (!m_scene || !m_valid)
        return;

    m_scene->removeShapeById(m_shape.id);

    if (m_onChanged)
        m_onChanged();
}

RemovePolyhedronElementCommand::RemovePolyhedronElementCommand(SceneDocument *scene,
                                                               int shapeId,
                                                               int polyhedronGroupId,
                                                               int pointChildIndex,
                                                               std::function<void()> onChanged)
    : SnapshotCommand("Remove polyhedron element", scene, std::move(onChanged))
{
    if (!m_scene || shapeId <= 0)
        return;

    m_oldSnapshot = m_scene->snapshot();
    const bool removed = m_scene->removeShapeById(shapeId);
    if (!removed) {
        m_scene->restoreSnapshot(m_oldSnapshot);
        return;
    }

    if (polyhedronGroupId > 0 && pointChildIndex >= 0)
        m_scene->remapPolyhedronFacesAfterChildDelete(polyhedronGroupId, pointChildIndex);

    m_newSnapshot = m_scene->snapshot();
    m_valid = true;
    m_scene->restoreSnapshot(m_oldSnapshot);
}

UpdateShapeCommand::UpdateShapeCommand(SceneDocument *scene, const ShapeNode &oldShape, const ShapeNode &newShape, std::function<void()> onChanged)
    : QUndoCommand("Update shape")
    , m_scene(scene)
    , m_oldShape(oldShape)
    , m_newShape(newShape)
    , m_valid(oldShape.id == newShape.id && oldShape != newShape)
    , m_onChanged(onChanged)
{
}

bool UpdateShapeCommand::isValid() const
{
    return m_valid;
}

void UpdateShapeCommand::undo()
{
    if (!m_scene || !m_valid)
        return;

    m_scene->updateShape(m_oldShape);

    if (m_onChanged)
        m_onChanged();
}

void UpdateShapeCommand::redo()
{
    if (!m_scene || !m_valid)
        return;

    m_scene->updateShape(m_newShape);

    if (m_onChanged)
        m_onChanged();
}

ReplaceSceneCommand::ReplaceSceneCommand(SceneDocument *scene, const QVector<ShapeNode> &newShapes, std::function<void()> onChanged)
    : SnapshotCommand("Apply OpenSCAD code", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    SceneDocument previewScene;
    previewScene.replaceShapes(newShapes);
    m_newSnapshot = previewScene.snapshot();
    m_valid = m_oldSnapshot.shapes != newShapes;
}

ReplaceSceneCommand::ReplaceSceneCommand(SceneDocument *scene, const SceneDocument::Snapshot &newSnapshot, std::function<void()> onChanged)
    : SnapshotCommand("Apply OpenSCAD code", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_newSnapshot = newSnapshot;
    m_valid = true;
}

AddGroupCommand::AddGroupCommand(SceneDocument *scene,
                                 SceneDocument::TreeNode::Operation operation,
                                 int parentGroupId,
                                 int insertIndex,
                                 std::function<void()> onChanged)
    : SnapshotCommand("Add group", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    const int groupId = m_scene->addGroup(operation, parentGroupId, insertIndex);
    m_valid = groupId > 0;
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

AddRawCodeCommand::AddRawCodeCommand(SceneDocument *scene,
                                     const QString &code,
                                     int parentGroupId,
                                     int insertIndex,
                                     std::function<void()> onChanged)
    : SnapshotCommand("Add raw OpenSCAD block", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    const int nodeId = m_scene->addRawCode(code, parentGroupId, insertIndex);
    m_valid = nodeId > 0;
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

AddPolyhedronGroupCommand::AddPolyhedronGroupCommand(SceneDocument *scene,
                                                     int parentGroupId,
                                                     int insertIndex,
                                                     std::function<void()> onChanged)
    : SnapshotCommand("Add polyhedron", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();

    const int groupId = m_scene->addGroup(SceneDocument::TreeNode::Polyhedron,
                                          parentGroupId, insertIndex);
    if (groupId <= 0) {
        m_valid = false;
        return;
    }

    m_newSnapshot = m_scene->snapshot();
    m_valid = true;
    m_scene->restoreSnapshot(m_oldSnapshot);
}

RemoveGroupCommand::RemoveGroupCommand(SceneDocument *scene, int groupId, std::function<void()> onChanged)
    : SnapshotCommand("Remove group", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_valid = m_scene->removeGroupById(groupId);
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

RemovePolyhedronGroupCommand::RemovePolyhedronGroupCommand(SceneDocument *scene,
                                                           int groupId,
                                                           std::function<void()> onChanged)
    : SnapshotCommand("Remove polyhedron", scene, std::move(onChanged))
{
    if (!m_scene || groupId <= 0)
        return;

    const SceneDocument::TreeNode *group = m_scene->treeNodeById(groupId);
    if (!group || group->type != SceneDocument::TreeNode::Group
        || group->operation != SceneDocument::TreeNode::Polyhedron) {
        return;
    }

    QVector<int> dataShapeIds;
    std::function<void(const SceneDocument::TreeNode &)> collectDataShapes =
        [&](const SceneDocument::TreeNode &node) {
            if (node.type == SceneDocument::TreeNode::Primitive) {
                const ShapeNode *shape = m_scene->shapeById(node.shapeId);
                if (shape && (shape->type == ShapeNode::Point3D || shape->type == ShapeNode::Face3D))
                    dataShapeIds.append(node.shapeId);
                return;
            }
            for (const SceneDocument::TreeNode &child : node.children)
                collectDataShapes(child);
        };
    collectDataShapes(*group);

    m_oldSnapshot = m_scene->snapshot();
    if (!m_scene->removeGroupById(groupId)) {
        m_scene->restoreSnapshot(m_oldSnapshot);
        return;
    }

    for (int shapeId : dataShapeIds)
        m_scene->removeShapeById(shapeId);

    m_newSnapshot = m_scene->snapshot();
    m_valid = true;
    m_scene->restoreSnapshot(m_oldSnapshot);
}

AddVariableCommand::AddVariableCommand(SceneDocument *scene, int insertIndex, std::function<void()> onChanged)
    : SnapshotCommand("Add variable", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    const int variableId = m_scene->addVariable(insertIndex);
    m_valid = variableId > 0;
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

AddModuleCallCommand::AddModuleCallCommand(SceneDocument *scene,
                                           int moduleGroupId,
                                           int parentGroupId,
                                           int insertIndex,
                                           std::function<void()> onChanged)
    : SnapshotCommand("Add module call", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    const int callId = m_scene->addModuleCall(moduleGroupId, parentGroupId, insertIndex);
    m_valid = callId > 0;
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

RemoveVariableCommand::RemoveVariableCommand(SceneDocument *scene, int variableId, std::function<void()> onChanged)
    : SnapshotCommand("Remove variable", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_valid = m_scene->removeVariableById(variableId);
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

RemoveModuleCallCommand::RemoveModuleCallCommand(SceneDocument *scene, int moduleCallId, std::function<void()> onChanged)
    : SnapshotCommand("Remove module call", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_valid = m_scene->removeModuleCallById(moduleCallId);
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

UpdateVariableExpressionCommand::UpdateVariableExpressionCommand(SceneDocument *scene,
                                                                 int variableId,
                                                                 const QString &expression,
                                                                 std::function<void()> onChanged)
    : SnapshotCommand("Update variable expression", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_valid = m_scene->updateVariableExpression(variableId, expression);
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

UpdateModuleCallArgumentCommand::UpdateModuleCallArgumentCommand(SceneDocument *scene,
                                                                 int moduleCallId,
                                                                 const QString &parameterName,
                                                                 const QString &expression,
                                                                 std::function<void()> onChanged)
    : SnapshotCommand("Update module call argument", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_valid = m_scene->updateModuleCallArgument(moduleCallId, parameterName, expression);
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

UpdateForLoopCommand::UpdateForLoopCommand(SceneDocument *scene,
                                           int groupId,
                                           const QString &loopVariable,
                                           const QString &rangeExpression,
                                           std::function<void()> onChanged)
    : SnapshotCommand("Update for loop", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_valid = m_scene->updateForLoop(groupId, loopVariable, rangeExpression);
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

UpdateConditionExpressionCommand::UpdateConditionExpressionCommand(SceneDocument *scene,
                                                                   int groupId,
                                                                   const QString &conditionExpression,
                                                                   std::function<void()> onChanged)
    : SnapshotCommand("Update if condition", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_valid = m_scene->updateConditionExpression(groupId, conditionExpression);
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

UpdateRawCodeCommand::UpdateRawCodeCommand(SceneDocument *scene,
                                           int groupId,
                                           const QString &code,
                                           std::function<void()> onChanged)
    : SnapshotCommand("Edit raw OpenSCAD block", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_valid = m_scene->updateRawCode(groupId, code);
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

UpdateTextContentCommand::UpdateTextContentCommand(SceneDocument *scene,
                                                   int shapeId,
                                                   const QString &text,
                                                   std::function<void()> onChanged)
    : SnapshotCommand("Edit text", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_valid = m_scene->updateTextContent(shapeId, text);
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

UpdateGroupColorCommand::UpdateGroupColorCommand(SceneDocument *scene,
                                                 int groupId,
                                                 const QColor &color,
                                                 std::function<void()> onChanged)
    : SnapshotCommand("Update group color", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_valid = m_scene->updateGroupColor(groupId, color);
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

MoveTreeNodeCommand::MoveTreeNodeCommand(SceneDocument *scene,
                                         int nodeId,
                                         int parentGroupId,
                                         int insertIndex,
                                         std::function<void()> onChanged,
                                         bool moduleParameterZone)
    : SnapshotCommand("Move tree node", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_valid = m_scene->moveTreeNode(nodeId, parentGroupId, insertIndex, moduleParameterZone);
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

UpdateGroupTransformCommand::UpdateGroupTransformCommand(SceneDocument *scene,
                                                         int groupId,
                                                         const QVector3D &position,
                                                         const QVector3D &rotation,
                                                         const QVector3D &scale,
                                                         std::function<void()> onChanged)
    : SnapshotCommand("Update group transform", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_valid = m_scene->updateGroupTransform(groupId, position, rotation, scale);
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

UpdateGroupTransformCommand::UpdateGroupTransformCommand(SceneDocument *scene,
                                                         int groupId,
                                                         const QVector3D &newPosition,
                                                         const QVector3D &newRotation,
                                                         const QVector3D &newScale,
                                                         std::function<void()> onChanged,
                                                         const QVector3D &oldPosition,
                                                         const QVector3D &oldRotation,
                                                         const QVector3D &oldScale)
    : SnapshotCommand("Update group transform", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_scene->updateGroupTransform(groupId, oldPosition, oldRotation, oldScale);
    m_oldSnapshot = m_scene->snapshot();

    m_valid = m_scene->updateGroupTransform(groupId, newPosition, newRotation, newScale);
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

UpdateGroupTransformCommand::UpdateGroupTransformCommand(SceneDocument *scene,
                                                         const SceneDocument::Snapshot &oldSnapshot,
                                                         const SceneDocument::Snapshot &newSnapshot,
                                                         std::function<void()> onChanged)
    : SnapshotCommand("Update group transform", scene, std::move(onChanged))
{
    m_oldSnapshot = oldSnapshot;
    m_newSnapshot = newSnapshot;
    m_valid = true;
}

RenameModuleCommand::RenameModuleCommand(SceneDocument *scene, int groupId, const QString &newName, std::function<void()> onChanged)
    : SnapshotCommand("Rename module", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_valid = m_scene->setModuleName(groupId, newName);
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

RenameVariableCommand::RenameVariableCommand(SceneDocument *scene, int variableId, const QString &newName, std::function<void()> onChanged)
    : SnapshotCommand("Rename variable", scene, std::move(onChanged))
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_valid = m_scene->renameVariable(variableId, newName);
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}
