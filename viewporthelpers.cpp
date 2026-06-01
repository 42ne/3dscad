#include "viewporthelpers.h"

#include <QHash>
#include <QLinearGradient>
#include <QPainterPath>
#include <QtMath>
#include <algorithm>
#include <cstddef>
#include <limits>

namespace ViewportHelpers {

int clampColorChannel(float value)
{
    return qBound(0, qRound(value), 255);
}

float normalizedDegrees(float value)
{
    while (value > 180.0f)
        value -= 360.0f;
    while (value < -180.0f)
        value += 360.0f;
    return value;
}

QVector3D toCameraPoint(const QVector3D &world,
                        float yawDegrees,
                        float pitchDegrees,
                        float cameraDistance,
                        const QVector3D &cameraTarget)
{
    const float yaw = qDegreesToRadians(yawDegrees);
    const float pitch = qDegreesToRadians(pitchDegrees);
    QVector3D p = world - cameraTarget;

    p = QVector3D(
        p.x() * qCos(yaw) + p.y() * qSin(yaw),
        -p.x() * qSin(yaw) + p.y() * qCos(yaw),
        p.z());

    p = QVector3D(
        p.x(),
        p.z() * qCos(pitch) - p.y() * qSin(pitch),
        p.y() * qCos(pitch) + p.z() * qSin(pitch));

    p.setZ(p.z() + cameraDistance);
    return p;
}

QVector3D toCameraDirection(const QVector3D &world, float yawDegrees, float pitchDegrees)
{
    return toCameraPoint(world, yawDegrees, pitchDegrees, 0.0f);
}

QVector3D cameraRightVector(float yawDegrees)
{
    const float yaw = qDegreesToRadians(yawDegrees);
    return QVector3D(qCos(yaw), qSin(yaw), 0.0f);
}

QVector3D cameraUpVector(float yawDegrees, float pitchDegrees)
{
    const float yaw = qDegreesToRadians(yawDegrees);
    const float pitch = qDegreesToRadians(pitchDegrees);
    return QVector3D(qSin(pitch) * qSin(yaw),
                     -qSin(pitch) * qCos(yaw),
                     qCos(pitch));
}

QVector3D rotatePoint(const QVector3D &point, const QVector3D &degrees)
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

QVector3D inverseRotatePoint(const QVector3D &point, const QVector3D &degrees)
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

QVector3D reflectAcrossPlane(const QVector3D &vector, const QVector3D &normal)
{
    const float lengthSquared = normal.lengthSquared();
    if (qFuzzyIsNull(lengthSquared))
        return vector;

    return vector - 2.0f * QVector3D::dotProduct(vector, normal) / lengthSquared * normal;
}

QVector3D transformPointForGroup(const QVector3D &point, const SceneDocument::TreeNode &group)
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

QVector3D transformPointByGroupStack(QVector3D point, const QVector<SceneDocument::TreeNode> &groupStack)
{
    for (auto it = groupStack.crbegin(); it != groupStack.crend(); ++it)
        point = transformPointForGroup(point, *it);

    return point;
}

QVector3D transformVectorByGroupStack(QVector3D vector, const QVector<SceneDocument::TreeNode> &groupStack)
{
    for (auto it = groupStack.crbegin(); it != groupStack.crend(); ++it) {
        if (it->operation == SceneDocument::TreeNode::Rotate)
            vector = rotatePoint(vector, it->rotation);
        else if (it->operation == SceneDocument::TreeNode::Scale)
            vector = QVector3D(vector.x() * it->scale.x(), vector.y() * it->scale.y(), vector.z() * it->scale.z());
        else if (it->operation == SceneDocument::TreeNode::Mirror)
            vector = reflectAcrossPlane(vector, it->position);
    }

    return vector;
}

QVector3D transformNormalByGroupStack(QVector3D normal, const QVector<SceneDocument::TreeNode> &groupStack)
{
    for (auto it = groupStack.crbegin(); it != groupStack.crend(); ++it) {
        if (it->operation == SceneDocument::TreeNode::Rotate) {
            normal = rotatePoint(normal, it->rotation);
        } else if (it->operation == SceneDocument::TreeNode::Scale) {
            normal = QVector3D(qFuzzyIsNull(it->scale.x()) ? normal.x() : normal.x() / it->scale.x(),
                               qFuzzyIsNull(it->scale.y()) ? normal.y() : normal.y() / it->scale.y(),
                               qFuzzyIsNull(it->scale.z()) ? normal.z() : normal.z() / it->scale.z());
        } else if (it->operation == SceneDocument::TreeNode::Mirror) {
            normal = -reflectAcrossPlane(normal, it->position);
        }
    }

    return normal.normalized();
}

QVector3D inverseTransformVectorByGroupStack(QVector3D vector, const QVector<SceneDocument::TreeNode> &groupStack)
{
    for (const SceneDocument::TreeNode &group : groupStack) {
        if (group.operation == SceneDocument::TreeNode::Rotate)
            vector = inverseRotatePoint(vector, group.rotation);
        else if (group.operation == SceneDocument::TreeNode::Scale)
            vector = QVector3D(qFuzzyIsNull(group.scale.x()) ? vector.x() : vector.x() / group.scale.x(),
                               qFuzzyIsNull(group.scale.y()) ? vector.y() : vector.y() / group.scale.y(),
                               qFuzzyIsNull(group.scale.z()) ? vector.z() : vector.z() / group.scale.z());
    }

    return vector;
}

bool collectParentGroupStackForShape(const SceneDocument::TreeNode &node,
                                     int shapeId,
                                     QVector<SceneDocument::TreeNode> *groupStack)
{
    if (node.type == SceneDocument::TreeNode::Primitive)
        return node.shapeId == shapeId;

    groupStack->append(node);
    for (const SceneDocument::TreeNode &child : node.children) {
        if (collectParentGroupStackForShape(child, shapeId, groupStack))
            return true;
    }

    groupStack->removeLast();
    return false;
}

bool collectParentGroupStackForGroup(const SceneDocument::TreeNode &node,
                                     int groupId,
                                     QVector<SceneDocument::TreeNode> *groupStack)
{
    if (node.type != SceneDocument::TreeNode::Group)
        return false;

    if (node.id == groupId)
        return true;

    groupStack->append(node);
    for (const SceneDocument::TreeNode &child : node.children) {
        if (collectParentGroupStackForGroup(child, groupId, groupStack))
            return true;
    }

    groupStack->removeLast();
    return false;
}

bool collectTreeNodePath(const SceneDocument::TreeNode &node,
                         int nodeId,
                         QVector<const SceneDocument::TreeNode *> *path)
{
    path->append(&node);
    if (node.id == nodeId)
        return true;

    for (const SceneDocument::TreeNode &child : node.children) {
        if (collectTreeNodePath(child, nodeId, path))
            return true;
    }

    path->removeLast();
    return false;
}

int primitiveTreeNodeIdForShape(const SceneDocument::TreeNode &node, int shapeId)
{
    if (node.type == SceneDocument::TreeNode::Primitive && node.shapeId == shapeId)
        return node.id;

    for (const SceneDocument::TreeNode &child : node.children) {
        const int found = primitiveTreeNodeIdForShape(child, shapeId);
        if (found > 0)
            return found;
    }

    return 0;
}

const ShapeNode *shapeForPrimitiveNode(const SceneDocument *scene, const SceneDocument::TreeNode *node)
{
    if (!scene || !node || node->type != SceneDocument::TreeNode::Primitive)
        return nullptr;
    return scene->shapeById(node->shapeId);
}

SceneMesh interactionMeshForShape(const SceneDocument *scene, const ShapeNode &shape)
{
    SceneMesh mesh = buildShapeMesh(shape);
    if (!scene)
        return mesh;

    QVector<SceneDocument::TreeNode> groupStack;
    if (!collectParentGroupStackForShape(scene->treeRoot(), shape.id, &groupStack))
        return mesh;

    for (MeshTriangle &triangle : mesh.triangles) {
        triangle.a = transformPointByGroupStack(triangle.a, groupStack);
        triangle.b = transformPointByGroupStack(triangle.b, groupStack);
        triangle.c = transformPointByGroupStack(triangle.c, groupStack);
        triangle.normal = transformNormalByGroupStack(triangle.normal, groupStack);
    }

    for (QVector3D &shadowPoint : mesh.shadowPoints)
        shadowPoint = transformPointByGroupStack(shadowPoint, groupStack);

    return mesh;
}

ProjectedPoint projectWorldPoint(const QVector3D &world,
                                 const QSize &viewportSize,
                                 float yawDegrees,
                                 float pitchDegrees,
                                 float cameraDistance,
                                 const QVector3D &cameraTarget,
                                 bool orthographic)
{
    const float focalLength = 420.0f;
    ProjectedPoint projected;
    const QVector3D camera = toCameraPoint(world, yawDegrees, pitchDegrees, cameraDistance, cameraTarget);
    projected.depth = camera.z();
    projected.visible = orthographic || camera.z() > 8.0f;

    const float scale = focalLength / (orthographic ? qMax(8.0f, cameraDistance) : qMax(8.0f, camera.z()));
    projected.point = QPointF(
        viewportSize.width() / 2.0f + camera.x() * scale,
        viewportSize.height() / 2.0f - camera.y() * scale);

    return projected;
}

float distanceToSegment(const QPointF &point, const QPointF &a, const QPointF &b)
{
    const QVector2D segment(b - a);
    const float lengthSquared = segment.lengthSquared();

    if (lengthSquared <= 0.0001f)
        return QVector2D(point - a).length();

    const float t = qBound(0.0f, QVector2D::dotProduct(QVector2D(point - a), segment) / lengthSquared, 1.0f);
    return QVector2D(point - (a + (b - a) * t)).length();
}

float cross2D(const QVector3D &origin, const QVector3D &a, const QVector3D &b)
{
    return (a.x() - origin.x()) * (b.y() - origin.y())
           - (a.y() - origin.y()) * (b.x() - origin.x());
}

QVector<QVector3D> convexHullXY(QVector<QVector3D> points)
{
    std::sort(points.begin(), points.end(), [](const QVector3D &left, const QVector3D &right) {
        if (!qFuzzyCompare(left.x(), right.x()))
            return left.x() < right.x();
        return left.y() < right.y();
    });

    QVector<QVector3D> hull;
    for (const QVector3D &point : points) {
        while (hull.size() >= 2 && cross2D(hull[hull.size() - 2], hull.last(), point) <= 0.0f)
            hull.removeLast();
        hull.append(point);
    }

    const int lowerSize = hull.size();
    for (int i = points.size() - 2; i >= 0; --i) {
        const QVector3D &point = points[i];
        while (hull.size() > lowerSize && cross2D(hull[hull.size() - 2], hull.last(), point) <= 0.0f)
            hull.removeLast();
        hull.append(point);
    }

    if (!hull.isEmpty())
        hull.removeLast();
    return hull;
}

qreal cross2D(const QPointF &origin, const QPointF &a, const QPointF &b)
{
    return (a.x() - origin.x()) * (b.y() - origin.y())
           - (a.y() - origin.y()) * (b.x() - origin.x());
}

QPolygonF convexHull2D(QVector<QPointF> points)
{
    std::sort(points.begin(), points.end(), [](const QPointF &left, const QPointF &right) {
        if (!qFuzzyCompare(left.x(), right.x()))
            return left.x() < right.x();
        return left.y() < right.y();
    });

    QVector<QPointF> hull;
    for (const QPointF &point : points) {
        while (hull.size() >= 2 && cross2D(hull[hull.size() - 2], hull.last(), point) <= 0.0)
            hull.removeLast();
        hull.append(point);
    }

    const int lowerSize = hull.size();
    for (int i = points.size() - 2; i >= 0; --i) {
        const QPointF &point = points[i];
        while (hull.size() > lowerSize && cross2D(hull[hull.size() - 2], hull.last(), point) <= 0.0)
            hull.removeLast();
        hull.append(point);
    }

    if (!hull.isEmpty())
        hull.removeLast();

    QPolygonF polygon;
    for (const QPointF &point : hull)
        polygon.append(point);

    return polygon;
}

void drawArrowHead(QPainter *painter,
                   const QPointF &start,
                   const QPointF &end,
                   const QColor &color,
                   float length,
                   float width,
                   qreal outlineWidth)
{
    QVector2D direction(end - start);
    if (direction.lengthSquared() <= 0.0001f)
        return;

    direction.normalize();
    const QVector2D normal(-direction.y(), direction.x());

    const QPointF tip = end;
    const QPointF base = end - (direction * length).toPointF();
    const QPointF left = base + (normal * width).toPointF();
    const QPointF right = base - (normal * width).toPointF();
    const QPointF ridge = end - (direction * (length * 0.42f)).toPointF();

    QColor darkSide = color.darker(145);
    QColor lightSide = color.lighter(125);
    QPolygonF lightFace;
    lightFace << tip << left << ridge;
    QPolygonF darkFace;
    darkFace << tip << ridge << right;

    painter->setPen(QPen(QColor(5, 8, 12, 190), outlineWidth));
    painter->setBrush(QColor(5, 8, 12, 150));
    painter->drawPolygon(QPolygonF() << tip << left << ridge << right);

    painter->setPen(QPen(color.darker(135), 1.2));
    painter->setBrush(lightSide);
    painter->drawPolygon(lightFace);

    painter->setBrush(darkSide);
    painter->drawPolygon(darkFace);
}

void drawHaloLine(QPainter *painter,
                  const QPointF &start,
                  const QPointF &end,
                  const QColor &color,
                  qreal width,
                  Qt::PenJoinStyle joinStyle)
{
    painter->setPen(QPen(QColor(255, 255, 255, 185), width + 5.0, Qt::SolidLine, Qt::RoundCap, joinStyle));
    painter->drawLine(start, end);
    painter->setPen(QPen(QColor(3, 6, 10, 215), width + 2.2, Qt::SolidLine, Qt::RoundCap, joinStyle));
    painter->drawLine(start, end);
    painter->setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, joinStyle));
    painter->drawLine(start, end);
}

