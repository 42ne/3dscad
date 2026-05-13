#include "scenedocument.h"

const QVector<ShapeNode> &SceneDocument::shapes() const
{
    return m_shapes;
}

const SceneDocument::TreeNode &SceneDocument::treeRoot() const
{
    return m_tree.root();
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

const SceneDocument::TreeNode *SceneDocument::treeNodeById(int id) const
{
    return m_tree.nodeById(id);
}

SceneDocument::TreeNode *SceneDocument::treeNodeById(int id)
{
    return m_tree.nodeById(id);
}

int SceneDocument::addShape(const ShapeNode &shape, int parentGroupId)
{
    return insertShape(m_shapes.size(), shape, parentGroupId);
}

int SceneDocument::insertShape(int index, const ShapeNode &shape, int parentGroupId)
{
    ShapeNode shapeWithId = shape;

    if (shapeWithId.id < 0)
        shapeWithId.id = m_nextShapeId++;
    else
        m_nextShapeId = qMax(m_nextShapeId, shapeWithId.id + 1);

    const int insertIndex = qBound(0, index, m_shapes.size());
    m_shapes.insert(insertIndex, shapeWithId);
    m_selectedShapeId = shapeWithId.id;

    if (!m_tree.addPrimitive(shapeWithId.id, operationForBooleanMode(shapeWithId.booleanMode), parentGroupId))
        rebuildTreeFromShapes();
    else if (parentGroupId > 0)
        synchronizeBooleanModesFromTree();

    return shapeWithId.id;
}

void SceneDocument::replaceShapes(const QVector<ShapeNode> &shapes)
{
    m_shapes.clear();
    m_selectedShapeId = -1;
    m_nextShapeId = 1;

    for (ShapeNode shape : shapes) {
        if (shape.id < 0)
            shape.id = m_nextShapeId++;
        else
            m_nextShapeId = qMax(m_nextShapeId, shape.id + 1);

        m_shapes.append(shape);
    }

    rebuildTreeFromShapes();

    if (!m_shapes.isEmpty())
        m_selectedShapeId = m_shapes.first().id;
}

SceneDocument::Snapshot SceneDocument::snapshot() const
{
    Snapshot snapshot;
    snapshot.shapes = m_shapes;
    snapshot.treeRoot = m_tree.root();
    snapshot.treeSnapshot = m_tree.snapshot();
    snapshot.selectedShapeId = m_selectedShapeId;
    snapshot.nextShapeId = m_nextShapeId;
    snapshot.nextTreeNodeId = snapshot.treeSnapshot.nextNodeId;
    return snapshot;
}

void SceneDocument::restoreSnapshot(const Snapshot &snapshot)
{
    m_shapes = snapshot.shapes;
    m_tree.restoreSnapshot(snapshot.treeSnapshot.treeRoot.id > 0
                               ? snapshot.treeSnapshot
                               : SceneTree::Snapshot{snapshot.treeRoot, snapshot.nextTreeNodeId});
    m_nextShapeId = snapshot.nextShapeId;
    setSelectedShapeId(snapshot.selectedShapeId);
}

bool SceneDocument::updateShape(const ShapeNode &shape)
{
    ShapeNode *existingShape = shapeById(shape.id);
    if (!existingShape)
        return false;

    const ShapeNode::BooleanMode oldBooleanMode = existingShape->booleanMode;
    *existingShape = shape;
    m_selectedShapeId = shape.id;

    if (oldBooleanMode != shape.booleanMode)
        m_tree.movePrimitiveToOperation(shape.id, operationForBooleanMode(shape.booleanMode));

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
    m_tree.removePrimitive(id);

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

int SceneDocument::addGroup(TreeNode::Operation operation, int parentGroupId, int insertIndex)
{
    return m_tree.addGroup(operation, parentGroupId, insertIndex);
}

bool SceneDocument::removeGroupById(int groupId)
{
    const bool removed = m_tree.removeGroupById(groupId);
    if (removed) {
        ensureTreeContainsAllShapes();
        synchronizeBooleanModesFromTree();
    }

    return removed;
}

bool SceneDocument::moveTreeNode(int nodeId, int parentGroupId, int insertIndex)
{
    SceneTree::MoveInfo moveInfo;
    const bool moved = m_tree.moveNode(nodeId, parentGroupId, insertIndex, &moveInfo);
    if (!moved)
        return false;

    for (int shapeId : moveInfo.movedPrimitiveShapeIds) {
        if (ShapeNode *shape = shapeById(shapeId))
            shape->position += moveInfo.primitiveOffset;
    }

    ensureTreeContainsAllShapes();
    synchronizeBooleanModesFromTree();
    return true;
}

bool SceneDocument::updateGroupTransform(int groupId, const QVector3D &position, const QVector3D &rotation)
{
    return m_tree.updateGroupTransform(groupId, position, rotation);
}

bool SceneDocument::isValidIndex(int index) const
{
    return index >= 0 && index < m_shapes.size();
}

void SceneDocument::ensureTreeContainsAllShapes()
{
    for (const ShapeNode &shape : m_shapes) {
        if (!m_tree.containsPrimitive(shape.id))
            m_tree.addPrimitive(shape.id, operationForBooleanMode(shape.booleanMode));
    }
}

void SceneDocument::synchronizeBooleanModesFromTree()
{
    applyTreeBooleanModes(m_tree.root(), ShapeNode::Add);
}

void SceneDocument::applyTreeBooleanModes(const TreeNode &node, ShapeNode::BooleanMode inheritedMode)
{
    if (node.type == TreeNode::Primitive) {
        if (ShapeNode *shape = shapeById(node.shapeId))
            shape->booleanMode = inheritedMode;

        return;
    }

    for (int i = 0; i < node.children.size(); ++i) {
        ShapeNode::BooleanMode childMode = inheritedMode;
        if (node.operation == TreeNode::Difference && i > 0)
            childMode = ShapeNode::Subtract;
        else if (node.operation == TreeNode::Intersection)
            childMode = ShapeNode::Intersect;

        applyTreeBooleanModes(node.children[i], childMode);
    }
}

void SceneDocument::rebuildTreeFromShapes()
{
    m_tree.clear();

    for (const ShapeNode &shape : m_shapes)
        m_tree.addPrimitive(shape.id, operationForBooleanMode(shape.booleanMode));
}

SceneDocument::TreeNode::Operation SceneDocument::operationForBooleanMode(ShapeNode::BooleanMode booleanMode) const
{
    if (booleanMode == ShapeNode::Subtract)
        return TreeNode::Difference;
    if (booleanMode == ShapeNode::Intersect)
        return TreeNode::Intersection;
    return TreeNode::Union;
}
