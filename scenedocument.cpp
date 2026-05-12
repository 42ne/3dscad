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

const SceneDocument::TreeNode *SceneDocument::treeNodeById(int id) const
{
    return treeNodeById(&m_treeRoot, id);
}

SceneDocument::TreeNode *SceneDocument::treeNodeById(int id)
{
    return treeNodeById(&m_treeRoot, id);
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

    if (m_treeRoot.id <= 0)
        rebuildTreeFromShapes();
    else
        appendPrimitiveToOperation(operationForBooleanMode(shapeWithId.booleanMode), makePrimitiveNode(shapeWithId.id));

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

    const ShapeNode::BooleanMode oldBooleanMode = existingShape->booleanMode;
    *existingShape = shape;
    m_selectedShapeId = shape.id;

    if (oldBooleanMode != shape.booleanMode)
        movePrimitiveToOperation(shape.id, operationForBooleanMode(shape.booleanMode));

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
    removePrimitiveFromTree(&m_treeRoot, id);
    pruneEmptyGroups(&m_treeRoot);

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
    if (m_treeRoot.id <= 0)
        m_treeRoot = makeGroupNode(TreeNode::Union);

    TreeNode *parent = parentGroupId > 0 ? treeNodeById(&m_treeRoot, parentGroupId) : &m_treeRoot;
    if (!parent || parent->type != TreeNode::Group)
        return 0;

    TreeNode group = makeGroupNode(operation);
    const int groupId = group.id;
    const int boundedIndex = insertIndex < 0
                                 ? parent->children.size()
                                 : qBound(0, insertIndex, parent->children.size());
    parent->children.insert(boundedIndex, group);
    return groupId;
}

bool SceneDocument::removeGroupById(int groupId)
{
    if (groupId <= 0 || m_treeRoot.id == groupId)
        return false;

    const bool removed = detachTreeNodeById(&m_treeRoot, groupId);
    if (removed) {
        pruneEmptyGroups(&m_treeRoot);
        synchronizeBooleanModesFromTree();
    }

    return removed;
}

bool SceneDocument::moveTreeNode(int nodeId, int parentGroupId, int insertIndex)
{
    if (nodeId <= 0 || m_treeRoot.id == nodeId)
        return false;

    TreeNode *targetParent = parentGroupId > 0 ? treeNodeById(&m_treeRoot, parentGroupId) : &m_treeRoot;
    if (!targetParent || targetParent->type != TreeNode::Group)
        return false;

    const TreeNode *node = treeNodeById(&m_treeRoot, nodeId);
    if (!node || treeContainsNodeId(*node, parentGroupId))
        return false;

    QVector3D sourceParentWorldPosition;
    QVector3D targetParentWorldPosition;
    if (!parentWorldPositionForNode(m_treeRoot, nodeId, QVector3D(), &sourceParentWorldPosition))
        return false;

    parentWorldPositionForNode(m_treeRoot, targetParent->id, QVector3D(), &targetParentWorldPosition);
    targetParentWorldPosition += targetParent->position;

    TreeNode movedNode;
    if (!detachTreeNodeById(&m_treeRoot, nodeId, &movedNode))
        return false;

    targetParent = parentGroupId > 0 ? treeNodeById(&m_treeRoot, parentGroupId) : &m_treeRoot;
    if (!targetParent || targetParent->type != TreeNode::Group) {
        if (m_treeRoot.id <= 0)
            m_treeRoot = makeGroupNode(TreeNode::Union);
        m_treeRoot.children.append(movedNode);
        return false;
    }

    const int boundedIndex = insertIndex < 0
                                 ? targetParent->children.size()
                                 : qBound(0, insertIndex, targetParent->children.size());
    offsetMovedTreeNode(&movedNode, sourceParentWorldPosition - targetParentWorldPosition);
    targetParent->children.insert(boundedIndex, movedNode);
    pruneEmptyGroups(&m_treeRoot);
    ensureTreeContainsAllShapes();
    synchronizeBooleanModesFromTree();
    return true;
}

