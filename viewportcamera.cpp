#include "viewportcamera.h"
#include "viewporthelpers.h"

ProjectedPoint ViewportCamera::project(const QVector3D &world, QSize viewportSize) const
{
    return ViewportHelpers::projectWorldPoint(world, viewportSize,
                                              yaw, pitch, distance, target, orthographic);
}

QVector3D ViewportCamera::toCameraPoint(const QVector3D &world) const
{
    return ViewportHelpers::toCameraPoint(world, yaw, pitch, distance, target);
}

QVector3D ViewportCamera::toCameraDirection(const QVector3D &dir) const
{
    return ViewportHelpers::toCameraDirection(dir, yaw, pitch);
}

QVector3D ViewportCamera::rightVector() const
{
    return ViewportHelpers::cameraRightVector(yaw);
}

QVector3D ViewportCamera::upVector() const
{
    return ViewportHelpers::cameraUpVector(yaw, pitch);
}
