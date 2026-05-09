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
    return m_selectedIndex;
}

void SceneDocument::setSelectedIndex(int index)
{
    m_selectedIndex = isValidIndex(index) ? index : -1;
}

bool SceneDocument::hasSelection() const
{
    return isValidIndex(m_selectedIndex);
}

const ShapeNode *SceneDocument::selectedShape() const
{
    return shapeAt(m_selectedIndex);
}

ShapeNode *SceneDocument::selectedShape()
{
    return shapeAt(m_selectedIndex);
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

int SceneDocument::addShape(const ShapeNode &shape)
{
    m_shapes.append(shape);
    return m_shapes.size() - 1;
}

bool SceneDocument::isValidIndex(int index) const
{
    return index >= 0 && index < m_shapes.size();
}

