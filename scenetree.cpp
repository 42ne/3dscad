#include "scenetree.h"

#include <functional>

namespace {

bool isValidIdentifier(const QString &name)
{
    if (name.isEmpty())
        return false;

    const QChar first = name.front();
    if (!(first == QLatin1Char('_') || first.isLetter()))
        return false;

    for (const QChar ch : name) {
        if (!(ch == QLatin1Char('_') || ch.isLetterOrNumber()))
            return false;
    }

    return true;
}

bool moduleNameExists(const SceneTree::TreeNode &node, const QString &name, int ignoredNodeId)
{
    if (node.type == SceneTree::TreeNode::Group
        && node.operation == SceneTree::TreeNode::Module
        && node.id != ignoredNodeId
        && node.moduleName == name)
        return true;

    for (const SceneTree::TreeNode &child : node.children) {
        if (moduleNameExists(child, name, ignoredNodeId))
            return true;
    }

    return false;
}

} // namespace

SceneTree::SceneTree()
{
    m_root = makeGroupNode(TreeNode::Module);
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
        m_root = makeGroupNode(TreeNode::Module);

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

int SceneTree::addVariable(const QString &name, const QString &expression, qreal value,
                           int parentGroupId, bool isParameter, int insertIndex)
{
    if (m_root.id <= 0)
        m_root = makeGroupNode(TreeNode::Module);

    if (name.trimmed().isEmpty() || expression.trimmed().isEmpty())
        return 0;

    TreeNode *parent = parentGroupId > 0 ? nodeById(&m_root, parentGroupId) : &m_root;
    if (!parent || parent->type != TreeNode::Group)
        return 0;

    TreeNode variable = makeVariableNode(name.trimmed(), expression.trimmed(), value);
    variable.isParameter = isParameter;
    const int variableId = variable.id;
    const int boundedIndex = insertIndex < 0
                                 ? parent->children.size()
                                 : qBound(0, insertIndex, parent->children.size());
    parent->children.insert(boundedIndex, variable);
    return variableId;
}

bool SceneTree::setModuleName(int groupId, const QString &name)
{
    TreeNode *node = nodeById(groupId > 0 ? groupId : m_root.id);
    if (!node || node->type != TreeNode::Group || node->operation != TreeNode::Module)
        return false;
    const QString trimmed = name.trimmed();
    if (!isValidIdentifier(trimmed))
        return false;
    if (moduleNameExists(m_root, trimmed, node->id))
        return false;
    node->moduleName = trimmed;
    return true;
}

bool SceneTree::setVariableIsParameter(int variableId, bool isParameter)
{
    TreeNode *node = nodeById(variableId);
    if (!node || node->type != TreeNode::Variable)
        return false;
    node->isParameter = isParameter;
    return true;
}

bool SceneTree::removeVariableById(int variableId)
{
    if (variableId <= 0)
        return false;

    const TreeNode *node = nodeById(variableId);
    if (!node || node->type != TreeNode::Variable)
        return false;

    return detachNodeById(&m_root, variableId);
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
    // Variables may only live at root or directly inside a Module node.
    if (node->type == TreeNode::Variable
        && targetParent != &m_root
        && targetParent->operation != TreeNode::Module)
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
            m_root = makeGroupNode(TreeNode::Module);
        m_root.children.append(movedNode);
        return false;
    }

    if (movedNode.type == TreeNode::Variable)
        movedNode.isParameter = targetParent != &m_root && targetParent->operation == TreeNode::Module;

    const int boundedIndex = insertIndex < 0
                                 ? targetParent->children.size()
                                 : qBound(0, insertIndex, targetParent->children.size());
    const QVector3D offset = sourceParentWorldPosition - targetParentWorldPosition;

    offsetMovedNode(&movedNode, offset);
    targetParent->children.insert(boundedIndex, movedNode);
    pruneEmptyGroups(&m_root);
    return true;
}

bool SceneTree::updateGroupTransform(int groupId, const QVector3D &position, const QVector3D &rotation, const QVector3D &scale, const QStringList &transformExpressions)
{
    TreeNode *node = nodeById(groupId);
    if (!node || node->type != TreeNode::Group)
        return false;

    if (node->position == position && node->rotation == rotation && node->scale == scale
        && node->transformExpressions == transformExpressions)
        return false;

    node->position = position;
    node->rotation = rotation;
    node->scale = scale;
    node->transformExpressions = transformExpressions;
    return true;
}

bool SceneTree::addPrimitive(int shapeId, TreeNode::Operation operation, int parentGroupId, int insertIndex)
{
    if (m_root.id <= 0)
        m_root = makeGroupNode(TreeNode::Module);

    if (parentGroupId <= 0)
        return appendPrimitiveToOperation(operation, makePrimitiveNode(shapeId));

    TreeNode *parent = nodeById(&m_root, parentGroupId);
    if (!parent || parent->type != TreeNode::Group)
        return false;

    if (containsPrimitiveShapeId(m_root, shapeId))
        return false;

    TreeNode primitiveNode = makePrimitiveNode(shapeId);
    const int boundedIndex = insertIndex < 0
                                 ? parent->children.size()
                                 : qBound(0, insertIndex, parent->children.size());
    parent->children.insert(boundedIndex, primitiveNode);
    return true;
}

