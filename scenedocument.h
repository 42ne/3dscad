#ifndef SCENEDOCUMENT_H
#define SCENEDOCUMENT_H

#include "shapenode.h"

#include <QVector>

class SceneDocument
{
public:
    const QVector<ShapeNode> &shapes() const;
    int shapeCount() const;
    bool isEmpty() const;

    int selectedIndex() const;
    void setSelectedIndex(int index);
    bool hasSelection() const;

    const ShapeNode *selectedShape() const;
    ShapeNode *selectedShape();
    const ShapeNode *shapeAt(int index) const;
    ShapeNode *shapeAt(int index);

    int addShape(const ShapeNode &shape);

private:
    bool isValidIndex(int index) const;

private:
    QVector<ShapeNode> m_shapes;
    int m_selectedIndex = -1;
};

#endif
