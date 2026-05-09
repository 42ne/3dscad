#include "scenedocument.h"

const QVector<ShapeNode> &SceneDocument::shapes() const
{
    return m_shapes;
}

int SceneDocument::shapeCount() const
{
    return m_shapes.size();
}

bool SceneDocument::isEmpty() const
{
    return m_shapes.isEmpty();
}

int SceneDocument::selectedIndex() const
{
    return indexOfShapeId(m_selectedShapeId);
}

int SceneDocument::selectedShapeId() const
{
    return m_selectedShapeId;
}

void SceneDocument::setSelectedIndex(int index)
{
    const ShapeNode *shape = shapeAt(index);
    setSelectedShapeId(shape ? shape->id : -1);
}

void SceneDocument::setSelectedShapeId(int id)
{
    m_selectedShapeId = shapeById(id) ? id : -1;
}

bool SceneDocument::hasSelection() const
{
    return selectedShape() != nullptr;
}

const ShapeNode *SceneDocument::selectedShape() const
{
    return shapeById(m_selectedShapeId);
}

ShapeNode *SceneDocument::selectedShape()
{
    return shapeById(m_selectedShapeId);
}

const ShapeNode *SceneDocument::shapeAt(int index) const
{
    if (!isValidIndex(index))
        return nullptr;

    return &m_shapes[index];
}

ShapeNode *SceneDocument::shapeAt(int index)
{
    if (!isValidIndex(index))
        return nullptr;

    return &m_shapes[index];
}

const ShapeNode *SceneDocument::shapeById(int id) const
{
    const int index = indexOfShapeId(id);
    return shapeAt(index);
}

ShapeNode *SceneDocument::shapeById(int id)
{
    const int index = indexOfShapeId(id);
    return shapeAt(index);
}

int SceneDocument::indexOfShapeId(int id) const
{
    for (int i = 0; i < m_shapes.size(); ++i) {
        if (m_shapes[i].id == id)
            return i;
    }

    return -1;
}

int SceneDocument::addShape(const ShapeNode &shape)
{
    ShapeNode shapeWithId = shape;
    shapeWithId.id = m_nextShapeId++;
    m_shapes.append(shapeWithId);
    return shapeWithId.id;
}

bool SceneDocument::removeShapeById(int id)
{
    const int index = indexOfShapeId(id);
    if (!isValidIndex(index))
        return false;

    const bool wasSelected = m_shapes[index].id == m_selectedShapeId;
    m_shapes.removeAt(index);

    if (wasSelected) {
        if (m_shapes.isEmpty()) {
            m_selectedShapeId = -1;
        } else {
            const int nextIndex = qMin(index, m_shapes.size() - 1);
            m_selectedShapeId = m_shapes[nextIndex].id;
        }
    }

    return true;
}

bool SceneDocument::removeSelectedShape()
{
    return removeShapeById(m_selectedShapeId);
}

bool SceneDocument::isValidIndex(int index) const
{
    return index >= 0 && index < m_shapes.size();
}
