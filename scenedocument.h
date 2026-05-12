#ifndef SCENEDOCUMENT_H
#define SCENEDOCUMENT_H

#include "shapenode.h"
#include "scenetree.h"

#include <QVector>

class SceneDocument
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

    struct Snapshot
    {
        QVector<ShapeNode> shapes;
        TreeNode treeRoot;
        int selectedShapeId = -1;
        int nextShapeId = 1;
        int nextTreeNodeId = 1;
    };

    const QVector<ShapeNode> &shapes() const;
    const TreeNode &treeRoot() const;
    int shapeCount() const;
    bool isEmpty() const;

    int selectedIndex() const;
    int selectedShapeId() const;
    void setSelectedIndex(int index);
    void setSelectedShapeId(int id);
    bool hasSelection() const;

    const ShapeNode *selectedShape() const;
    ShapeNode *selectedShape();
    const ShapeNode *shapeAt(int index) const;
    ShapeNode *shapeAt(int index);
    const ShapeNode *shapeById(int id) const;
    ShapeNode *shapeById(int id);
    int indexOfShapeId(int id) const;
    const TreeNode *treeNodeById(int id) const;
    TreeNode *treeNodeById(int id);

    int addShape(const ShapeNode &shape);
    int insertShape(int index, const ShapeNode &shape);
    void replaceShapes(const QVector<ShapeNode> &shapes);
    Snapshot snapshot() const;
    void restoreSnapshot(const Snapshot &snapshot);
    bool updateShape(const ShapeNode &shape);
    bool removeShapeById(int id);
    bool removeSelectedShape();
    bool takeShapeById(int id, ShapeNode *removedShape, int *removedIndex);
    int addGroup(TreeNode::Operation operation, int parentGroupId = 0, int insertIndex = -1);
    bool removeGroupById(int groupId);
    bool moveTreeNode(int nodeId, int parentGroupId, int insertIndex = -1);
    bool updateGroupTransform(int groupId, const QVector3D &position, const QVector3D &rotation);

private:
    bool isValidIndex(int index) const;
    TreeNode *treeNodeById(TreeNode *node, int id);
    const TreeNode *treeNodeById(const TreeNode *node, int id) const;
    bool treeContainsNodeId(const TreeNode &node, int id) const;
    bool parentWorldPositionForNode(const TreeNode &node, int id, const QVector3D &worldPosition, QVector3D *parentWorldPosition) const;
    void offsetMovedTreeNode(TreeNode *node, const QVector3D &offset);
    bool detachTreeNodeById(TreeNode *node, int id, TreeNode *detachedNode = nullptr);
    bool removePrimitiveFromTree(TreeNode *node, int shapeId, TreeNode *removedNode = nullptr);
    bool treeContainsPrimitiveShapeId(const TreeNode &node, int shapeId) const;
    void ensureTreeContainsAllShapes();
    void synchronizeBooleanModesFromTree();
    void applyTreeBooleanModes(const TreeNode &node, ShapeNode::BooleanMode inheritedMode);
    bool appendPrimitiveToOperation(TreeNode::Operation operation, const TreeNode &primitiveNode);
    bool movePrimitiveToOperation(int shapeId, TreeNode::Operation operation);
    void pruneEmptyGroups(TreeNode *node);
    void rebuildTreeFromShapes();
    TreeNode makeGroupNode(TreeNode::Operation operation);
    TreeNode makePrimitiveNode(int shapeId);
    TreeNode::Operation operationForBooleanMode(ShapeNode::BooleanMode booleanMode) const;
    void syncTreeRootFromSceneTree();

private:
    QVector<ShapeNode> m_shapes;
    TreeNode m_treeRoot;
    SceneTree m_tree;
    int m_selectedShapeId = -1;
    int m_nextShapeId = 1;
    int m_nextTreeNodeId = 1;
};

#endif
