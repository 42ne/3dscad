#ifndef CSGEVALUATOR_H
#define CSGEVALUATOR_H

#include "scenedocument.h"
#include "scenemesh.h"

#include <QColor>
#include <QString>
#include <QVector>

struct CsgRenderItem
{
    SceneMesh mesh;
    int shapeIndex = -1;
    int treeNodeId = 0;
    ShapeNode::BooleanMode booleanMode = ShapeNode::Add;
    QColor color;
    bool computed = false;
    bool helper = false;
};

struct CsgPreview
{
    enum Mode {
        Plain,
        BoxComputed,
        ManifoldComputed,
        MeshApproximate,
        Fallback
    };

    QVector<CsgRenderItem> items;
    Mode mode = Plain;
    QString statusText;
};

CsgPreview buildCsgPreview(const QVector<ShapeNode> &shapes, int fn = 0);
CsgPreview buildCsgPreview(const SceneDocument &scene);

#endif
