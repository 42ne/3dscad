#include "csgevaluator.h"

#include <algorithm>
#include <functional>
#include <QtMath>

struct Box
{
    QVector3D minimum;
    QVector3D maximum;
    int shapeIndex = -1;
};

static bool isAxisAlignedCube(const ShapeNode &shape)
{
    return shape.type == ShapeNode::Cube
           && qFuzzyIsNull(shape.rotation.x())
           && qFuzzyIsNull(shape.rotation.y())
           && qFuzzyIsNull(shape.rotation.z());
}

static QVector3D inverseRotatePoint(const QVector3D &point, const QVector3D &degrees)
{
    const float rx = qDegreesToRadians(-degrees.x());
    const float ry = qDegreesToRadians(-degrees.y());
    const float rz = qDegreesToRadians(-degrees.z());

    QVector3D p = point;

    p = QVector3D(
        p.x() * qCos(rz) - p.y() * qSin(rz),
        p.x() * qSin(rz) + p.y() * qCos(rz),
        p.z());

    p = QVector3D(
        p.x() * qCos(ry) + p.z() * qSin(ry),
        p.y(),
        -p.x() * qSin(ry) + p.z() * qCos(ry));

    p = QVector3D(
        p.x(),
        p.y() * qCos(rx) - p.z() * qSin(rx),
        p.y() * qSin(rx) + p.z() * qCos(rx));

    return p;
}

static QVector3D toShapeLocal(const ShapeNode &shape, const QVector3D &worldPoint)
{
    return inverseRotatePoint(worldPoint - shape.position, shape.rotation);
}

static bool containsPoint(const ShapeNode &shape, const QVector3D &worldPoint)
{
    const QVector3D local = toShapeLocal(shape, worldPoint);

    if (shape.type == ShapeNode::Sphere)
        return local.lengthSquared() <= shape.radius * shape.radius;

    if (shape.type == ShapeNode::Cylinder) {
        const float radialDistanceSquared = local.x() * local.x() + local.y() * local.y();
        return radialDistanceSquared <= shape.radius * shape.radius
               && qAbs(local.z()) <= shape.height * 0.5f;
    }

    const QVector3D half = shape.size * 0.5f;
    return qAbs(local.x()) <= half.x()
           && qAbs(local.y()) <= half.y()
           && qAbs(local.z()) <= half.z();
}

static Box boxFromCube(const ShapeNode &shape, int shapeIndex)
{
    const QVector3D half = shape.size * 0.5f;
    return {shape.position - half, shape.position + half, shapeIndex};
}

static bool intersects(const Box &left, const Box &right)
{
    return left.minimum.x() < right.maximum.x() && left.maximum.x() > right.minimum.x()
           && left.minimum.y() < right.maximum.y() && left.maximum.y() > right.minimum.y()
           && left.minimum.z() < right.maximum.z() && left.maximum.z() > right.minimum.z();
}

static Box intersectionBox(const Box &left, const Box &right)
{
    return {
        QVector3D(qMax(left.minimum.x(), right.minimum.x()),
                  qMax(left.minimum.y(), right.minimum.y()),
                  qMax(left.minimum.z(), right.minimum.z())),
        QVector3D(qMin(left.maximum.x(), right.maximum.x()),
                  qMin(left.maximum.y(), right.maximum.y()),
                  qMin(left.maximum.z(), right.maximum.z())),
        left.shapeIndex
    };
}

static QVector3D faceNormal(const QVector3D &a, const QVector3D &b, const QVector3D &c)
{
    QVector3D normal = -QVector3D::crossProduct(b - a, c - a);

    if (normal.lengthSquared() <= 0.0001f)
        return QVector3D(0.0f, 0.0f, 1.0f);

    return normal.normalized();
}

static MeshTriangle makeTriangle(const QVector3D &a, const QVector3D &b, const QVector3D &c, int shade = 100)
{
    MeshTriangle triangle;
    triangle.a = a;
    triangle.b = b;
    triangle.c = c;
    triangle.normal = faceNormal(a, b, c);
    triangle.shade = shade;
    return triangle;
}

