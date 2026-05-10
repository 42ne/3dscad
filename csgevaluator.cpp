#include "csgevaluator.h"

#include <algorithm>
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

    for (int x = 0; x < xCount; ++x) {
        for (int y = 0; y < yCount; ++y) {
            for (int z = 0; z < zCount; ++z) {
                const int shapeIndex = cellShapes[cellIndex(x, y, z, yCount, zCount)];
                if (shapeIndex < 0 || shapeIndex >= shapeCount)
                    continue;

                const float x0 = xCoordinates[x];
                const float x1 = xCoordinates[x + 1];
                const float y0 = yCoordinates[y];
                const float y1 = yCoordinates[y + 1];
                const float z0 = zCoordinates[z];
                const float z1 = zCoordinates[z + 1];
                SceneMesh &mesh = meshes[shapeIndex];

                if (!isFilled(x - 1, y, z))
                    appendQuad(&mesh, {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, 92);
                if (!isFilled(x + 1, y, z))
                    appendQuad(&mesh, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, 105);
                if (!isFilled(x, y - 1, z))
                    appendQuad(&mesh, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, 96);
                if (!isFilled(x, y + 1, z))
                    appendQuad(&mesh, {x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, 122);
                if (!isFilled(x, y, z - 1))
                    appendQuad(&mesh, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}, 82);
                if (!isFilled(x, y, z + 1))
                    appendQuad(&mesh, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, 116);
            }
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

CsgPreview buildCsgPreview(const QVector<ShapeNode> &shapes)
{
    CsgPreview preview;
    bool hasBoolean = false;
    bool canComputeBoxes = true;
    QVector<Box> addBoxes;
    QVector<Box> subtractBoxes;
    QVector<Box> intersectBoxes;

    for (int i = 0; i < shapes.size(); ++i) {
        const ShapeNode &shape = shapes[i];
        if (shape.booleanMode != ShapeNode::Add)
            hasBoolean = true;

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
    else if (!canComputeBoxes || addBoxes.isEmpty())
        preview.mode = CsgPreview::Fallback;
    else
        preview.mode = CsgPreview::BoxComputed;

    if (preview.mode != CsgPreview::BoxComputed) {
        for (int i = 0; i < shapes.size(); ++i)
            preview.items.append(renderItemFromShape(shapes[i], i));

        preview.statusText = preview.mode == CsgPreview::Plain
                                 ? "CSG preview: plain mesh"
                                 : "CSG preview: fallback (box CSG needs unrotated cubes)";
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

    for (int i = 0; i < shapes.size(); ++i) {
        if (shapes[i].booleanMode == ShapeNode::Add)
            continue;

        CsgRenderItem helper = renderItemFromShape(shapes[i], i);
        helper.helper = true;
        preview.items.append(helper);
    }

    preview.statusText = "CSG preview: box mode";
    return preview;
}
