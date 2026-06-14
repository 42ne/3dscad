#include "scenetree.h"
#include "scenestringutils.h"

#include <functional>

namespace {

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
    m_root.children.append(makeGroupNode(TreeNode::Scene));
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
    for (const TreeNode &child : m_root.children) {
        if (child.type == TreeNode::Group && child.operation == TreeNode::Scene) {
            if (!child.children.isEmpty())
                return false;
        } else {
            return false;
        }
    }
    return true;
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

    if (operation != TreeNode::Module && parentNodeId <= 0) {
        parent = sceneNode();
        if (!parent)
            parent = &m_root;
    }

    if (operation == TreeNode::Module && parent->operation == TreeNode::Scene)
        parent = &m_root;

    TreeNode group = makeGroupNode(operation);
    if (operation == TreeNode::Conditional) {
        group.conditionExpression = QStringLiteral("true");
        group.children.append(makeConditionalBranchNode(false));
        group.children.append(makeConditionalBranchNode(true));
    }
    if (parent->operation == TreeNode::Conditional && !parent->children.isEmpty())
        parent = &parent->children.first();
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

    // The Scene container is permanent; it cannot be deleted.
    const TreeNode *sn = sceneNode();
    if (sn && sn->id == groupId)
        return false;

    // Check if this is a Module before detaching, so we can clean up its call node.
    const TreeNode *node = nodeById(groupId);
    const bool wasModule = node && node->type == TreeNode::Group && node->operation == TreeNode::Module;
    const bool wasConditional = node && node->type == TreeNode::Group && node->operation == TreeNode::Conditional;