static void appendQuad(SceneMesh *mesh,
                       const QVector3D &a,
                       const QVector3D &b,
                       const QVector3D &c,
                       const QVector3D &d,
                       int shade = 100)
{
    mesh->triangles.append(makeTriangle(a, b, c, shade));
    mesh->triangles.append(makeTriangle(a, c, d, shade));
}

static void appendUniqueCoordinate(QVector<float> *coordinates, float value)
{
    for (float existing : *coordinates) {
        if (qAbs(existing - value) < 0.0001f)
            return;
    }

    coordinates->append(value);
}

static int cellIndex(int x, int y, int z, int yCount, int zCount)
{
    return (x * yCount + y) * zCount + z;
}

static void appendMergedFaceRects(const QVector<QVector<bool>> &filled,
                                  std::function<void(int, int, int, int)> appendRect)
{
    if (filled.isEmpty() || filled.first().isEmpty())
        return;

    const int uCount = filled.size();
    const int vCount = filled.first().size();
    QVector<QVector<bool>> visited(uCount, QVector<bool>(vCount, false));

    for (int u = 0; u < uCount; ++u) {
        for (int v = 0; v < vCount; ++v) {
            if (!filled[u][v] || visited[u][v])
                continue;

            int width = 1;
            while (u + width < uCount && filled[u + width][v] && !visited[u + width][v])
                ++width;

            int height = 1;
            bool canGrow = true;
            while (v + height < vCount && canGrow) {
                for (int offset = 0; offset < width; ++offset) {
                    if (!filled[u + offset][v + height] || visited[u + offset][v + height]) {
                        canGrow = false;
                        break;
                    }
                }

                if (canGrow)
                    ++height;
            }

            for (int du = 0; du < width; ++du) {
                for (int dv = 0; dv < height; ++dv)
                    visited[u + du][v + dv] = true;
            }

            appendRect(u, v, u + width, v + height);
        }
    }
}