bool SceneTree::removePrimitive(int shapeId)
{
    const bool removed = removePrimitiveFromTree(&m_root, shapeId);
    if (removed)
        pruneEmptyGroups(&m_root);
    return removed;
}

bool SceneTree::movePrimitiveToOperation(int shapeId, TreeNode::Operation operation)
{
    if (!containsPrimitiveShapeId(m_root, shapeId))
        return false;

    // Remove primitive from tree
    TreeNode removedNode;
    if (!removePrimitiveFromTree(&m_root, shapeId, &removedNode))
        return false;

    pruneEmptyGroups(&m_root);
    return appendPrimitiveToOperation(operation, removedNode);
}

bool SceneTree::containsPrimitive(int shapeId) const
{
    return containsPrimitiveShapeId(m_root, shapeId);
}

QVector<int> SceneTree::primitiveShapeIdsForNode(int nodeId) const
{
    QVector<int> shapeIds;
    const TreeNode *node = nodeById(nodeId);
    if (node)
        collectPrimitiveShapeIds(*node, &shapeIds);
    return shapeIds;
}

void SceneTree::clear()
{
    m_nextNodeId = 1;
    m_root = makeGroupNode(TreeNode::Module);
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

void SceneTree::collectPrimitiveShapeIds(const TreeNode &node, QVector<int> *shapeIds) const
{
    if (!shapeIds)
        return;

    if (node.type == TreeNode::Primitive) {
        shapeIds->append(node.shapeId);
        return;
    }

    for (const TreeNode &child : node.children)
        collectPrimitiveShapeIds(child, shapeIds);
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

bool SceneTree::appendPrimitiveToOperation(TreeNode::Operation operation, const TreeNode &primitiveNode)
{
    if (primitiveNode.type != TreeNode::Primitive)
        return false;

    if (containsPrimitiveShapeId(m_root, primitiveNode.shapeId))
        return false;

    if (m_root.id <= 0 || m_root.type != TreeNode::Group) {
        m_root = makeGroupNode(TreeNode::Module);
        m_root.children.append(primitiveNode);
        return true;
    }

    if (m_root.operation == TreeNode::Module) {
        if (operation == TreeNode::Union) {
            m_root.children.append(primitiveNode);
            return true;
        }

        for (TreeNode &child : m_root.children) {
            if (child.type == TreeNode::Group && child.operation == operation) {
                child.children.append(primitiveNode);
                return true;
            }
        }

        TreeNode group = makeGroupNode(operation);
        group.children.append(primitiveNode);
        m_root.children.append(group);
        return true;
    }

    if (m_root.operation == operation) {
        m_root.children.append(primitiveNode);
        return true;
    }

    for (TreeNode &child : m_root.children) {
        if (child.type == TreeNode::Group && child.operation == operation) {
            child.children.append(primitiveNode);
            return true;
        }
    }

    TreeNode group = makeGroupNode(operation);
    group.children.append(primitiveNode);

    if (operation == TreeNode::Intersection) {
        TreeNode intersectionRoot = makeGroupNode(TreeNode::Intersection);
        intersectionRoot.children.append(m_root);
        intersectionRoot.children.append(group);
        m_root = intersectionRoot;
        return true;
    }

    if (m_root.operation == TreeNode::Difference) {
        m_root.children.append(primitiveNode);
        return true;
    }

    TreeNode differenceRoot = makeGroupNode(TreeNode::Difference);
    differenceRoot.children.append(m_root);
    differenceRoot.children.append(primitiveNode);
    m_root = differenceRoot;
    return true;
}

void SceneTree::pruneEmptyGroups(TreeNode *node)
{
    if (!node || node->type != TreeNode::Group)
        return;

    for (int i = node->children.size() - 1; i >= 0; --i) {
        TreeNode &child = node->children[i];
        if (child.type == TreeNode::Group) {
            pruneEmptyGroups(&child);
            if (child.children.isEmpty()) {
                node->children.removeAt(i);
            }
        }
    }

    if (node == &m_root
        && node->operation != TreeNode::Module
        && node->children.size() == 1
        && node->children.first().type == TreeNode::Group) {
        *node = node->children.first();
    }
}

SceneTree::TreeNode SceneTree::makeGroupNode(TreeNode::Operation operation)
{
    TreeNode node;
    node.id = m_nextNodeId++;
    node.type = TreeNode::Group;
    node.operation = operation;
    // Root module keeps the default "scene_model"; new user-added modules get unique auto-names.
    if (operation == TreeNode::Module && node.id > 1)
        node.moduleName = QString("module_%1").arg(node.id);
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

SceneTree::TreeNode SceneTree::makeVariableNode(const QString &name, const QString &expression, qreal value)
{
    TreeNode node;
    node.id = m_nextNodeId++;
    node.type = TreeNode::Variable;
    node.variableName = name;
    node.variableExpression = expression;
    node.variableValue = value;
    return node;
}
