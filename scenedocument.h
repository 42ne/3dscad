#ifndef SCENEDOCUMENT_H
#define SCENEDOCUMENT_H

#include "shapenode.h"
#include "scenetree.h"

#include <QString>
#include <QVector>

class SceneDocument
{
public:
    using TreeNode = SceneTree::TreeNode;

    struct Snapshot
    {
        QVector<ShapeNode> shapes;
        TreeNode treeRoot;
        SceneTree::Snapshot treeSnapshot;
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

    int addShape(const ShapeNode &shape, int parentGroupId = 0, int treeInsertIndex = -1);
    int insertShape(int index, const ShapeNode &shape, int parentGroupId = 0, int treeInsertIndex = -1);
    void replaceShapes(const QVector<ShapeNode> &shapes);
    Snapshot snapshot() const;
    void restoreSnapshot(const Snapshot &snapshot);
    bool updateShape(const ShapeNode &shape);
    bool removeShapeById(int id);
    bool removeSelectedShape();
    bool takeShapeById(int id, ShapeNode *removedShape, int *removedIndex);
    int addGroup(TreeNode::Operation operation, int parentGroupId = 0, int insertIndex = -1);
    bool removeGroupById(int groupId);
    int addVariable(int insertIndex = -1);
    bool removeVariableById(int variableId);
    bool updateVariableExpression(int variableId, const QString &expression);
    bool moveTreeNode(int nodeId, int parentGroupId, int insertIndex = -1);
    bool updateGroupTransform(int groupId, const QVector3D &position, const QVector3D &rotation, const QVector3D &scale);

private:
    bool isValidIndex(int index) const;
    void ensureTreeContainsAllShapes();
    void synchronizeBooleanModesFromTree();
    void applyTreeBooleanModes(const TreeNode &node, ShapeNode::BooleanMode inheritedMode);
    void rebuildTreeFromShapes();
    TreeNode::Operation operationForBooleanMode(ShapeNode::BooleanMode booleanMode) const;
    QString uniqueVariableName() const;

private:
    QVector<ShapeNode> m_shapes;
    SceneTree m_tree;
    int m_selectedShapeId = -1;
    int m_nextShapeId = 1;
};

#endif
