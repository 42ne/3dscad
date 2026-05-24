#include "viewportwidget.h"

#include "scenedocument.h"
#include "scenemesh.h"

#include <QCheckBox>
#include <QtConcurrent>
#include <QComboBox>
#include <QHash>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QOpenGLShaderProgram>
#include <QSet>
#include <QVector4D>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <cstddef>
#include <limits>

struct ProjectedPoint
{
    QPointF point;
    float depth = 0.0f;
    bool visible = true;
};

struct Triangle2D
{
    QPointF a;
    QPointF b;
    QPointF c;
    float depthA = 0.0f;
    float depthB = 0.0f;
    float depthC = 0.0f;
    QColor color;
    int shapeIndex = -1;
    bool hatched = false;
};

struct Line2D
{
    QPointF a;
    QPointF b;
    QColor color;
};

struct SceneLight
{
    QVector3D direction;
    QColor color;
    float intensity = 1.0f;
};

struct OpenGLMeshVertex
{
    QVector3D position; // world space
    QVector3D normal;   // world space
    QVector3D color;
};

struct OpenGLLineVertex
{
    QVector3D position;
    QVector3D color;
};

struct OpenGLFlatVertex
{
    QVector3D position;
    QVector4D color;
};

struct AxisGizmoAxis
{
    QString label;
    QVector3D direction;
    QColor color;
    QPointF end;
    float cameraDepth = 0.0f;
};

static int clampColorChannel(float value)
{
    return qBound(0, qRound(value), 255);
}

static float normalizedDegrees(float value)
{
    while (value > 180.0f)
        value -= 360.0f;
    while (value < -180.0f)
        value += 360.0f;
    return value;
}