void drawVolumetricGizmoAxis(QPainter *painter,
                             const QPointF &start,
                             const QPointF &end,
                             const QColor &color)
{
    QVector2D direction(end - start);
    if (direction.lengthSquared() <= 0.0001f)
        return;
    direction.normalize();
    const QPointF shaftEnd = end - (direction * 15.0f).toPointF();

    const QPointF shadowOffset(3.0, 4.0);
    painter->setPen(QPen(QColor(0, 0, 0, 54), 11.5, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(start + shadowOffset, shaftEnd + shadowOffset);

    painter->setPen(QPen(QColor(255, 255, 255, 108), 10.0, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(start, shaftEnd);

    painter->setPen(QPen(QColor(2, 5, 10, 168), 7.2, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(start, shaftEnd);

    QLinearGradient shaftGradient(start, shaftEnd);
    shaftGradient.setColorAt(0.0, color.lighter(165));
    shaftGradient.setColorAt(0.42, color.lighter(118));
    shaftGradient.setColorAt(1.0, color.darker(132));
    painter->setPen(QPen(QBrush(shaftGradient), 4.4, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(start, shaftEnd);

    const QVector2D normal(-direction.y(), direction.x());
    const QPointF highlightA = start + (normal * 1.8f).toPointF();
    const QPointF highlightB = shaftEnd + (normal * 1.8f).toPointF();
    painter->setPen(QPen(QColor(255, 255, 255, 82), 1.2, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(highlightA, highlightB);

    QColor arrowColor = color;
    arrowColor.setAlpha(qMin(205, arrowColor.alpha()));
    drawArrowHead(painter, start, end, arrowColor, 23.0f, 11.0f, 3.4);
}

void drawHaloPolyline(QPainter *painter,
                      const QPolygonF &points,
                      const QColor &color,
                      qreal width)
{
    painter->setPen(QPen(QColor(255, 255, 255, 175), width + 4.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawPolyline(points);
    painter->setPen(QPen(QColor(3, 6, 10, 210), width + 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawPolyline(points);
    painter->setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawPolyline(points);
}

void drawValueLabel(QPainter *painter, const QPointF &anchor, const QString &text)
{
    QFont labelFont = painter->font();
    labelFont.setBold(true);
    labelFont.setPointSizeF(qMax<qreal>(8.0, labelFont.pointSizeF()));
    painter->setFont(labelFont);

    const QFontMetricsF metrics(labelFont);
    const QSizeF size(metrics.horizontalAdvance(text) + 14.0, metrics.height() + 6.0);
    const QRectF rect(anchor.x() - size.width() * 0.5, anchor.y() - size.height() * 0.5, size.width(), size.height());
    painter->setPen(QPen(QColor(255, 204, 88), 1.4));
    painter->setBrush(QColor(18, 24, 32, 220));
    painter->drawRoundedRect(rect, 5.0, 5.0);
    painter->setPen(QColor(255, 248, 210));
    painter->drawText(rect, Qt::AlignCenter, text);
}

void drawGlassPanel(QPainter *painter,
                    const QRectF &rect,
                    bool darkTheme,
                    const ViewportAppearanceTheme *theme)
{
    QPainterPath path;
    path.addRoundedRect(rect, 8.0, 8.0);

    painter->setPen(Qt::NoPen);
    painter->setBrush(darkTheme ? QColor(0, 0, 0, 92) : QColor(30, 42, 58, 30));
    painter->drawRoundedRect(rect.translated(3.0, 4.0), 8.0, 8.0);

    QLinearGradient glass(rect.topLeft(), rect.bottomLeft());
    glass.setColorAt(0.0, theme ? theme->glassTop
                                : darkTheme ? QColor(24, 34, 50, 218) : QColor(255, 255, 255, 118));
    glass.setColorAt(1.0, theme ? theme->glassBottom
                                : darkTheme ? QColor(8, 13, 22, 196) : QColor(240, 246, 250, 76));
    painter->setPen(QPen(theme ? theme->glassBorder
                              : darkTheme ? QColor(142, 178, 215, 120)
                                          : QColor(120, 145, 170, 72), 1.0));
    painter->setBrush(glass);
    painter->drawPath(path);
}

QString formatPreviewValue(float value, int precision)
{
    if (precision >= 0)
        return QString::number(value, 'f', precision);

    return QString::number(value, 'f', qAbs(value - qRound(value)) < 0.01f ? 0 : 1);
}

void drawDirectionLabel(QPainter *painter, const QPointF &anchor, const QPointF &awayFrom, const QString &label)
{
    QPointF direction = anchor - awayFrom;
    const qreal length = std::hypot(direction.x(), direction.y());
    if (length > 0.001)
        direction /= length;
    else
        direction = QPointF(0.0, -1.0);

    const QPointF center = anchor + direction * 20.0;
    const QRectF labelRect(center.x() - 12.0, center.y() - 12.0, 24.0, 24.0);
    QFont labelFont = painter->font();
    labelFont.setBold(true);
    labelFont.setWeight(QFont::Black);
    labelFont.setPointSizeF(qMax<qreal>(12.0, labelFont.pointSizeF() + 4.0));
    painter->setFont(labelFont);

    painter->setPen(QPen(QColor(255, 255, 255, 190), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawText(labelRect, Qt::AlignCenter, label);
    painter->setPen(QPen(QColor(5, 8, 12, 235), 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawText(labelRect, Qt::AlignCenter, label);
    painter->setPen(QColor(255, 248, 190, 245));
    painter->drawText(labelRect, Qt::AlignCenter, label);
}

QVector3D rotationRingPoint(const QVector3D &origin, ViewportWidget::DragMode dragMode, float radius, float degrees)
{
    const float angle = qDegreesToRadians(degrees);
    const float c = qCos(angle) * radius;
    const float s = qSin(angle) * radius;

    if (dragMode == ViewportWidget::RotateXDrag)
        return origin + QVector3D(0.0f, c, s);
    if (dragMode == ViewportWidget::RotateYDrag)
        return origin + QVector3D(c, 0.0f, s);

    return origin + QVector3D(c, s, 0.0f);
}

QVector3D rotationVectorForMode(ViewportWidget::DragMode dragMode, float degrees)
{
    if (dragMode == ViewportWidget::RotateXDrag)
        return QVector3D(degrees, 0.0f, 0.0f);
    if (dragMode == ViewportWidget::RotateYDrag)
        return QVector3D(0.0f, degrees, 0.0f);
    if (dragMode == ViewportWidget::RotateZDrag)
        return QVector3D(0.0f, 0.0f, degrees);

    return QVector3D();
}

QColor litColor(const QColor &baseColor, const QVector3D &normal,
                const QVector<SceneLight> &lights, float ambient)
{
    float red   = baseColor.redF()   * ambient;
    float green = baseColor.greenF() * ambient;
    float blue  = baseColor.blueF()  * ambient;

    for (const SceneLight &light : lights) {
        const float amount = qMax(0.0f, QVector3D::dotProduct(normal, light.direction.normalized())) * light.intensity;
        red   += baseColor.redF()   * light.color.redF()   * amount;
        green += baseColor.greenF() * light.color.greenF() * amount;
        blue  += baseColor.blueF()  * light.color.blueF()  * amount;
    }

    red   += baseColor.redF() * 0.06f;
    green += baseColor.greenF() * 0.06f;
    blue  += baseColor.blueF() * 0.06f;

    return QColor(
        clampColorChannel(red * 255.0f),
        clampColorChannel(green * 255.0f),
        clampColorChannel(blue * 255.0f),
        baseColor.alpha());
}

QColor thumbnailLitColor(const QColor &baseColor, const QVector3D &normal,
                         const QVector<SceneLight> &lights)
{
    constexpr float ambient = 0.24f;
    float red   = baseColor.redF()   * ambient;
    float green = baseColor.greenF() * ambient;
    float blue  = baseColor.blueF()  * ambient;

    const QVector3D n = normal.normalized();

    for (const SceneLight &light : lights) {
        const QVector3D l = light.direction.normalized();
        const float front = qMax(0.0f, QVector3D::dotProduct(n, l));
        const float back  = qMax(0.0f, QVector3D::dotProduct(-n, l)) * 0.38f;
        const float amount = (front + back) * light.intensity;
        red   += baseColor.redF()   * light.color.redF()   * amount;
        green += baseColor.greenF() * light.color.greenF() * amount;
        blue  += baseColor.blueF()  * light.color.blueF()  * amount;
    }

    const float lift = 0.08f;
    red   += lift;
    green += lift;
    blue  += lift;

    return QColor(
        clampColorChannel(red * 255.0f),
        clampColorChannel(green * 255.0f),
        clampColorChannel(blue * 255.0f),
        baseColor.alpha());
}

QVector<SceneLight> thumbnailLights()
{
    return {
        { QVector3D(-0.55f, -0.42f, 1.0f).normalized(), QColor(255, 250, 232), 1.18f },
        { QVector3D( 0.78f,  0.10f, 0.45f).normalized(), QColor(110, 176, 255), 0.30f },
        { QVector3D( 0.45f,  0.82f, -0.08f).normalized(), QColor(255, 195, 130), 0.42f }
    };
}

QVector<SceneLight> viewportLightsForPreset(int preset)
{
    switch (preset) {
    case 1:
        return {
            {QVector3D(-0.25f, -0.15f, 1.0f).normalized(), QColor(255, 248, 230), 0.56f},
            {QVector3D(0.65f, 0.35f, 0.55f).normalized(), QColor(190, 220, 255), 0.28f},
            {QVector3D(-0.7f, 0.65f, 0.35f).normalized(), QColor(255, 205, 170), 0.18f}
        };
    case 2:
        return {
            {QVector3D(0.92f, -0.22f, 0.42f).normalized(), QColor(255, 238, 205), 0.84f},
            {QVector3D(-0.55f, 0.35f, 0.85f).normalized(), QColor(125, 185, 255), 0.20f},
            {QVector3D(-0.25f, -0.9f, 0.18f).normalized(), QColor(255, 145, 95), 0.10f}
        };
    case 3:
        return {
            {QVector3D(-0.55f, -0.35f, 1.0f).normalized(), QColor(255, 250, 230), 1.02f},
            {QVector3D(0.9f, 0.25f, 0.25f).normalized(), QColor(90, 150, 255), 0.18f},
            {QVector3D(-0.25f, 0.85f, 0.25f).normalized(), QColor(255, 120, 70), 0.18f}
        };
    default:
        return {
            {QVector3D(-0.45f, -0.35f, 1.0f).normalized(), QColor(255, 244, 214), 0.78f},
            {QVector3D(0.85f, 0.15f, 0.45f).normalized(), QColor(160, 205, 255), 0.34f},
            {QVector3D(-0.2f, 0.9f, 0.25f).normalized(), QColor(255, 170, 110), 0.24f}
        };
    }
}

float viewportAmbientForLightingPreset(int preset)
{
    if (preset == 1)
        return 0.30f;
    if (preset == 3)
        return 0.14f;
    return 0.20f;
}

float viewportSpecularForLightingPreset(int preset)
{
    if (preset == 1)
        return 0.24f;
    if (preset == 2)
        return 0.34f;
    if (preset == 3)
        return 0.54f;
    return 0.42f;
}

QVector3D colorToVector(const QColor &color)
{
    return QVector3D(color.redF(), color.greenF(), color.blueF());
}

QVector4D colorToVector4(const QColor &color)
{
    return QVector4D(color.redF(), color.greenF(), color.blueF(), color.alphaF());
}

QColor viewportBackgroundColor(bool darkTheme, const ViewportAppearanceTheme *theme)
{
    if (theme)
        return theme->background;
    return darkTheme ? QColor(30, 32, 36) : QColor(232, 236, 238);
}

QColor viewportMinorGridColor(bool darkTheme, const ViewportAppearanceTheme *theme)
{
    if (theme)
        return theme->grid;
    return darkTheme ? QColor(70, 74, 82) : QColor(156, 166, 176);
}

QColor viewportComputedSolidColor(bool darkTheme, int variant, const ViewportAppearanceTheme *theme)
{
    if (theme)
        return theme->computedSolid;
    switch (variant) {
    case 1:
        return darkTheme ? QColor(118, 214, 168) : QColor(42, 150, 116);
    case 2:
        return darkTheme ? QColor(213, 157, 126) : QColor(173, 91, 58);
    case 3:
        return darkTheme ? QColor(164, 181, 206) : QColor(86, 112, 148);
    case 4:
        return darkTheme ? QColor(229, 185, 91) : QColor(186, 119, 36);
    default:
        return darkTheme ? QColor(132, 192, 184) : QColor(74, 132, 150);
    }
}

QColor viewportPlainSolidColor(bool darkTheme, int variant, const ViewportAppearanceTheme *theme)
{
    if (theme)
        return theme->solid;
    switch (variant) {
    case 1:
        return darkTheme ? QColor(91, 190, 151) : QColor(38, 128, 105);
    case 2:
        return darkTheme ? QColor(198, 128, 95) : QColor(151, 74, 47);
    case 3:
        return darkTheme ? QColor(126, 154, 198) : QColor(62, 92, 137);
    case 4:
        return darkTheme ? QColor(214, 161, 70) : QColor(166, 95, 31);
    default:
        return darkTheme ? QColor(92, 168, 224) : QColor(64, 116, 176);
    }
}

QColor subduedViewportColor(QColor color, float factor)
{
    color.setRed(clampColorChannel(color.red() * factor));
    color.setGreen(clampColorChannel(color.green() * factor));
    color.setBlue(clampColorChannel(color.blue() * factor));
    return color;
}

QColor selectionHighlightColor(QColor color, bool selected)
{
    if (selected) {
        color = subduedViewportColor(color, 0.90f);
        constexpr int wr = 255, wg = 210, wb = 95;
        return QColor(
            clampColorChannel(color.red()   + (wr - color.red())   * 14 / 100),
            clampColorChannel(color.green() + (wg - color.green()) * 14 / 100),
            clampColorChannel(color.blue()  + (wb - color.blue())  * 14 / 100));
    }
    return subduedViewportColor(color, 0.46f);
}

void collectPrimitiveShapeIds(const SceneDocument::TreeNode &node, QSet<int> *shapeIds)
{
    if (!shapeIds)
        return;

    if (node.type == SceneDocument::TreeNode::Primitive) {
        shapeIds->insert(node.shapeId);
        return;
    }

    for (const SceneDocument::TreeNode &child : node.children)
        collectPrimitiveShapeIds(child, shapeIds);
}

bool selectionHasTreeNodeId(const SceneDocument *scene, int selectedGroupId)
{
    return scene && selectedGroupId > 0 && scene->treeNodeById(selectedGroupId);
}

QSet<int> selectedViewportShapeIds(const SceneDocument *scene,
                                   const QVector<ShapeNode> *shapes,
                                   int selectedIndex,
                                   int selectedGroupId)
{
    QSet<int> shapeIds;
    if (scene && selectedGroupId > 0) {
        if (const SceneDocument::TreeNode *group = scene->treeNodeById(selectedGroupId))
            collectPrimitiveShapeIds(*group, &shapeIds);
    } else if (shapes && selectedIndex >= 0 && selectedIndex < shapes->size()) {
        shapeIds.insert(shapes->at(selectedIndex).id);
    }
    return shapeIds;
}

bool itemBelongsToSelection(const CsgRenderItem &item,
                            const QVector<ShapeNode> *shapes,
                            const QSet<int> &selectedShapeIds,
                            int selectedTreeNodeId)
{
    if (selectedTreeNodeId > 0 && item.treeNodeId == selectedTreeNodeId)
        return true;

    if (item.shapeIndex < 0 || !shapes || item.shapeIndex >= shapes->size())
        return false;

    return selectedShapeIds.contains(shapes->at(item.shapeIndex).id);
}

float edgeValue(const QPointF &a, const QPointF &b, const QPointF &point)
{
    return static_cast<float>((point.x() - a.x()) * (b.y() - a.y())
                              - (point.y() - a.y()) * (b.x() - a.x()));
}

void rasterizeTriangle(QImage *image,
                       QVector<float> *depthBuffer,
                       QVector<int> *pickBuffer,
                       const QSize &viewportSize,
                       const QPointF &a,
                       const QPointF &b,
                       const QPointF &c,
                       float depthA,
                       float depthB,
                       float depthC,
                       const QColor &color,
                       int shapeIndex)
{
    const float area = edgeValue(a, b, c);
    if (qFuzzyIsNull(area))
        return;

    const int minX = qMax(0, qFloor(qMin(a.x(), qMin(b.x(), c.x()))));
    const int maxX = qMin(viewportSize.width() - 1, qCeil(qMax(a.x(), qMax(b.x(), c.x()))));
    const int minY = qMax(0, qFloor(qMin(a.y(), qMin(b.y(), c.y()))));
    const int maxY = qMin(viewportSize.height() - 1, qCeil(qMax(a.y(), qMax(b.y(), c.y()))));

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const QPointF pixelCenter(x + 0.5, y + 0.5);
            const float w0 = edgeValue(b, c, pixelCenter);
            const float w1 = edgeValue(c, a, pixelCenter);
            const float w2 = edgeValue(a, b, pixelCenter);

            const bool inside = area > 0.0f
                                    ? (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                                    : (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);

            if (!inside)
                continue;

            const float normalizedW0 = w0 / area;
            const float normalizedW1 = w1 / area;
            const float normalizedW2 = w2 / area;
            const float depth = normalizedW0 * depthA + normalizedW1 * depthB + normalizedW2 * depthC;
            const int bufferIndex = y * viewportSize.width() + x;

            if (depth < depthBuffer->at(bufferIndex)) {
                (*depthBuffer)[bufferIndex] = depth;
                if (pickBuffer)
                    (*pickBuffer)[bufferIndex] = shapeIndex;

                image->setPixelColor(x, y, color);
            }
        }
    }
}

void drawTrianglesWithDepth(QPainter *painter,
                            const QVector<Triangle2D> &triangles,
                            const QSize &viewportSize,
                            QVector<int> *pickBuffer,
                            QVector<float> *depthBuffer,
                            QImage *image)
{
    if (image->size() != viewportSize || image->format() != QImage::Format_ARGB32_Premultiplied)
        *image = QImage(viewportSize, QImage::Format_ARGB32_Premultiplied);

    image->fill(Qt::transparent);

    const int bufferSize = viewportSize.width() * viewportSize.height();
    depthBuffer->fill(std::numeric_limits<float>::max(), bufferSize);

    if (pickBuffer)
        pickBuffer->fill(-1, bufferSize);

    for (const Triangle2D &triangle : triangles) {
        rasterizeTriangle(image, depthBuffer, pickBuffer, viewportSize,
                          triangle.a, triangle.b, triangle.c,
                          triangle.depthA, triangle.depthB, triangle.depthC,
                          triangle.color, triangle.shapeIndex);
    }

    painter->drawImage(0, 0, *image);
}

void drawTransparentTriangles(QPainter *painter, QVector<Triangle2D> triangles)
{
    std::sort(triangles.begin(), triangles.end(), [](const Triangle2D &left, const Triangle2D &right) {
        const float leftDepth = (left.depthA + left.depthB + left.depthC) / 3.0f;
        const float rightDepth = (right.depthA + right.depthB + right.depthC) / 3.0f;
        return leftDepth > rightDepth;
    });

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    for (const Triangle2D &triangle : triangles) {
        painter->setPen(triangle.hatched ? Qt::NoPen : QPen(triangle.color.darker(135), 0.7));
        painter->setBrush(triangle.color);
        const QPolygonF polygon = QPolygonF() << triangle.a << triangle.b << triangle.c;
        painter->drawPolygon(polygon);

        if (triangle.hatched) {
            QPainterPath clipPath;
            clipPath.addPolygon(polygon);

            const QRectF bounds = polygon.boundingRect().adjusted(-18.0, -18.0, 18.0, 18.0);
            painter->save();
            painter->setClipPath(clipPath);
            painter->setPen(QPen(QColor(86, 109, 124, qBound(12, triangle.color.alpha() / 2, 58)),
                                  0.9,
                                  Qt::SolidLine,
                                  Qt::RoundCap));
            const qreal step = 10.0;
            for (qreal x = bounds.left() - bounds.height(); x < bounds.right() + bounds.height(); x += step)
                painter->drawLine(QPointF(x, bounds.bottom()), QPointF(x + bounds.height(), bounds.top()));

            painter->restore();
        }
    }
    painter->restore();
}

QVector<QPair<QVector3D, QVector3D>> meshEdges(const SceneMesh &mesh)
{
    QVector<QPair<QVector3D, QVector3D>> edges;
    for (const MeshTriangle &triangle : mesh.triangles) {
        edges.append({triangle.a, triangle.b});
        edges.append({triangle.b, triangle.c});
        edges.append({triangle.c, triangle.a});
    }

    return edges;
}

QVector<QPair<QVector3D, QVector3D>> characteristicMeshEdges(const SceneMesh &mesh,
                                                             float cameraYaw,
                                                             float cameraPitch,
                                                             bool visibleFacesOnly,
                                                             bool includeViewSilhouette,
                                                             bool includeStructural)
{
    struct EdgeInfo
    {
        QVector3D from;
        QVector3D to;
        QVector<QVector3D> normals;
        QVector<bool> frontFacing;
    };

    auto pointKey = [](const QVector3D &point) {
        return QStringLiteral("%1,%2,%3")
            .arg(qRound64(point.x() * 1000.0f))
            .arg(qRound64(point.y() * 1000.0f))
            .arg(qRound64(point.z() * 1000.0f));
    };
    auto edgeKey = [&](const QVector3D &a, const QVector3D &b) {
        const QString aKey = pointKey(a);
        const QString bKey = pointKey(b);
        return aKey < bKey ? aKey + QLatin1Char('|') + bKey : bKey + QLatin1Char('|') + aKey;
    };
    auto appendEdge = [&](QHash<QString, EdgeInfo> *edges,
                          const QVector3D &a,
                          const QVector3D &b,
                          const QVector3D &normal) {
        EdgeInfo &edge = (*edges)[edgeKey(a, b)];
        edge.from = a;
        edge.to = b;
        edge.normals.append(normal.normalized());
        edge.frontFacing.append(toCameraDirection(normal, cameraYaw, cameraPitch).z() >= 0.0f);
    };

    QHash<QString, EdgeInfo> edges;
    for (const MeshTriangle &triangle : mesh.triangles) {
        appendEdge(&edges, triangle.a, triangle.b, triangle.normal);
        appendEdge(&edges, triangle.b, triangle.c, triangle.normal);
        appendEdge(&edges, triangle.c, triangle.a, triangle.normal);
    }

    QVector<QPair<QVector3D, QVector3D>> result;
    constexpr float structuralCreaseDotLimit = 0.80f;
    for (const EdgeInfo &edge : edges) {
        const bool hasVisibleFace = edge.frontFacing.contains(true);
        if (visibleFacesOnly && !hasVisibleFace)
            continue;

        const bool boundary = edge.normals.size() == 1;
        bool structuralCrease = false;
        for (int i = 0; i < edge.normals.size() && !structuralCrease; ++i) {
            for (int j = i + 1; j < edge.normals.size(); ++j) {
                if (qAbs(QVector3D::dotProduct(edge.normals[i], edge.normals[j]))
                    < structuralCreaseDotLimit) {
                    structuralCrease = true;
                    break;
                }
            }
        }
        bool silhouette = false;
        if (includeViewSilhouette && !structuralCrease) {
            bool hasFront = false;
            bool hasBack = false;
            for (bool frontFacing : edge.frontFacing) {
                hasFront = hasFront || frontFacing;
                hasBack = hasBack || !frontFacing;
            }
            silhouette = hasFront && hasBack;
        }

        if ((includeStructural && (boundary || structuralCrease)) || silhouette)
            result.append({edge.from, edge.to});
    }
    return result;
}

QVector<ViewportSelectionEdgeCandidate> selectionEdgeTopology(const SceneMesh &mesh)
{
    struct EdgeInfo
    {
        QVector3D from;
        QVector3D to;
        QVector<QVector3D> normals;
    };

    auto pointKey = [](const QVector3D &point) {
        return QStringLiteral("%1,%2,%3")
            .arg(qRound64(point.x() * 1000.0f))
            .arg(qRound64(point.y() * 1000.0f))
            .arg(qRound64(point.z() * 1000.0f));
    };
    auto edgeKey = [&](const QVector3D &a, const QVector3D &b) {
        const QString aKey = pointKey(a);
        const QString bKey = pointKey(b);
        return aKey < bKey ? aKey + QLatin1Char('|') + bKey : bKey + QLatin1Char('|') + aKey;
    };

    QHash<QString, EdgeInfo> edges;
    auto appendEdge = [&](const QVector3D &a, const QVector3D &b, const QVector3D &normal) {
        EdgeInfo &edge = edges[edgeKey(a, b)];
        edge.from = a;
        edge.to = b;
        edge.normals.append(normal.normalized());
    };
    for (const MeshTriangle &triangle : mesh.triangles) {
        appendEdge(triangle.a, triangle.b, triangle.normal);
        appendEdge(triangle.b, triangle.c, triangle.normal);
        appendEdge(triangle.c, triangle.a, triangle.normal);
    }

    constexpr float structuralCreaseDotLimit = 0.80f;
    QVector<ViewportSelectionEdgeCandidate> result;
    result.reserve(edges.size());
    for (const EdgeInfo &edge : edges) {
        bool structural = edge.normals.size() == 1;
        for (int i = 0; i < edge.normals.size() && !structural; ++i) {
            for (int j = i + 1; j < edge.normals.size(); ++j) {
                if (qAbs(QVector3D::dotProduct(edge.normals[i], edge.normals[j]))
                    < structuralCreaseDotLimit) {
                    structural = true;
                    break;
                }
            }
        }
        result.append({edge.from, edge.to, edge.normals, structural});
    }
    return result;
}

uint shapeFingerprint(const ShapeNode &shape, uint seed)
{
    seed = qHash(shape.id, seed);
    seed = qHash(static_cast<int>(shape.type), seed);
    seed = qHash(static_cast<int>(shape.booleanMode), seed);
    seed = qHash(shape.name, seed);
    seed = qHash(shape.position.x(), seed);
    seed = qHash(shape.position.y(), seed);
    seed = qHash(shape.position.z(), seed);
    seed = qHash(shape.rotation.x(), seed);
    seed = qHash(shape.rotation.y(), seed);
    seed = qHash(shape.rotation.z(), seed);
    seed = qHash(shape.size.x(), seed);
    seed = qHash(shape.size.y(), seed);
    seed = qHash(shape.size.z(), seed);
    seed = qHash(shape.radius, seed);
    seed = qHash(shape.height, seed);
    for (const QString &expression : shape.parameterExpressions)
        seed = qHash(expression, seed);
    return seed;
}

uint shapesFingerprint(const QVector<ShapeNode> &shapes)
{
    uint seed = qHash(shapes.size());
    for (const ShapeNode &shape : shapes)
        seed = shapeFingerprint(shape, seed);

    return seed;
}

uint treeFingerprint(const SceneDocument::TreeNode &node, uint seed)
{
    seed = qHash(node.id, seed);
    seed = qHash(static_cast<int>(node.type), seed);
    seed = qHash(static_cast<int>(node.operation), seed);
    seed = qHash(node.shapeId, seed);
    seed = qHash(node.moduleName, seed);
    seed = qHash(node.moduleCallArguments, seed);
    seed = qHash(node.variableName, seed);
    seed = qHash(node.variableExpression, seed);
    seed = qHash(node.variableValue, seed);
    seed = qHash(node.loopVariable, seed);
    seed = qHash(node.loopRangeExpression, seed);
    seed = qHash(node.position.x(), seed);
    seed = qHash(node.position.y(), seed);
    seed = qHash(node.position.z(), seed);
    seed = qHash(node.rotation.x(), seed);
    seed = qHash(node.rotation.y(), seed);
    seed = qHash(node.rotation.z(), seed);
    seed = qHash(node.scale.x(), seed);
    seed = qHash(node.scale.y(), seed);
    seed = qHash(node.scale.z(), seed);
    seed = qHash(node.color.rgba(), seed);
    for (const QString &expression : node.transformExpressions)
        seed = qHash(expression, seed);
    seed = qHash(node.children.size(), seed);

    for (const SceneDocument::TreeNode &child : node.children)
        seed = treeFingerprint(child, seed);

    return seed;
}

uint sceneFingerprint(const SceneDocument &scene)
{
    return treeFingerprint(scene.treeRoot(), shapesFingerprint(scene.shapes()));
}

QMatrix4x4 buildViewMatrix(float yawDeg, float pitchDeg, float dist,
                           const QVector3D &target)
{
    const float yaw   = qDegreesToRadians(yawDeg);
    const float pitch = qDegreesToRadians(pitchDeg);
    const float C  = qCos(yaw),   S  = qSin(yaw);
    const float Cp = qCos(pitch), Sp = qSin(pitch);
    const float tx = target.x(), ty = target.y(), tz = target.z();

    QMatrix4x4 V;
    V(0,0)= C;      V(0,1)= S;      V(0,2)= 0;   V(0,3)= -C*tx - S*ty;
    V(1,0)= Sp*S;   V(1,1)=-Sp*C;   V(1,2)= Cp;  V(1,3)= -Sp*(S*tx - C*ty) - Cp*tz;
    V(2,0)=-Cp*S;   V(2,1)= Cp*C;   V(2,2)= Sp;  V(2,3)=  Cp*(S*tx - C*ty) - Sp*tz + dist;
    V(3,0)= 0;      V(3,1)= 0;      V(3,2)= 0;   V(3,3)= 1;
    return V;
}

QMatrix4x4 buildProjectionMatrix(float viewW, float viewH, float dist, bool ortho)
{
    const float focal = 420.0f;
    const float safeW = qMax(1.0f, viewW);
    const float safeH = qMax(1.0f, viewH);
    const float safeDist = qMax(8.0f, dist);

    if (ortho) {
        const float nearZ = -(safeDist * 5.0f + 500.0f);
        const float farZ  = safeDist * 10.0f + 1000.0f;
        QMatrix4x4 P;
        P.fill(0.0f);
        P(0,0) = 2.0f * focal / (safeW * safeDist);
        P(1,1) = 2.0f * focal / (safeH * safeDist);
        P(2,2) = 2.0f / (farZ - nearZ);
        P(2,3) = -(farZ + nearZ) / (farZ - nearZ);
        P(3,3) = 1.0f;
        return P;
    }

    const float nearZ = qMax(0.5f, dist * 0.005f);
    const float farZ  = dist * 10.0f + 1000.0f;
    const float fx = 2.0f * focal / safeW;
    const float fy = 2.0f * focal / safeH;
    const float a  = (farZ + nearZ) / (farZ - nearZ);
    const float b  = -2.0f * farZ * nearZ / (farZ - nearZ);
    QMatrix4x4 P;
    P.fill(0.0f);
    P(0,0) = fx;  P(1,1) = fy;
    P(2,2) = a;   P(2,3) = b;
    P(3,2) = 1.0f;
    return P;
}

} // namespace ViewportHelpers