static QVector<CsgRenderItem> buildSurfaceItems(const QVector<Box> &boxes, int shapeCount)
{
    QVector<CsgRenderItem> items;
    if (boxes.isEmpty())
        return items;

    QVector<float> xCoordinates;
    QVector<float> yCoordinates;
    QVector<float> zCoordinates;

    for (const Box &box : boxes) {
        appendUniqueCoordinate(&xCoordinates, box.minimum.x());
        appendUniqueCoordinate(&xCoordinates, box.maximum.x());
        appendUniqueCoordinate(&yCoordinates, box.minimum.y());
        appendUniqueCoordinate(&yCoordinates, box.maximum.y());
        appendUniqueCoordinate(&zCoordinates, box.minimum.z());
        appendUniqueCoordinate(&zCoordinates, box.maximum.z());
    }

    std::sort(xCoordinates.begin(), xCoordinates.end());
    std::sort(yCoordinates.begin(), yCoordinates.end());
    std::sort(zCoordinates.begin(), zCoordinates.end());

    const int xCount = xCoordinates.size() - 1;
    const int yCount = yCoordinates.size() - 1;
    const int zCount = zCoordinates.size() - 1;

    QVector<int> cellShapes(xCount * yCount * zCount, -1);

    for (int x = 0; x < xCount; ++x) {
        for (int y = 0; y < yCount; ++y) {
            for (int z = 0; z < zCount; ++z) {
                const QVector3D center(
                    (xCoordinates[x] + xCoordinates[x + 1]) * 0.5f,
                    (yCoordinates[y] + yCoordinates[y + 1]) * 0.5f,
                    (zCoordinates[z] + zCoordinates[z + 1]) * 0.5f);

                for (const Box &box : boxes) {
                    if (center.x() > box.minimum.x() && center.x() < box.maximum.x()
                        && center.y() > box.minimum.y() && center.y() < box.maximum.y()
                        && center.z() > box.minimum.z() && center.z() < box.maximum.z()) {
                        cellShapes[cellIndex(x, y, z, yCount, zCount)] = box.shapeIndex;
                        break;
                    }
                }
            }
        }
    }

    QVector<SceneMesh> meshes(shapeCount);

    auto isFilled = [&](int x, int y, int z) {
        if (x < 0 || x >= xCount || y < 0 || y >= yCount || z < 0 || z >= zCount)
            return false;

        return cellShapes[cellIndex(x, y, z, yCount, zCount)] >= 0;
    };

    for (int shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex) {
        SceneMesh &mesh = meshes[shapeIndex];

        for (int x = 0; x < xCount; ++x) {
            QVector<QVector<bool>> negativeX(yCount, QVector<bool>(zCount, false));
            QVector<QVector<bool>> positiveX(yCount, QVector<bool>(zCount, false));

            for (int y = 0; y < yCount; ++y) {
                for (int z = 0; z < zCount; ++z) {
                    if (cellShapes[cellIndex(x, y, z, yCount, zCount)] != shapeIndex)
                        continue;

                    negativeX[y][z] = !isFilled(x - 1, y, z);
                    positiveX[y][z] = !isFilled(x + 1, y, z);
                }
            }

            appendMergedFaceRects(negativeX, [&](int y0, int z0, int y1, int z1) {
                const float px = xCoordinates[x];
                appendQuad(&mesh,
                           {px, yCoordinates[y0], zCoordinates[z0]},
                           {px, yCoordinates[y0], zCoordinates[z1]},
                           {px, yCoordinates[y1], zCoordinates[z1]},
                           {px, yCoordinates[y1], zCoordinates[z0]},
                           92);
            });
            appendMergedFaceRects(positiveX, [&](int y0, int z0, int y1, int z1) {
                const float px = xCoordinates[x + 1];
                appendQuad(&mesh,
                           {px, yCoordinates[y0], zCoordinates[z0]},
                           {px, yCoordinates[y1], zCoordinates[z0]},
                           {px, yCoordinates[y1], zCoordinates[z1]},
                           {px, yCoordinates[y0], zCoordinates[z1]},
                           105);
            });
        }

        for (int y = 0; y < yCount; ++y) {
            QVector<QVector<bool>> negativeY(xCount, QVector<bool>(zCount, false));
            QVector<QVector<bool>> positiveY(xCount, QVector<bool>(zCount, false));

            for (int x = 0; x < xCount; ++x) {
                for (int z = 0; z < zCount; ++z) {
                    if (cellShapes[cellIndex(x, y, z, yCount, zCount)] != shapeIndex)
                        continue;

                    negativeY[x][z] = !isFilled(x, y - 1, z);
                    positiveY[x][z] = !isFilled(x, y + 1, z);
                }
            }

            appendMergedFaceRects(negativeY, [&](int x0, int z0, int x1, int z1) {
                const float py = yCoordinates[y];
                appendQuad(&mesh,
                           {xCoordinates[x0], py, zCoordinates[z0]},
                           {xCoordinates[x1], py, zCoordinates[z0]},
                           {xCoordinates[x1], py, zCoordinates[z1]},
                           {xCoordinates[x0], py, zCoordinates[z1]},
                           96);
            });
            appendMergedFaceRects(positiveY, [&](int x0, int z0, int x1, int z1) {
                const float py = yCoordinates[y + 1];
                appendQuad(&mesh,
                           {xCoordinates[x0], py, zCoordinates[z0]},
                           {xCoordinates[x0], py, zCoordinates[z1]},
                           {xCoordinates[x1], py, zCoordinates[z1]},
                           {xCoordinates[x1], py, zCoordinates[z0]},
                           122);
            });
        }

        for (int z = 0; z < zCount; ++z) {
            QVector<QVector<bool>> negativeZ(xCount, QVector<bool>(yCount, false));
            QVector<QVector<bool>> positiveZ(xCount, QVector<bool>(yCount, false));

            for (int x = 0; x < xCount; ++x) {
                for (int y = 0; y < yCount; ++y) {
                    if (cellShapes[cellIndex(x, y, z, yCount, zCount)] != shapeIndex)
                        continue;

                    negativeZ[x][y] = !isFilled(x, y, z - 1);
                    positiveZ[x][y] = !isFilled(x, y, z + 1);
                }
            }

            appendMergedFaceRects(negativeZ, [&](int x0, int y0, int x1, int y1) {
                const float pz = zCoordinates[z];
                appendQuad(&mesh,
                           {xCoordinates[x0], yCoordinates[y0], pz},
                           {xCoordinates[x0], yCoordinates[y1], pz},
                           {xCoordinates[x1], yCoordinates[y1], pz},
                           {xCoordinates[x1], yCoordinates[y0], pz},
                           82);
            });
            appendMergedFaceRects(positiveZ, [&](int x0, int y0, int x1, int y1) {
                const float pz = zCoordinates[z + 1];
                appendQuad(&mesh,
                           {xCoordinates[x0], yCoordinates[y0], pz},
                           {xCoordinates[x1], yCoordinates[y0], pz},
                           {xCoordinates[x1], yCoordinates[y1], pz},
                           {xCoordinates[x0], yCoordinates[y1], pz},
                           116);
            });
        }
    }

    for (int i = 0; i < meshes.size(); ++i) {
        if (meshes[i].triangles.isEmpty())
            continue;

        CsgRenderItem item;
        item.mesh = meshes[i];
        item.shapeIndex = i;
        item.booleanMode = ShapeNode::Add;
        item.computed = true;

        for (const MeshTriangle &triangle : item.mesh.triangles) {
            item.mesh.shadowPoints.append(triangle.a);
            item.mesh.shadowPoints.append(triangle.b);
            item.mesh.shadowPoints.append(triangle.c);
        }

        items.append(item);
    }

    return items;
}