bool SceneDocument::updateGroupTransform(int groupId, const QVector3D &position, const QVector3D &rotation)
{
    TreeNode *node = treeNodeById(groupId);
    if (!node || node->type != TreeNode::Group)
        return false;

    if (node->position == position && node->rotation == rotation)
        return false;

    node->position = position;
    node->rotation = rotation;
    return true;
}

bool SceneDocument::isValidIndex(int index) const
{
    return index >= 0 && index < m_shapes.size();
}

SceneDocument::TreeNode *SceneDocument::treeNodeById(TreeNode *node, int id)
{
    if (!node)
        return nullptr;

    if (node->id == id)
        return node;

    for (TreeNode &child : node->children) {
        if (TreeNode *found = treeNodeById(&child, id))
            return found;
    }

    return nullptr;
}

const SceneDocument::TreeNode *SceneDocument::treeNodeById(const TreeNode *node, int id) const
{
    if (!node)
        return nullptr;

    if (node->id == id)
        return node;

    for (const TreeNode &child : node->children) {
        if (const TreeNode *found = treeNodeById(&child, id))
            return found;
    }

    return nullptr;
}

bool SceneDocument::treeContainsNodeId(const TreeNode &node, int id) const
{
    if (node.id == id)
        return true;

    for (const TreeNode &child : node.children) {
        if (treeContainsNodeId(child, id))
            return true;
    }

    return false;
}

bool SceneDocument::parentWorldPositionForNode(const TreeNode &node,
                                               int id,
                                               const QVector3D &worldPosition,
                                               QVector3D *parentWorldPosition) const
{
    if (node.id == id) {
        if (parentWorldPosition)
            *parentWorldPosition = worldPosition;
        return true;
    }

    const QVector3D childWorldPosition = node.type == TreeNode::Group
                                             ? worldPosition + node.position
                                             : worldPosition;

    for (const TreeNode &child : node.children) {
        if (parentWorldPositionForNode(child, id, childWorldPosition, parentWorldPosition))
            return true;
    }

    return false;
}

void SceneDocument::offsetMovedTreeNode(TreeNode *node, const QVector3D &offset)
{
    if (!node)
        return;

    if (node->type == TreeNode::Group) {
        node->position += offset;
        return;
    }

    if (ShapeNode *shape = shapeById(node->shapeId))
        shape->position += offset;
}

bool SceneDocument::detachTreeNodeById(TreeNode *node, int id, TreeNode *detachedNode)
{
    if (!node)
        return false;

    for (int i = 0; i < node->children.size(); ++i) {
        TreeNode &child = node->children[i];
        if (child.id == id) {
            if (detachedNode) {
                *detachedNode = child;
                node->children.removeAt(i);
            } else {
                const QVector<TreeNode> promotedChildren = child.children;
                node->children.removeAt(i);

                int insertIndex = i;
                for (const TreeNode &promotedChild : promotedChildren)
                    node->children.insert(insertIndex++, promotedChild);
            }

            return true;
        }

        if (child.type == TreeNode::Group && detachTreeNodeById(&child, id, detachedNode))
            return true;
    }

    return false;
}

bool SceneDocument::removePrimitiveFromTree(TreeNode *node, int shapeId, TreeNode *removedNode)
{
    if (!node)
        return false;

    for (int i = 0; i < node->children.size(); ++i) {
        TreeNode &child = node->children[i];
        if (child.type == TreeNode::Primitive && child.shapeId == shapeId) {
            if (removedNode)
                *removedNode = child;

            node->children.removeAt(i);
            return true;
        }

        if (child.type == TreeNode::Group && removePrimitiveFromTree(&child, shapeId, removedNode))
            return true;
    }

    return false;
}

