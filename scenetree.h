#ifndef SCENETREE_H
#define SCENETREE_H

#include <QVector>
#include <QVector3D>

/**
 * @brief Scene tree model for boolean operations
 *
 * Manages explicit tree hierarchy with groups (Union, Difference, Intersection)
 * and primitive nodes. Separate from ShapeNode list to allow independent development.
 *
 * This class is owned by SceneDocument but has isolated tree logic.
 */
class SceneTree
{
public:
    struct TreeNode
    {
        enum Type {
            Primitive,
            Group
        };

        enum Operation {
            Union,
            Difference,
            Intersection
        };

        int id = 0;
        Type type = Group;
        Operation operation = Union;
        int shapeId = -1;
        QVector3D position = QVector3D(0, 0, 0);
        QVector3D rotation = QVector3D(0, 0, 0);
        QVector<TreeNode> children;
    };

    struct MoveInfo
    {
        QVector<int> movedPrimitiveShapeIds;
        QVector3D primitiveOffset = QVector3D(0, 0, 0);
    };

    SceneTree();

    const TreeNode &root() const;
    int shapeCount() const;
    bool isEmpty() const;

    const TreeNode *nodeById(int id) const;
    TreeNode *nodeById(int id);

    int addGroup(TreeNode::Operation operation, int parentNodeId = 0, int insertIndex = -1);
    bool removeGroupById(int groupId);
    bool moveNode(int nodeId, int parentGroupId, int insertIndex = -1, MoveInfo *moveInfo = nullptr);
    bool updateGroupTransform(int groupId, const QVector3D &position, const QVector3D &rotation);

    // Primitive management
    bool addPrimitive(int shapeId, TreeNode::Operation operation, int parentGroupId = 0);
    bool removePrimitive(int shapeId);
    bool movePrimitiveToOperation(int shapeId, TreeNode::Operation operation);
    bool containsPrimitive(int shapeId) const;
    QVector<int> primitiveShapeIdsForNode(int nodeId) const;

    // Tree reconstruction
    void clear();
    struct Snapshot
    {
        TreeNode treeRoot;
        int nextNodeId = 1;
    };

    Snapshot snapshot() const;
    void restoreSnapshot(const Snapshot &snapshot);

private:
    // Helper methods
    TreeNode *nodeById(TreeNode *node, int id);
    const TreeNode *nodeById(const TreeNode *node, int id) const;
    bool containsNodeId(const TreeNode &node, int id) const;
    bool containsPrimitiveShapeId(const TreeNode &node, int shapeId) const;
    void collectPrimitiveShapeIds(const TreeNode &node, QVector<int> *shapeIds) const;
    bool parentWorldPositionForNode(const TreeNode &node, int id, const QVector3D &worldPosition, QVector3D *parentWorldPosition) const;
    void offsetMovedNode(TreeNode *node, const QVector3D &offset);
    bool detachNodeById(TreeNode *node, int id, TreeNode *detachedNode = nullptr);
    bool removePrimitiveFromTree(TreeNode *node, int shapeId, TreeNode *removedNode = nullptr);
    bool appendPrimitiveToOperation(TreeNode::Operation operation, const TreeNode &primitiveNode);
    void pruneEmptyGroups(TreeNode *node);

    TreeNode makeGroupNode(TreeNode::Operation operation);
    TreeNode makePrimitiveNode(int shapeId);

private:
    TreeNode m_root;
    int m_nextNodeId = 1;
};

#endif
