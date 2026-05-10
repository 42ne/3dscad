#include "scenecommands.h"

AddShapeCommand::AddShapeCommand(SceneDocument *scene, const ShapeNode &shape, std::function<void()> onChanged)
    : QUndoCommand("Add shape")
    , m_scene(scene)
    , m_shape(shape)
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
        const int id = m_scene->addShape(m_shape);
        const ShapeNode *insertedShape = m_scene->shapeById(id);
        if (insertedShape)
            m_shape = *insertedShape;

        m_index = m_scene->indexOfShapeId(id);
        m_firstRedo = false;
    } else {
        m_scene->insertShape(m_index, m_shape);
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
    : QUndoCommand("Apply OpenSCAD code")
    , m_scene(scene)
    , m_newShapes(newShapes)
    , m_onChanged(onChanged)
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_valid = m_oldSnapshot.shapes != m_newShapes;
}

bool ReplaceSceneCommand::isValid() const
{
    return m_valid;
}

void ReplaceSceneCommand::undo()
{
    if (!m_scene || !m_valid)
        return;

    m_scene->restoreSnapshot(m_oldSnapshot);

    if (m_onChanged)
        m_onChanged();
}

void ReplaceSceneCommand::redo()
{
    if (!m_scene || !m_valid)
        return;

    m_scene->replaceShapes(m_newShapes);

    if (m_onChanged)
        m_onChanged();
}

AddGroupCommand::AddGroupCommand(SceneDocument *scene,
                                 SceneDocument::TreeNode::Operation operation,
                                 int parentGroupId,
                                 int insertIndex,
                                 std::function<void()> onChanged)
    : QUndoCommand("Add group")
    , m_scene(scene)
    , m_onChanged(onChanged)
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

bool AddGroupCommand::isValid() const
{
    return m_valid;
}

void AddGroupCommand::undo()
{
    if (!m_scene || !m_valid)
        return;

    m_scene->restoreSnapshot(m_oldSnapshot);

    if (m_onChanged)
        m_onChanged();
}

void AddGroupCommand::redo()
{
    if (!m_scene || !m_valid)
        return;

    m_scene->restoreSnapshot(m_newSnapshot);

    if (m_onChanged)
        m_onChanged();
}

RemoveGroupCommand::RemoveGroupCommand(SceneDocument *scene, int groupId, std::function<void()> onChanged)
    : QUndoCommand("Remove group")
    , m_scene(scene)
    , m_onChanged(onChanged)
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_valid = m_scene->removeGroupById(groupId);
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

bool RemoveGroupCommand::isValid() const
{
    return m_valid;
}

void RemoveGroupCommand::undo()
{
    if (!m_scene || !m_valid)
        return;

    m_scene->restoreSnapshot(m_oldSnapshot);

    if (m_onChanged)
        m_onChanged();
}

void RemoveGroupCommand::redo()
{
    if (!m_scene || !m_valid)
        return;

    m_scene->restoreSnapshot(m_newSnapshot);

    if (m_onChanged)
        m_onChanged();
}

MoveTreeNodeCommand::MoveTreeNodeCommand(SceneDocument *scene, int nodeId, int parentGroupId, int insertIndex, std::function<void()> onChanged)
    : QUndoCommand("Move tree node")
    , m_scene(scene)
    , m_onChanged(onChanged)
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_valid = m_scene->moveTreeNode(nodeId, parentGroupId, insertIndex);
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

bool MoveTreeNodeCommand::isValid() const
{
    return m_valid;
}

void MoveTreeNodeCommand::undo()
{
    if (!m_scene || !m_valid)
        return;

    m_scene->restoreSnapshot(m_oldSnapshot);

    if (m_onChanged)
        m_onChanged();
}

void MoveTreeNodeCommand::redo()
{
    if (!m_scene || !m_valid)
        return;

    m_scene->restoreSnapshot(m_newSnapshot);

    if (m_onChanged)
        m_onChanged();
}

UpdateGroupTransformCommand::UpdateGroupTransformCommand(SceneDocument *scene,
                                                         int groupId,
                                                         const QVector3D &position,
                                                         const QVector3D &rotation,
                                                         std::function<void()> onChanged)
    : QUndoCommand("Update group transform")
    , m_scene(scene)
    , m_onChanged(onChanged)
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_valid = m_scene->updateGroupTransform(groupId, position, rotation);
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

UpdateGroupTransformCommand::UpdateGroupTransformCommand(SceneDocument *scene,
                                                         int groupId,
                                                         const QVector3D &newPosition,
                                                         const QVector3D &newRotation,
                                                         std::function<void()> onChanged,
                                                         const QVector3D &oldPosition,
                                                         const QVector3D &oldRotation)
    : QUndoCommand("Update group transform")
    , m_scene(scene)
    , m_onChanged(onChanged)
{
    if (!m_scene)
        return;

    m_oldSnapshot = m_scene->snapshot();
    m_scene->updateGroupTransform(groupId, oldPosition, oldRotation);
    m_oldSnapshot = m_scene->snapshot();

    m_valid = m_scene->updateGroupTransform(groupId, newPosition, newRotation);
    if (m_valid)
        m_newSnapshot = m_scene->snapshot();

    m_scene->restoreSnapshot(m_oldSnapshot);
}

UpdateGroupTransformCommand::UpdateGroupTransformCommand(SceneDocument *scene,
                                                         const SceneDocument::Snapshot &oldSnapshot,
                                                         const SceneDocument::Snapshot &newSnapshot,
                                                         std::function<void()> onChanged)
    : QUndoCommand("Update group transform")
    , m_scene(scene)
    , m_oldSnapshot(oldSnapshot)
    , m_newSnapshot(newSnapshot)
    , m_valid(true)
    , m_onChanged(onChanged)
{
}

bool UpdateGroupTransformCommand::isValid() const
{
    return m_valid;
}

void UpdateGroupTransformCommand::undo()
{
    if (!m_scene || !m_valid)
        return;

    m_scene->restoreSnapshot(m_oldSnapshot);

    if (m_onChanged)
        m_onChanged();
}

void UpdateGroupTransformCommand::redo()
{
    if (!m_scene || !m_valid)
        return;

    m_scene->restoreSnapshot(m_newSnapshot);

    if (m_onChanged)
        m_onChanged();
}