bool SceneDocument::treeContainsPrimitiveShapeId(const TreeNode &node, int shapeId) const
{
    if (node.type == TreeNode::Primitive && node.shapeId == shapeId)
        return true;

    for (const TreeNode &child : node.children) {
        if (treeContainsPrimitiveShapeId(child, shapeId))
            return true;
    }

    return false;
}

void SceneDocument::ensureTreeContainsAllShapes()
{
    if (m_treeRoot.id <= 0 || m_treeRoot.type != TreeNode::Group)
        m_treeRoot = makeGroupNode(TreeNode::Union);

    for (const ShapeNode &shape : m_shapes) {
        if (!treeContainsPrimitiveShapeId(m_treeRoot, shape.id))
            m_treeRoot.children.append(makePrimitiveNode(shape.id));
    }
}

void SceneDocument::synchronizeBooleanModesFromTree()
{
    applyTreeBooleanModes(m_treeRoot, ShapeNode::Add);
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

bool SceneDocument::appendPrimitiveToOperation(TreeNode::Operation operation, const TreeNode &primitiveNode)
{
    if (primitiveNode.type != TreeNode::Primitive)
        return false;

    if (m_treeRoot.id <= 0 || m_treeRoot.type != TreeNode::Group) {
        m_treeRoot = makeGroupNode(operation);
        m_treeRoot.children.append(primitiveNode);
        return true;
    }

    if (m_treeRoot.operation == operation) {
        m_treeRoot.children.append(primitiveNode);
        return true;
    }

    for (TreeNode &child : m_treeRoot.children) {
        if (child.type == TreeNode::Group && child.operation == operation) {
            child.children.append(primitiveNode);
            return true;
        }
    }

    TreeNode group = makeGroupNode(operation);
    group.children.append(primitiveNode);

    if (operation == TreeNode::Intersection) {
        TreeNode intersectionRoot = makeGroupNode(TreeNode::Intersection);
        intersectionRoot.children.append(m_treeRoot);
        intersectionRoot.children.append(group);
        m_treeRoot = intersectionRoot;
        return true;
    }

    if (m_treeRoot.operation == TreeNode::Difference) {
        m_treeRoot.children.append(primitiveNode);
        return true;
    }

    TreeNode differenceRoot = makeGroupNode(TreeNode::Difference);
    differenceRoot.children.append(m_treeRoot);
    differenceRoot.children.append(primitiveNode);
    m_treeRoot = differenceRoot;
    return true;
}

bool SceneDocument::movePrimitiveToOperation(int shapeId, TreeNode::Operation operation)
{
    TreeNode primitive;
    if (!removePrimitiveFromTree(&m_treeRoot, shapeId, &primitive))
        primitive = makePrimitiveNode(shapeId);

    pruneEmptyGroups(&m_treeRoot);
    return appendPrimitiveToOperation(operation, primitive);
}

void SceneDocument::pruneEmptyGroups(TreeNode *node)
{
    if (!node || node->type != TreeNode::Group)
        return;

    for (int i = node->children.size() - 1; i >= 0; --i) {
        TreeNode &child = node->children[i];
        if (child.type != TreeNode::Group)
            continue;

        pruneEmptyGroups(&child);
        if (child.children.isEmpty())
            node->children.removeAt(i);
    }

    if (node == &m_treeRoot && node->children.size() == 1 && node->children.first().type == TreeNode::Group)
        *node = node->children.first();
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

SceneDocument::TreeNode::Operation SceneDocument::operationForBooleanMode(ShapeNode::BooleanMode booleanMode) const
{
    if (booleanMode == ShapeNode::Subtract)
        return TreeNode::Difference;
    if (booleanMode == ShapeNode::Intersect)
        return TreeNode::Intersection;
    return TreeNode::Union;
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

void SceneDocument::syncTreeRootFromSceneTree()
{
    // Placeholder: will sync m_tree -> m_treeRoot when SceneTree is fully integrated
    // For now, m_tree is kept in sync by manual updates as we migrate operations
}
