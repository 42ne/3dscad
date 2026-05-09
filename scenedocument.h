#ifndef SCENEDOCUMENT_H
#define SCENEDOCUMENT_H

#include "shapenode.h"

#include <QVector>

class SceneDocument
{
public:
    struct Snapshot
    {
        QVector<ShapeNode> shapes;
        int selectedShapeId = -1;
        int nextShapeId = 1;
    };

    const QVector<ShapeNode> &shapes() const;
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

private:
    QVector<ShapeNode> m_shapes;
    int m_selectedShapeId = -1;
    int m_nextShapeId = 1;
};

#endif
