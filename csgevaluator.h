#ifndef CSGEVALUATOR_H
#define CSGEVALUATOR_H

#include "scenemesh.h"

#include <QVector>

struct CsgRenderItem
{
    SceneMesh mesh;
    int shapeIndex = -1;
    ShapeNode::BooleanMode booleanMode = ShapeNode::Add;
    bool computed = false;
};

QVector<CsgRenderItem> buildCsgPreviewItems(const QVector<ShapeNode> &shapes, int selectedIndex);

#endif
