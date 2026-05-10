#ifndef CSGEVALUATOR_H
#define CSGEVALUATOR_H

#include "scenedocument.h"
#include "scenemesh.h"

#include <QString>
#include <QVector>

struct CsgRenderItem
{
    SceneMesh mesh;
    int shapeIndex = -1;
    ShapeNode::BooleanMode booleanMode = ShapeNode::Add;
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

CsgPreview buildCsgPreview(const QVector<ShapeNode> &shapes);
CsgPreview buildCsgPreview(const SceneDocument &scene);

#endif
