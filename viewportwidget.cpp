#include "viewportwidget.h"

#include "scenemesh.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
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

struct SceneLight
{
    QVector3D direction;
    QColor color;
    float intensity = 1.0f;
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

ViewportWidget::ViewportWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(500, 400);
    setFocusPolicy(Qt::StrongFocus);
}

void ViewportWidget::setShapes(const QVector<ShapeNode> *shapes)
{
    m_shapes = shapes;
    update();
}

void ViewportWidget::setSelectedIndex(int index)
{
    m_selectedIndex = index;
    update();
}

void ViewportWidget::initializeGL()
{
    glClearColor(0.12f, 0.13f, 0.15f, 1.0f);
}

void ViewportWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void ViewportWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

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

    drawGrid();

    if (m_shapes) {
        QVector<Triangle2D> triangles;

        for (int i = 0; i < m_shapes->size(); ++i) {
            const ShapeNode &s = m_shapes->at(i);
            QColor color = (i == m_selectedIndex)
                               ? QColor(255, 180, 60)
                               : QColor(80, 160, 255);

            appendMesh(triangles, buildShapeMesh(s), color, i);
        }

        m_pickBufferSize = size();
        drawTrianglesWithDepth(&painter, triangles, size(), &m_pickBuffer, &m_depthBuffer, &m_renderImage);

        if (m_selectedIndex >= 0 && m_selectedIndex < m_shapes->size()) {
            const QVector3D origin = m_shapes->at(m_selectedIndex).position;
            const QVector<QPair<QVector3D, QColor>> axes = {
                {QVector3D(36.0f, 0.0f, 0.0f), QColor(235, 80, 80)},
                {QVector3D(0.0f, 36.0f, 0.0f), QColor(80, 210, 120)},
                {QVector3D(0.0f, 0.0f, 36.0f), QColor(90, 155, 245)}
            };

            for (const auto &axis : axes) {
                const QPointF start = project(origin).point;
                const QPointF end = project(origin + axis.first).point;
                painter.setPen(QPen(axis.second, 3, Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(start, end);
                painter.setBrush(axis.second);
                painter.setPen(Qt::NoPen);
                painter.drawEllipse(end, 4, 4);
            }
        }
    }

    painter.setPen(QColor(220, 220, 220));
    painter.drawText(12, 24, "3D viewport: drag to orbit, wheel to zoom, drag selected axes to move");

    painter.end();
}

void ViewportWidget::mousePressEvent(QMouseEvent *event)
{
    m_lastMousePosition = event->pos();

    if (event->button() == Qt::LeftButton
        && m_shapes
        && m_selectedIndex >= 0
        && m_selectedIndex < m_shapes->size()) {
        const QVector3D origin = m_shapes->at(m_selectedIndex).position;
        const QVector<QPair<DragMode, QVector3D>> axes = {
            {AxisXDrag, QVector3D(36.0f, 0.0f, 0.0f)},
            {AxisYDrag, QVector3D(0.0f, 36.0f, 0.0f)},
            {AxisZDrag, QVector3D(0.0f, 0.0f, 36.0f)}
        };

        float bestDistance = 9.0f;
        DragMode pickedAxis = NoDrag;
        const QPointF start = projectWorldPoint(origin, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance).point;

        for (const auto &axis : axes) {
            const QPointF end = projectWorldPoint(origin + axis.second, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance).point;
            const float distance = distanceToSegment(event->pos(), start, end);
            if (distance < bestDistance) {
                bestDistance = distance;
                pickedAxis = axis.first;
            }
        }

        if (pickedAxis != NoDrag) {
            m_draggingShape = true;
            m_dragMode = pickedAxis;
            m_dragShapeIndex = m_selectedIndex;
            m_dragStartMousePosition = event->pos();
            m_lastDragDelta = QVector3D();
            emit shapeDragStarted(m_selectedIndex);
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
    if (m_draggingShape && (event->buttons() & Qt::LeftButton)) {
        const QPoint pixelDelta = event->pos() - m_dragStartMousePosition;
        const float worldUnitsPerPixel = m_cameraDistance / 420.0f;
        QVector3D worldDelta(pixelDelta.x() * worldUnitsPerPixel,
                             -pixelDelta.y() * worldUnitsPerPixel,
                             0.0f);

        if (m_shapes && m_dragShapeIndex >= 0 && m_dragShapeIndex < m_shapes->size() && m_dragMode != PlaneDrag) {
            QVector3D axisVector;
            if (m_dragMode == AxisXDrag)
                axisVector = QVector3D(1.0f, 0.0f, 0.0f);
            else if (m_dragMode == AxisYDrag)
                axisVector = QVector3D(0.0f, 1.0f, 0.0f);
            else if (m_dragMode == AxisZDrag)
                axisVector = QVector3D(0.0f, 0.0f, 1.0f);

            const QVector3D origin = m_shapes->at(m_dragShapeIndex).position;
            const QPointF screenOrigin = projectWorldPoint(origin, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance).point;
            const QPointF screenEnd = projectWorldPoint(origin + axisVector * 36.0f, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance).point;
            QVector2D screenAxis(screenEnd - screenOrigin);

            if (screenAxis.lengthSquared() > 0.0001f) {
                screenAxis.normalize();
                const float screenAmount = QVector2D::dotProduct(QVector2D(pixelDelta), screenAxis);
                worldDelta = axisVector * screenAmount * worldUnitsPerPixel;
            } else {
                worldDelta = QVector3D();
            }
        }

        if ((worldDelta - m_lastDragDelta).lengthSquared() < 0.0001f) {
            m_lastMousePosition = event->pos();
            return;
        }

        m_lastDragDelta = worldDelta;
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
    if (event->button() == Qt::LeftButton && m_draggingShape) {
        const int shapeIndex = m_dragShapeIndex;
        m_draggingShape = false;
        m_dragMode = NoDrag;
        m_dragShapeIndex = -1;
        emit shapeDragFinished(shapeIndex);
    }
}

void ViewportWidget::wheelEvent(QWheelEvent *event)
{
    m_cameraDistance -= event->angleDelta().y() * 0.12f;
    m_cameraDistance = qBound(70.0f, m_cameraDistance, 700.0f);
    update();
}
