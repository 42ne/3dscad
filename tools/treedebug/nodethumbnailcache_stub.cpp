// Stub implementation of NodeThumbnailCache for the tree debug app.
// Replaces the real nodethumbnailcache.cpp which depends on ViewportWidget/CSG rendering.
// All thumbnails remain null QImages — primitive cards show their icon only.

#include "../../nodethumbnailcache.h"

NodeThumbnailCache::NodeThumbnailCache(QSize size, QObject *parent)
    : QObject(parent)
    , m_size(size)
    , m_renderTimer(new QTimer(this))
{
    m_renderTimer->setSingleShot(true);
    connect(m_renderTimer, &QTimer::timeout,
            this, &NodeThumbnailCache::onRenderTimerTimeout);
}

NodeThumbnailCache::~NodeThumbnailCache()
{
    m_renderTimer->stop();
}

void NodeThumbnailCache::syncPrimitives(const QHash<int, ShapeNode> &)
{
    // No rendering in the debug stub — thumbnails stay null.
}

QImage NodeThumbnailCache::thumbnail(int nodeId) const
{
    return m_cache.value(nodeId);
}

void NodeThumbnailCache::setSuspended(bool suspended)
{
    m_suspended = suspended;
}

void NodeThumbnailCache::onRenderTimerTimeout()
{
    // Intentionally empty — no CSG backend available in the debug app.
}
