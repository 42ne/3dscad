#ifndef VIEWPORTOVERLAYPREVIEW_H
#define VIEWPORTOVERLAYPREVIEW_H

#include <QPointF>
#include <QVector3D>

class QPainter;
class ViewportWidget;

class ViewportOverlayPreview
{
public:
    void drawTransformControlPreview(QPainter &painter, ViewportWidget &w) const;
    void drawShapeParameterPreview(QPainter &painter, ViewportWidget &w) const;
};

#endif // VIEWPORTOVERLAYPREVIEW_H
