#ifndef VIEWPORTAXISGIZMO_H
#define VIEWPORTAXISGIZMO_H

#include "viewportwidget.h"

#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QVector3D>

class QPainter;

struct AxisGizmoAxis
{
    QString label;
    QVector3D direction;
    QColor color;
    QPointF end;
    float cameraDepth = 0.0f;
};

class ViewportAxisGizmo
{
public:
    void drawAxisGizmo(QPainter &painter, ViewportWidget &w) const;
    void drawPolyhedronElementSelectionOverlay(QPainter &painter, ViewportWidget &w) const;
    void drawPolyhedronSelectionMoveTool(QPainter &painter, ViewportWidget &w) const;

    bool pickSelectedTransformAxis(const QPoint &position, ViewportWidget::DragMode *dragMode, ViewportWidget &w) const;
    bool pickPolyhedronSelectionAxis(const QPoint &position, ViewportWidget::DragMode *dragMode, ViewportWidget &w) const;
    bool hitTestPolyhedronSelection(const QPoint &position, ViewportWidget &w) const;
};

#endif // VIEWPORTAXISGIZMO_H
