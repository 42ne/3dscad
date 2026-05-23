#include "csgevaluator.h"
#include "expression.h"
#include "manifoldcsg.h"

#include <algorithm>
#include <functional>
#include <numeric>
#include <QHash>
#include <QStringList>
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

static QVector3D rotatePoint(const QVector3D &point, const QVector3D &degrees)
{
    const float rx = qDegreesToRadians(degrees.x());
    const float ry = qDegreesToRadians(degrees.y());
    const float rz = qDegreesToRadians(degrees.z());

    QVector3D p = point;

    p = QVector3D(
        p.x(),
        p.y() * qCos(rx) - p.z() * qSin(rx),
        p.y() * qSin(rx) + p.z() * qCos(rx));

    p = QVector3D(
        p.x() * qCos(ry) + p.z() * qSin(ry),
        p.y(),
        -p.x() * qSin(ry) + p.z() * qCos(ry));

    p = QVector3D(
        p.x() * qCos(rz) - p.y() * qSin(rz),
        p.x() * qSin(rz) + p.y() * qCos(rz),
        p.z());

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

    if (shape.type == ShapeNode::Cone) {
        // Approximate: use max(r1, r2) as a bounding cylinder (conservative)
        const float maxR = qMax(shape.radius, shape.radius2);
        const float radialDistanceSquared = local.x() * local.x() + local.y() * local.y();
        return radialDistanceSquared <= maxR * maxR
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

static MeshTriangle reversedTriangle(const MeshTriangle &triangle, int shade = 104)
{
    MeshTriangle reversed;
    reversed.a = triangle.a;
    reversed.b = triangle.c;
    reversed.c = triangle.b;
    reversed.normal = -triangle.normal;
    reversed.shade = shade;
    return reversed;
}

static bool clipInside(float value, float limit, bool keepGreater)
{
    return keepGreater ? value >= limit - 0.0001f : value <= limit + 0.0001f;
}

static float axisValue(const QVector3D &point, int axis)
{
    if (axis == 0)
        return point.x();
    if (axis == 1)
        return point.y();
    return point.z();
}

static QVector<QVector3D> clipPolygonByAxisPlane(const QVector<QVector3D> &polygon,
                                                 int axis,
                                                 float limit,
                                                 bool keepGreater)
{
    QVector<QVector3D> clipped;
    if (polygon.isEmpty())
        return clipped;

    QVector3D previous = polygon.last();
    bool previousInside = clipInside(axisValue(previous, axis), limit, keepGreater);

    for (const QVector3D &current : polygon) {
        const bool currentInside = clipInside(axisValue(current, axis), limit, keepGreater);

        if (currentInside != previousInside) {
            const float previousValue = axisValue(previous, axis);
            const float currentValue = axisValue(current, axis);
            const float denominator = currentValue - previousValue;
            if (qAbs(denominator) > 0.0001f) {
                const float t = (limit - previousValue) / denominator;
                clipped.append(previous + (current - previous) * t);
            }
        }

        if (currentInside)
            clipped.append(current);

        previous = current;
        previousInside = currentInside;
    }

    return clipped;
}

static QVector<QVector3D> clipPolygonByBox(const QVector<QVector3D> &polygon, const Box &box)
{
    QVector<QVector3D> clipped = polygon;
    clipped = clipPolygonByAxisPlane(clipped, 0, box.minimum.x(), true);
    clipped = clipPolygonByAxisPlane(clipped, 0, box.maximum.x(), false);
    clipped = clipPolygonByAxisPlane(clipped, 1, box.minimum.y(), true);
    clipped = clipPolygonByAxisPlane(clipped, 1, box.maximum.y(), false);
    clipped = clipPolygonByAxisPlane(clipped, 2, box.minimum.z(), true);
    clipped = clipPolygonByAxisPlane(clipped, 2, box.maximum.z(), false);
    return clipped;
}

static void appendReversedPolygon(SceneMesh *mesh, const QVector<QVector3D> &vertices, int shade = 104)
{
    if (vertices.size() < 3)
        return;

    for (int i = 1; i + 1 < vertices.size(); ++i) {
        MeshTriangle triangle = makeTriangle(vertices[0], vertices[i + 1], vertices[i], shade);
        mesh->triangles.append(triangle);
        mesh->shadowPoints.append(triangle.a);
        mesh->shadowPoints.append(triangle.b);
        mesh->shadowPoints.append(triangle.c);
    }
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

static CsgRenderItem renderItemFromShape(const ShapeNode &shape, int shapeIndex, int treeNodeId = 0)
{
    CsgRenderItem item;
    item.mesh = buildShapeMesh(shape);
    item.shapeIndex = shapeIndex;
    item.treeNodeId = treeNodeId;
    item.booleanMode = shape.booleanMode;
    return item;
}

static ShapeNode shapeWithEvaluatedParameters(const ShapeNode &shape, const QHash<QString, qreal> &variables)
{
    ShapeNode evaluated = shape;
    if (evaluated.parameterExpressions.isEmpty())
        return evaluated;

    for (int i = 0; i < evaluated.parameterExpressions.size(); ++i) {
        const QString expression = evaluated.parameterExpressions[i].trimmed();
        if (expression.isEmpty())
            continue;

        qreal value = 0.0;
        if (!ExpressionSyntax::evaluate(expression, variables, &value))
            continue;

        const qreal rawValue = value;
        value = qMax<qreal>(0.1, value);
        if (evaluated.type == ShapeNode::Cube) {
            if (i == 0)
                evaluated.size.setX(static_cast<float>(value));
            else if (i == 1)
                evaluated.size.setY(static_cast<float>(value));
            else if (i == 2)
                evaluated.size.setZ(static_cast<float>(value));
        } else if (evaluated.type == ShapeNode::Sphere) {
            if (i == 0)
                evaluated.radius = static_cast<float>(value);
        } else if (evaluated.type == ShapeNode::Cylinder) {
            if (i == 0)
                evaluated.radius = static_cast<float>(value);
            else if (i == 1)
                evaluated.height = static_cast<float>(value);
        } else if (evaluated.type == ShapeNode::Cone) {
            if (i == 0)
                evaluated.radius  = static_cast<float>(qMax<qreal>(0.1, rawValue));
            else if (i == 1)
                evaluated.radius2 = static_cast<float>(qMax<qreal>(0.0, rawValue));
            else if (i == 2)
                evaluated.height  = static_cast<float>(qMax<qreal>(0.1, rawValue));
        }
    }

    return evaluated;
}

static SceneDocument::TreeNode nodeWithEvaluatedTransform(const SceneDocument::TreeNode &node,
                                                          const QHash<QString, qreal> &variables)
{
    SceneDocument::TreeNode evaluated = node;
    if (evaluated.transformExpressions.isEmpty())
        return evaluated;

    for (int axis = 0; axis < evaluated.transformExpressions.size() && axis < 3; ++axis) {
        const QString expression = evaluated.transformExpressions[axis].trimmed();
        if (expression.isEmpty())
            continue;

        qreal value = 0.0;
        if (!ExpressionSyntax::evaluate(expression, variables, &value))
            continue;

        if (evaluated.operation == SceneDocument::TreeNode::Translate) {
            if (axis == 0)
                evaluated.position.setX(static_cast<float>(value));
            else if (axis == 1)
                evaluated.position.setY(static_cast<float>(value));
            else
                evaluated.position.setZ(static_cast<float>(value));
        } else if (evaluated.operation == SceneDocument::TreeNode::Rotate) {
            if (axis == 0)
                evaluated.rotation.setX(static_cast<float>(value));
            else if (axis == 1)
                evaluated.rotation.setY(static_cast<float>(value));
            else
                evaluated.rotation.setZ(static_cast<float>(value));
        } else if (evaluated.operation == SceneDocument::TreeNode::Scale) {
            value = qMax<qreal>(0.01, value);
            if (axis == 0)
                evaluated.scale.setX(static_cast<float>(value));
            else if (axis == 1)
                evaluated.scale.setY(static_cast<float>(value));
            else
                evaluated.scale.setZ(static_cast<float>(value));
        } else if (evaluated.operation == SceneDocument::TreeNode::Mirror) {
            if (axis == 0)
                evaluated.position.setX(static_cast<float>(value));
            else if (axis == 1)
                evaluated.position.setY(static_cast<float>(value));
            else
                evaluated.position.setZ(static_cast<float>(value));
        }
    }

    return evaluated;
}

static QHash<QString, qreal> variablesWithScopedVariables(const SceneDocument::TreeNode &node,
                                                          QHash<QString, qreal> variables,
                                                          const QHash<QString, QString> &argumentOverrides = {})
{
    if (node.type != SceneDocument::TreeNode::Group)
        return variables;

    if (node.operation != SceneDocument::TreeNode::Module
        && node.operation != SceneDocument::TreeNode::Scene)
        return variables;

    for (const SceneDocument::TreeNode &child : node.children) {
        if (child.type != SceneDocument::TreeNode::Variable)
            continue;

        qreal value = child.variableValue;
        const QString expression = child.isParameter && argumentOverrides.contains(child.variableName)
                                       ? argumentOverrides.value(child.variableName).trimmed()
                                       : child.variableExpression.trimmed();
        if (!expression.isEmpty())
            ExpressionSyntax::evaluate(expression, variables, &value);
        variables[child.variableName] = value;
    }

    return variables;
}

static bool isTopLevelModuleDeclaration(const SceneDocument::TreeNode &node)
{
    return node.type == SceneDocument::TreeNode::Group
           && node.operation == SceneDocument::TreeNode::Module;
}

static QStringList splitAtTopLevelCommas(const QString &text)
{
    QStringList result;
    int depth = 0;
    int start = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text[i];
        if (ch == QLatin1Char('(') || ch == QLatin1Char('['))
            ++depth;
        else if (ch == QLatin1Char(')') || ch == QLatin1Char(']'))
            --depth;
        else if (ch == QLatin1Char(',') && depth == 0) {
            const QString part = text.mid(start, i - start).trimmed();
            if (!part.isEmpty())
                result.append(part);
            start = i + 1;
        }
    }
    const QString tail = text.mid(start).trimmed();
    if (!tail.isEmpty())
        result.append(tail);
    return result;
}

// Resolves both named and positional call arguments against a module's parameter list.
static QHash<QString, QString> resolveModuleArguments(
    const QString &callArguments,
    const SceneDocument::TreeNode &moduleNode)
{
    QStringList paramOrder;
    for (const SceneDocument::TreeNode &child : moduleNode.children)
        if (child.type == SceneDocument::TreeNode::Variable && child.isParameter)
            paramOrder.append(child.variableName);

    QHash<QString, QString> result;
    int positionalIndex = 0;
    for (const QString &part : splitAtTopLevelCommas(callArguments)) {
        const int equal = part.indexOf(QLatin1Char('='));
        if (equal > 0) {
            const QString name = part.left(equal).trimmed();
            const QString expr  = part.mid(equal + 1).trimmed();
            if (!name.isEmpty() && !expr.isEmpty())
                result[name] = expr;
        } else {
            const QString expr = part.trimmed();
            if (!expr.isEmpty() && positionalIndex < paramOrder.size())
                result[paramOrder[positionalIndex]] = expr;
            ++positionalIndex;
        }
    }
    return result;
}

static QHash<QString, qreal> topLevelVariables(const SceneDocument::TreeNode &root)
{
    QHash<QString, qreal> variables;
    for (const SceneDocument::TreeNode &child : root.children) {
        if (child.type == SceneDocument::TreeNode::Variable) {
            variables[child.variableName] = child.variableValue;
        } else if (child.type == SceneDocument::TreeNode::Group
                   && child.operation == SceneDocument::TreeNode::Scene) {
            for (const SceneDocument::TreeNode &sceneChild : child.children) {
                if (sceneChild.type == SceneDocument::TreeNode::Variable)
                    variables[sceneChild.variableName] = sceneChild.variableValue;
            }
        }
    }
    return variables;
}

static bool evaluateRangeExpression(const QString &rangeExpression,
                                    const QHash<QString, qreal> &variables,
                                    QVector<qreal> *values)
{
    if (!values)
        return false;

    values->clear();

    const QString range = rangeExpression.trimmed();
    if (!range.startsWith(QLatin1Char('[')) || !range.endsWith(QLatin1Char(']')))
        return false;

    const QStringList parts = range.mid(1, range.size() - 2).split(QLatin1Char(':'));
    if (parts.size() != 2 && parts.size() != 3) {
        // Might be a comma-separated list: [45, 135, 225, 315]
        const QStringList listParts = splitAtTopLevelCommas(range.mid(1, range.size() - 2));
        if (listParts.size() < 1)
            return false;
        for (const QString &p : listParts) {
            qreal v = 0.0;
            if (!ExpressionSyntax::evaluate(p.trimmed(), variables, &v))
                return false;
            values->append(v);
        }
        return !values->isEmpty();
    }

    qreal start = 0.0;
    qreal step = 1.0;
    qreal end = 0.0;
    if (!ExpressionSyntax::evaluate(parts[0].trimmed(), variables, &start))
        return false;
    if (parts.size() == 2) {
        if (!ExpressionSyntax::evaluate(parts[1].trimmed(), variables, &end))
            return false;
    } else {
        if (!ExpressionSyntax::evaluate(parts[1].trimmed(), variables, &step))
            return false;
        if (!ExpressionSyntax::evaluate(parts[2].trimmed(), variables, &end))
            return false;
    }

    if (qFuzzyIsNull(step))
        return false;

    constexpr int MaxForIterations = 200;
    const qreal epsilon = qAbs(step) * 0.0001 + 0.0001;
    for (qreal value = start;
         step > 0.0 ? value <= end + epsilon : value >= end - epsilon;
         value += step) {
        values->append(value);
        if (values->size() >= MaxForIterations)
            break;
    }

    return true;
}

// Reflect a vector across the plane whose normal is n (Householder reflection).
static QVector3D reflectAcrossPlane(const QVector3D &v, const QVector3D &n)
{
    const float len2 = n.lengthSquared();
    if (qFuzzyIsNull(len2))
        return v;
    return v - 2.0f * QVector3D::dotProduct(v, n) / len2 * n;
}

static QVector3D transformPositionForGroup(const QVector3D &point, const SceneDocument::TreeNode &group)
{
    if (group.operation == SceneDocument::TreeNode::Translate)
        return point + group.position;
    if (group.operation == SceneDocument::TreeNode::Rotate)
        return rotatePoint(point, group.rotation);
    if (group.operation == SceneDocument::TreeNode::Scale)
        return QVector3D(point.x() * group.scale.x(), point.y() * group.scale.y(), point.z() * group.scale.z());
    if (group.operation == SceneDocument::TreeNode::Mirror)
        return reflectAcrossPlane(point, group.position);
    return point;
}

static QVector3D transformNormalForGroup(const QVector3D &normal, const SceneDocument::TreeNode &group)
{
    if (group.operation == SceneDocument::TreeNode::Translate)
        return normal;
    if (group.operation == SceneDocument::TreeNode::Rotate)
        return rotatePoint(normal, group.rotation);
    if (group.operation == SceneDocument::TreeNode::Scale) {
        return QVector3D(qFuzzyIsNull(group.scale.x()) ? normal.x() : normal.x() / group.scale.x(),
                         qFuzzyIsNull(group.scale.y()) ? normal.y() : normal.y() / group.scale.y(),
                         qFuzzyIsNull(group.scale.z()) ? normal.z() : normal.z() / group.scale.z());
    }
    if (group.operation == SceneDocument::TreeNode::Mirror)
        return -reflectAcrossPlane(normal, group.position); // negate to compensate winding reversal
    return normal;
}

static SceneMesh transformedMesh(const SceneMesh &source, const QVector<SceneDocument::TreeNode> &groupStack)
{
    SceneMesh mesh = source;

    auto transformPosition = [&](QVector3D point) {
        for (auto it = groupStack.crbegin(); it != groupStack.crend(); ++it)
            point = transformPositionForGroup(point, *it);
        return point;
    };

    auto transformNormal = [&](QVector3D normal) {
        for (auto it = groupStack.crbegin(); it != groupStack.crend(); ++it)
            normal = transformNormalForGroup(normal, *it);
        return normal.normalized();
    };

    for (MeshTriangle &triangle : mesh.triangles) {
        triangle.a = transformPosition(triangle.a);
        triangle.b = transformPosition(triangle.b);
        triangle.c = transformPosition(triangle.c);
        triangle.normal = transformNormal(triangle.normal);
    }

    for (QVector3D &point : mesh.shadowPoints)
        point = transformPosition(point);

    return mesh;
}

static void appendTreeHelpers(CsgPreview *preview,
                              const SceneDocument &scene,
                              const SceneDocument::TreeNode &node,
                              ShapeNode::BooleanMode inheritedMode,
                              const QHash<QString, qreal> &variables,
                              QVector<SceneDocument::TreeNode> groupStack = {},
                              const QHash<QString, QString> &moduleArgumentOverrides = {},
                              int ownerTreeNodeId = 0)
{
    if (node.type == SceneDocument::TreeNode::Primitive) {
        const ShapeNode *shape = scene.shapeById(node.shapeId);
        if (!shape)
            return;

        const ShapeNode evaluatedShape = shapeWithEvaluatedParameters(*shape, variables);
        CsgRenderItem helper = renderItemFromShape(evaluatedShape,
                                                   scene.indexOfShapeId(shape->id),
                                                   ownerTreeNodeId > 0 ? ownerTreeNodeId : node.id);
        helper.mesh = transformedMesh(helper.mesh, groupStack);
        helper.booleanMode = inheritedMode;
        helper.helper = true;
        preview->items.append(helper);
        return;
    }

    if (node.type == SceneDocument::TreeNode::Variable)
        return;

    if (node.type == SceneDocument::TreeNode::ModuleCall) {
        const SceneDocument::TreeNode *module = scene.treeNodeById(node.shapeId);
        if (module && module->type == SceneDocument::TreeNode::Group
            && module->operation == SceneDocument::TreeNode::Module) {
            appendTreeHelpers(preview,
                              scene,
                              *module,
                              inheritedMode,
                              variables,
                              groupStack,
                              resolveModuleArguments(node.moduleCallArguments, *module),
                              node.id);
        }
        return;
    }

    if (node.operation == SceneDocument::TreeNode::For) {
        QVector<qreal> values;
        const QString variableName = node.loopVariable.trimmed().isEmpty()
                                         ? QStringLiteral("i")
                                         : node.loopVariable.trimmed();
        const QString rangeExpression = node.loopRangeExpression.trimmed().isEmpty()
                                            ? QStringLiteral("[0 : 1 : 3]")
                                            : node.loopRangeExpression.trimmed();
        if (!evaluateRangeExpression(rangeExpression, variables, &values) || values.isEmpty())
            return;

        for (qreal value : values) {
            QHash<QString, qreal> iterationVariables = variables;
            iterationVariables[variableName] = value;
            for (int i = 0; i < node.children.size(); ++i)
                appendTreeHelpers(preview, scene, node.children[i], inheritedMode, iterationVariables, groupStack, {}, ownerTreeNodeId);
        }
        return;
    }

    const QHash<QString, qreal> localVariables = variablesWithScopedVariables(node, variables, moduleArgumentOverrides);
    const SceneDocument::TreeNode evaluatedNode = nodeWithEvaluatedTransform(node, localVariables);
    groupStack.append(evaluatedNode);
    for (int i = 0; i < node.children.size(); ++i) {
        if (node.id == scene.treeRoot().id && isTopLevelModuleDeclaration(node.children[i]))
            continue;

        ShapeNode::BooleanMode childMode = inheritedMode;
        if (node.operation == SceneDocument::TreeNode::Difference && i > 0)
            childMode = ShapeNode::Subtract;
        else if (node.operation == SceneDocument::TreeNode::Intersection)
            childMode = ShapeNode::Intersect;

        appendTreeHelpers(preview, scene, node.children[i], childMode, localVariables, groupStack, {}, ownerTreeNodeId);
    }
}

static bool isInsideAnyAddShape(const QVector<ShapeNode> &shapes, const QVector3D &point)
{
    for (const ShapeNode &shape : shapes) {
        if (shape.booleanMode == ShapeNode::Add && containsPoint(shape, point))
            return true;
    }

    return false;
}

static bool isInsideIntersectionMask(const QVector<ShapeNode> &shapes, const QVector3D &point)
{
    bool hasIntersectionMask = false;

    for (const ShapeNode &shape : shapes) {
        if (shape.booleanMode != ShapeNode::Intersect)
            continue;

        hasIntersectionMask = true;
        if (containsPoint(shape, point))
            return true;
    }

    return !hasIntersectionMask;
}

static bool isInsideOtherSubtractShape(const QVector<ShapeNode> &shapes, int currentShapeIndex, const QVector3D &point)
{
    for (int i = 0; i < shapes.size(); ++i) {
        if (i == currentShapeIndex || shapes[i].booleanMode != ShapeNode::Subtract)
            continue;

        if (containsPoint(shapes[i], point))
            return true;
    }

    return false;
}

static QVector<QVector3D> cubeFacePolygon(const ShapeNode &shape, const QVector<int> &indices)
{
    const QVector3D half = shape.size * 0.5f;
    const QVector<QVector3D> localVertices = {
        {-half.x(), -half.y(), -half.z()}, { half.x(), -half.y(), -half.z()},
        { half.x(),  half.y(), -half.z()}, {-half.x(),  half.y(), -half.z()},
        {-half.x(), -half.y(),  half.z()}, { half.x(), -half.y(),  half.z()},
        { half.x(),  half.y(),  half.z()}, {-half.x(),  half.y(),  half.z()}
    };

    QVector<QVector3D> polygon;
    for (int index : indices)
        polygon.append(rotatePoint(localVertices[index], shape.rotation) + shape.position);

    return polygon;
}

static bool appendClippedCubeSubtractCutFaces(CsgRenderItem *item,
                                              const QVector<ShapeNode> &shapes,
                                              int subtractShapeIndex)
{
    const ShapeNode &subtractShape = shapes[subtractShapeIndex];
    if (subtractShape.type != ShapeNode::Cube)
        return false;

    QVector<Box> clippingBoxes;
    for (int i = 0; i < shapes.size(); ++i) {
        const ShapeNode &shape = shapes[i];
        if (shape.booleanMode == ShapeNode::Add && isAxisAlignedCube(shape))
            clippingBoxes.append(boxFromCube(shape, i));
    }

    if (clippingBoxes.isEmpty())
        return false;

    const QVector<QVector<int>> faceIndices = {
        {0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1},
        {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0}
    };
    const QVector<int> shades = {88, 118, 98, 108, 124, 102};
    const int originalTriangleCount = item->mesh.triangles.size();

    for (int faceIndex = 0; faceIndex < faceIndices.size(); ++faceIndex) {
        const QVector<QVector3D> face = cubeFacePolygon(subtractShape, faceIndices[faceIndex]);

        for (const Box &box : clippingBoxes) {
            QVector<QVector3D> clipped = clipPolygonByBox(face, box);
            if (clipped.size() < 3)
                continue;

            const QVector3D centroid = std::accumulate(clipped.cbegin(), clipped.cend(), QVector3D()) / clipped.size();
            if (!containsPoint(subtractShape, centroid))
                continue;

            if (!isInsideIntersectionMask(shapes, centroid))
                continue;

            if (isInsideOtherSubtractShape(shapes, subtractShapeIndex, centroid))
                continue;

            appendReversedPolygon(&item->mesh, clipped, shades[faceIndex]);
        }
    }

    return item->mesh.triangles.size() > originalTriangleCount;
}

static void appendSubtractCutFaceItems(CsgPreview *preview, const QVector<ShapeNode> &shapes)
{
    for (int i = 0; i < shapes.size(); ++i) {
        const ShapeNode &shape = shapes[i];
        if (shape.booleanMode != ShapeNode::Subtract)
            continue;

        const SceneMesh cutterMesh = buildShapeMesh(shape);
        CsgRenderItem item;
        item.shapeIndex = i;
        item.booleanMode = ShapeNode::Subtract;
        item.computed = true;

        if (appendClippedCubeSubtractCutFaces(&item, shapes, i)) {
            preview->items.append(item);
            continue;
        }

        for (const MeshTriangle &triangle : cutterMesh.triangles) {
            const QVector3D centroid = (triangle.a + triangle.b + triangle.c) / 3.0f;
            if (!isInsideAnyAddShape(shapes, centroid))
                continue;

            if (!isInsideIntersectionMask(shapes, centroid))
                continue;

            if (isInsideOtherSubtractShape(shapes, i, centroid))
                continue;

            const MeshTriangle cutTriangle = reversedTriangle(triangle);
            item.mesh.triangles.append(cutTriangle);
            item.mesh.shadowPoints.append(cutTriangle.a);
            item.mesh.shadowPoints.append(cutTriangle.b);
            item.mesh.shadowPoints.append(cutTriangle.c);
        }

        if (!item.mesh.triangles.isEmpty())
            preview->items.append(item);
    }
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

    appendSubtractCutFaceItems(&preview, shapes);
    appendHelpers(&preview, shapes);
    preview.statusText = "CSG preview: mesh approximate with subtract cut faces";
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

static bool treeHasBooleanOperation(const SceneDocument::TreeNode &node)
{
    if (node.type == SceneDocument::TreeNode::Group
        && (node.operation == SceneDocument::TreeNode::Difference
            || node.operation == SceneDocument::TreeNode::Intersection)) {
        return true;
    }

    for (const SceneDocument::TreeNode &child : node.children) {
        if (treeHasBooleanOperation(child))
            return true;
    }

    return false;
}

static bool hasVectorValue(const QVector3D &vector)
{
    return !qFuzzyIsNull(vector.x()) || !qFuzzyIsNull(vector.y()) || !qFuzzyIsNull(vector.z());
}

static bool hasScaleValue(const QVector3D &vector)
{
    return !qFuzzyCompare(vector.x(), 1.0f)
           || !qFuzzyCompare(vector.y(), 1.0f)
           || !qFuzzyCompare(vector.z(), 1.0f);
}

static bool treeHasGroupTransform(const SceneDocument::TreeNode &node)
{
    if (node.type == SceneDocument::TreeNode::Group) {
        if (node.operation == SceneDocument::TreeNode::Translate && hasVectorValue(node.position))
            return true;
        if (node.operation == SceneDocument::TreeNode::Rotate && hasVectorValue(node.rotation))
            return true;
        if (node.operation == SceneDocument::TreeNode::Scale && hasScaleValue(node.scale))
            return true;
        if (node.operation == SceneDocument::TreeNode::Mirror)
            return true;
        if (node.operation == SceneDocument::TreeNode::Hull)
            return true;
        if (node.operation == SceneDocument::TreeNode::Minkowski)
            return true;
    }

    for (const SceneDocument::TreeNode &child : node.children) {
        if (treeHasGroupTransform(child))
            return true;
    }

    return false;
}

static bool treeHasForOperation(const SceneDocument::TreeNode &node)
{
    if (node.type == SceneDocument::TreeNode::Group && node.operation == SceneDocument::TreeNode::For)
        return true;

    for (const SceneDocument::TreeNode &child : node.children) {
        if (treeHasForOperation(child))
            return true;
    }

    return false;
}

static bool treeHasModuleCall(const SceneDocument::TreeNode &node)
{
    if (node.type == SceneDocument::TreeNode::ModuleCall)
        return true;

    for (const SceneDocument::TreeNode &child : node.children) {
        if (treeHasModuleCall(child))
            return true;
    }

    return false;
}

static bool treeHasModuleDeclarationChild(const SceneDocument::TreeNode &node)
{
    if (node.type == SceneDocument::TreeNode::Group && node.operation == SceneDocument::TreeNode::Module)
        return true;
    return false;
}

CsgPreview buildCsgPreview(const SceneDocument &scene)
{
    const QVector<ShapeNode> &shapes = scene.shapes();
    const bool hasTreeBoolean = treeHasBooleanOperation(scene.treeRoot());
    const bool hasGroupTransform = treeHasGroupTransform(scene.treeRoot());
    const bool hasForOperation = treeHasForOperation(scene.treeRoot());
    const bool hasModuleCall = treeHasModuleCall(scene.treeRoot());
    bool hasModuleDeclaration = false;
    for (const SceneDocument::TreeNode &child : scene.treeRoot().children) {
        if (treeHasModuleDeclarationChild(child)) {
            hasModuleDeclaration = true;
            break;
        }
    }

    if ((hasTreeBoolean || hasGroupTransform || hasForOperation || hasModuleCall || hasModuleDeclaration) && !shapes.isEmpty()) {
        SceneMesh manifoldMesh;
        QString manifoldError;
        if (buildManifoldCsgMesh(scene, &manifoldMesh, &manifoldError)) {
            CsgPreview preview;
            CsgRenderItem item;
            item.mesh = manifoldMesh;
            item.shapeIndex = -1;
            item.booleanMode = ShapeNode::Add;
            item.computed = true;

            preview.mode = CsgPreview::ManifoldComputed;
            preview.items.append(item);
            appendTreeHelpers(&preview, scene, scene.treeRoot(), ShapeNode::Add, topLevelVariables(scene.treeRoot()));
            preview.statusText = hasTreeBoolean
                                     ? "CSG preview: Manifold exact mesh"
                                     : hasForOperation
                                           ? "CSG preview: Manifold for-expanded mesh"
                                           : hasModuleCall
                                                 ? "CSG preview: Manifold module-expanded mesh"
                                                 : "CSG preview: Manifold tree transform mesh";
            return preview;
        }

        if (hasModuleDeclaration && manifoldError.contains(QStringLiteral("empty"), Qt::CaseInsensitive)) {
            CsgPreview preview;
            preview.statusText = QStringLiteral("CSG preview: empty scene");
            return preview;
        }

        CsgPreview preview = buildCsgPreview(shapes);
        preview.statusText = QString("CSG preview: plain mesh (Manifold CSG unavailable: %1)").arg(manifoldError);
        return preview;
    }

    return buildCsgPreview(shapes);
}
