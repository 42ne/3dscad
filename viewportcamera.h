#ifndef VIEWPORTCAMERA_H
#define VIEWPORTCAMERA_H

#include <QPointF>
#include <QSize>
#include <QVector3D>

struct ProjectedPoint;

class ViewportCamera
{
public:
    float yaw = -35.0f;
    float pitch = -28.0f;
    float distance = 220.0f;
    QVector3D target;
    bool orthographic = false;

    ProjectedPoint project(const QVector3D &world, QSize viewportSize) const;
    QVector3D toCameraPoint(const QVector3D &world) const;
    QVector3D toCameraDirection(const QVector3D &dir) const;
    QVector3D rightVector() const;
    QVector3D upVector() const;
};

#endif // VIEWPORTCAMERA_H
