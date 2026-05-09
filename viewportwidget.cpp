#include "viewportwidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>

struct ProjectedPoint
{
    QPointF point;
    float depth = 0.0f;
    bool visible = true;
};

struct Face2D
{
    QVector<QPointF> points;
    QBrush brush;
    float depth = 0.0f;
    QPen pen = Qt::NoPen;
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

static QVector3D faceNormal(const QVector3D &a, const QVector3D &b, const QVector3D &c)
{
    QVector3D normal = -QVector3D::crossProduct(b - a, c - a);

    if (normal.lengthSquared() <= 0.0001f)
        return QVector3D(0.0f, 0.0f, 1.0f);

    return normal.normalized();
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

    const float yaw = qDegreesToRadians(m_cameraYaw);
    const float pitch = qDegreesToRadians(m_cameraPitch);
    const float focalLength = 420.0f;
    const QVector<SceneLight> lights = {
        {QVector3D(-0.45f, -0.35f, 1.0f).normalized(), QColor(255, 244, 214), 0.78f},
        {QVector3D(0.85f, 0.15f, 0.45f).normalized(), QColor(160, 205, 255), 0.34f},
        {QVector3D(-0.2f, 0.9f, 0.25f).normalized(), QColor(255, 170, 110), 0.24f}
    };

    auto toCamera = [&](const QVector3D &world) {
        QVector3D p = world;

        p = QVector3D(
            p.x() * qCos(yaw) + p.z() * qSin(yaw),
            p.y(),
            -p.x() * qSin(yaw) + p.z() * qCos(yaw));

        p = QVector3D(
            p.x(),
            p.y() * qCos(pitch) - p.z() * qSin(pitch),
            p.y() * qSin(pitch) + p.z() * qCos(pitch));

        p.setZ(p.z() + m_cameraDistance);
        return p;
    };

    auto project = [&](const QVector3D &world) {
        ProjectedPoint projected;
        const QVector3D camera = toCamera(world);
        projected.depth = camera.z();
        projected.visible = camera.z() > 8.0f;

        const float scale = focalLength / qMax(8.0f, camera.z());
        projected.point = QPointF(
            width() / 2.0f + camera.x() * scale,
            height() / 2.0f - camera.y() * scale);

        return projected;
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

    auto appendCube = [&](QVector<Face2D> &faces, const ShapeNode &shape, const QColor &baseColor) {
        const QVector3D half = shape.size * 0.5f;
        QVector<QVector3D> vertices = {
            {-half.x(), -half.y(), -half.z()}, {half.x(), -half.y(), -half.z()},
            {half.x(), half.y(), -half.z()}, {-half.x(), half.y(), -half.z()},
            {-half.x(), -half.y(), half.z()}, {half.x(), -half.y(), half.z()},
            {half.x(), half.y(), half.z()}, {-half.x(), half.y(), half.z()}
        };

        for (QVector3D &vertex : vertices)
            vertex = rotatePoint(vertex, shape.rotation) + shape.position;

        drawShadow(vertices);

        const QVector<QVector<int>> faceIndices = {
            {0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1},
            {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0}
        };

        const QVector<int> shade = {82, 116, 92, 105, 122, 96};
        for (int i = 0; i < faceIndices.size(); ++i) {
            Face2D face;
            const QVector<int> indices = faceIndices[i];
            const QVector3D normal = faceNormal(vertices[indices[0]], vertices[indices[1]], vertices[indices[2]]);
            face.brush = litColor(baseColor.lighter(shade[i]), normal, lights);
            face.pen = QPen(baseColor.darker(145), 1);

            for (int index : indices) {
                const ProjectedPoint projected = project(vertices[index]);
                face.points.append(projected.point);
                face.depth += projected.depth;
            }

            face.depth /= indices.size();
            faces.append(face);
        }
    };

    auto appendSphere = [&](QVector<Face2D> &faces, const ShapeNode &shape, const QColor &baseColor) {
        QVector<QVector3D> shadowPoints;
        for (int i = 0; i < 24; ++i) {
            const float angle = 2.0f * M_PI * i / 24.0f;
            shadowPoints.append(shape.position + QVector3D(shape.radius * qCos(angle), shape.radius * qSin(angle), shape.radius));
        }

        drawShadow(shadowPoints);

        const ProjectedPoint center = project(shape.position);
        const ProjectedPoint edge = project(shape.position + QVector3D(shape.radius, 0, 0));
        const float radius = QLineF(center.point, edge.point).length();

        Face2D face;
        QRadialGradient gradient(center.point - QPointF(radius * 0.35f, radius * 0.45f), radius * 1.25f);
        gradient.setColorAt(0.0, litColor(baseColor.lighter(150), QVector3D(-0.45f, -0.35f, 1.0f).normalized(), lights));
        gradient.setColorAt(0.45, litColor(baseColor, QVector3D(0.0f, 0.0f, 1.0f), lights));
        gradient.setColorAt(1.0, litColor(baseColor.darker(155), QVector3D(0.45f, 0.35f, -0.25f).normalized(), lights));
        face.brush = QBrush(gradient);
        face.depth = center.depth;
        face.pen = QPen(baseColor.darker(150), 1);

        for (int i = 0; i < 36; ++i) {
            const float angle = 2.0f * M_PI * i / 36.0f;
            face.points.append(center.point + QPointF(qCos(angle) * radius, qSin(angle) * radius));
        }

        faces.append(face);
    };

    auto appendCylinder = [&](QVector<Face2D> &faces, const ShapeNode &shape, const QColor &baseColor) {
        QVector<QVector3D> top;
        QVector<QVector3D> bottom;

        for (int i = 0; i < 24; ++i) {
            const float angle = 2.0f * M_PI * i / 24.0f;
            const QVector3D ringPoint(shape.radius * qCos(angle), shape.radius * qSin(angle), 0);
            top.append(rotatePoint(ringPoint + QVector3D(0, 0, shape.height * 0.5f), shape.rotation) + shape.position);
            bottom.append(rotatePoint(ringPoint - QVector3D(0, 0, shape.height * 0.5f), shape.rotation) + shape.position);
        }

        QVector<QVector3D> shadowPoints = bottom + top;
        drawShadow(shadowPoints);

        for (int i = 0; i < top.size(); ++i) {
            const int next = (i + 1) % top.size();
            Face2D side;
            const QVector3D normal = faceNormal(bottom[i], bottom[next], top[next]);
            side.brush = litColor(baseColor, normal, lights);
            side.pen = QPen(baseColor.darker(150), 1);

            for (const QVector3D &point : {bottom[i], bottom[next], top[next], top[i]}) {
                const ProjectedPoint projected = project(point);
                side.points.append(projected.point);
                side.depth += projected.depth;
            }

            side.depth /= 4.0f;
            faces.append(side);
        }

        const QVector<QVector<QVector3D>> caps = {bottom, top};
        for (int i = 0; i < caps.size(); ++i) {
            Face2D cap;
            const QVector3D normal = faceNormal(caps[i][0], caps[i][1], caps[i][2]);
            cap.brush = litColor(baseColor.lighter(i == 1 ? 120 : 82), normal, lights);
            cap.pen = QPen(baseColor.darker(150), 1);

            for (const QVector3D &point : caps[i]) {
                const ProjectedPoint projected = project(point);
                cap.points.append(projected.point);
                cap.depth += projected.depth;
            }

            cap.depth /= caps[i].size();
            faces.append(cap);
        }
    };

    drawGrid();

    if (m_shapes) {
        QVector<Face2D> faces;

        for (int i = 0; i < m_shapes->size(); ++i) {
            const ShapeNode &s = m_shapes->at(i);
            QColor color = (i == m_selectedIndex)
                               ? QColor(255, 180, 60)
                               : QColor(80, 160, 255);

            if (s.type == ShapeNode::Cube)
                appendCube(faces, s, color);
            else if (s.type == ShapeNode::Sphere)
                appendSphere(faces, s, color);
            else if (s.type == ShapeNode::Cylinder)
                appendCylinder(faces, s, color);
        }

        std::sort(faces.begin(), faces.end(), [](const Face2D &left, const Face2D &right) {
            return left.depth > right.depth;
        });

        for (const Face2D &face : faces) {
            painter.setBrush(face.brush);
            painter.setPen(face.pen);
            painter.drawPolygon(QPolygonF(face.points));
        }
    }

    painter.setPen(QColor(220, 220, 220));
    painter.drawText(12, 24, "3D viewport: drag to orbit, mouse wheel to zoom; 3 scene lights");

    painter.end();
}

void ViewportWidget::mousePressEvent(QMouseEvent *event)
{
    m_lastMousePosition = event->pos();
}

void ViewportWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
        const QPoint delta = event->pos() - m_lastMousePosition;
        m_cameraYaw += delta.x() * 0.45f;
        m_cameraPitch += delta.y() * 0.35f;
        m_cameraPitch = qBound(-85.0f, m_cameraPitch, 85.0f);
        update();
    }

    m_lastMousePosition = event->pos();
}

void ViewportWidget::wheelEvent(QWheelEvent *event)
{
    m_cameraDistance -= event->angleDelta().y() * 0.12f;
    m_cameraDistance = qBound(70.0f, m_cameraDistance, 700.0f);
    update();
}
