#ifndef SCENEMESH_H
#define SCENEMESH_H

#include "shapenode.h"

#include <QVector>
#include <QVector3D>

struct MeshTriangle
{
    QVector3D a;
    QVector3D b;
    QVector3D c;
    QVector3D normal = QVector3D(0.0f, 0.0f, 1.0f);
    int shade = 100;
};

struct SceneMesh
{
    QVector<MeshTriangle> triangles;
    QVector<QVector3D> shadowPoints;
};

// fn: $fn override (0 = use default segment count).
SceneMesh buildShapeMesh(const ShapeNode &shape, int fn = 0);
SceneMesh buildBoxMesh(const QVector3D &minimum, const QVector3D &maximum);

#endif
