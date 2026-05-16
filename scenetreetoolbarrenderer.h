#ifndef SCENETREETOOLBARRENDERER_H
#define SCENETREETOOLBARRENDERER_H

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <functional>

class QGraphicsScene;

class SceneTreeToolbarRenderer
{
public:
    using PreviewMovedCallback = std::function<void(const QPointF &, const QSizeF &, const QString &)>;
    using PreviewFinishedCallback = std::function<void()>;
    using ToolDroppedCallback = std::function<void(const QString &, const QPointF &)>;

    explicit SceneTreeToolbarRenderer(QGraphicsScene *scene);

    QRectF render(PreviewMovedCallback onPreviewMoved,
                  PreviewFinishedCallback onPreviewFinished,
                  ToolDroppedCallback onDropped);

private:
    QRectF toolbarRect(int toolCount) const;

private:
    QGraphicsScene *m_scene = nullptr;
};

#endif
