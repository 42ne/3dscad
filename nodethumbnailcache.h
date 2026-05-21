#ifndef NODETHUMBNAILCACHE_H
#define NODETHUMBNAILCACHE_H

#include "shapenode.h"

#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPair>
#include <QSize>
#include <QTimer>
#include <QVector>

// Manages async thumbnail renders for scene tree primitive nodes.
// After syncPrimitives() detects a changed shape, rendering is deferred
// by RenderDelayMs so fast edits (dragging parameters) don't trigger
// constant re-renders.  Results arrive via thumbnailsUpdated().
class NodeThumbnailCache : public QObject
{
    Q_OBJECT

public:
    explicit NodeThumbnailCache(QSize size = QSize(68, 68), QObject *parent = nullptr);
    ~NodeThumbnailCache() override;

    // Compare current scene primitives with the last-rendered set.
    // Nodes with changed shapes are queued for re-render.
    // nodeShapes: map from tree-node id → evaluated ShapeNode.
    void syncPrimitives(const QHash<int, ShapeNode> &nodeShapes);

    // Return cached thumbnail for nodeId, or a null QImage if not yet ready.
    QImage thumbnail(int nodeId) const;

signals:
    // Emitted on the main thread after one or more thumbnails become available.
    void thumbnailsUpdated();

private slots:
    void onRenderTimerTimeout();
    void onRenderFinished();

private:
    static constexpr int RenderDelayMs = 2000; // debounce: render 2 s after last change
    static constexpr int RetryDelayMs  =  300; // retry delay when a render is in progress

    QSize m_size;
    QHash<int, QImage>    m_cache;        // nodeId → ready thumbnail
    QHash<int, ShapeNode> m_pending;      // nodeId → shape queued for render
    QHash<int, ShapeNode> m_inProgress;   // nodeId → shape currently being rendered
    QHash<int, ShapeNode> m_lastRendered; // nodeId → shape that produced m_cache[id]

    QTimer *m_renderTimer = nullptr;
    QFutureWatcher<QVector<QPair<int, QImage>>> *m_watcher = nullptr;
};

#endif // NODETHUMBNAILCACHE_H
