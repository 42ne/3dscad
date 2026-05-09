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