static bool isValidBox(const Box &box)
{
    return box.minimum.x() < box.maximum.x()
           && box.minimum.y() < box.maximum.y()
           && box.minimum.z() < box.maximum.z();
}

static void appendBox(QVector<Box> *boxes, const QVector3D &minimum, const QVector3D &maximum, int shapeIndex)
{
    const Box box{minimum, maximum, shapeIndex};
    if (isValidBox(box))
        boxes->append(box);
}

static QVector<Box> subtractBox(const Box &source, const Box &cutter)
{
    if (!intersects(source, cutter))
        return {source};

    const Box cut = intersectionBox(source, cutter);
    QVector<Box> result;

    appendBox(&result,
              QVector3D(source.minimum.x(), source.minimum.y(), source.minimum.z()),
              QVector3D(cut.minimum.x(), source.maximum.y(), source.maximum.z()),
              source.shapeIndex);
    appendBox(&result,
              QVector3D(cut.maximum.x(), source.minimum.y(), source.minimum.z()),
              QVector3D(source.maximum.x(), source.maximum.y(), source.maximum.z()),
              source.shapeIndex);
    appendBox(&result,
              QVector3D(cut.minimum.x(), source.minimum.y(), source.minimum.z()),
              QVector3D(cut.maximum.x(), cut.minimum.y(), source.maximum.z()),
              source.shapeIndex);
    appendBox(&result,
              QVector3D(cut.minimum.x(), cut.maximum.y(), source.minimum.z()),
              QVector3D(cut.maximum.x(), source.maximum.y(), source.maximum.z()),
              source.shapeIndex);
    appendBox(&result,
              QVector3D(cut.minimum.x(), cut.minimum.y(), source.minimum.z()),
              QVector3D(cut.maximum.x(), cut.maximum.y(), cut.minimum.z()),
              source.shapeIndex);
    appendBox(&result,
              QVector3D(cut.minimum.x(), cut.minimum.y(), cut.maximum.z()),
              QVector3D(cut.maximum.x(), cut.maximum.y(), source.maximum.z()),
              source.shapeIndex);

    return result;
}

static CsgRenderItem renderItemFromShape(const ShapeNode &shape, int shapeIndex)
{
    CsgRenderItem item;
    item.mesh = buildShapeMesh(shape);
    item.shapeIndex = shapeIndex;
    item.booleanMode = shape.booleanMode;
    return item;
}

static void appendHelpers(CsgPreview *preview, const QVector<ShapeNode> &shapes)
{
    for (int i = 0; i < shapes.size(); ++i) {
        if (shapes[i].booleanMode == ShapeNode::Add)
            continue;

        CsgRenderItem helper = renderItemFromShape(shapes[i], i);
        helper.helper = true;
        preview->items.append(helper);
    }
}

