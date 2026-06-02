#ifndef SCENETREETOOLBARRENDERER_H
#define SCENETREETOOLBARRENDERER_H

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>
#include <functional>

class QGraphicsItem;
class QGraphicsScene;

class SceneTreeToolbarRenderer
{
public:
    using PreviewMovedCallback = std::function<void(const QPointF &, const QSizeF &, const QString &)>;
    using PreviewFinishedCallback = std::function<void()>;
    using ToolDroppedCallback = std::function<void(const QString &, const QPointF &)>;
    using ToolHoverChangedCallback = std::function<void(const QString &, bool)>;

    explicit SceneTreeToolbarRenderer(QGraphicsScene *scene,
                                      QVector<QGraphicsItem *> *toolbarItems = nullptr,
                                      int theme = 0,
                                      bool darkGlass = true);

    // toolbarSide: 0 = top (horizontal), 1 = left (vertical), 2 = right (vertical)
    // outPanelVpRect / outToolVpRects: viewport-pixel rects for drag-zone hit testing
    QRectF render(PreviewMovedCallback onPreviewMoved,
                  PreviewFinishedCallback onPreviewFinished,
                  ToolDroppedCallback onDropped,
                  ToolHoverChangedCallback onHoverChanged,
                  const QPointF &viewportTopLeft,
                  qreal viewportWidth,
                  qreal viewportHeight,
                  qreal viewportScale,
                  int toolbarSide,
                  QRectF *outPanelVpRect = nullptr,
                  QVector<QRectF> *outToolVpRects = nullptr);

private:
    void trackToolbarItem(QGraphicsItem *item) const;

private:
    QGraphicsScene *m_scene = nullptr;
    QVector<QGraphicsItem *> *m_toolbarItems = nullptr;
    int m_theme = 0;
    bool m_darkGlass = true;
};

#endif
