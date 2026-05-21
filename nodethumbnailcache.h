#ifndef NODETHUMBNAILCACHE_H
#define NODETHUMBNAILCACHE_H

#include "shapenode.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPair>
#include <QSize>
#include <QTimer>
#include <QVector>

// Manages thumbnail renders for scene tree primitive nodes.
// After syncPrimitives() detects a changed shape, rendering is deferred
// by RenderDelayMs so fast edits (dragging parameters) don't trigger
// constant re-renders. Results arrive via thumbnailsUpdated().
//
// Rendering happens on the GUI thread for now. The thumbnail path uses the
// same CSG backend as the viewport, and running both concurrently is risky with
// the optional Manifold backend, especially in 32-bit builds.
class NodeThumbnailCache : public QObject
{
    Q_OBJECT

public:
    explicit NodeThumbnailCache(QSize size = QSize(68, 68), QObject *parent = nullptr);
    ~NodeThumbnailCache() override;

    // Compare current scene primitives with the last-rendered set.
    // Nodes with changed shapes are queued for re-render.
    // nodeShapes: map from tree-node id to evaluated ShapeNode.
    void syncPrimitives(const QHash<int, ShapeNode> &nodeShapes);

    // Return cached thumbnail for nodeId, or a null QImage if not yet ready.
    QImage thumbnail(int nodeId) const;

    void setSuspended(bool suspended);

signals:
    // Emitted on the main thread after one or more thumbnails become available.
    void thumbnailsUpdated();

private slots:
    void onRenderTimerTimeout();

private:
    static constexpr int RenderDelayMs = 2000; // debounce: render 2 s after last change

    QSize m_size;
    QHash<int, QImage> m_cache;           // nodeId to ready thumbnail
    QHash<int, ShapeNode> m_pending;      // nodeId to shape queued for render
    QHash<int, ShapeNode> m_lastRendered; // nodeId to shape that produced m_cache[id]

    QTimer *m_renderTimer = nullptr;
    bool m_suspended = false;
};

#endif // NODETHUMBNAILCACHE_H