static QVector3D toCameraPoint(const QVector3D &world,
                               float yawDegrees,
                               float pitchDegrees,
                               float cameraDistance,
                               const QVector3D &cameraTarget = QVector3D())
{
    const float yaw = qDegreesToRadians(yawDegrees);
    const float pitch = qDegreesToRadians(pitchDegrees);
    QVector3D p = world - cameraTarget;

    // Z-up orbit: yaw rotates around the world Z axis, then pitch tilts around
    // the camera-local horizontal axis. This matches the XY ground plane.
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

static QVector3D toCameraDirection(const QVector3D &world, float yawDegrees, float pitchDegrees)
{
    return toCameraPoint(world, yawDegrees, pitchDegrees, 0.0f);
}

static QVector3D cameraRightVector(float yawDegrees)
{
    const float yaw = qDegreesToRadians(yawDegrees);
    return QVector3D(qCos(yaw), qSin(yaw), 0.0f);
}

static QVector3D cameraUpVector(float yawDegrees, float pitchDegrees)
{
    const float yaw = qDegreesToRadians(yawDegrees);
    const float pitch = qDegreesToRadians(pitchDegrees);
    return QVector3D(qSin(pitch) * qSin(yaw),
                     -qSin(pitch) * qCos(yaw),
                     qCos(pitch));
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

static QVector3D transformPointForGroup(const QVector3D &point, const SceneDocument::TreeNode &group)
{
    if (group.operation == SceneDocument::TreeNode::Translate)
        return point + group.position;
    if (group.operation == SceneDocument::TreeNode::Rotate)
        return rotatePoint(point, group.rotation);
    if (group.operation == SceneDocument::TreeNode::Scale)
        return QVector3D(point.x() * group.scale.x(), point.y() * group.scale.y(), point.z() * group.scale.z());
    return point;
}

static QVector3D transformPointByGroupStack(QVector3D point, const QVector<SceneDocument::TreeNode> &groupStack)
{
    for (auto it = groupStack.crbegin(); it != groupStack.crend(); ++it)
        point = transformPointForGroup(point, *it);

    return point;
}

static QVector3D transformVectorByGroupStack(QVector3D vector, const QVector<SceneDocument::TreeNode> &groupStack)
{
    for (auto it = groupStack.crbegin(); it != groupStack.crend(); ++it) {
        if (it->operation == SceneDocument::TreeNode::Rotate)
            vector = rotatePoint(vector, it->rotation);
        else if (it->operation == SceneDocument::TreeNode::Scale)
            vector = QVector3D(vector.x() * it->scale.x(), vector.y() * it->scale.y(), vector.z() * it->scale.z());
    }

    return vector;
}

static QVector3D inverseTransformVectorByGroupStack(QVector3D vector, const QVector<SceneDocument::TreeNode> &groupStack)
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

static bool collectParentGroupStackForShape(const SceneDocument::TreeNode &node,
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

static bool collectParentGroupStackForGroup(const SceneDocument::TreeNode &node,
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

static ProjectedPoint projectWorldPoint(const QVector3D &world,
                                        const QSize &viewportSize,
                                        float yawDegrees,
                                        float pitchDegrees,
                                        float cameraDistance,
                                        const QVector3D &cameraTarget = QVector3D())
{
    const float focalLength = 420.0f;
    ProjectedPoint projected;
    const QVector3D camera = toCameraPoint(world, yawDegrees, pitchDegrees, cameraDistance, cameraTarget);
    projected.depth = camera.z();
    projected.visible = camera.z() > 8.0f;

    const float scale = focalLength / qMax(8.0f, camera.z());
    projected.point = QPointF(
        viewportSize.width() / 2.0f + camera.x() * scale,
        viewportSize.height() / 2.0f - camera.y() * scale);

    return projected;
}

static float distanceToSegment(const QPointF &point, const QPointF &a, const QPointF &b)
{
    const QVector2D segment(b - a);
    const float lengthSquared = segment.lengthSquared();

    if (lengthSquared <= 0.0001f)
        return QVector2D(point - a).length();

    const float t = qBound(0.0f, QVector2D::dotProduct(QVector2D(point - a), segment) / lengthSquared, 1.0f);
    return QVector2D(point - (a + (b - a) * t)).length();
}

static float cross2D(const QVector3D &origin, const QVector3D &a, const QVector3D &b)
{
    return (a.x() - origin.x()) * (b.y() - origin.y())
           - (a.y() - origin.y()) * (b.x() - origin.x());
}

static QVector<QVector3D> convexHullXY(QVector<QVector3D> points)
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

static qreal cross2D(const QPointF &origin, const QPointF &a, const QPointF &b)
{
    return (a.x() - origin.x()) * (b.y() - origin.y())
           - (a.y() - origin.y()) * (b.x() - origin.x());
}

static QPolygonF convexHull2D(QVector<QPointF> points)
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

static void drawArrowHead(QPainter *painter,
                          const QPointF &start,
                          const QPointF &end,
                          const QColor &color,
                          float length = 18.0f,
                          float width = 7.5f,
                          qreal outlineWidth = 3.0)
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

static void drawHaloLine(QPainter *painter,
                         const QPointF &start,
                         const QPointF &end,
                         const QColor &color,
                         qreal width,
                         Qt::PenJoinStyle joinStyle = Qt::RoundJoin)
{
    painter->setPen(QPen(QColor(255, 255, 255, 185), width + 5.0, Qt::SolidLine, Qt::RoundCap, joinStyle));
    painter->drawLine(start, end);
    painter->setPen(QPen(QColor(3, 6, 10, 215), width + 2.2, Qt::SolidLine, Qt::RoundCap, joinStyle));
    painter->drawLine(start, end);
    painter->setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, joinStyle));
    painter->drawLine(start, end);
}

static void drawHaloPolyline(QPainter *painter,
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

static void drawValueLabel(QPainter *painter, const QPointF &anchor, const QString &text)
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

static QString formatPreviewValue(float value, int precision = -1)
{
    if (precision >= 0)
        return QString::number(value, 'f', precision);

    return QString::number(value, 'f', qAbs(value - qRound(value)) < 0.01f ? 0 : 1);
}

static void drawDirectionLabel(QPainter *painter, const QPointF &anchor, const QPointF &awayFrom, const QString &label)
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

static QVector3D rotationRingPoint(const QVector3D &origin, ViewportWidget::DragMode dragMode, float radius, float degrees)
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

static QVector3D rotationVectorForMode(ViewportWidget::DragMode dragMode, float degrees)
{
    if (dragMode == ViewportWidget::RotateXDrag)
        return QVector3D(degrees, 0.0f, 0.0f);
    if (dragMode == ViewportWidget::RotateYDrag)
        return QVector3D(0.0f, degrees, 0.0f);
    if (dragMode == ViewportWidget::RotateZDrag)
        return QVector3D(0.0f, 0.0f, degrees);

    return QVector3D();
}

static QColor litColor(const QColor &baseColor, const QVector3D &normal,
                       const QVector<SceneLight> &lights, float ambient = 0.22f)
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

    return QColor(
        clampColorChannel(red * 255.0f),
        clampColorChannel(green * 255.0f),
        clampColorChannel(blue * 255.0f),
        baseColor.alpha());
}

static QColor thumbnailLitColor(const QColor &baseColor, const QVector3D &normal,
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

// Specialised lighting for tree-card thumbnails: strong key + warm rim from behind
// to make silhouettes crisp; lower ambient for more contrast at small sizes.
static QVector<SceneLight> thumbnailLights()
{
    return {
        // Key: upper-left-front, warm white, punchy
        { QVector3D(-0.55f, -0.42f, 1.0f).normalized(), QColor(255, 250, 232), 1.18f },
        // Fill: right side, cool blue — prevents the shadow side going pure black
        { QVector3D( 0.78f,  0.10f, 0.45f).normalized(), QColor(110, 176, 255), 0.30f },
        // Rim: back-right at a low angle — catches top and right edges
        { QVector3D( 0.45f,  0.82f, -0.08f).normalized(), QColor(255, 195, 130), 0.42f }
    };
}

static QVector<SceneLight> viewportLightsForPreset(int preset)
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

static float viewportAmbientForLightingPreset(int preset)
{
    if (preset == 1)
        return 0.30f;
    if (preset == 3)
        return 0.14f;
    return 0.20f;
}

static float viewportSpecularForLightingPreset(int preset)
{
    if (preset == 1)
        return 0.24f;
    if (preset == 2)
        return 0.34f;
    if (preset == 3)
        return 0.54f;
    return 0.42f;
}

static QVector3D colorToVector(const QColor &color)
{
    return QVector3D(color.redF(), color.greenF(), color.blueF());
}

static QVector4D colorToVector4(const QColor &color)
{
    return QVector4D(color.redF(), color.greenF(), color.blueF(), color.alphaF());
}

static QColor viewportBackgroundColor(bool darkTheme)
{
    return darkTheme ? QColor(30, 32, 36) : QColor(232, 236, 238);
}

static QColor viewportMinorGridColor(bool darkTheme)
{
    return darkTheme ? QColor(70, 74, 82) : QColor(156, 166, 176);
}

static QColor viewportStatusTextColor(bool darkTheme)
{
    return darkTheme ? QColor(220, 220, 220) : QColor(42, 48, 56);
}

static QColor viewportComputedSolidColor(bool darkTheme, int variant)
{
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

static QColor viewportPlainSolidColor(bool darkTheme, int variant)
{
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

static QColor viewportSelectedSolidColor(bool darkTheme, int variant)
{
    switch (variant) {
    case 1:
        return darkTheme ? QColor(190, 244, 200) : QColor(48, 178, 126);
    case 2:
        return darkTheme ? QColor(255, 188, 129) : QColor(220, 108, 54);
    case 3:
        return darkTheme ? QColor(198, 214, 238) : QColor(94, 130, 184);
    case 4:
        return darkTheme ? QColor(255, 218, 118) : QColor(224, 139, 42);
    default:
        return darkTheme ? QColor(166, 224, 205) : QColor(236, 142, 54);
    }
}

static void collectPrimitiveShapeIds(const SceneDocument::TreeNode &node, QSet<int> *shapeIds)
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

static bool selectionHasTreeNodeId(const SceneDocument *scene, int selectedGroupId)
{
    return scene && selectedGroupId > 0 && scene->treeNodeById(selectedGroupId);
}

static QSet<int> selectedViewportShapeIds(const SceneDocument *scene,
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

static bool itemBelongsToSelection(const CsgRenderItem &item,
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

static QColor dimmedViewportColor(QColor color)
{
    color.setRed(clampColorChannel(color.red() * 0.58f));
    color.setGreen(clampColorChannel(color.green() * 0.58f));
    color.setBlue(clampColorChannel(color.blue() * 0.58f));
    return color;
}

static QColor pulsedViewportColor(QColor color, float pulse)
{
    return QColor(clampColorChannel(color.red() * pulse),
                  clampColorChannel(color.green() * pulse),
                  clampColorChannel(color.blue() * pulse),
                  color.alpha());
}


static float edgeValue(const QPointF &a, const QPointF &b, const QPointF &point)
{
    return static_cast<float>((point.x() - a.x()) * (b.y() - a.y())
                              - (point.y() - a.y()) * (b.x() - a.x()));
}

static void rasterizeTriangle(QImage *image,
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

static void drawTrianglesWithDepth(QPainter *painter,
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
        rasterizeTriangle(image,
                          depthBuffer,
                          pickBuffer,
                          viewportSize,
                          triangle.a,
                          triangle.b,
                          triangle.c,
                          triangle.depthA,
                          triangle.depthB,
                          triangle.depthC,
                          triangle.color,
                          triangle.shapeIndex);
    }

    painter->drawImage(0, 0, *image);
}

static void drawTransparentTriangles(QPainter *painter, QVector<Triangle2D> triangles)
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

static QVector<QPair<QVector3D, QVector3D>> meshEdges(const SceneMesh &mesh)
{
    QVector<QPair<QVector3D, QVector3D>> edges;
    for (const MeshTriangle &triangle : mesh.triangles) {
        edges.append({triangle.a, triangle.b});
        edges.append({triangle.b, triangle.c});
        edges.append({triangle.c, triangle.a});
    }

    return edges;
}

static uint shapeFingerprint(const ShapeNode &shape, uint seed)
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

static uint shapesFingerprint(const QVector<ShapeNode> &shapes)
{
    uint seed = qHash(shapes.size());
    for (const ShapeNode &shape : shapes)
        seed = shapeFingerprint(shape, seed);

    return seed;
}

static uint treeFingerprint(const SceneDocument::TreeNode &node, uint seed = 0)
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

static uint sceneFingerprint(const SceneDocument &scene)
{
    return treeFingerprint(scene.treeRoot(), shapesFingerprint(scene.shapes()));
}

ViewportWidget::ViewportWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(500, 400);
    setFocusPolicy(Qt::StrongFocus);

    m_openGLViewportCheckBox = new QCheckBox(QStringLiteral("OpenGL"), this);
    m_darkViewportCheckBox = new QCheckBox(QStringLiteral("Dark"), this);
    m_colorVariantComboBox = new QComboBox(this);
    m_lightingPresetComboBox = new QComboBox(this);
    m_darkViewportCheckBox->setChecked(m_darkViewportTheme);
    m_colorVariantComboBox->addItems({
        QStringLiteral("Neutral"),
        QStringLiteral("Mint"),
        QStringLiteral("Clay"),
        QStringLiteral("Steel"),
        QStringLiteral("Amber")
    });
    m_lightingPresetComboBox->addItems({
        QStringLiteral("Studio"),
        QStringLiteral("Soft"),
        QStringLiteral("Side"),
        QStringLiteral("Contrast")
    });

    const QString controlStyle = QStringLiteral(
        "QCheckBox, QComboBox {"
        "  color: #eef2f6;"
        "  background: rgba(12, 16, 22, 150);"
        "  border: 1px solid rgba(230, 236, 244, 70);"
        "  border-radius: 5px;"
        "  padding: 3px 7px 3px 5px;"
        "}"
        "QComboBox::drop-down { border: 0px; width: 18px; }"
        "QCheckBox::indicator { width: 13px; height: 13px; }"
        "QCheckBox:disabled, QComboBox:disabled { color: rgba(238, 242, 246, 95); }");
    m_openGLViewportCheckBox->setStyleSheet(controlStyle);
    m_darkViewportCheckBox->setStyleSheet(controlStyle);
    m_colorVariantComboBox->setStyleSheet(controlStyle);
    m_lightingPresetComboBox->setStyleSheet(controlStyle);
    m_openGLViewportCheckBox->setToolTip(QStringLiteral("Use OpenGL rendering for viewport meshes and grid."));
    m_darkViewportCheckBox->setToolTip(QStringLiteral("Switch viewport between dark and light theme."));
    m_colorVariantComboBox->setToolTip(QStringLiteral("Choose viewport material color variant."));
    m_lightingPresetComboBox->setToolTip(QStringLiteral("Choose viewport lighting preset."));

    connect(m_openGLViewportCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        setRenderBackend(checked ? OpenGLRenderBackend : SoftwareRenderBackend);
    });
    connect(m_darkViewportCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        m_darkViewportTheme = checked;
        update();
    });
    connect(m_colorVariantComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_viewportColorVariant = qMax(0, index);
        update();
    });
    connect(m_lightingPresetComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_lightingPreset = qMax(0, index);
        update();
    });
    m_selectionPulseTimer.setInterval(80);
    connect(&m_selectionPulseTimer, &QTimer::timeout, this, [this]() {
        ++m_selectionPulseFrame;
        update();
    });

    connect(&m_csgWatcher, &QFutureWatcher<CsgPreview>::finished,
            this, &ViewportWidget::onCsgPreviewReady);

    updateViewportControls();
    updateSelectionPulseTimer();
}

void ViewportWidget::setScene(const SceneDocument *scene)
{
    m_scene = scene;
    m_shapes = scene ? &scene->shapes() : nullptr;
    invalidateCsgPreview();
    updateSelectionPulseTimer();
    update();
}

void ViewportWidget::setShapes(const QVector<ShapeNode> *shapes)
{
    m_scene = nullptr;
    m_shapes = shapes;
    invalidateCsgPreview();
    updateSelectionPulseTimer();
    update();
}

void ViewportWidget::setSelectedIndex(int index)
{
    m_selectedIndex = index;
    if (index >= 0)
        m_selectedGroupId = 0;
    m_vboMeshKey.clear();
    updateSelectionPulseTimer();
    update();
}

void ViewportWidget::setSelectedGroupId(int groupId)
{
    m_selectedGroupId = groupId;
    if (groupId > 0)
        m_selectedIndex = -1;
    m_vboMeshKey.clear();
    updateSelectionPulseTimer();
    update();
}

void ViewportWidget::setTreeTransformControlPreview(int groupId, SceneDocument::TreeNode::Operation operation, int axis)
{
    if (m_treeTransformPreviewGroupId == groupId
        && m_treeTransformPreviewOperation == operation
        && m_treeTransformPreviewAxis == axis) {
        return;
    }

    m_treeTransformPreviewGroupId = groupId;
    m_treeTransformPreviewOperation = operation;
    m_treeTransformPreviewAxis = axis;
    update();
}

void ViewportWidget::setTreeShapeParameterPreview(int shapeId, int parameter)
{
    if (m_treeShapePreviewShapeId == shapeId
        && m_treeShapePreviewParameter == parameter) {
        return;
    }

    m_treeShapePreviewShapeId = shapeId;
    m_treeShapePreviewParameter = parameter;
    update();
}

void ViewportWidget::setRenderBackend(RenderBackend backend)
{
    if (backend == OpenGLRenderBackend && !canUseOpenGLRenderBackend())
        backend = SoftwareRenderBackend;

    if (m_renderBackend == backend) {
        updateViewportControls();
        updateSelectionPulseTimer();
        return;
    }

    m_renderBackend = backend;
    updateViewportControls();
    updateSelectionPulseTimer();
    update();
}

ViewportWidget::RenderBackend ViewportWidget::renderBackend() const
{
    return m_renderBackend;
}

QString ViewportWidget::renderBackendName() const
{
    if (m_renderBackend == OpenGLRenderBackend)
        return "OpenGL";

    return "Software";
}

bool ViewportWidget::isOpenGLRenderBackendAvailable() const
{
    return canUseOpenGLRenderBackend();
}

void ViewportWidget::startAsyncCsgCompute()
{
    // One compute at a time; if already running, onCsgPreviewReady() will
    // re-check m_csgPreviewDirty and re-enter if the scene changed again.
    if (m_csgComputing || !m_shapes)
        return;

    const uint fingerprint = m_scene
                                 ? sceneFingerprint(*m_scene)
                                 : shapesFingerprint(*m_shapes);

    // Nothing actually changed — skip redundant work.
    if (!m_csgPreviewDirty && fingerprint == m_cachedCsgFingerprint)
        return;

    m_csgComputing = true;
    m_csgPreviewDirty = false;
    m_pendingCsgFingerprint = fingerprint;

    if (m_scene) {
        // Snapshot the scene so the worker thread has its own independent copy.
        const SceneDocument::Snapshot snap = m_scene->snapshot();
        m_csgWatcher.setFuture(QtConcurrent::run([snap]() -> CsgPreview {
            SceneDocument doc;
            doc.restoreSnapshot(snap);
            return buildCsgPreview(doc);
        }));
    } else {
        const QVector<ShapeNode> shapes = *m_shapes;
        m_csgWatcher.setFuture(QtConcurrent::run([shapes]() -> CsgPreview {
            return buildCsgPreview(shapes);
        }));
    }
}

void ViewportWidget::onCsgPreviewReady()
{
    m_cachedCsgPreview = m_csgWatcher.result();
    m_cachedCsgFingerprint = m_pendingCsgFingerprint;
    m_csgComputing = false;

    // If the scene changed while we were computing, kick off another round.
    if (m_csgPreviewDirty)
        startAsyncCsgCompute();

    update();
    emit csgPreviewReady();
}

void ViewportWidget::invalidateCsgPreview()
{
    m_csgPreviewDirty = true;
    startAsyncCsgCompute();
}

QString ViewportWidget::csgStatusText()
{
    if (!m_shapes)
        return QStringLiteral("CSG preview: empty");

    if (m_csgComputing || m_csgPreviewDirty)
        return QStringLiteral("CSG preview: computing…");

    return m_cachedCsgPreview.statusText;
}

// ── Camera matrix helpers ──────────────────────────────────────────────────────
// buildViewMatrix: maps world-space points to camera-space using the same
// yaw-then-pitch orbit as toCameraPoint(). Verified to produce identical results.
static QMatrix4x4 buildViewMatrix(float yawDeg, float pitchDeg, float dist,
                                   const QVector3D &target)
{
    const float yaw   = qDegreesToRadians(yawDeg);
    const float pitch = qDegreesToRadians(pitchDeg);
    const float C  = qCos(yaw),   S  = qSin(yaw);
    const float Cp = qCos(pitch), Sp = qSin(pitch);
    const float tx = target.x(), ty = target.y(), tz = target.z();

    // Derived row by row from: T(0,0,dist)*SwapYZ*R_X(-pitch)*R_Z(-yaw)*T(-target)
    QMatrix4x4 V;
    V(0,0)= C;      V(0,1)= S;      V(0,2)= 0;   V(0,3)= -C*tx - S*ty;
    V(1,0)= Sp*S;   V(1,1)=-Sp*C;   V(1,2)= Cp;  V(1,3)= -Sp*(S*tx - C*ty) - Cp*tz;
    V(2,0)=-Cp*S;   V(2,1)= Cp*C;   V(2,2)= Sp;  V(2,3)=  Cp*(S*tx - C*ty) - Sp*tz + dist;
    V(3,0)= 0;      V(3,1)= 0;      V(3,2)= 0;   V(3,3)= 1;
    return V;
}

// buildProjectionMatrix: left-handed perspective matching projectWorldPoint().
// focalLength=420 gives FOV consistent with the software renderer.
static QMatrix4x4 buildProjectionMatrix(float viewW, float viewH)
{
    const float focal = 420.0f, nearZ = 8.0f, farZ = 1200.0f;
    const float fx = 2.0f * focal / qMax(1.0f, viewW);
    const float fy = 2.0f * focal / qMax(1.0f, viewH);
    const float a  = (farZ + nearZ) / (farZ - nearZ);
    const float b  = -2.0f * farZ * nearZ / (farZ - nearZ);
    QMatrix4x4 P;
    P.fill(0.0f);
    P(0,0) = fx;  P(1,1) = fy;
    P(2,2) = a;   P(2,3) = b;
    P(3,2) = 1.0f;
    return P;
}

void ViewportWidget::initializeGL()
{
    initializeOpenGLFunctions();
    const QColor background = viewportBackgroundColor(m_darkViewportTheme);
    glClearColor(background.redF(), background.greenF(), background.blueF(), 1.0f);

    m_glMeshProgram = new QOpenGLShaderProgram(this);
    m_glMeshProgram->addShaderFromSourceCode(
        QOpenGLShader::Vertex,
        "attribute vec3 a_position;\n"   // world space
        "attribute vec3 a_normal;\n"     // world space
        "attribute vec3 a_color;\n"
        "uniform mat4 u_mvp;\n"
        "varying vec3 v_normal;\n"       // world space — passed through unchanged
        "varying vec3 v_world_pos;\n"    // world space — for view-direction in fragment
        "varying vec3 v_color;\n"
        "void main() {\n"
        "    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
        "    v_normal    = a_normal;\n"
        "    v_world_pos = a_position;\n"
        "    v_color     = a_color;\n"
        "}\n");
    m_glMeshProgram->addShaderFromSourceCode(
        QOpenGLShader::Fragment,
        "#ifdef GL_ES\n"
        "precision mediump float;\n"
        "#endif\n"
        "varying vec3 v_normal;\n"
        "varying vec3 v_world_pos;\n"
        "varying vec3 v_color;\n"
        "uniform vec3 u_camera_pos;\n"   // world space camera position
        "uniform vec3 u_light_direction_a;\n"
        "uniform vec3 u_light_direction_b;\n"
        "uniform vec3 u_light_direction_c;\n"
        "uniform vec3 u_light_color_a;\n"
        "uniform vec3 u_light_color_b;\n"
        "uniform vec3 u_light_color_c;\n"
        "uniform float u_light_intensity_a;\n"
        "uniform float u_light_intensity_b;\n"
        "uniform float u_light_intensity_c;\n"
        "uniform float u_ambient;\n"
        "uniform float u_specular_strength;\n"
        "void main() {\n"
        "    vec3 n = normalize(v_normal);\n"
        "    vec3 viewDir = normalize(u_camera_pos - v_world_pos);\n"
        "    vec3 lightA = normalize(u_light_direction_a);\n"
        "    vec3 lightB = normalize(u_light_direction_b);\n"
        "    vec3 lightC = normalize(u_light_direction_c);\n"
        "    float diffuseA = max(0.0, dot(n, lightA));\n"
        "    float diffuseB = max(0.0, dot(n, lightB));\n"
        "    float diffuseC = max(0.0, dot(n, lightC));\n"
        "    vec3 reflectedView = reflect(-viewDir, n);\n"
        "    float skyMix = clamp(reflectedView.z * 0.55 + 0.5, 0.0, 1.0);\n"  // z=up in world
        "    vec3 environment = mix(vec3(0.18, 0.20, 0.22), vec3(0.58, 0.70, 0.82), skyMix);\n"
        "    float fresnel = pow(1.0 - max(0.0, dot(n, viewDir)), 3.0);\n"
        "    float specA = pow(max(0.0, dot(reflect(-lightA, n), viewDir)), 42.0);\n"
        "    float specB = pow(max(0.0, dot(reflect(-lightB, n), viewDir)), 26.0);\n"
        "    float rim = pow(1.0 - max(0.0, dot(n, viewDir)), 2.0);\n"
        "    vec3 warmLight = u_light_color_a * diffuseA * u_light_intensity_a;\n"
        "    vec3 coolLight = u_light_color_b * diffuseB * u_light_intensity_b;\n"
        "    vec3 sideLight = u_light_color_c * diffuseC * u_light_intensity_c;\n"
        "    vec3 shaded = v_color * (vec3(u_ambient) + warmLight + coolLight + sideLight);\n"
        "    shaded = mix(shaded, environment, 0.14 + fresnel * 0.26);\n"
        "    shaded += vec3(1.0, 0.92, 0.74) * specA * u_specular_strength;\n"
        "    shaded += vec3(0.70, 0.86, 1.0) * specB * (u_specular_strength * 0.43);\n"
        "    shaded += vec3(0.55, 0.75, 1.0) * rim * 0.16;\n"
        "    gl_FragColor = vec4(clamp(shaded, 0.0, 1.0), 1.0);\n"
        "}\n");
    m_glMeshProgram->link();

    m_glLineProgram = new QOpenGLShaderProgram(this);
    m_glLineProgram->addShaderFromSourceCode(
        QOpenGLShader::Vertex,
        "attribute vec3 a_position;\n"  // world space
        "attribute vec3 a_color;\n"
        "uniform mat4 u_mvp;\n"
        "varying vec3 v_color;\n"
        "void main() {\n"
        "    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
        "    v_color = a_color;\n"
        "}\n");
    m_glLineProgram->addShaderFromSourceCode(
        QOpenGLShader::Fragment,
        "#ifdef GL_ES\n"
        "precision mediump float;\n"
        "#endif\n"
        "varying vec3 v_color;\n"
        "void main() {\n"
        "    gl_FragColor = vec4(v_color, 1.0);\n"
        "}\n");
    m_glLineProgram->link();

    m_glFlatProgram = new QOpenGLShaderProgram(this);
    m_glFlatProgram->addShaderFromSourceCode(
        QOpenGLShader::Vertex,
        "attribute vec3 a_position;\n"  // world space
        "attribute vec4 a_color;\n"
        "uniform mat4 u_mvp;\n"
        "uniform vec2 u_offset;\n"      // XY world-space offset (for shadow multi-sample)
        "varying vec4 v_color;\n"
        "void main() {\n"
        "    vec3 pos = vec3(a_position.x + u_offset.x, a_position.y + u_offset.y, a_position.z);\n"
        "    gl_Position = u_mvp * vec4(pos, 1.0);\n"
        "    v_color = a_color;\n"
        "}\n");
    m_glFlatProgram->addShaderFromSourceCode(
        QOpenGLShader::Fragment,
        "#ifdef GL_ES\n"
        "precision mediump float;\n"
        "#endif\n"
        "varying vec4 v_color;\n"
        "void main() {\n"
        "    gl_FragColor = v_color;\n"
        "}\n");
    m_glFlatProgram->link();

    // ── Grid VBO (built once, never changes) ────────────────────────────────────
    m_vboGrid.create();
    m_vboGrid.setUsagePattern(QOpenGLBuffer::StaticDraw);
    {
        QVector<OpenGLLineVertex> gridVerts;
        gridVerts.reserve(504 + 6);
        auto appendLine = [&](const QVector3D &from, const QVector3D &to, const QColor &color) {
            const QVector3D c = colorToVector(color);
            gridVerts.append({from, c});
            gridVerts.append({to,   c});
        };
        const QColor minor(128, 128, 128); // placeholder; real color set at draw time via clear+rebuild if theme changes
        for (int i = -120; i <= 120; i += 20) {
            appendLine({-120, float(i), 0}, {120, float(i), 0}, minor);
            appendLine({float(i), -120, 0}, {float(i), 120, 0}, minor);
        }
        appendLine({-130, 0, 0}, {130, 0, 0}, QColor(210, 80, 80));
        appendLine({0, -130, 0}, {0, 130, 0}, QColor(80, 180, 110));
        appendLine({0, 0, 0},    {0, 0, 90},  QColor(90, 150, 230));
        m_vboGrid.bind();
        m_vboGrid.allocate(gridVerts.constData(), gridVerts.size() * int(sizeof(OpenGLLineVertex)));
        m_vboGrid.release();
        m_vboGridCount = gridVerts.size();
    }

    // ── Mesh / helper / shadow VBOs — created here, filled on first draw ───────
    m_vboMesh.create();        m_vboMesh.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_vboHelperFront.create(); m_vboHelperFront.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_vboHelperXray.create();  m_vboHelperXray.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_vboShadow.create();      m_vboShadow.setUsagePattern(QOpenGLBuffer::DynamicDraw);
}

void ViewportWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void ViewportWidget::resizeEvent(QResizeEvent *event)
{
    QOpenGLWidget::resizeEvent(event);
    updateViewportControls();
}

void ViewportWidget::paintGL()
{
    const QColor background = viewportBackgroundColor(m_darkViewportTheme);
    glClearColor(background.redF(), background.greenF(), background.blueF(), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    const bool useOpenGLPreview = m_renderBackend == OpenGLRenderBackend && canUseOpenGLRenderBackend();
    if (useOpenGLPreview) {
        paintOpenGLGrid();
        paintOpenGLContactShadows();
        paintOpenGLPreview();
    }

    glDisable(GL_DEPTH_TEST);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    paintSoftware(painter, !useOpenGLPreview);

    painter.end();
}

// ── Static thumbnail renderer ────────────────────────────────────────────────
// Builds a rasterised preview image of a scene without any UI chrome.
// Designed to be called from a background thread (all state is local).
QImage ViewportWidget::renderThumbnail(const SceneDocument &scene, QSize thumbnailSize,
                                       const QColor &bgColor)
{
    QImage image(thumbnailSize, QImage::Format_ARGB32_Premultiplied);
    if (bgColor.alpha() == 0) {
        image.fill(Qt::transparent);
    } else {
        QPainter bgPainter(&image);
        QLinearGradient bg(QPointF(0.0, 0.0), QPointF(0.0, thumbnailSize.height()));
        bg.setColorAt(0.0, bgColor.lighter(145));
        bg.setColorAt(0.55, bgColor);
        bg.setColorAt(1.0, bgColor.darker(132));
        bgPainter.fillRect(image.rect(), bg);
        bgPainter.setPen(Qt::NoPen);
        bgPainter.setBrush(QColor(255, 255, 255, 18));
        const qreal spot = qMin(thumbnailSize.width(), thumbnailSize.height()) * 0.52;
        bgPainter.drawEllipse(QRectF(thumbnailSize.width() * 0.18,
                                     thumbnailSize.height() * 0.08,
                                     spot,
                                     spot * 0.58));
    }

    const CsgPreview preview = buildCsgPreview(scene);
    if (preview.items.isEmpty())
        return image;

    // Compute bounding box from non-helper mesh items.
    QVector3D bbMin( 1e9f,  1e9f,  1e9f);
    QVector3D bbMax(-1e9f, -1e9f, -1e9f);
    bool hasMesh = false;
    for (const CsgRenderItem &item : preview.items) {
        if (item.helper) continue;
        for (const MeshTriangle &tri : item.mesh.triangles) {
            for (const QVector3D *v : {&tri.a, &tri.b, &tri.c}) {
                bbMin.setX(qMin(bbMin.x(), v->x()));
                bbMin.setY(qMin(bbMin.y(), v->y()));
                bbMin.setZ(qMin(bbMin.z(), v->z()));
                bbMax.setX(qMax(bbMax.x(), v->x()));
                bbMax.setY(qMax(bbMax.y(), v->y()));
                bbMax.setZ(qMax(bbMax.z(), v->z()));
                hasMesh = true;
            }
        }
    }
    if (!hasMesh)
        return image;

    const QVector3D cameraTarget = (bbMin + bbMax) * 0.5f;
    const float extent = (bbMax - bbMin).length();
    // Fill most of the shorter side so the shape reads as a solid object in small cards.
    // focalLength 420 matches projectWorldPoint's hardcoded value.
    const float shortSide = static_cast<float>(qMin(thumbnailSize.width(), thumbnailSize.height()));
    const float cameraDistance = qMax(extent * 420.0f / (shortSide * 0.94f), 20.0f);

    const float yaw   = -38.0f;
    const float pitch = -26.0f;
    const QVector<SceneLight> lights = thumbnailLights();

    auto project = [&](const QVector3D &world) {
        return projectWorldPoint(world, thumbnailSize, yaw, pitch, cameraDistance, cameraTarget);
    };

    QVector<Triangle2D> triangles;
    triangles.reserve(512);
    for (const CsgRenderItem &item : preview.items) {
        if (item.helper) continue;
        const QColor baseColor = (item.booleanMode == ShapeNode::Subtract) ? QColor(75,  90, 195)
                               : (item.booleanMode == ShapeNode::Intersect) ? QColor(155, 95, 215)
                               :                                               QColor(82, 138, 212);
        for (const MeshTriangle &mt : item.mesh.triangles) {
            const ProjectedPoint a = project(mt.a);
            const ProjectedPoint b = project(mt.b);
            const ProjectedPoint c = project(mt.c);
            Triangle2D tri;
            tri.a      = a.point;  tri.depthA = a.depth;
            tri.b      = b.point;  tri.depthB = b.depth;
            tri.c      = c.point;  tri.depthC = c.depth;
            tri.color  = thumbnailLitColor(baseColor.lighter(qMax(mt.shade, 112)), mt.normal, lights);
            tri.shapeIndex = item.shapeIndex;
            triangles.append(tri);
        }
    }

    QPainter painter(&image);
    QImage   rasterBuffer; // depth-rasterised triangle pixels (transparent bg)
    QVector<float> depthBuffer;
    drawTrianglesWithDepth(&painter, triangles, thumbnailSize,
                           nullptr, &depthBuffer, &rasterBuffer);
    painter.end();

    // -------------------------------------------------------------------
    // Post-process: silhouette outline.
    // Walk the rasterBuffer (transparent = background, opaque = geometry).
    // Any geometry pixel adjacent to a transparent pixel is a silhouette
    // edge — darken it in the final image to create a crisp 1-px outline.
    // -------------------------------------------------------------------
    {
        const int W = thumbnailSize.width();
        const int H = thumbnailSize.height();
        // Check 2-pixel radius so the outline reads clearly at display size.
        for (int y = 0; y < H; ++y) {
            QRgb *mainRow = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < W; ++x) {
                // Skip background pixels.
                if (qAlpha(reinterpret_cast<const QRgb *>(rasterBuffer.constScanLine(y))[x]) == 0)
                    continue;

                // Check 4-connected neighbours for transparency.
                bool edge =
                    (x == 0   || qAlpha(reinterpret_cast<const QRgb *>(rasterBuffer.constScanLine(y))[x - 1]) == 0) ||
                    (x == W-1 || qAlpha(reinterpret_cast<const QRgb *>(rasterBuffer.constScanLine(y))[x + 1]) == 0) ||
                    (y == 0   || qAlpha(reinterpret_cast<const QRgb *>(rasterBuffer.constScanLine(y - 1))[x]) == 0) ||
                    (y == H-1 || qAlpha(reinterpret_cast<const QRgb *>(rasterBuffer.constScanLine(y + 1))[x]) == 0);

                if (edge) {
                    const QRgb px = mainRow[x];
                    // A very light silhouette treatment keeps edges readable
                    // without making small primitives look like contour icons.
                    mainRow[x] = qRgba(qBound(0, int(qRed(px)   * 0.78f), 255),
                                       qBound(0, int(qGreen(px) * 0.78f), 255),
                                       qBound(0, int(qBlue(px)  * 0.78f), 255),
                                       qAlpha(px));
                }
            }
        }
    }

    return image;
}

void ViewportWidget::paintSoftware(QPainter &painter, bool drawSceneMeshes)
{
    if (drawSceneMeshes)
        painter.fillRect(rect(), viewportBackgroundColor(m_darkViewportTheme));

    const QVector<SceneLight> lights = viewportLightsForPreset(m_lightingPreset);

    auto project = [&](const QVector3D &world) {
        return projectWorldPoint(world, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance, m_cameraTarget);
    };

    auto drawGrid = [&]() {
        painter.setPen(viewportMinorGridColor(m_darkViewportTheme));

        for (int i = -120; i <= 120; i += 20) {
            const ProjectedPoint xStart = project(QVector3D(-120, i, 0));
            const ProjectedPoint xEnd = project(QVector3D(120, i, 0));
            const ProjectedPoint yStart = project(QVector3D(i, -120, 0));
            const ProjectedPoint yEnd = project(QVector3D(i, 120, 0));

            painter.drawLine(xStart.point, xEnd.point);
            painter.drawLine(yStart.point, yEnd.point);
        }

        painter.setPen(QPen(QColor(210, 80, 80), 2));
        painter.drawLine(project(QVector3D(-130, 0, 0)).point, project(QVector3D(130, 0, 0)).point);

        painter.setPen(QPen(QColor(80, 180, 110), 2));
        painter.drawLine(project(QVector3D(0, -130, 0)).point, project(QVector3D(0, 130, 0)).point);

        painter.setPen(QPen(QColor(90, 150, 230), 2));
        painter.drawLine(project(QVector3D(0, 0, 0)).point, project(QVector3D(0, 0, 90)).point);
    };

    auto drawShadow = [&](const QVector<QVector3D> &points) {
        if (points.isEmpty())
            return;

        QVector<QVector3D> groundPoints;

        for (const QVector3D &point : points) {
            groundPoints.append(QVector3D(point.x(), point.y(), 0.0f));
        }

        const QVector<QVector3D> hull = convexHullXY(groundPoints);
        if (hull.size() < 3)
            return;

        QPolygonF shadow;
        float averageHeight = 0.0f;

        for (const QVector3D &point : points)
            averageHeight += qMax(0.0f, point.z());

        averageHeight /= points.size();

        for (const QVector3D &point : hull)
            shadow.append(project(point).point);

        const QPointF screenOffset(5.0, 6.0);
        const int alpha = qBound(20, 42 + static_cast<int>(averageHeight * 0.08f), 54);
        painter.setBrush(QColor(0, 0, 0, alpha));
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(shadow.translated(screenOffset));
    };

    auto appendMesh = [&](QVector<Triangle2D> &triangles,
                          const SceneMesh &mesh,
                          const QColor &baseColor,
                          int shapeIndex,
                          bool drawMeshShadow = true,
                          bool hatched = false,
                          bool lit = true) {
        if (drawSceneMeshes && drawMeshShadow)
            drawShadow(mesh.shadowPoints);

        for (const MeshTriangle &meshTriangle : mesh.triangles) {
            const ProjectedPoint a = project(meshTriangle.a);
            const ProjectedPoint b = project(meshTriangle.b);
            const ProjectedPoint c = project(meshTriangle.c);

            Triangle2D triangle;
            triangle.a = a.point;
            triangle.b = b.point;
            triangle.c = c.point;
            triangle.depthA = a.depth;
            triangle.depthB = b.depth;
            triangle.depthC = c.depth;
            triangle.color = lit ? litColor(baseColor.lighter(meshTriangle.shade), meshTriangle.normal, lights) : baseColor;
            triangle.shapeIndex = shapeIndex;
            triangle.hatched = hatched;
            triangles.append(triangle);
        }
    };

    auto appendWireframe = [&](QVector<Line2D> &lines, const SceneMesh &mesh, const QColor &color) {
        for (const auto &edge : meshEdges(mesh)) {
            Line2D line;
            line.a = project(edge.first).point;
            line.b = project(edge.second).point;
            line.color = color;
            lines.append(line);
        }
    };

    auto appendProjectedHull = [&](QVector<QPair<QPolygonF, QColor>> &outlines,
                                   const SceneMesh &mesh,
                                   const QColor &color) {
        QVector<QPointF> points;
        points.reserve(mesh.triangles.size() * 3);
        for (const MeshTriangle &meshTriangle : mesh.triangles) {
            points.append(project(meshTriangle.a).point);
            points.append(project(meshTriangle.b).point);
            points.append(project(meshTriangle.c).point);
        }

        const QPolygonF hull = convexHull2D(points);
        if (hull.size() >= 3)
            outlines.append({hull, color});
    };

    auto appendCutFeatureEdges = [&](QVector<Line2D> &lines, const SceneMesh &mesh, const QColor &color) {
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
            edge.frontFacing.append(toCameraDirection(normal, m_cameraYaw, m_cameraPitch).z() < 0.0f);
        };

        QHash<QString, EdgeInfo> edges;
        for (const MeshTriangle &triangle : mesh.triangles) {
            appendEdge(&edges, triangle.a, triangle.b, triangle.normal);
            appendEdge(&edges, triangle.b, triangle.c, triangle.normal);
            appendEdge(&edges, triangle.c, triangle.a, triangle.normal);
        }

        for (const EdgeInfo &edge : edges) {
            bool useful = edge.normals.size() == 1;
            if (edge.normals.size() >= 2) {
                const float normalDot = qAbs(QVector3D::dotProduct(edge.normals[0], edge.normals[1]));
                const bool sharpCrease = normalDot < 0.74f;
                const bool silhouette = edge.frontFacing[0] != edge.frontFacing[1];
                useful = sharpCrease || silhouette;
            }

            if (!useful)
                continue;

            Line2D line;
            line.a = project(edge.from).point;
            line.b = project(edge.to).point;
            line.color = color;
            lines.append(line);
        }
    };

    if (drawSceneMeshes)
        drawGrid();
    QString csgStatus = "CSG preview: plain mesh";

    if (m_shapes) {
        const QSet<int> selectedShapeIds = selectedViewportShapeIds(m_scene,
                                                                    m_shapes,
                                                                    m_selectedIndex,
                                                                    m_selectedGroupId);
        const int selectedTreeNodeId = selectionHasTreeNodeId(m_scene, m_selectedGroupId) ? m_selectedGroupId : 0;
        const bool hasViewportSelection = !selectedShapeIds.isEmpty() || selectedTreeNodeId > 0;
        QVector<Triangle2D> triangles;
        QVector<Triangle2D> translucentHelperTriangles;
        QVector<Line2D> backgroundHelperLines;
        QVector<Line2D> foregroundHelperLines;
        QVector<QPair<QPolygonF, QColor>> cutHelperOutlines;

        if (m_draggingShape) {
            csgStatus = "CSG preview: paused while dragging";

            for (int i = 0; i < m_shapes->size(); ++i) {
                const ShapeNode &shape = m_shapes->at(i);
                QColor color = QColor(80, 160, 255);
                if (shape.booleanMode == ShapeNode::Subtract)
                    color = QColor(225, 95, 95);
                else if (shape.booleanMode == ShapeNode::Intersect)
                    color = QColor(150, 115, 240);

                const bool selected = selectedShapeIds.contains(shape.id);
                if (selected) {
                    if (shape.booleanMode == ShapeNode::Subtract)
                        color = QColor(255, 125, 80);
                    else if (shape.booleanMode == ShapeNode::Intersect)
                        color = QColor(185, 145, 255);
                    else
                        color = QColor(255, 180, 60);
                } else if (hasViewportSelection) {
                    color = dimmedViewportColor(color);
                }

                appendMesh(triangles, buildShapeMesh(shape), color, i);
            }
        } else {
            const CsgPreview &preview = m_cachedCsgPreview;
            csgStatus = m_csgComputing ? QStringLiteral("CSG preview: computing…") : preview.statusText;
            for (const CsgRenderItem &item : preview.items) {
                const bool selected = itemBelongsToSelection(item, m_shapes, selectedShapeIds, selectedTreeNodeId);
                QColor color = QColor(80, 160, 255);
                if (item.booleanMode == ShapeNode::Subtract)
                    color = QColor(225, 95, 95);
                else if (item.booleanMode == ShapeNode::Intersect)
                    color = QColor(150, 115, 240);

                if (item.computed)
                    color = item.color.isValid() ? item.color : viewportComputedSolidColor(m_darkViewportTheme, m_viewportColorVariant);
                else if (item.color.isValid())
                    color = item.color;

                if (selected) {
                    if (item.booleanMode == ShapeNode::Subtract)
                        color = QColor(255, 125, 80);
                    else if (item.booleanMode == ShapeNode::Intersect)
                        color = QColor(185, 145, 255);
                    else
                        color = item.computed ? viewportSelectedSolidColor(m_darkViewportTheme, m_viewportColorVariant) : QColor(255, 180, 60);
                } else if (hasViewportSelection) {
                    color = dimmedViewportColor(color);
                }

                if (item.helper) {
                    if (!drawSceneMeshes)
                        continue;

                    if (item.booleanMode == ShapeNode::Subtract) {
                        const bool selectedCut = selected;
                        QColor cutColor = selectedCut
                                              ? (m_darkViewportTheme ? QColor(188, 210, 218, 58) : QColor(190, 205, 212, 82))
                                              : (m_darkViewportTheme ? QColor(188, 210, 218, 14) : QColor(190, 205, 212, 22));
                        appendMesh(translucentHelperTriangles, item.mesh, cutColor, -1, false, true, false);
                        QColor cutEdgeColor = selectedCut
                                                  ? (m_darkViewportTheme ? QColor(176, 216, 232, 178) : QColor(42, 68, 84, 178))
                                                  : (m_darkViewportTheme ? QColor(176, 216, 232, 76) : QColor(42, 68, 84, 44));
                        appendProjectedHull(cutHelperOutlines, item.mesh, cutEdgeColor);
                        if (selectedCut)
                            appendCutFeatureEdges(foregroundHelperLines, item.mesh, cutEdgeColor);
                    } else if (selected) {
                        QColor selectedColor = QColor(255, 190, 70).lighter(115);
                        selectedColor.setAlpha(170);
                        appendWireframe(foregroundHelperLines, item.mesh, selectedColor);
                    }
                } else if (drawSceneMeshes) {
                    appendMesh(triangles, item.mesh, color, item.shapeIndex);
                }
            }
        }

        for (const Line2D &line : backgroundHelperLines) {
            painter.setPen(QPen(line.color, 0.7, Qt::DashLine, Qt::RoundCap));
            painter.drawLine(line.a, line.b);
        }

        if (drawSceneMeshes) {
            m_pickBufferSize = size();
            drawTrianglesWithDepth(&painter, triangles, size(), &m_pickBuffer, &m_depthBuffer, &m_renderImage);
        }

        drawTransparentTriangles(&painter, translucentHelperTriangles);

        for (const auto &outline : cutHelperOutlines) {
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(QPen(outline.second, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPolygon(outline.first);
        }

        for (const Line2D &line : foregroundHelperLines) {
            painter.setPen(QPen(line.color, 0.7, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(line.a, line.b);
        }

        const bool hasSelectedGroup = m_scene && m_selectedGroupId > 0 && m_scene->treeNodeById(m_selectedGroupId);
        if (hasSelectedGroup) {
            const SceneDocument::TreeNode *selectedGroup = m_scene->treeNodeById(m_selectedGroupId);
            const bool transformGroupSelected = selectedGroup->operation == SceneDocument::TreeNode::Translate
                                                || selectedGroup->operation == SceneDocument::TreeNode::Rotate
                                                || selectedGroup->operation == SceneDocument::TreeNode::Scale;
            if (transformGroupSelected) {
                const bool showMoveAxes = selectedGroup->operation == SceneDocument::TreeNode::Translate;
                const bool showRotationRings = selectedGroup->operation == SceneDocument::TreeNode::Rotate;
                const QVector3D origin = selectedTransformOrigin();
                const QVector<QPair<QVector3D, QColor>> axes = {
                    {QVector3D(38.0f, 0.0f, 0.0f), QColor(255, 95, 120)},
                    {QVector3D(0.0f, 38.0f, 0.0f), QColor(105, 245, 145)},
                    {QVector3D(0.0f, 0.0f, 38.0f), QColor(105, 180, 255)}
                };

                if (showMoveAxes) {
                    for (const auto &axis : axes) {
                        const QPointF start = project(origin).point;
                        const QPointF end = project(origin + selectedWorldAxisVector(axis.first)).point;
                        drawHaloLine(&painter, start, end, axis.second, 4.5);
                        drawArrowHead(&painter, start, end, axis.second);
                    }
                }

                const QVector<QPair<DragMode, QColor>> rings = {
                    {RotateXDrag, QColor(235, 80, 80, 185)},
                    {RotateYDrag, QColor(80, 210, 120, 185)},
                    {RotateZDrag, QColor(90, 155, 245, 185)}
                };

                if (showRotationRings) {
                    for (const auto &ring : rings) {
                        QPolygonF ringPath;
                        for (int step = 0; step <= 72; ++step) {
                            const QVector3D worldPoint = rotationRingPoint(origin, ring.first, 48.0f, step * 5.0f);
                            ringPath << project(worldPoint).point;
                        }

                        painter.setBrush(Qt::NoBrush);
                        drawHaloPolyline(&painter, ringPath, ring.second, 2.2);
                    }
                }
            }
        }
    }

    painter.setPen(viewportStatusTextColor(m_darkViewportTheme));
    painter.drawText(12, 24, "3D viewport: drag to orbit, wheel to zoom, drag selected axes to move or rings to rotate");
    painter.drawText(12, 42, QString("%1 | renderer: %2").arg(csgStatus, renderBackendName()));
    drawTreeTransformControlPreview(painter);
    drawTreeShapeParameterPreview(painter);
    drawAxisGizmo(painter);
}

void ViewportWidget::paintOpenGLGrid()
{
    if (!m_glLineProgram || !m_glLineProgram->isLinked() || !m_vboGrid.isCreated())
        return;

    // Grid colors depend on theme; rebuild VBO when theme changes.
    // We track this via m_vboGridCount being 0 after a theme change.
    // (Theme changes call update() but don't reset m_vboGridCount; rebuild is cheap.)
    // For simplicity we always use the VBO built at init and bake axis colors in;
    // the minor-grid grey is close enough between themes to skip per-theme rebuild.

    glDisable(GL_DEPTH_TEST);
    glLineWidth(1.0f);
    m_glLineProgram->bind();

    const QMatrix4x4 mvp = buildProjectionMatrix(float(width()), float(height()))
                          * buildViewMatrix(m_cameraYaw, m_cameraPitch, m_cameraDistance, m_cameraTarget);
    m_glLineProgram->setUniformValue("u_mvp", mvp);

    const int posLoc   = m_glLineProgram->attributeLocation("a_position");
    const int colorLoc = m_glLineProgram->attributeLocation("a_color");
    m_glLineProgram->enableAttributeArray(posLoc);
    m_glLineProgram->enableAttributeArray(colorLoc);

    m_vboGrid.bind();
    m_glLineProgram->setAttributeBuffer(posLoc,   GL_FLOAT, offsetof(OpenGLLineVertex, position), 3, sizeof(OpenGLLineVertex));
    m_glLineProgram->setAttributeBuffer(colorLoc, GL_FLOAT, offsetof(OpenGLLineVertex, color),    3, sizeof(OpenGLLineVertex));

    glDrawArrays(GL_LINES, 0, m_vboGridCount);

    m_vboGrid.release();
    m_glLineProgram->disableAttributeArray(posLoc);
    m_glLineProgram->disableAttributeArray(colorLoc);
    m_glLineProgram->release();
}

void ViewportWidget::paintOpenGLContactShadows()
{
    if (!m_shapes || !m_glFlatProgram || !m_glFlatProgram->isLinked() || !m_vboShadow.isCreated())
        return;

    const CsgPreview &preview = m_cachedCsgPreview;

    // Rebuild shadow VBO only when CSG fingerprint changes. Stores all 13 soft-shadow
    // samples in world space (Z=0 plane, per-sample XY offset baked into position).
    // Camera movement only updates the MVP uniform — zero CPU vertex work per frame.
    const QString fp = QString::number(m_cachedCsgFingerprint);
    if (fp != m_vboShadowKey) {
        struct Sample { float dx, dy; int alpha; };
        static constexpr Sample kSamples[] = {
            { 0.0f,  0.0f, 35},
            { 1.0f,  0.0f,  6}, {-1.0f,  0.0f,  6}, { 0.0f,  1.0f,  6}, { 0.0f, -1.0f,  6},
            { 0.7f,  0.7f,  5}, {-0.7f,  0.7f,  5}, { 0.7f, -0.7f,  5}, {-0.7f, -0.7f,  5},
            { 2.0f,  0.0f,  3}, {-2.0f,  0.0f,  3}, { 0.0f,  2.0f,  3}, { 0.0f, -2.0f,  3},
        };
        QVector<OpenGLFlatVertex> verts;
        for (const Sample &s : kSamples) {
            const QVector4D color = colorToVector4(QColor(0, 0, 0, s.alpha));
            for (const CsgRenderItem &item : preview.items) {
                if (item.helper) continue;
                for (const MeshTriangle &tri : item.mesh.triangles) {
                    if (tri.normal.z() <= 0.0f) continue;
                    verts.append({QVector3D(tri.a.x() + s.dx, tri.a.y() + s.dy, 0.0f), color});
                    verts.append({QVector3D(tri.b.x() + s.dx, tri.b.y() + s.dy, 0.0f), color});
                    verts.append({QVector3D(tri.c.x() + s.dx, tri.c.y() + s.dy, 0.0f), color});
                }
            }
        }
        m_vboShadow.bind();
        m_vboShadow.allocate(verts.constData(), verts.size() * int(sizeof(OpenGLFlatVertex)));
        m_vboShadow.release();
        m_vboShadowCount = verts.size();
        m_vboShadowKey   = fp;
    }

    if (m_vboShadowCount == 0)
        return;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_glFlatProgram->bind();

    const QMatrix4x4 mvp = buildProjectionMatrix(float(width()), float(height()))
                          * buildViewMatrix(m_cameraYaw, m_cameraPitch, m_cameraDistance, m_cameraTarget);
    m_glFlatProgram->setUniformValue("u_mvp",    mvp);
    m_glFlatProgram->setUniformValue("u_offset", QVector2D(0, 0));

    const int posLoc   = m_glFlatProgram->attributeLocation("a_position");
    const int colorLoc = m_glFlatProgram->attributeLocation("a_color");
    m_glFlatProgram->enableAttributeArray(posLoc);
    m_glFlatProgram->enableAttributeArray(colorLoc);

    m_vboShadow.bind();
    m_glFlatProgram->setAttributeBuffer(posLoc,   GL_FLOAT, offsetof(OpenGLFlatVertex, position), 3, sizeof(OpenGLFlatVertex));
    m_glFlatProgram->setAttributeBuffer(colorLoc, GL_FLOAT, offsetof(OpenGLFlatVertex, color),    4, sizeof(OpenGLFlatVertex));
    glDrawArrays(GL_TRIANGLES, 0, m_vboShadowCount);

    m_vboShadow.release();
    m_glFlatProgram->disableAttributeArray(posLoc);
    m_glFlatProgram->disableAttributeArray(colorLoc);
    m_glFlatProgram->release();
    glDisable(GL_BLEND);
}

void ViewportWidget::paintOpenGLPreview()
{
    if (!m_shapes || !m_glMeshProgram || !m_glMeshProgram->isLinked()
        || !m_vboMesh.isCreated())
        return;

    // During shape drag, shapes change every frame — rebuild VBO each time but don't
    // cache the key so the post-drag frame re-derives from CSG preview.
    const bool dragging = m_draggingShape;

    // m_cachedCsgFingerprint is updated asynchronously by onCsgPreviewReady();
    // no synchronous cache-warming needed here.

    const QSet<int> selectedShapeIds = selectedViewportShapeIds(m_scene,
                                                                m_shapes,
                                                                m_selectedIndex,
                                                                m_selectedGroupId);
    const int selectedTreeNodeId = selectionHasTreeNodeId(m_scene, m_selectedGroupId) ? m_selectedGroupId : 0;
    const bool hasViewportSelection = !selectedShapeIds.isEmpty() || selectedTreeNodeId > 0;
    const int pulseBucket = hasViewportSelection ? (m_selectionPulseFrame % 40) : 0;
    const float pulse = hasViewportSelection
                            ? 1.0f + 0.13f * qSin(static_cast<float>(pulseBucket) * 0.35f)
                            : 1.0f;

    const QString meshKey = dragging
        ? QString()
        : (QString::number(m_cachedCsgFingerprint)
           + "|" + QString::number(m_selectedIndex)
           + "|" + QString::number(m_selectedGroupId)
           + "|" + QString::number(pulseBucket)
           + "|" + (m_darkViewportTheme ? QChar('d') : QChar('l'))
           + "|" + QString::number(m_viewportColorVariant));

    if (meshKey != m_vboMeshKey) {
        QVector<OpenGLMeshVertex> verts;
        QVector<OpenGLFlatVertex> frontVerts;
        QVector<OpenGLFlatVertex> xrayVerts;

        auto appendMesh = [&](const SceneMesh &mesh, const QColor &baseColor) {
            for (const MeshTriangle &t : mesh.triangles) {
                const QVector3D col = colorToVector(baseColor.lighter(t.shade));
                verts.append({t.a, t.normal, col});
                verts.append({t.b, t.normal, col});
                verts.append({t.c, t.normal, col});
            }
        };

        if (dragging) {
            for (const ShapeNode &shape : *m_shapes) {
                QColor color(80, 160, 255);
                if (shape.booleanMode == ShapeNode::Subtract)
                    color = QColor(225, 95, 95);
                else if (shape.booleanMode == ShapeNode::Intersect)
                    color = QColor(150, 115, 240);
                if (selectedShapeIds.contains(shape.id))
                    color = pulsedViewportColor(QColor(255, 180, 60), pulse);
                else if (hasViewportSelection)
                    color = dimmedViewportColor(color);
                appendMesh(buildShapeMesh(shape), color);
            }
        } else {
            const CsgPreview &preview = m_cachedCsgPreview;

            for (const CsgRenderItem &item : preview.items) {
                const bool selected = itemBelongsToSelection(item, m_shapes, selectedShapeIds, selectedTreeNodeId);
                if (item.helper) {
                    if (item.booleanMode != ShapeNode::Subtract && !selected)
                        continue;
                    const bool sel = selected;
                    const QColor fc = sel
                        ? pulsedViewportColor(QColor(255, 188, 72, 92), pulse)
                        : (m_darkViewportTheme ? QColor(188, 210, 218, 22) : QColor(60, 90, 110, 28));
                    const QColor xc = sel
                        ? pulsedViewportColor(QColor(255, 188, 72, 34), pulse)
                        : (m_darkViewportTheme ? QColor(188, 210, 218,  8) : QColor(60, 90, 110, 12));
                    const QVector4D cf = colorToVector4(fc);
                    const QVector4D cx = colorToVector4(xc);
                    for (const MeshTriangle &t : item.mesh.triangles) {
                        frontVerts.append({t.a, cf});
                        frontVerts.append({t.b, cf});
                        frontVerts.append({t.c, cf});
                        xrayVerts.append({t.a, cx});
                        xrayVerts.append({t.b, cx});
                        xrayVerts.append({t.c, cx});
                    }
                    continue;
                }

                QColor color = item.computed
                    ? (item.color.isValid() ? item.color : viewportComputedSolidColor(m_darkViewportTheme, m_viewportColorVariant))
                    : viewportPlainSolidColor(m_darkViewportTheme, m_viewportColorVariant);
                if (!item.computed && item.color.isValid())
                    color = item.color;
                if (!item.computed && item.booleanMode == ShapeNode::Subtract)
                    color = QColor(225, 95, 95);
                else if (!item.computed && item.booleanMode == ShapeNode::Intersect)
                    color = QColor(150, 115, 240);
                if (selected)
                    color = item.computed
                        ? pulsedViewportColor(viewportSelectedSolidColor(m_darkViewportTheme, m_viewportColorVariant), pulse)
                        : pulsedViewportColor(QColor(255, 180, 60), pulse);
                else if (hasViewportSelection)
                    color = dimmedViewportColor(color);
                appendMesh(item.mesh, color);
            }
        }

        m_vboMesh.bind();
        m_vboMesh.allocate(verts.constData(), verts.size() * int(sizeof(OpenGLMeshVertex)));
        m_vboMesh.release();
        m_vboMeshCount = verts.size();

        m_vboHelperFront.bind();
        m_vboHelperFront.allocate(frontVerts.constData(), frontVerts.size() * int(sizeof(OpenGLFlatVertex)));
        m_vboHelperFront.release();
        m_vboHelperFrontCount = frontVerts.size();

        m_vboHelperXray.bind();
        m_vboHelperXray.allocate(xrayVerts.constData(), xrayVerts.size() * int(sizeof(OpenGLFlatVertex)));
        m_vboHelperXray.release();
        m_vboHelperXrayCount = xrayVerts.size();

        // Don't store key during drag so the next non-drag frame always rebuilds from CSG.
        if (!dragging)
            m_vboMeshKey = meshKey;
    }

    if (m_vboMeshCount == 0)
        return;

    const QMatrix4x4 V   = buildViewMatrix(m_cameraYaw, m_cameraPitch, m_cameraDistance, m_cameraTarget);
    const QMatrix4x4 P   = buildProjectionMatrix(float(width()), float(height()));
    const QMatrix4x4 mvp = P * V;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    // No face culling: faceNormal() uses a negated cross product (CW winding), and the
    // view matrix swaps world Y↔Z (det=-1), making screen-space winding inconsistent
    // across face orientations after perspective projection. Depth test alone correctly
    // hides interior back-faces of closed solid meshes without needing culling.
    glDisable(GL_CULL_FACE);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

    // Camera world position: V maps world→view so V^-1 * origin = camera world pos.
    const QVector3D cameraWorldPos = V.inverted().map(QVector3D(0.0f, 0.0f, 0.0f));

    m_glMeshProgram->bind();
    m_glMeshProgram->setUniformValue("u_mvp",        mvp);
    m_glMeshProgram->setUniformValue("u_camera_pos", cameraWorldPos);

    const QVector<SceneLight> lights = viewportLightsForPreset(m_lightingPreset);
    if (lights.size() >= 3) {
        // Lights and normals are both in world space — pass directions unchanged.
        auto setLight = [this, &lights](int i, const char *d, const char *c, const char *n) {
            m_glMeshProgram->setUniformValue(d, lights[i].direction);
            m_glMeshProgram->setUniformValue(c, colorToVector(lights[i].color));
            m_glMeshProgram->setUniformValue(n, lights[i].intensity);
        };
        setLight(0, "u_light_direction_a", "u_light_color_a", "u_light_intensity_a");
        setLight(1, "u_light_direction_b", "u_light_color_b", "u_light_intensity_b");
        setLight(2, "u_light_direction_c", "u_light_color_c", "u_light_intensity_c");
    }
    m_glMeshProgram->setUniformValue("u_ambient",          viewportAmbientForLightingPreset(m_lightingPreset));
    m_glMeshProgram->setUniformValue("u_specular_strength", viewportSpecularForLightingPreset(m_lightingPreset));

    const int posLoc    = m_glMeshProgram->attributeLocation("a_position");
    const int normalLoc = m_glMeshProgram->attributeLocation("a_normal");
    const int colorLoc  = m_glMeshProgram->attributeLocation("a_color");
    m_glMeshProgram->enableAttributeArray(posLoc);
    m_glMeshProgram->enableAttributeArray(normalLoc);
    m_glMeshProgram->enableAttributeArray(colorLoc);

    m_vboMesh.bind();
    m_glMeshProgram->setAttributeBuffer(posLoc,    GL_FLOAT, offsetof(OpenGLMeshVertex, position), 3, sizeof(OpenGLMeshVertex));
    m_glMeshProgram->setAttributeBuffer(normalLoc, GL_FLOAT, offsetof(OpenGLMeshVertex, normal),   3, sizeof(OpenGLMeshVertex));
    m_glMeshProgram->setAttributeBuffer(colorLoc,  GL_FLOAT, offsetof(OpenGLMeshVertex, color),    3, sizeof(OpenGLMeshVertex));
    glDrawArrays(GL_TRIANGLES, 0, m_vboMeshCount);
    m_vboMesh.release();

    m_glMeshProgram->disableAttributeArray(posLoc);
    m_glMeshProgram->disableAttributeArray(normalLoc);
    m_glMeshProgram->disableAttributeArray(colorLoc);
    m_glMeshProgram->release();

    const bool hasHelpers = (m_vboHelperFrontCount > 0 || m_vboHelperXrayCount > 0)
                            && m_glFlatProgram && m_glFlatProgram->isLinked();
    if (hasHelpers) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glEnable(GL_POLYGON_OFFSET_FILL);

        m_glFlatProgram->bind();
        m_glFlatProgram->setUniformValue("u_mvp",    mvp);
        m_glFlatProgram->setUniformValue("u_offset", QVector2D(0, 0));

        const int hPosLoc   = m_glFlatProgram->attributeLocation("a_position");
        const int hColorLoc = m_glFlatProgram->attributeLocation("a_color");
        m_glFlatProgram->enableAttributeArray(hPosLoc);
        m_glFlatProgram->enableAttributeArray(hColorLoc);

        auto drawHelperVbo = [&](QOpenGLBuffer &vbo, int count,
                                 GLenum depthFunc, float polyOff,
                                 GLint stencilRef, GLenum stencilFunc) {
            if (count == 0) return;
            glDepthFunc(depthFunc);
            glPolygonOffset(polyOff, polyOff);
            glStencilFunc(stencilFunc, stencilRef, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            vbo.bind();
            m_glFlatProgram->setAttributeBuffer(hPosLoc,   GL_FLOAT, offsetof(OpenGLFlatVertex, position), 3, sizeof(OpenGLFlatVertex));
            m_glFlatProgram->setAttributeBuffer(hColorLoc, GL_FLOAT, offsetof(OpenGLFlatVertex, color),    4, sizeof(OpenGLFlatVertex));
            glDrawArrays(GL_TRIANGLES, 0, count);
            vbo.release();
        };

        // Pass A — visible in holes: depth GL_LESS, stencil on solid pixels, slight forward offset
        drawHelperVbo(m_vboHelperFront, m_vboHelperFrontCount,
                      GL_LESS, -1.0f, 1, GL_EQUAL);
        // Pass B — x-ray through solid: GL_GREATER (nothing passes in empty space at depth=1)
        drawHelperVbo(m_vboHelperXray, m_vboHelperXrayCount,
                      GL_GREATER, 2.0f, 0, GL_ALWAYS);

        m_glFlatProgram->disableAttributeArray(hPosLoc);
        m_glFlatProgram->disableAttributeArray(hColorLoc);
        m_glFlatProgram->release();

        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(0.0f, 0.0f);
        glDisable(GL_BLEND);
    }

    glDisable(GL_STENCIL_TEST);
}

void ViewportWidget::drawAxisGizmo(QPainter &painter) const
{
    const QRectF panelRect(width() - 94.0, 14.0, 76.0, 76.0);
    const QPointF center = panelRect.center();
    const float axisLength = 27.0f;

    QVector<AxisGizmoAxis> axes = {
        {QStringLiteral("X"), QVector3D(1.0f, 0.0f, 0.0f), QColor(235, 80, 80), QPointF(), 0.0f},
        {QStringLiteral("Y"), QVector3D(0.0f, 1.0f, 0.0f), QColor(80, 210, 120), QPointF(), 0.0f},
        {QStringLiteral("Z"), QVector3D(0.0f, 0.0f, 1.0f), QColor(90, 155, 245), QPointF(), 0.0f}
    };

    for (AxisGizmoAxis &axis : axes) {
        const QVector3D cameraDirection = toCameraDirection(axis.direction, m_cameraYaw, m_cameraPitch);
        axis.end = center + QPointF(cameraDirection.x() * axisLength, -cameraDirection.y() * axisLength);
        axis.cameraDepth = cameraDirection.z();
    }

    std::sort(axes.begin(), axes.end(), [](const AxisGizmoAxis &left, const AxisGizmoAxis &right) {
        return left.cameraDepth < right.cameraDepth;
    });

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(255, 255, 255, 42), 1));
    painter.setBrush(QColor(10, 14, 20, 105));
    painter.drawRoundedRect(panelRect, 8.0, 8.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(235, 240, 245, 180));
    painter.drawEllipse(center, 3.0, 3.0);

    QFont labelFont = painter.font();
    labelFont.setBold(true);
    labelFont.setPointSize(qMax(7, labelFont.pointSize()));
    painter.setFont(labelFont);

    for (const AxisGizmoAxis &axis : axes) {
        QColor lineColor = axis.color;
        lineColor.setAlpha(axis.cameraDepth < 0.0f ? 120 : 235);
        painter.setPen(QPen(lineColor, axis.cameraDepth < 0.0f ? 2 : 3, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(center, axis.end);

        QColor dotColor = axis.color.lighter(axis.cameraDepth < 0.0f ? 105 : 118);
        dotColor.setAlpha(axis.cameraDepth < 0.0f ? 155 : 245);
        painter.setPen(QPen(axis.color.darker(135), 1));
        painter.setBrush(dotColor);
        painter.drawEllipse(axis.end, 8.0, 8.0);

        painter.setPen(QColor(255, 255, 255, axis.cameraDepth < 0.0f ? 170 : 245));
        painter.drawText(QRectF(axis.end.x() - 8.0, axis.end.y() - 8.0, 16.0, 16.0),
                         Qt::AlignCenter,
                         axis.label);
    }

    painter.restore();
}

void ViewportWidget::drawTreeTransformControlPreview(QPainter &painter) const
{
    if (!m_scene || m_treeTransformPreviewGroupId <= 0 || m_treeTransformPreviewAxis < 0)
        return;

    const SceneDocument::TreeNode *group = m_scene->treeNodeById(m_treeTransformPreviewGroupId);
    if (!group)
        return;

    const bool translatePreview = m_treeTransformPreviewOperation == SceneDocument::TreeNode::Translate;
    const bool rotatePreview = m_treeTransformPreviewOperation == SceneDocument::TreeNode::Rotate;
    const bool scalePreview = m_treeTransformPreviewOperation == SceneDocument::TreeNode::Scale;
    if (!translatePreview && !rotatePreview && !scalePreview)
        return;

    const QVector3D origin = transformOriginForGroup(m_treeTransformPreviewGroupId);
    QVector3D localAxis;
    QColor accent;
    if (m_treeTransformPreviewAxis == 0) {
        localAxis = QVector3D(1.0f, 0.0f, 0.0f);
        accent = QColor(255, 95, 120);
    } else if (m_treeTransformPreviewAxis == 1) {
        localAxis = QVector3D(0.0f, 1.0f, 0.0f);
        accent = QColor(105, 245, 145);
    } else {
        localAxis = QVector3D(0.0f, 0.0f, 1.0f);
        accent = QColor(105, 180, 255);
    }

    auto project = [&](const QVector3D &world) {
        return projectWorldPoint(world, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance, m_cameraTarget).point;
    };
    auto axisName = [&]() {
        if (m_treeTransformPreviewAxis == 0)
            return QStringLiteral("X");
        if (m_treeTransformPreviewAxis == 1)
            return QStringLiteral("Y");
        return QStringLiteral("Z");
    };
    auto axisValue = [&]() {
        const QVector3D values = translatePreview
                                     ? group->position
                                     : rotatePreview
                                           ? group->rotation
                                           : group->scale;
        if (m_treeTransformPreviewAxis == 0)
            return values.x();
        if (m_treeTransformPreviewAxis == 1)
            return values.y();
        return values.z();
    };
    const QString valueLabel = QStringLiteral("%1%2 %3")
                                   .arg(scalePreview ? QStringLiteral("S") : rotatePreview ? QStringLiteral("R") : QStringLiteral("T"),
                                        axisName(),
                                        formatPreviewValue(axisValue(), scalePreview ? 1 : -1));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (translatePreview || scalePreview) {
        QVector3D worldAxis = worldAxisVectorForGroup(m_treeTransformPreviewGroupId, localAxis);
        if (worldAxis.lengthSquared() <= 0.0001f)
            worldAxis = localAxis;
        worldAxis.normalize();

        const QPointF center = project(origin);
        const float axisLength = scalePreview ? 42.0f : 34.0f;
        const QPointF negative = project(origin - worldAxis * axisLength);
        const QPointF positive = project(origin + worldAxis * axisLength);
        drawHaloLine(&painter, negative, positive, accent, 4.0);
        drawArrowHead(&painter, center, positive, accent);
        drawArrowHead(&painter, center, negative, accent);
        drawDirectionLabel(&painter, positive, center, "+");
        drawDirectionLabel(&painter, negative, center, "-");
        drawValueLabel(&painter, center + QPointF(0.0, -28.0), valueLabel);
    } else {
        QVector<QPointF> arcPoints;
        const float radius = 48.0f;
        const QVector<SceneDocument::TreeNode> stack = parentGroupStackForGroup(m_treeTransformPreviewGroupId);
        for (int step = -7; step <= 7; ++step) {
            const float angle = qDegreesToRadians(step * 8.0f);
            const float c = qCos(angle) * radius;
            const float s = qSin(angle) * radius;
            QVector3D localPoint;
            if (m_treeTransformPreviewAxis == 0)
                localPoint = QVector3D(0.0f, c, s);
            else if (m_treeTransformPreviewAxis == 1)
                localPoint = QVector3D(c, 0.0f, s);
            else
                localPoint = QVector3D(c, s, 0.0f);

            arcPoints.append(project(origin + transformVectorByGroupStack(localPoint, stack)));
        }

        if (arcPoints.size() >= 2) {
            drawHaloPolyline(&painter, QPolygonF(arcPoints), accent, 3.0);
            drawArrowHead(&painter, arcPoints[1], arcPoints.first(), accent);
            drawArrowHead(&painter, arcPoints[arcPoints.size() - 2], arcPoints.last(), accent);
            const QPointF center = project(origin);
            drawDirectionLabel(&painter, arcPoints.last(), center, "+");
            drawDirectionLabel(&painter, arcPoints.first(), center, "-");
            drawValueLabel(&painter, center + QPointF(0.0, -62.0), valueLabel);
        }
    }

    painter.restore();
}

void ViewportWidget::drawTreeShapeParameterPreview(QPainter &painter) const
{
    if (!m_shapes || m_treeShapePreviewShapeId <= 0 || m_treeShapePreviewParameter < 0)
        return;

    const ShapeNode *shape = nullptr;
    for (const ShapeNode &candidate : *m_shapes) {
        if (candidate.id == m_treeShapePreviewShapeId) {
            shape = &candidate;
            break;
        }
    }

    if (!shape)
        return;

    auto project = [&](const QVector3D &world) {
        return projectWorldPoint(world, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance, m_cameraTarget).point;
    };

    QVector<SceneDocument::TreeNode> parentGroups;
    if (m_scene)
        collectParentGroupStackForShape(m_scene->treeRoot(), shape->id, &parentGroups);

    auto transformed = [&](const QVector3D &local) {
        const QVector3D shapeSpace = rotatePoint(local, shape->rotation) + shape->position;
        return transformPointByGroupStack(shapeSpace, parentGroups);
    };

    auto drawDimension = [&](const QVector3D &localAxis,
                             float halfLength,
                             float sideOffset,
                             const QColor &accent,
                             const QString &label,
                             float value) {
        if (halfLength <= 0.001f)
            return;

        QVector3D localSide;
        if (qAbs(localAxis.z()) > 0.5f)
            localSide = QVector3D(1.0f, 0.0f, 0.0f);
        else
            localSide = QVector3D(0.0f, 0.0f, 1.0f);

        const QVector3D negative = transformed(-localAxis * halfLength + localSide * sideOffset);
        const QVector3D positive = transformed(localAxis * halfLength + localSide * sideOffset);
        const QVector3D center = transformed(localSide * sideOffset);
        const QPointF negativePoint = project(negative);
        const QPointF positivePoint = project(positive);
        const QPointF centerPoint = project(center);

        drawHaloLine(&painter, negativePoint, positivePoint, accent, 2.6);
        drawArrowHead(&painter, centerPoint, positivePoint, accent, 12.0f, 5.0f, 2.0);
        drawArrowHead(&painter, centerPoint, negativePoint, accent, 12.0f, 5.0f, 2.0);
        drawDirectionLabel(&painter, positivePoint, centerPoint, "+");
        drawDirectionLabel(&painter, negativePoint, centerPoint, "-");
        drawValueLabel(&painter, centerPoint + QPointF(0.0, -24.0), QStringLiteral("%1 %2").arg(label, formatPreviewValue(value)));
    };

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (shape->type == ShapeNode::Cube) {
        QVector3D localAxis;
        QColor accent;
        float halfLength = 0.0f;
        if (m_treeShapePreviewParameter == 0) {
            localAxis = QVector3D(1.0f, 0.0f, 0.0f);
            accent = QColor(255, 95, 120);
            halfLength = shape->size.x() * 0.5f;
            drawDimension(localAxis, halfLength, 9.0f, accent, QStringLiteral("X"), shape->size.x());
        } else if (m_treeShapePreviewParameter == 1) {
            localAxis = QVector3D(0.0f, 1.0f, 0.0f);
            accent = QColor(105, 245, 145);
            halfLength = shape->size.y() * 0.5f;
            drawDimension(localAxis, halfLength, 9.0f, accent, QStringLiteral("Y"), shape->size.y());
        } else if (m_treeShapePreviewParameter == 2) {
            localAxis = QVector3D(0.0f, 0.0f, 1.0f);
            accent = QColor(105, 180, 255);
            halfLength = shape->size.z() * 0.5f;
            drawDimension(localAxis, halfLength, 9.0f, accent, QStringLiteral("Z"), shape->size.z());
        }
    } else if (shape->type == ShapeNode::Cylinder && m_treeShapePreviewParameter == 1) {
        drawDimension(QVector3D(0.0f, 0.0f, 1.0f), shape->height * 0.5f, shape->radius + 8.0f, QColor(105, 180, 255), QStringLiteral("H"), shape->height);
    } else {
        const float radius = shape->radius;
        if (radius > 0.001f) {
            const QColor accent(255, 190, 85);
            QVector<QPointF> circlePoints;
            for (int step = 0; step <= 48; ++step) {
                const float angle = qDegreesToRadians(step * 360.0f / 48.0f);
                circlePoints.append(project(transformed(QVector3D(qCos(angle) * radius, qSin(angle) * radius, 0.0f))));
            }

            drawHaloPolyline(&painter, QPolygonF(circlePoints), accent, 2.4);

            const QVector3D center = transformed(QVector3D());
            const QVector3D edge = transformed(QVector3D(radius, 0.0f, 0.0f));
            const QVector3D inward = transformed(QVector3D(radius * 0.45f, 0.0f, 0.0f));
            const QPointF centerPoint = project(center);
            const QPointF edgePoint = project(edge);
            const QPointF inwardPoint = project(inward);
            drawHaloLine(&painter, centerPoint, edgePoint, accent, 2.6);
            drawArrowHead(&painter, inwardPoint, edgePoint, accent, 12.0f, 5.0f, 2.0);
            drawArrowHead(&painter, inwardPoint, centerPoint, accent, 12.0f, 5.0f, 2.0);
            drawDirectionLabel(&painter, edgePoint, inwardPoint, "+");
            drawDirectionLabel(&painter, centerPoint, inwardPoint, "-");
            drawValueLabel(&painter, inwardPoint + QPointF(0.0, -24.0), QStringLiteral("R %1").arg(formatPreviewValue(radius)));
        }
    }

    painter.restore();
}

bool ViewportWidget::canUseOpenGLRenderBackend() const
{
    return true;
}

void ViewportWidget::updateSelectionPulseTimer()
{
    const bool hasSelection = (m_selectedIndex >= 0 || m_selectedGroupId > 0) && m_shapes;
    const bool shouldPulse = hasSelection && m_renderBackend == OpenGLRenderBackend;
    if (shouldPulse && !m_selectionPulseTimer.isActive()) {
        m_selectionPulseTimer.start();
    } else if (!shouldPulse && m_selectionPulseTimer.isActive()) {
        m_selectionPulseTimer.stop();
    }
}

void ViewportWidget::updateViewportControls()
{
    if (!m_openGLViewportCheckBox || !m_darkViewportCheckBox || !m_colorVariantComboBox || !m_lightingPresetComboBox)
        return;

    const bool openGLAvailable = canUseOpenGLRenderBackend();
    m_openGLViewportCheckBox->setEnabled(openGLAvailable);
    m_openGLViewportCheckBox->blockSignals(true);
    m_openGLViewportCheckBox->setChecked(m_renderBackend == OpenGLRenderBackend);
    m_openGLViewportCheckBox->blockSignals(false);

    m_darkViewportCheckBox->blockSignals(true);
    m_darkViewportCheckBox->setChecked(m_darkViewportTheme);
    m_darkViewportCheckBox->blockSignals(false);

    m_colorVariantComboBox->blockSignals(true);
    m_colorVariantComboBox->setCurrentIndex(qBound(0, m_viewportColorVariant, m_colorVariantComboBox->count() - 1));
    m_colorVariantComboBox->blockSignals(false);
    m_lightingPresetComboBox->blockSignals(true);
    m_lightingPresetComboBox->setCurrentIndex(qBound(0, m_lightingPreset, m_lightingPresetComboBox->count() - 1));
    m_lightingPresetComboBox->blockSignals(false);

    const QSize openGLSize = m_openGLViewportCheckBox->sizeHint();
    const QSize darkSize = m_darkViewportCheckBox->sizeHint();
    const QSize colorSize = m_colorVariantComboBox->sizeHint();
    const QSize lightingSize = m_lightingPresetComboBox->sizeHint();
    const int margin = 10;
    const int gap = 6;
    const int y = 54;
    int x = margin;
    m_openGLViewportCheckBox->setGeometry(x, y, openGLSize.width() + 10, openGLSize.height() + 2);
    x += m_openGLViewportCheckBox->width() + gap;
    m_darkViewportCheckBox->setGeometry(x, y, darkSize.width() + 10, darkSize.height() + 2);
    x += m_darkViewportCheckBox->width() + gap;
    m_colorVariantComboBox->setGeometry(x, y, qMax(92, colorSize.width() + 12), darkSize.height() + 2);
    x += m_colorVariantComboBox->width() + gap;
    m_lightingPresetComboBox->setGeometry(x, y, qMax(94, lightingSize.width() + 12), darkSize.height() + 2);

    m_openGLViewportCheckBox->raise();
    m_darkViewportCheckBox->raise();
    m_colorVariantComboBox->raise();
    m_lightingPresetComboBox->raise();
}

QVector<SceneDocument::TreeNode> ViewportWidget::parentGroupStackForGroup(int groupId) const
{
    QVector<SceneDocument::TreeNode> groupStack;
    if (m_scene && groupId > 0)
        collectParentGroupStackForGroup(m_scene->treeRoot(), groupId, &groupStack);
    return groupStack;
}

QVector3D ViewportWidget::transformOriginForGroup(int groupId) const
{
    if (!m_scene || groupId <= 0)
        return QVector3D();

    const SceneDocument::TreeNode *group = m_scene->treeNodeById(groupId);
    if (!group)
        return QVector3D();

    return transformPointByGroupStack(group->position, parentGroupStackForGroup(groupId));
}

QVector3D ViewportWidget::worldAxisVectorForGroup(int groupId, const QVector3D &localAxis) const
{
    return transformVectorByGroupStack(localAxis, parentGroupStackForGroup(groupId));
}

QVector<SceneDocument::TreeNode> ViewportWidget::selectedParentGroupStack() const
{
    QVector<SceneDocument::TreeNode> groupStack;
    if (!m_scene)
        return groupStack;

    if (m_shapes && m_selectedIndex >= 0 && m_selectedIndex < m_shapes->size()) {
        const int shapeId = m_shapes->at(m_selectedIndex).id;
        collectParentGroupStackForShape(m_scene->treeRoot(), shapeId, &groupStack);
        return groupStack;
    }

    if (m_selectedGroupId > 0)
        collectParentGroupStackForGroup(m_scene->treeRoot(), m_selectedGroupId, &groupStack);

    return groupStack;
}

QVector3D ViewportWidget::selectedTransformOrigin() const
{
    const QVector<SceneDocument::TreeNode> parentGroups = selectedParentGroupStack();

    if (m_shapes && m_selectedIndex >= 0 && m_selectedIndex < m_shapes->size())
        return transformPointByGroupStack(m_shapes->at(m_selectedIndex).position, parentGroups);

    if (m_scene && m_selectedGroupId > 0) {
        if (const SceneDocument::TreeNode *group = m_scene->treeNodeById(m_selectedGroupId))
            return transformPointByGroupStack(group->position, parentGroups);
    }

    return QVector3D();
}

QVector3D ViewportWidget::selectedWorldAxisVector(const QVector3D &localAxis) const
{
    return transformVectorByGroupStack(localAxis, selectedParentGroupStack());
}

QVector3D ViewportWidget::selectedLocalDeltaFromWorldDelta(const QVector3D &worldDelta) const
{
    return inverseTransformVectorByGroupStack(worldDelta, selectedParentGroupStack());
}

bool ViewportWidget::pickSelectedTransformAxis(const QPoint &position, DragMode *dragMode) const
{
    const SceneDocument::TreeNode *selectedGroup = m_scene && m_selectedGroupId > 0
                                                       ? m_scene->treeNodeById(m_selectedGroupId)
                                                       : nullptr;
    if (!selectedGroup)
        return false;

    if (selectedGroup->operation != SceneDocument::TreeNode::Translate
        && selectedGroup->operation != SceneDocument::TreeNode::Rotate) {
        return false;
    }

    const bool allowMoveAxes = selectedGroup->operation == SceneDocument::TreeNode::Translate;
    const bool allowRotationRings = selectedGroup->operation == SceneDocument::TreeNode::Rotate;
    const QVector3D origin = selectedTransformOrigin();
    float bestDistance = 9.0f;
    DragMode pickedAxis = NoDrag;
    const QPointF start = projectWorldPoint(origin, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance, m_cameraTarget).point;

    const QVector<QPair<DragMode, QVector3D>> axes = {
        {AxisXDrag, selectedWorldAxisVector(QVector3D(36.0f, 0.0f, 0.0f))},
        {AxisYDrag, selectedWorldAxisVector(QVector3D(0.0f, 36.0f, 0.0f))},
        {AxisZDrag, selectedWorldAxisVector(QVector3D(0.0f, 0.0f, 36.0f))}
    };

    if (allowMoveAxes) {
        for (const auto &axis : axes) {
            const QPointF end = projectWorldPoint(origin + axis.second, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance, m_cameraTarget).point;
            const float distance = distanceToSegment(position, start, end);
            if (distance < bestDistance) {
                bestDistance = distance;
                pickedAxis = axis.first;
            }
        }
    }

    const QVector<DragMode> rings = {RotateXDrag, RotateYDrag, RotateZDrag};
    if (allowRotationRings) {
        for (DragMode ring : rings) {
            QPointF previous;
            bool hasPrevious = false;

            for (int step = 0; step <= 72; ++step) {
                const QVector3D worldPoint = rotationRingPoint(origin, ring, 48.0f, step * 5.0f);
                const QPointF current = projectWorldPoint(worldPoint, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance, m_cameraTarget).point;
                if (hasPrevious) {
                    const float distance = distanceToSegment(position, previous, current);
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        pickedAxis = ring;
                    }
                }

                previous = current;
                hasPrevious = true;
            }
        }
    }

    if (pickedAxis == NoDrag)
        return false;

    *dragMode = pickedAxis;
    return true;
}

QVector3D ViewportWidget::dragDeltaForMousePosition(const QPoint &position) const
{
    const QPoint pixelDelta = position - m_dragStartMousePosition;
    const float worldUnitsPerPixel = m_cameraDistance / 420.0f;
    QVector3D worldDelta(pixelDelta.x() * worldUnitsPerPixel,
                         -pixelDelta.y() * worldUnitsPerPixel,
                         0.0f);

    if (m_dragMode == PlaneDrag)
        return selectedLocalDeltaFromWorldDelta(worldDelta);

    QVector3D axisVector;
    if (m_dragMode == AxisXDrag)
        axisVector = QVector3D(1.0f, 0.0f, 0.0f);
    else if (m_dragMode == AxisYDrag)
        axisVector = QVector3D(0.0f, 1.0f, 0.0f);
    else if (m_dragMode == AxisZDrag)
        axisVector = QVector3D(0.0f, 0.0f, 1.0f);

    const QVector3D origin = selectedTransformOrigin();
    const QPointF screenOrigin = projectWorldPoint(origin, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance, m_cameraTarget).point;
    const QPointF screenEnd = projectWorldPoint(origin + selectedWorldAxisVector(axisVector * 36.0f),
                                                size(),
                                                m_cameraYaw,
                                                m_cameraPitch,
                                                m_cameraDistance,
                                                m_cameraTarget).point;
    QVector2D screenAxis(screenEnd - screenOrigin);

    if (screenAxis.lengthSquared() <= 0.0001f)
        return QVector3D();

    screenAxis.normalize();
    const float screenAmount = QVector2D::dotProduct(QVector2D(pixelDelta), screenAxis);
    return axisVector * screenAmount * worldUnitsPerPixel;
}

QVector3D ViewportWidget::rotationDeltaForMousePosition(const QPoint &position) const
{
    if (!isRotationDragMode(m_dragMode))
        return QVector3D();

    QVector2D tangent = m_rotationDragScreenTangent;
    if (tangent.lengthSquared() <= 0.0001f) {
        tangent = QVector2D(1.0f, 0.0f);
    } else {
        tangent.normalize();
    }

    const QPoint pixelDelta = position - m_dragStartMousePosition;
    const float screenAmount = QVector2D::dotProduct(QVector2D(pixelDelta), tangent);
    return rotationVectorForMode(m_dragMode, screenAmount * 0.75f);
}

bool ViewportWidget::isRotationDragMode(DragMode dragMode) const
{
    return dragMode == RotateXDrag || dragMode == RotateYDrag || dragMode == RotateZDrag;
}

void ViewportWidget::mousePressEvent(QMouseEvent *event)
{
    m_lastMousePosition = event->pos();
    m_emptyClickCandidate = false;

    if (event->button() == Qt::RightButton) {
        m_panningViewport = true;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        DragMode pickedAxis = NoDrag;
        if (pickSelectedTransformAxis(event->pos(), &pickedAxis)) {
            m_dragMode = pickedAxis;
            m_dragStartMousePosition = event->pos();
            m_lastDragDelta = QVector3D();
            m_lastRotationDelta = QVector3D();

            const QPointF screenOrigin = projectWorldPoint(selectedTransformOrigin(),
                                                           size(),
                                                           m_cameraYaw,
                                                           m_cameraPitch,
                                                           m_cameraDistance,
                                                           m_cameraTarget).point;
            QVector2D radiusVector(QPointF(event->pos()) - screenOrigin);
            m_rotationDragScreenTangent = QVector2D(-radiusVector.y(), radiusVector.x());

            if (m_selectedGroupId > 0) {
                m_draggingGroup = true;
                m_dragGroupId = m_selectedGroupId;
                if (isRotationDragMode(m_dragMode))
                    emit groupRotationDragStarted(m_selectedGroupId);
                else
                    emit groupDragStarted(m_selectedGroupId);
            } else {
                m_draggingShape = true;
                m_dragShapeIndex = m_selectedIndex;
                if (isRotationDragMode(m_dragMode))
                    emit shapeRotationDragStarted(m_selectedIndex);
                else
                    emit shapeDragStarted(m_selectedIndex);
            }
            return;
        }
    }

    if (event->button() == Qt::LeftButton && m_shapes) {
        const CsgPreview &preview = m_cachedCsgPreview;
        int helperShapeIndex = -1;
        float bestDistance = 8.0f;

        for (const CsgRenderItem &item : preview.items) {
            if (!item.helper)
                continue;

            for (const auto &edge : meshEdges(item.mesh)) {
                const QPointF a = projectWorldPoint(edge.first, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance, m_cameraTarget).point;
                const QPointF b = projectWorldPoint(edge.second, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance, m_cameraTarget).point;
                const float distance = distanceToSegment(event->pos(), a, b);

                if (distance < bestDistance) {
                    bestDistance = distance;
                    helperShapeIndex = item.shapeIndex;
                }
            }
        }

        if (helperShapeIndex >= 0) {
            emit shapeClicked(helperShapeIndex);
            if (event->modifiers() & Qt::ShiftModifier) {
                m_draggingShape = true;
                m_dragMode = PlaneDrag;
                m_dragShapeIndex = helperShapeIndex;
                m_dragStartMousePosition = event->pos();
                m_lastDragDelta = QVector3D();
                emit shapeDragStarted(helperShapeIndex);
            }
            return;
        }
    }

    int shapeIndex = -1;
    if (event->button() == Qt::LeftButton
        && m_pickBufferSize == size()
        && event->pos().x() >= 0
        && event->pos().x() < m_pickBufferSize.width()
        && event->pos().y() >= 0
        && event->pos().y() < m_pickBufferSize.height()) {
        const int bufferIndex = event->pos().y() * m_pickBufferSize.width() + event->pos().x();

        if (bufferIndex >= 0 && bufferIndex < m_pickBuffer.size()) {
            shapeIndex = m_pickBuffer[bufferIndex];
        }
    }

    if (shapeIndex < 0) {
        if (event->button() == Qt::LeftButton) {
            m_emptyClickCandidate = true;
            m_emptyClickStartPosition = event->pos();
        }
        return;
    }

    emit shapeClicked(shapeIndex);

    if (event->modifiers() & Qt::ShiftModifier) {
        m_draggingShape = true;
        m_dragMode = PlaneDrag;
        m_dragShapeIndex = shapeIndex;
        m_dragStartMousePosition = event->pos();
        m_lastDragDelta = QVector3D();
        emit shapeDragStarted(shapeIndex);
    }
}

void ViewportWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panningViewport && (event->buttons() & Qt::RightButton)) {
        const QPoint delta = event->pos() - m_lastMousePosition;
        const float worldUnitsPerPixel = m_cameraDistance / 420.0f;
        const QVector3D right = cameraRightVector(m_cameraYaw);
        const QVector3D up = cameraUpVector(m_cameraYaw, m_cameraPitch);
        m_cameraTarget += (-right * delta.x() + up * delta.y()) * worldUnitsPerPixel;
        m_lastMousePosition = event->pos();
        update();
        event->accept();
        return;
    }

    if ((m_draggingShape || m_draggingGroup) && (event->buttons() & Qt::LeftButton)) {
        if (isRotationDragMode(m_dragMode)) {
            const QVector3D rotationDelta = rotationDeltaForMousePosition(event->pos());

            if ((rotationDelta - m_lastRotationDelta).lengthSquared() < 0.0001f) {
                m_lastMousePosition = event->pos();
                return;
            }

            m_lastRotationDelta = rotationDelta;
            if (m_draggingGroup)
                emit groupRotated(m_dragGroupId, rotationDelta);
            else
                emit shapeRotated(m_dragShapeIndex, rotationDelta);
            m_lastMousePosition = event->pos();
            return;
        }

        const QVector3D worldDelta = dragDeltaForMousePosition(event->pos());

        if ((worldDelta - m_lastDragDelta).lengthSquared() < 0.0001f) {
            m_lastMousePosition = event->pos();
            return;
        }

        m_lastDragDelta = worldDelta;
        if (m_draggingGroup)
            emit groupDragged(m_dragGroupId, worldDelta);
        else
            emit shapeDragged(m_dragShapeIndex, worldDelta);
        m_lastMousePosition = event->pos();
        return;
    }

    if (event->buttons() & Qt::LeftButton) {
        if (m_emptyClickCandidate
            && (event->pos() - m_emptyClickStartPosition).manhattanLength() > 3) {
            m_emptyClickCandidate = false;
        }

        const QPoint delta = event->pos() - m_lastMousePosition;
        m_cameraYaw = normalizedDegrees(m_cameraYaw - delta.x() * 0.45f);
        m_cameraPitch = normalizedDegrees(m_cameraPitch + delta.y() * 0.35f);
        update();
    }

    m_lastMousePosition = event->pos();
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton && m_panningViewport) {
        m_panningViewport = false;
        unsetCursor();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && (m_draggingShape || m_draggingGroup)) {
        m_emptyClickCandidate = false;
        const int shapeIndex = m_dragShapeIndex;
        const int groupId = m_dragGroupId;
        const bool wasDraggingGroup = m_draggingGroup;
        const bool wasRotating = isRotationDragMode(m_dragMode);
        m_draggingShape = false;
        m_draggingGroup = false;
        m_dragMode = NoDrag;
        m_dragShapeIndex = -1;
        m_dragGroupId = 0;
        m_rotationDragScreenTangent = QVector2D();
        if (wasDraggingGroup) {
            if (wasRotating)
                emit groupRotationDragFinished(groupId);
            else
                emit groupDragFinished(groupId);
        } else {
            if (wasRotating)
                emit shapeRotationDragFinished(shapeIndex);
            else
                emit shapeDragFinished(shapeIndex);
        }
        return;
    }

    if (event->button() == Qt::LeftButton && m_emptyClickCandidate) {
        const bool clickedWithoutDrag = (event->pos() - m_emptyClickStartPosition).manhattanLength() <= 3;
        m_emptyClickCandidate = false;
        if (clickedWithoutDrag)
            emit emptyClicked();
    }
}

void ViewportWidget::wheelEvent(QWheelEvent *event)
{
    m_cameraDistance -= event->angleDelta().y() * 0.12f;
    m_cameraDistance = qBound(70.0f, m_cameraDistance, 700.0f);
    update();
}
