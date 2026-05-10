#include "csgevaluator.h"

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

    for (const Box &box : result) {
        CsgRenderItem item;
        item.mesh = buildBoxMesh(box.minimum, box.maximum);
        item.shapeIndex = box.shapeIndex;
        item.booleanMode = ShapeNode::Add;
        item.computed = true;
        preview.items.append(item);
    }

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
