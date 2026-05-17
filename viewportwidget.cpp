#include "viewportwidget.h"

#include "scenemesh.h"

#include <QHash>
#include <QMouseEvent>
#include <QPainter>
#include <QOpenGLShaderProgram>
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
    QVector3D position;
    QVector3D normal;
    QVector3D color;
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

static QVector3D toCameraPoint(const QVector3D &world, float yawDegrees, float pitchDegrees, float cameraDistance)
{
    const float yaw = qDegreesToRadians(yawDegrees);
    const float pitch = qDegreesToRadians(pitchDegrees);
    QVector3D p = world;

    p = QVector3D(
        p.x() * qCos(yaw) + p.z() * qSin(yaw),
        p.y(),
        -p.x() * qSin(yaw) + p.z() * qCos(yaw));

    p = QVector3D(
        p.x(),
        p.y() * qCos(pitch) - p.z() * qSin(pitch),
        p.y() * qSin(pitch) + p.z() * qCos(pitch));

    p.setZ(p.z() + cameraDistance);
    return p;
}

static QVector3D toCameraDirection(const QVector3D &world, float yawDegrees, float pitchDegrees)
{
    return toCameraPoint(world, yawDegrees, pitchDegrees, 0.0f);
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
    }

    return vector;
}

static QVector3D inverseTransformVectorByGroupStack(QVector3D vector, const QVector<SceneDocument::TreeNode> &groupStack)
{
    for (const SceneDocument::TreeNode &group : groupStack) {
        if (group.operation == SceneDocument::TreeNode::Rotate)
            vector = inverseRotatePoint(vector, group.rotation);
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
                                        float cameraDistance)
{
    const float focalLength = 420.0f;
    ProjectedPoint projected;
    const QVector3D camera = toCameraPoint(world, yawDegrees, pitchDegrees, cameraDistance);
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

static void drawArrowHead(QPainter *painter, const QPointF &start, const QPointF &end, const QColor &color)
{
    QVector2D direction(end - start);
    if (direction.lengthSquared() <= 0.0001f)
        return;

    direction.normalize();
    const QVector2D normal(-direction.y(), direction.x());
    const float length = 18.0f;
    const float width = 7.5f;

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

    painter->setPen(QPen(QColor(5, 8, 12, 190), 3));
    painter->setBrush(QColor(5, 8, 12, 150));
    painter->drawPolygon(QPolygonF() << tip << left << ridge << right);

    painter->setPen(QPen(color.darker(135), 1.2));
    painter->setBrush(lightSide);
    painter->drawPolygon(lightFace);

    painter->setBrush(darkSide);
    painter->drawPolygon(darkFace);
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

static QColor litColor(const QColor &baseColor, const QVector3D &normal, const QVector<SceneLight> &lights)
{
    float red = baseColor.redF() * 0.22f;
    float green = baseColor.greenF() * 0.22f;
    float blue = baseColor.blueF() * 0.22f;

    for (const SceneLight &light : lights) {
        const float amount = qMax(0.0f, QVector3D::dotProduct(normal, light.direction.normalized())) * light.intensity;
        red += baseColor.redF() * light.color.redF() * amount;
        green += baseColor.greenF() * light.color.greenF() * amount;
        blue += baseColor.blueF() * light.color.blueF() * amount;
    }

    return QColor(
        clampColorChannel(red * 255.0f),
        clampColorChannel(green * 255.0f),
        clampColorChannel(blue * 255.0f));
}

static QVector3D colorToVector(const QColor &color)
{
    return QVector3D(color.redF(), color.greenF(), color.blueF());
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
    seed = qHash(node.position.x(), seed);
    seed = qHash(node.position.y(), seed);
    seed = qHash(node.position.z(), seed);
    seed = qHash(node.rotation.x(), seed);
    seed = qHash(node.rotation.y(), seed);
    seed = qHash(node.rotation.z(), seed);
    seed = qHash(node.children.size(), seed);

    for (const SceneDocument::TreeNode &child : node.children)
        seed = treeFingerprint(child, seed);

    return seed;
}

static uint sceneFingerprint(const SceneDocument &scene)
{
    return treeFingerprint(scene.treeRoot(), shapesFingerprint(scene.shapes()));
}

static const CsgPreview &cachedCsgPreview(const QVector<ShapeNode> &shapes,
                                          CsgPreview *cache,
                                          uint *cachedFingerprint,
                                          bool *dirty)
{
    const uint fingerprint = shapesFingerprint(shapes);
    if (*dirty || fingerprint != *cachedFingerprint) {
        *cache = buildCsgPreview(shapes);
        *cachedFingerprint = fingerprint;
        *dirty = false;
    }

    return *cache;
}

static const CsgPreview &cachedCsgPreview(const SceneDocument &scene,
                                          CsgPreview *cache,
                                          uint *cachedFingerprint,
                                          bool *dirty)
{
    const uint fingerprint = sceneFingerprint(scene);
    if (*dirty || fingerprint != *cachedFingerprint) {
        *cache = buildCsgPreview(scene);
        *cachedFingerprint = fingerprint;
        *dirty = false;
    }

    return *cache;
}

ViewportWidget::ViewportWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(500, 400);
    setFocusPolicy(Qt::StrongFocus);
}

void ViewportWidget::setScene(const SceneDocument *scene)
{
    m_scene = scene;
    m_shapes = scene ? &scene->shapes() : nullptr;
    invalidateCsgPreview();
    update();
}

void ViewportWidget::setShapes(const QVector<ShapeNode> *shapes)
{
    m_scene = nullptr;
    m_shapes = shapes;
    invalidateCsgPreview();
    update();
}

void ViewportWidget::setSelectedIndex(int index)
{
    m_selectedIndex = index;
    if (index >= 0)
        m_selectedGroupId = 0;
    update();
}

void ViewportWidget::setSelectedGroupId(int groupId)
{
    m_selectedGroupId = groupId;
    if (groupId > 0)
        m_selectedIndex = -1;
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

void ViewportWidget::setRenderBackend(RenderBackend backend)
{
    if (backend == OpenGLRenderBackend && !canUseOpenGLRenderBackend())
        backend = SoftwareRenderBackend;

    if (m_renderBackend == backend)
        return;

    m_renderBackend = backend;
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

void ViewportWidget::invalidateCsgPreview()
{
    m_csgPreviewDirty = true;
}

void ViewportWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.12f, 0.13f, 0.15f, 1.0f);

    m_glMeshProgram = new QOpenGLShaderProgram(this);
    m_glMeshProgram->addShaderFromSourceCode(
        QOpenGLShader::Vertex,
        "attribute vec3 a_position;\n"
        "attribute vec3 a_normal;\n"
        "attribute vec3 a_color;\n"
        "varying vec3 v_normal;\n"
        "varying vec3 v_color;\n"
        "void main() {\n"
        "    gl_Position = vec4(a_position, 1.0);\n"
        "    v_normal = normalize(a_normal);\n"
        "    v_color = a_color;\n"
        "}\n");
    m_glMeshProgram->addShaderFromSourceCode(
        QOpenGLShader::Fragment,
        "#ifdef GL_ES\n"
        "precision mediump float;\n"
        "#endif\n"
        "varying vec3 v_normal;\n"
        "varying vec3 v_color;\n"
        "void main() {\n"
        "    vec3 n = normalize(v_normal);\n"
        "    vec3 lightA = normalize(vec3(-0.45, -0.35, 1.0));\n"
        "    vec3 lightB = normalize(vec3(0.85, 0.15, 0.45));\n"
        "    vec3 lightC = normalize(vec3(-0.2, 0.9, 0.25));\n"
        "    float amount = 0.22;\n"
        "    amount += max(0.0, dot(n, lightA)) * 0.78;\n"
        "    amount += max(0.0, dot(n, lightB)) * 0.34;\n"
        "    amount += max(0.0, dot(n, lightC)) * 0.24;\n"
        "    gl_FragColor = vec4(clamp(v_color * amount, 0.0, 1.0), 1.0);\n"
        "}\n");
    m_glMeshProgram->link();
}

void ViewportWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void ViewportWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const bool useOpenGLPreview = m_renderBackend == OpenGLRenderBackend && canUseOpenGLRenderBackend();
    if (useOpenGLPreview)
        paintOpenGLPreview();

    glDisable(GL_DEPTH_TEST);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    paintSoftware(painter, !useOpenGLPreview);

    painter.end();
}

void ViewportWidget::paintSoftware(QPainter &painter, bool drawSceneMeshes)
{
    if (drawSceneMeshes)
        painter.fillRect(rect(), QColor(30, 32, 36));

    const QVector<SceneLight> lights = {
        {QVector3D(-0.45f, -0.35f, 1.0f).normalized(), QColor(255, 244, 214), 0.78f},
        {QVector3D(0.85f, 0.15f, 0.45f).normalized(), QColor(160, 205, 255), 0.34f},
        {QVector3D(-0.2f, 0.9f, 0.25f).normalized(), QColor(255, 170, 110), 0.24f}
    };

    auto project = [&](const QVector3D &world) {
        return projectWorldPoint(world, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance);
    };

    auto drawGrid = [&]() {
        painter.setPen(QColor(70, 74, 82));

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

        const QVector3D lightDirection(-0.45f, -0.35f, 1.0f);
        QVector<QVector3D> groundPoints;
        QVector3D center;

        for (const QVector3D &point : points) {
            const float height = qMax(0.0f, point.z());
            const QVector3D shadowPoint(
                point.x() - lightDirection.x() * height / lightDirection.z(),
                point.y() - lightDirection.y() * height / lightDirection.z(),
                0.0f);

            groundPoints.append(shadowPoint);
            center += shadowPoint;
        }

        center /= groundPoints.size();

        std::sort(groundPoints.begin(), groundPoints.end(), [center](const QVector3D &left, const QVector3D &right) {
            return qAtan2(left.y() - center.y(), left.x() - center.x())
                   < qAtan2(right.y() - center.y(), right.x() - center.x());
        });

        QPolygonF shadow;
        float averageHeight = 0.0f;

        for (const QVector3D &point : points)
            averageHeight += qMax(0.0f, point.z());

        averageHeight /= points.size();

        for (const QVector3D &point : groundPoints)
            shadow.append(project(point).point);

        const int alpha = qBound(28, 52 + static_cast<int>(averageHeight * 0.55f), 96);
        painter.setBrush(QColor(0, 0, 0, alpha));
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(shadow);
    };

    auto appendMesh = [&](QVector<Triangle2D> &triangles, const SceneMesh &mesh, const QColor &baseColor, int shapeIndex) {
        if (drawSceneMeshes)
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
            triangle.color = litColor(baseColor.lighter(meshTriangle.shade), meshTriangle.normal, lights);
            triangle.shapeIndex = shapeIndex;
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

    drawGrid();
    QString csgStatus = "CSG preview: plain mesh";

    if (m_shapes) {
        QVector<Triangle2D> triangles;
        QVector<Line2D> backgroundHelperLines;
        QVector<Line2D> foregroundHelperLines;

        if (m_draggingShape) {
            csgStatus = "CSG preview: paused while dragging";

            for (int i = 0; i < m_shapes->size(); ++i) {
                const ShapeNode &shape = m_shapes->at(i);
                QColor color = QColor(80, 160, 255);
                if (shape.booleanMode == ShapeNode::Subtract)
                    color = QColor(225, 95, 95);
                else if (shape.booleanMode == ShapeNode::Intersect)
                    color = QColor(150, 115, 240);

                if (i == m_selectedIndex) {
                    if (shape.booleanMode == ShapeNode::Subtract)
                        color = QColor(255, 125, 80);
                    else if (shape.booleanMode == ShapeNode::Intersect)
                        color = QColor(185, 145, 255);
                    else
                        color = QColor(255, 180, 60);
                }

                appendMesh(triangles, buildShapeMesh(shape), color, i);
            }
        } else {
            const CsgPreview &preview = m_scene
                                            ? cachedCsgPreview(*m_scene,
                                                               &m_cachedCsgPreview,
                                                               &m_cachedCsgFingerprint,
                                                               &m_csgPreviewDirty)
                                            : cachedCsgPreview(*m_shapes,
                                                               &m_cachedCsgPreview,
                                                               &m_cachedCsgFingerprint,
                                                               &m_csgPreviewDirty);
            csgStatus = preview.statusText;
            for (const CsgRenderItem &item : preview.items) {
                QColor color = QColor(80, 160, 255);
                if (item.booleanMode == ShapeNode::Subtract)
                    color = QColor(225, 95, 95);
                else if (item.booleanMode == ShapeNode::Intersect)
                    color = QColor(150, 115, 240);

                if (item.computed)
                    color = QColor(95, 185, 155);

                if (item.shapeIndex == m_selectedIndex) {
                    if (item.booleanMode == ShapeNode::Subtract)
                        color = QColor(255, 125, 80);
                    else if (item.booleanMode == ShapeNode::Intersect)
                        color = QColor(185, 145, 255);
                    else
                        color = item.computed ? QColor(115, 220, 180) : QColor(255, 180, 60);
                }

                if (item.helper) {
                    if (item.shapeIndex == m_selectedIndex) {
                        QColor selectedColor = color.lighter(115);
                        selectedColor.setAlpha(215);
                        appendWireframe(foregroundHelperLines, item.mesh, selectedColor);
                    } else {
                        QColor quietColor = color.lighter(95);
                        quietColor.setAlpha(75);
                        appendWireframe(backgroundHelperLines, item.mesh, quietColor);
                    }
                } else {
                    appendMesh(triangles, item.mesh, color, item.shapeIndex);
                }
            }
        }

        for (const Line2D &line : backgroundHelperLines) {
            painter.setPen(QPen(line.color, 1, Qt::DashLine, Qt::RoundCap));
            painter.drawLine(line.a, line.b);
        }

        if (drawSceneMeshes) {
            m_pickBufferSize = size();
            drawTrianglesWithDepth(&painter, triangles, size(), &m_pickBuffer, &m_depthBuffer, &m_renderImage);
        } else {
            QImage pickImage(size(), QImage::Format_ARGB32_Premultiplied);
            QPainter pickPainter(&pickImage);
            m_pickBufferSize = size();
            drawTrianglesWithDepth(&pickPainter, triangles, size(), &m_pickBuffer, &m_depthBuffer, &m_renderImage);
        }

        for (const Line2D &line : foregroundHelperLines) {
            painter.setPen(QPen(line.color, 2, Qt::DashLine, Qt::RoundCap));
            painter.drawLine(line.a, line.b);
        }

        const bool hasSelectedGroup = m_scene && m_selectedGroupId > 0 && m_scene->treeNodeById(m_selectedGroupId);
        if (hasSelectedGroup) {
            const SceneDocument::TreeNode *selectedGroup = m_scene->treeNodeById(m_selectedGroupId);
            const bool transformGroupSelected = selectedGroup->operation == SceneDocument::TreeNode::Translate
                                                || selectedGroup->operation == SceneDocument::TreeNode::Rotate;
            if (transformGroupSelected) {
                const bool showMoveAxes = selectedGroup->operation != SceneDocument::TreeNode::Rotate;
                const bool showRotationRings = selectedGroup->operation != SceneDocument::TreeNode::Translate;
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
                        painter.setPen(QPen(QColor(5, 8, 12, 185), 7, Qt::SolidLine, Qt::RoundCap));
                        painter.drawLine(start, end);
                        painter.setPen(QPen(axis.second, 4.5, Qt::SolidLine, Qt::RoundCap));
                        painter.drawLine(start, end);
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

                        painter.setPen(QPen(ring.second, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                        painter.setBrush(Qt::NoBrush);
                        painter.drawPolyline(ringPath);
                    }
                }
            }
        }
    }

    painter.setPen(QColor(220, 220, 220));
    painter.drawText(12, 24, "3D viewport: drag to orbit, wheel to zoom, drag selected axes to move or rings to rotate");
    painter.drawText(12, 42, QString("%1 | renderer: %2").arg(csgStatus, renderBackendName()));
    drawTreeTransformControlPreview(painter);
    drawAxisGizmo(painter);
}

void ViewportWidget::paintOpenGLPreview()
{
    if (!m_shapes || !m_glMeshProgram || !m_glMeshProgram->isLinked())
        return;

    QVector<OpenGLMeshVertex> vertices;
    auto toClipPosition = [this](const QVector3D &world) {
        const ProjectedPoint projected = projectWorldPoint(world, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance);
        const float x = (static_cast<float>(projected.point.x()) / qMax(1, width())) * 2.0f - 1.0f;
        const float y = 1.0f - (static_cast<float>(projected.point.y()) / qMax(1, height())) * 2.0f;
        const float z = qBound(-1.0f, ((projected.depth - 8.0f) / (1200.0f - 8.0f)) * 2.0f - 1.0f, 1.0f);
        return QVector3D(x, y, z);
    };

    auto appendOpenGLMesh = [&](const SceneMesh &mesh, const QColor &baseColor) {
        for (const MeshTriangle &triangle : mesh.triangles) {
            const QVector3D color = colorToVector(baseColor.lighter(triangle.shade));
            vertices.append({toClipPosition(triangle.a), triangle.normal, color});
            vertices.append({toClipPosition(triangle.b), triangle.normal, color});
            vertices.append({toClipPosition(triangle.c), triangle.normal, color});
        }
    };

    if (m_draggingShape) {
        for (const ShapeNode &shape : *m_shapes) {
            QColor color = QColor(80, 160, 255);
            if (shape.booleanMode == ShapeNode::Subtract)
                color = QColor(225, 95, 95);
            else if (shape.booleanMode == ShapeNode::Intersect)
                color = QColor(150, 115, 240);

            appendOpenGLMesh(buildShapeMesh(shape), color);
        }
    } else {
        const CsgPreview &preview = m_scene
                                        ? cachedCsgPreview(*m_scene,
                                                           &m_cachedCsgPreview,
                                                           &m_cachedCsgFingerprint,
                                                           &m_csgPreviewDirty)
                                        : cachedCsgPreview(*m_shapes,
                                                           &m_cachedCsgPreview,
                                                           &m_cachedCsgFingerprint,
                                                           &m_csgPreviewDirty);

        for (const CsgRenderItem &item : preview.items) {
            if (item.helper)
                continue;

            QColor color = item.computed ? QColor(95, 185, 155) : QColor(80, 160, 255);
            if (!item.computed && item.booleanMode == ShapeNode::Subtract)
                color = QColor(225, 95, 95);
            else if (!item.computed && item.booleanMode == ShapeNode::Intersect)
                color = QColor(150, 115, 240);

            if (item.shapeIndex == m_selectedIndex)
                color = item.computed ? QColor(115, 220, 180) : QColor(255, 180, 60);

            appendOpenGLMesh(item.mesh, color);
        }
    }

    if (vertices.isEmpty())
        return;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    m_glMeshProgram->bind();

    const int positionLocation = m_glMeshProgram->attributeLocation("a_position");
    const int normalLocation = m_glMeshProgram->attributeLocation("a_normal");
    const int colorLocation = m_glMeshProgram->attributeLocation("a_color");

    m_glMeshProgram->enableAttributeArray(positionLocation);
    m_glMeshProgram->enableAttributeArray(normalLocation);
    m_glMeshProgram->enableAttributeArray(colorLocation);

    const char *data = reinterpret_cast<const char *>(vertices.constData());
    m_glMeshProgram->setAttributeArray(positionLocation,
                                       GL_FLOAT,
                                       data + offsetof(OpenGLMeshVertex, position),
                                       3,
                                       sizeof(OpenGLMeshVertex));
    m_glMeshProgram->setAttributeArray(normalLocation,
                                       GL_FLOAT,
                                       data + offsetof(OpenGLMeshVertex, normal),
                                       3,
                                       sizeof(OpenGLMeshVertex));
    m_glMeshProgram->setAttributeArray(colorLocation,
                                       GL_FLOAT,
                                       data + offsetof(OpenGLMeshVertex, color),
                                       3,
                                       sizeof(OpenGLMeshVertex));

    glDrawArrays(GL_TRIANGLES, 0, vertices.size());

    m_glMeshProgram->disableAttributeArray(positionLocation);
    m_glMeshProgram->disableAttributeArray(normalLocation);
    m_glMeshProgram->disableAttributeArray(colorLocation);
    m_glMeshProgram->release();
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
    if (!translatePreview && !rotatePreview)
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
        return projectWorldPoint(world, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance).point;
    };

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    auto drawDirectionLabel = [&](const QPointF &anchor, const QPointF &awayFrom, const QString &label) {
        QPointF direction = anchor - awayFrom;
        const qreal length = std::hypot(direction.x(), direction.y());
        if (length > 0.001)
            direction /= length;
        else
            direction = QPointF(0.0, -1.0);

        const QPointF center = anchor + direction * 18.0;
        const QRectF labelRect(center.x() - 9.0, center.y() - 9.0, 18.0, 18.0);
        QFont labelFont = painter.font();
        labelFont.setBold(true);
        labelFont.setPointSizeF(qMax<qreal>(8.0, labelFont.pointSizeF() + 1.0));
        painter.setFont(labelFont);

        painter.setPen(QPen(QColor(5, 8, 12, 220), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawText(labelRect, Qt::AlignCenter, label);
        painter.setPen(QColor(255, 248, 190, 245));
        painter.drawText(labelRect, Qt::AlignCenter, label);
    };

    if (translatePreview) {
        QVector3D worldAxis = worldAxisVectorForGroup(m_treeTransformPreviewGroupId, localAxis);
        if (worldAxis.lengthSquared() <= 0.0001f)
            worldAxis = localAxis;
        worldAxis.normalize();

        const QPointF center = project(origin);
        const QPointF negative = project(origin - worldAxis * 34.0f);
        const QPointF positive = project(origin + worldAxis * 34.0f);
        painter.setPen(QPen(QColor(5, 8, 12, 190), 8, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(negative, positive);
        painter.setPen(QPen(accent, 4, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(negative, positive);
        drawArrowHead(&painter, center, positive, accent);
        drawArrowHead(&painter, center, negative, accent);
        drawDirectionLabel(positive, center, "+");
        drawDirectionLabel(negative, center, "-");
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
            painter.setPen(QPen(QColor(5, 8, 12, 190), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPolyline(QPolygonF(arcPoints));
            painter.setPen(QPen(accent, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPolyline(QPolygonF(arcPoints));
            drawArrowHead(&painter, arcPoints[1], arcPoints.first(), accent);
            drawArrowHead(&painter, arcPoints[arcPoints.size() - 2], arcPoints.last(), accent);
            const QPointF center = project(origin);
            drawDirectionLabel(arcPoints.last(), center, "+");
            drawDirectionLabel(arcPoints.first(), center, "-");
        }
    }

    painter.restore();
}

bool ViewportWidget::canUseOpenGLRenderBackend() const
{
    return true;
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

    const bool allowMoveAxes = selectedGroup->operation != SceneDocument::TreeNode::Rotate;
    const bool allowRotationRings = selectedGroup->operation != SceneDocument::TreeNode::Translate;
    const QVector3D origin = selectedTransformOrigin();
    float bestDistance = 9.0f;
    DragMode pickedAxis = NoDrag;
    const QPointF start = projectWorldPoint(origin, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance).point;

    const QVector<QPair<DragMode, QVector3D>> axes = {
        {AxisXDrag, selectedWorldAxisVector(QVector3D(36.0f, 0.0f, 0.0f))},
        {AxisYDrag, selectedWorldAxisVector(QVector3D(0.0f, 36.0f, 0.0f))},
        {AxisZDrag, selectedWorldAxisVector(QVector3D(0.0f, 0.0f, 36.0f))}
    };

    if (allowMoveAxes) {
        for (const auto &axis : axes) {
            const QPointF end = projectWorldPoint(origin + axis.second, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance).point;
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
                const QPointF current = projectWorldPoint(worldPoint, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance).point;
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
    const QPointF screenOrigin = projectWorldPoint(origin, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance).point;
    const QPointF screenEnd = projectWorldPoint(origin + selectedWorldAxisVector(axisVector * 36.0f),
                                                size(),
                                                m_cameraYaw,
                                                m_cameraPitch,
                                                m_cameraDistance).point;
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
                                                           m_cameraDistance).point;
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
        const CsgPreview &preview = m_scene
                                        ? cachedCsgPreview(*m_scene,
                                                           &m_cachedCsgPreview,
                                                           &m_cachedCsgFingerprint,
                                                           &m_csgPreviewDirty)
                                        : cachedCsgPreview(*m_shapes,
                                                           &m_cachedCsgPreview,
                                                           &m_cachedCsgFingerprint,
                                                           &m_csgPreviewDirty);
        int helperShapeIndex = -1;
        float bestDistance = 8.0f;

        for (const CsgRenderItem &item : preview.items) {
            if (!item.helper)
                continue;

            for (const auto &edge : meshEdges(item.mesh)) {
                const QPointF a = projectWorldPoint(edge.first, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance).point;
                const QPointF b = projectWorldPoint(edge.second, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance).point;
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

    if (shapeIndex < 0)
        return;

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
        const QPoint delta = event->pos() - m_lastMousePosition;
        m_cameraYaw += delta.x() * 0.45f;
        m_cameraPitch += delta.y() * 0.35f;
        m_cameraPitch = qBound(-85.0f, m_cameraPitch, 85.0f);
        update();
    }

    m_lastMousePosition = event->pos();
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && (m_draggingShape || m_draggingGroup)) {
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
    }
}

void ViewportWidget::wheelEvent(QWheelEvent *event)
{
    m_cameraDistance -= event->angleDelta().y() * 0.12f;
    m_cameraDistance = qBound(70.0f, m_cameraDistance, 700.0f);
    update();
}
