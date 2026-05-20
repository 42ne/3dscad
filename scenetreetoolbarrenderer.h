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

    explicit SceneTreeToolbarRenderer(QGraphicsScene *scene, QVector<QGraphicsItem *> *toolbarItems = nullptr);

    QRectF render(PreviewMovedCallback onPreviewMoved,
                  PreviewFinishedCallback onPreviewFinished,
                  ToolDroppedCallback onDropped,
                  const QPointF &viewportTopLeft,
                  qreal viewportWidth,
                  qreal viewportScale);

private:
    void trackToolbarItem(QGraphicsItem *item) const;

private:
    QGraphicsScene *m_scene = nullptr;
    QVector<QGraphicsItem *> *m_toolbarItems = nullptr;
};

#endif
