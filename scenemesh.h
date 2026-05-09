#ifndef SCENEMESH_H
#define SCENEMESH_H

#include "shapenode.h"

#include <QVector>
#include <QVector3D>

struct MeshFace
{
    QVector<QVector3D> vertices;
    QVector3D normal = QVector3D(0.0f, 0.0f, 1.0f);
    int shade = 100;
};

struct SceneMesh
{
    QVector<MeshFace> faces;
    QVector<QVector3D> shadowPoints;
};

SceneMesh buildShapeMesh(const ShapeNode &shape);

#endif
