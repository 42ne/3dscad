#include "scenetree.h"
#include "shapenode.h"

SceneTree::SceneTree()
{
    m_root = makeGroupNode(TreeNode::Union);
}

const SceneTree::TreeNode &SceneTree::root() const
{
    return m_root;
}

int SceneTree::shapeCount() const
{
    // Count primitives in tree
    int count = 0;
    std::function<void(const TreeNode &)> countPrimitives = [&](const TreeNode &node) {
        if (node.type == TreeNode::Primitive)
            count++;
        for (const TreeNode &child : node.children)
            countPrimitives(child);
    };
    countPrimitives(m_root);
    return count;
}

bool SceneTree::isEmpty() const
{
    return m_root.children.isEmpty();
}

const SceneTree::TreeNode *SceneTree::nodeById(int id) const
{
    return nodeById(&m_root, id);
}

SceneTree::TreeNode *SceneTree::nodeById(int id)
{
    return nodeById(&m_root, id);
}

int SceneTree::addGroup(TreeNode::Operation operation, int parentNodeId, int insertIndex)
{
    if (m_root.id <= 0)
        m_root = makeGroupNode(TreeNode::Union);

    TreeNode *parent = parentNodeId > 0 ? nodeById(&m_root, parentNodeId) : &m_root;
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

bool SceneTree::removeGroupById(int groupId)
{
    if (groupId <= 0 || m_root.id == groupId)
        return false;

    const bool removed = detachNodeById(&m_root, groupId);
    if (removed) {
        pruneEmptyGroups(&m_root);
    }

    return removed;
}

bool SceneTree::moveNode(int nodeId, int parentGroupId, int insertIndex)
{
    if (nodeId <= 0 || m_root.id == nodeId)
        return false;

    TreeNode *targetParent = parentGroupId > 0 ? nodeById(&m_root, parentGroupId) : &m_root;
    if (!targetParent || targetParent->type != TreeNode::Group)
        return false;

    const TreeNode *node = nodeById(&m_root, nodeId);
    if (!node || containsNodeId(*node, parentGroupId))
        return false;

    QVector3D sourceParentWorldPosition;
    QVector3D targetParentWorldPosition;
    if (!parentWorldPositionForNode(m_root, nodeId, QVector3D(), &sourceParentWorldPosition))
        return false;

    parentWorldPositionForNode(m_root, targetParent->id, QVector3D(), &targetParentWorldPosition);
    targetParentWorldPosition += targetParent->position;

    TreeNode movedNode;
    if (!detachNodeById(&m_root, nodeId, &movedNode))
        return false;

    targetParent = parentGroupId > 0 ? nodeById(&m_root, parentGroupId) : &m_root;
    if (!targetParent || targetParent->type != TreeNode::Group) {
        if (m_root.id <= 0)
            m_root = makeGroupNode(TreeNode::Union);
        m_root.children.append(movedNode);
        return false;
    }

    const int boundedIndex = insertIndex < 0
                                 ? targetParent->children.size()
                                 : qBound(0, insertIndex, targetParent->children.size());
    offsetMovedNode(&movedNode, sourceParentWorldPosition - targetParentWorldPosition);
    targetParent->children.insert(boundedIndex, movedNode);
    pruneEmptyGroups(&m_root);
    return true;
}

bool SceneTree::updateGroupTransform(int groupId, const QVector3D &position, const QVector3D &rotation)
{
    TreeNode *node = nodeById(groupId);
    if (!node || node->type != TreeNode::Group)
        return false;

    if (node->position == position && node->rotation == rotation)
        return false;

    node->position = position;
    node->rotation = rotation;
    return true;
}

bool SceneTree::addPrimitive(int shapeId, TreeNode::Operation operation, int parentGroupId)
{
    if (m_root.id <= 0)
        m_root = makeGroupNode(TreeNode::Union);

    TreeNode *parent = parentGroupId > 0 ? nodeById(&m_root, parentGroupId) : &m_root;
    if (!parent || parent->type != TreeNode::Group)
        return false;

    if (containsPrimitiveShapeId(m_root, shapeId))
        return false;

    TreeNode primitiveNode = makePrimitiveNode(shapeId);
    parent->children.append(primitiveNode);
    return true;
}

bool SceneTree::removePrimitive(int shapeId)
{
    return removePrimitiveFromTree(&m_root, shapeId);
}

bool SceneTree::movePrimitiveToOperation(int shapeId, TreeNode::Operation operation)
{
    if (!containsPrimitiveShapeId(m_root, shapeId))
        return false;

    // Remove primitive from tree
    TreeNode removedNode;
    if (!removePrimitiveFromTree(&m_root, shapeId, &removedNode))
        return false;

    // Find or create operation group
    TreeNode *opGroup = nullptr;
    for (TreeNode &child : m_root.children) {
        if (child.type == TreeNode::Group && child.operation == operation) {
            opGroup = &child;
            break;
        }
    }

    if (!opGroup) {
        TreeNode newGroup = makeGroupNode(operation);
        opGroup = &m_root.children.last();
        m_root.children.append(newGroup);
        opGroup = &m_root.children.last();
    }

    opGroup->children.append(removedNode);
    return true;
}

void SceneTree::clear()
{
    m_root = makeGroupNode(TreeNode::Union);
    m_nextNodeId = 1;
}

SceneTree::Snapshot SceneTree::snapshot() const
{
    Snapshot snap;
    snap.treeRoot = m_root;
    snap.nextNodeId = m_nextNodeId;
    return snap;
}

void SceneTree::restoreSnapshot(const Snapshot &snapshot)
{
    m_root = snapshot.treeRoot;
    m_nextNodeId = snapshot.nextNodeId;
}

// Private helpers

SceneTree::TreeNode *SceneTree::nodeById(TreeNode *node, int id)
{
    if (!node)
        return nullptr;

    if (node->id == id)
        return node;

    for (TreeNode &child : node->children) {
        if (TreeNode *found = nodeById(&child, id))
            return found;
    }

    return nullptr;
}

const SceneTree::TreeNode *SceneTree::nodeById(const TreeNode *node, int id) const
{
    if (!node)
        return nullptr;

    if (node->id == id)
        return node;

    for (const TreeNode &child : node->children) {
        if (const TreeNode *found = nodeById(&child, id))
            return found;
    }

    return nullptr;
}

bool SceneTree::containsNodeId(const TreeNode &node, int id) const
{
    if (node.id == id)
        return true;

    for (const TreeNode &child : node.children) {
        if (containsNodeId(child, id))
            return true;
    }

    return false;
}

bool SceneTree::containsPrimitiveShapeId(const TreeNode &node, int shapeId) const
{
    if (node.type == TreeNode::Primitive && node.shapeId == shapeId)
        return true;

    for (const TreeNode &child : node.children) {
        if (containsPrimitiveShapeId(child, shapeId))
            return true;
    }

    return false;
}

bool SceneTree::parentWorldPositionForNode(const TreeNode &node,
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

void SceneTree::offsetMovedNode(TreeNode *node, const QVector3D &offset)
{
    if (!node)
        return;

    if (node->type == TreeNode::Group) {
        node->position += offset;
    }
}

bool SceneTree::detachNodeById(TreeNode *node, int id, TreeNode *detachedNode)
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

        if (child.type == TreeNode::Group && detachNodeById(&child, id, detachedNode))
            return true;
    }

    return false;
}

bool SceneTree::removePrimitiveFromTree(TreeNode *node, int shapeId, TreeNode *removedNode)
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

void SceneTree::pruneEmptyGroups(TreeNode *node)
{
    if (!node || node->type != TreeNode::Group)
        return;

    for (int i = node->children.size() - 1; i >= 0; --i) {
        TreeNode &child = node->children[i];
        if (child.type == TreeNode::Group) {
            pruneEmptyGroups(&child);
            if (child.children.isEmpty() && node->id != 0) {
                node->children.removeAt(i);
            }
        }
    }
}

SceneTree::TreeNode SceneTree::makeGroupNode(TreeNode::Operation operation)
{
    TreeNode node;
    node.id = m_nextNodeId++;
    node.type = TreeNode::Group;
    node.operation = operation;
    return node;
}

SceneTree::TreeNode SceneTree::makePrimitiveNode(int shapeId)
{
    TreeNode node;
    node.id = m_nextNodeId++;
    node.type = TreeNode::Primitive;
    node.shapeId = shapeId;
    return node;
}
