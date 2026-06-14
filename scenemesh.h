#ifndef SCENEMESH_H
#define SCENEMESH_H

#include "shapenode.h"

#include <QVector>
#include <QVector3D>
#include <QtMath>

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

// Follows OpenSCAD's get_fragments_from_r() logic.
inline int computeCircularSegments(int fn, double r, double fa = 12.0, double fs = 2.0)
{
    if (fn > 0) return qMax(3, fn);
    if (r <= 0.0) return 24;
    const int fromAngle = static_cast<int>(qCeil(360.0 / fa));
    const int fromSize  = static_cast<int>(qCeil(2.0 * M_PI * r / fs));
    return qMax(3, qMax(fromAngle, fromSize));
}

// fn: $fn override (0 = auto, uses $fa/$fs). fa=$fa default 12°, fs=$fs default 2mm.
SceneMesh buildShapeMesh(const ShapeNode &shape, int fn = 0, double fa = 12.0, double fs = 2.0);
SceneMesh buildBoxMesh(const QVector3D &minimum, const QVector3D &maximum);

// Builds 2D glyph contours for a Text shape in model space (Y up, z=0), scaled
// to textSize and aligned per halign/valign. Holes come out as separate
// sub-contours. Uses QFont/QPainterPath, so call it on the GUI thread (it is
// cached into ShapeNode::textContours by SceneDocument::snapshot()).
QVector<QVector<QVector3D>> buildGlyphContours(const ShapeNode &shape);

// Loads an STL file (ASCII or binary), populating points and triangle faces.
// Returns true on success. On failure, sets *errorMessage.
bool loadStlFile(const QString &filePath,
                 QVector<QVector3D> *outPoints,
                 QVector<QVector<int>> *outFaces,
                 QString *errorMessage = nullptr);

#endif
