#include "scenedocument.h"

const QVector<ShapeNode> &SceneDocument::shapes() const
{
    return m_shapes;
}

const SceneDocument::TreeNode &SceneDocument::treeRoot() const
{
    return m_treeRoot;
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
    return insertShape(m_shapes.size(), shape);
}

int SceneDocument::insertShape(int index, const ShapeNode &shape)
{
    ShapeNode shapeWithId = shape;

    if (shapeWithId.id < 0)
        shapeWithId.id = m_nextShapeId++;
    else
        m_nextShapeId = qMax(m_nextShapeId, shapeWithId.id + 1);

    const int insertIndex = qBound(0, index, m_shapes.size());
    m_shapes.insert(insertIndex, shapeWithId);
    m_selectedShapeId = shapeWithId.id;
    rebuildTreeFromShapes();

    return shapeWithId.id;
}

void SceneDocument::replaceShapes(const QVector<ShapeNode> &shapes)
{
    m_shapes.clear();
    m_selectedShapeId = -1;
    m_treeRoot = {};
    m_nextTreeNodeId = 1;

    for (const ShapeNode &shape : shapes)
        addShape(shape);

    if (!m_shapes.isEmpty())
        m_selectedShapeId = m_shapes.first().id;

    rebuildTreeFromShapes();
}

SceneDocument::Snapshot SceneDocument::snapshot() const
{
    Snapshot snapshot;
    snapshot.shapes = m_shapes;
    snapshot.treeRoot = m_treeRoot;
    snapshot.selectedShapeId = m_selectedShapeId;
    snapshot.nextShapeId = m_nextShapeId;
    snapshot.nextTreeNodeId = m_nextTreeNodeId;
    return snapshot;
}

void SceneDocument::restoreSnapshot(const Snapshot &snapshot)
{
    m_shapes = snapshot.shapes;
    m_treeRoot = snapshot.treeRoot;
    m_nextShapeId = snapshot.nextShapeId;
    m_nextTreeNodeId = snapshot.nextTreeNodeId;
    setSelectedShapeId(snapshot.selectedShapeId);
}

bool SceneDocument::updateShape(const ShapeNode &shape)
{
    ShapeNode *existingShape = shapeById(shape.id);
    if (!existingShape)
        return false;

    *existingShape = shape;
    m_selectedShapeId = shape.id;
    rebuildTreeFromShapes();
    return true;
}

bool SceneDocument::takeShapeById(int id, ShapeNode *removedShape, int *removedIndex)
{
    const int index = indexOfShapeId(id);
    if (!isValidIndex(index))
        return false;

    if (removedShape)
        *removedShape = m_shapes[index];

    if (removedIndex)
        *removedIndex = index;

    const bool wasSelected = m_shapes[index].id == m_selectedShapeId;
    m_shapes.removeAt(index);
    rebuildTreeFromShapes();

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

bool SceneDocument::removeShapeById(int id)
{
    return takeShapeById(id, nullptr, nullptr);
}

bool SceneDocument::removeSelectedShape()
{
    return removeShapeById(m_selectedShapeId);
}

bool SceneDocument::isValidIndex(int index) const
{
    return index >= 0 && index < m_shapes.size();
}

SceneDocument::TreeNode SceneDocument::makeGroupNode(TreeNode::Operation operation)
{
    TreeNode node;
    node.id = m_nextTreeNodeId++;
    node.type = TreeNode::Group;
    node.operation = operation;
    return node;
}

SceneDocument::TreeNode SceneDocument::makePrimitiveNode(int shapeId)
{
    TreeNode node;
    node.id = m_nextTreeNodeId++;
    node.type = TreeNode::Primitive;
    node.shapeId = shapeId;
    return node;
}

void SceneDocument::rebuildTreeFromShapes()
{
    m_nextTreeNodeId = 1;

    TreeNode unionGroup = makeGroupNode(TreeNode::Union);
    TreeNode differenceGroup = makeGroupNode(TreeNode::Difference);
    TreeNode intersectionGroup = makeGroupNode(TreeNode::Intersection);

    for (const ShapeNode &shape : m_shapes) {
        if (shape.booleanMode == ShapeNode::Subtract)
            differenceGroup.children.append(makePrimitiveNode(shape.id));
        else if (shape.booleanMode == ShapeNode::Intersect)
            intersectionGroup.children.append(makePrimitiveNode(shape.id));
        else
            unionGroup.children.append(makePrimitiveNode(shape.id));
    }

    if (m_shapes.isEmpty()) {
        m_treeRoot = makeGroupNode(TreeNode::Union);
        return;
    }

    if (!differenceGroup.children.isEmpty()) {
        TreeNode differenceRoot = makeGroupNode(TreeNode::Difference);
        differenceRoot.children.append(unionGroup);
        differenceRoot.children += differenceGroup.children;
        m_treeRoot = differenceRoot;
    } else {
        m_treeRoot = unionGroup;
    }

    if (!intersectionGroup.children.isEmpty()) {
        TreeNode intersectionRoot = makeGroupNode(TreeNode::Intersection);
        intersectionRoot.children.append(m_treeRoot);
        intersectionRoot.children.append(intersectionGroup);
        m_treeRoot = intersectionRoot;
    }
}