    TreeNode removedModule;
    const bool removed = (wasModule || wasConditional)
                             ? detachNodeById(&m_root, groupId, &removedModule)
                             : detachNodeById(&m_root, groupId);
    if (removed) {
        if (wasModule)
            removeModuleCallForModule(groupId);
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

    TreeNode *parent;
    if (parentGroupId > 0) {
        parent = nodeById(&m_root, parentGroupId);
    } else if (!isParameter) {
        parent = sceneNode();
        if (!parent) parent = &m_root;
    } else {
        parent = &m_root;
    }
    if (!parent || parent->type != TreeNode::Group)
        return 0;
    if (parent->operation == TreeNode::Conditional)
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

    // Sync the name in the corresponding ModuleCall node.
    TreeNode *scene = sceneNode();
    if (scene) {
        for (TreeNode &child : scene->children) {
            if (child.type == TreeNode::ModuleCall && child.shapeId == node->id) {
                child.moduleName = trimmed;
                break;
            }
        }
    }

    return true;
}

bool SceneTree::renameVariable(int variableId, const QString &newName)
{
    TreeNode *node = nodeById(variableId);
    if (!node || node->type != TreeNode::Variable)
        return false;
    const QString trimmed = newName.trimmed();
    if (!isValidIdentifier(trimmed))
        return false;
    if (node->variableName == trimmed)
        return false;
    node->variableName = trimmed;
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

bool SceneTree::updateConditionExpression(int groupId, const QString &conditionExpression)
{
    TreeNode *node = nodeById(groupId);
    if (!node || node->type != TreeNode::Group || node->operation != TreeNode::Conditional)
        return false;

    const QString trimmed = conditionExpression.trimmed().isEmpty()
                                ? QStringLiteral("true")
                                : conditionExpression.trimmed();
    if (node->conditionExpression == trimmed)
        return false;

    node->conditionExpression = trimmed;
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

bool SceneTree::moveNode(int nodeId, int parentGroupId, int insertIndex, bool moduleParameterZone)
{
    if (nodeId <= 0 || m_root.id == nodeId)
        return false;

    TreeNode *targetParent = parentGroupId > 0 ? nodeById(&m_root, parentGroupId) : &m_root;
    if (!targetParent || targetParent->type != TreeNode::Group)
        return false;
    if (targetParent->operation == TreeNode::Conditional)
        return false;

    const TreeNode *node = nodeById(&m_root, nodeId);
    if (!node || containsNodeId(*node, parentGroupId))
        return false;
    if (targetParent == &m_root
        && (node->type != TreeNode::Group || node->operation != TreeNode::Module))
        return false;
    // Variables may only live in Scene or directly inside a Module node.
    if (node->type == TreeNode::Variable
        && targetParent->operation != TreeNode::Module
        && targetParent->operation != TreeNode::Scene)
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
    if (targetParent->operation == TreeNode::Conditional) {
        m_root.children.append(movedNode);
        return false;
    }

    if (movedNode.type == TreeNode::Variable)
        movedNode.isParameter = targetParent != &m_root
                                && targetParent->operation == TreeNode::Module
                                && moduleParameterZone;

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

bool SceneTree::updateGroupLinearExtrudeParams(int groupId, const QVector3D &scale,
                                                float twist, int slices, float scaleVal,
                                                const QStringList &transformExpressions)
{
    TreeNode *node = nodeById(groupId);
    if (!node || node->type != TreeNode::Group
        || node->operation != TreeNode::LinearExtrude)
        return false;

    if (node->scale == scale
        && qFuzzyCompare(node->linearExtrudeTwist, twist)
        && node->linearExtrudeSlices == slices
        && qFuzzyCompare(node->linearExtrudeScaleVal, scaleVal)
        && node->transformExpressions == transformExpressions)
        return false;

    node->scale = scale;
    node->linearExtrudeTwist = twist;
    node->linearExtrudeSlices = slices;
    node->linearExtrudeScaleVal = scaleVal;
    node->transformExpressions = transformExpressions;
    return true;
}

bool SceneTree::updateGroupColor(int groupId, const QColor &color)
{
    TreeNode *node = nodeById(groupId);
    if (!node || node->type != TreeNode::Group || node->operation != TreeNode::Color)
        return false;

    if (node->color == color)
        return false;

    node->color = color;
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
    if (parent->operation == TreeNode::Conditional && !parent->children.isEmpty())
        parent = &parent->children.first();

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
    m_root.children.append(makeGroupNode(TreeNode::Scene));
}

int SceneTree::sceneNodeId() const
{
    const TreeNode *node = sceneNode();
    return node ? node->id : 0;
}

int SceneTree::addModuleCall(int moduleGroupId, int parentGroupId, int insertIndex, const QString &arguments)
{
    const TreeNode *moduleNode = nodeById(moduleGroupId);
    if (!moduleNode || moduleNode->type != TreeNode::Group || moduleNode->operation != TreeNode::Module)
        return 0;

    TreeNode *parent = parentGroupId > 0 ? nodeById(parentGroupId) : sceneNode();
    if (!parent || parent->type != TreeNode::Group || parent == &m_root)
        return 0;
    if (containsNodeId(*moduleNode, parent->id))
        return 0;

    TreeNode call = makeModuleCallNode(moduleGroupId, moduleNode->moduleName, arguments);
    const int callId = call.id;
    const int boundedIndex = insertIndex < 0
                                 ? parent->children.size()
                                 : qBound(0, insertIndex, parent->children.size());
    parent->children.insert(boundedIndex, call);
    return callId;
}

bool SceneTree::removeModuleCallById(int moduleCallId)
{
    if (moduleCallId <= 0)
        return false;

    const TreeNode *node = nodeById(moduleCallId);
    if (!node || node->type != TreeNode::ModuleCall)
        return false;

    return detachNodeById(&m_root, moduleCallId);
}

bool SceneTree::removeModuleCallForModule(int moduleGroupId)
{
    std::function<bool(TreeNode *)> removeCalls = [&](TreeNode *node) {
        if (!node)
            return false;

        bool removed = false;
        for (int i = node->children.size() - 1; i >= 0; --i) {
            TreeNode &child = node->children[i];
            if (child.type == TreeNode::ModuleCall && child.shapeId == moduleGroupId) {
                node->children.removeAt(i);
                removed = true;
                continue;
            }

            if (removeCalls(&child))
                removed = true;
        }
        return removed;
    };

    return removeCalls(&m_root);
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

    if (m_root.id <= 0 || m_root.type != TreeNode::Group)
        m_root = makeGroupNode(TreeNode::Module);

    // Union: add to the Scene container (creating it if absent).
    if (operation == TreeNode::Union) {
        TreeNode *scene = sceneNode();
        if (scene) {
            scene->children.append(primitiveNode);
        } else {
            m_root.children.append(primitiveNode);
        }
        return true;
    }

    // Difference / Intersection: find an existing group at root or create one.
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

SceneTree::TreeNode *SceneTree::sceneNode()
{
    for (TreeNode &child : m_root.children) {
        if (child.type == TreeNode::Group && child.operation == TreeNode::Scene)
            return &child;
    }
    return nullptr;
}

const SceneTree::TreeNode *SceneTree::sceneNode() const
{
    for (const TreeNode &child : m_root.children) {
        if (child.type == TreeNode::Group && child.operation == TreeNode::Scene)
            return &child;
    }
    return nullptr;
}

void SceneTree::pruneEmptyGroups(TreeNode *node)
{
    if (!node || node->type != TreeNode::Group)
        return;

    for (int i = node->children.size() - 1; i >= 0; --i) {
        TreeNode &child = node->children[i];
        if (child.type != TreeNode::Group)
            continue;
        pruneEmptyGroups(&child);
        // Never remove the Scene container or Polyhedron groups (they show template buttons when empty).
        if (node->operation != TreeNode::Conditional
            && !child.isElseBranch
            && child.operation != TreeNode::Scene
            && child.operation != TreeNode::Polyhedron
            && child.children.isEmpty())
            node->children.removeAt(i);
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
    // Mirror axis defaults to [1,0,0] (reflect across the Y-Z plane).
    if (operation == TreeNode::Mirror)
        node.position = QVector3D(1, 0, 0);
    if (operation == TreeNode::LinearExtrude) {
        node.scale = QVector3D(20, 0, 1);
        node.linearExtrudeTwist    = 0.0f;
        node.linearExtrudeSlices   = 0;
        node.linearExtrudeScaleVal = 1.0f;
        node.transformExpressions = QStringList({QStringLiteral("20"), QStringLiteral("0"),
                                                 QStringLiteral("0"),  QStringLiteral("1")});
    }
    if (operation == TreeNode::RotateExtrude) {
        node.scale = QVector3D(360, 1, 1);
        node.transformExpressions = QStringList({QStringLiteral("360")});
    }
    if (operation == TreeNode::Resize) {
        node.scale = QVector3D(10, 10, 10);
        node.transformExpressions = QStringList({QStringLiteral("[10, 10, 10]")});
    }
    return node;
}

SceneTree::TreeNode SceneTree::makeConditionalBranchNode(bool elseBranch)
{
    TreeNode node = makeGroupNode(TreeNode::Union);
    node.isElseBranch = elseBranch;
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

SceneTree::TreeNode SceneTree::makeModuleCallNode(int moduleGroupId, const QString &moduleName, const QString &arguments)
{
    TreeNode node;
    node.id = m_nextNodeId++;
    node.type = TreeNode::ModuleCall;
    node.shapeId = moduleGroupId;   // references the Module Group by id
    node.moduleName = moduleName;
    node.moduleCallArguments = arguments.trimmed();
    return node;
}
