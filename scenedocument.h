#ifndef SCENEDOCUMENT_H
#define SCENEDOCUMENT_H

#include "shapenode.h"

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

    int addShape(const ShapeNode &shape);
    int insertShape(int index, const ShapeNode &shape);
    void replaceShapes(const QVector<ShapeNode> &shapes);
    Snapshot snapshot() const;
    void restoreSnapshot(const Snapshot &snapshot);
    bool updateShape(const ShapeNode &shape);
    bool removeShapeById(int id);
    bool removeSelectedShape();
    bool takeShapeById(int id, ShapeNode *removedShape, int *removedIndex);

private:
    bool isValidIndex(int index) const;
    void rebuildTreeFromShapes();
    TreeNode makeGroupNode(TreeNode::Operation operation);
    TreeNode makePrimitiveNode(int shapeId);

private:
    QVector<ShapeNode> m_shapes;
    TreeNode m_treeRoot;
    int m_selectedShapeId = -1;
    int m_nextShapeId = 1;
    int m_nextTreeNodeId = 1;
};

#endif