static CsgPreview buildMeshApproximationPreview(const QVector<ShapeNode> &shapes)
{
    CsgPreview preview;
    preview.mode = CsgPreview::MeshApproximate;

    QVector<ShapeNode> subtractShapes;
    QVector<ShapeNode> intersectShapes;

    for (const ShapeNode &shape : shapes) {
        if (shape.booleanMode == ShapeNode::Subtract)
            subtractShapes.append(shape);
        else if (shape.booleanMode == ShapeNode::Intersect)
            intersectShapes.append(shape);
    }

    for (int i = 0; i < shapes.size(); ++i) {
        const ShapeNode &shape = shapes[i];
        if (shape.booleanMode != ShapeNode::Add)
            continue;

        const SceneMesh sourceMesh = buildShapeMesh(shape);
        CsgRenderItem item;
        item.shapeIndex = i;
        item.booleanMode = ShapeNode::Add;
        item.computed = true;

        for (const MeshTriangle &triangle : sourceMesh.triangles) {
            const QVector3D centroid = (triangle.a + triangle.b + triangle.c) / 3.0f;
            bool removed = false;

            for (const ShapeNode &subtractShape : subtractShapes) {
                if (containsPoint(subtractShape, centroid)) {
                    removed = true;
                    break;
                }
            }

            if (removed)
                continue;

            if (!intersectShapes.isEmpty()) {
                bool insideIntersection = false;
                for (const ShapeNode &intersectShape : intersectShapes) {
                    if (containsPoint(intersectShape, centroid)) {
                        insideIntersection = true;
                        break;
                    }
                }

                if (!insideIntersection)
                    continue;
            }

            item.mesh.triangles.append(triangle);
            item.mesh.shadowPoints.append(triangle.a);
            item.mesh.shadowPoints.append(triangle.b);
            item.mesh.shadowPoints.append(triangle.c);
        }

        if (!item.mesh.triangles.isEmpty())
            preview.items.append(item);
    }

    appendHelpers(&preview, shapes);
    preview.statusText = "CSG preview: mesh approximate (no cut faces yet)";
    return preview;
}

CsgPreview buildCsgPreview(const QVector<ShapeNode> &shapes)
{
    CsgPreview preview;
    bool hasBoolean = false;
    bool hasAddShape = false;
    bool canComputeBoxes = true;
    QVector<Box> addBoxes;
    QVector<Box> subtractBoxes;
    QVector<Box> intersectBoxes;

    for (int i = 0; i < shapes.size(); ++i) {
        const ShapeNode &shape = shapes[i];
        if (shape.booleanMode != ShapeNode::Add)
            hasBoolean = true;
        else
            hasAddShape = true;

        if (!isAxisAlignedCube(shape)) {
            canComputeBoxes = false;
            break;
        }

        if (shape.booleanMode == ShapeNode::Subtract)
            subtractBoxes.append(boxFromCube(shape, i));
        else if (shape.booleanMode == ShapeNode::Intersect)
            intersectBoxes.append(boxFromCube(shape, i));
        else
            addBoxes.append(boxFromCube(shape, i));
    }

    if (!hasBoolean)
        preview.mode = CsgPreview::Plain;
    else if (!hasAddShape)
        preview.mode = CsgPreview::Fallback;
    else if (!canComputeBoxes)
        preview.mode = CsgPreview::MeshApproximate;
    else
        preview.mode = CsgPreview::BoxComputed;

    if (preview.mode == CsgPreview::MeshApproximate)
        return buildMeshApproximationPreview(shapes);

    if (preview.mode != CsgPreview::BoxComputed) {
        for (int i = 0; i < shapes.size(); ++i)
            preview.items.append(renderItemFromShape(shapes[i], i));

        preview.statusText = preview.mode == CsgPreview::Plain
                                 ? "CSG preview: plain mesh"
                                 : "CSG preview: fallback (add a solid base shape)";
        return preview;
    }

    QVector<Box> result = addBoxes;

    for (const Box &cutter : subtractBoxes) {
        QVector<Box> next;
        for (const Box &box : result)
            next += subtractBox(box, cutter);
        result = next;
    }

    if (!intersectBoxes.isEmpty()) {
        QVector<Box> next;
        for (const Box &box : result) {
            for (const Box &mask : intersectBoxes) {
                if (intersects(box, mask))
                    next.append(intersectionBox(box, mask));
            }
        }
        result = next;
    }

    preview.items = buildSurfaceItems(result, shapes.size());

    appendHelpers(&preview, shapes);

    preview.statusText = "CSG preview: box mode";
    return preview;
}
