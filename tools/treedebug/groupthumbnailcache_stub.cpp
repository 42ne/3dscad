// Stub implementation of GroupThumbnailCache for the tree debug app.
// Replaces the real groupthumbnailcache.cpp which depends on ViewportWidget
// and the Manifold CSG backend.  All group thumbnails remain null QImages —
// group cards show their abstract operation icon only, which is fine for
// layout / hit-testing work in the debugger.

#include "../../groupthumbnailcache.h"

bool GroupThumbnailCache::isEligibleOperation(SceneDocument::TreeNode::Operation op)
{
    using Op = SceneDocument::TreeNode;
    return op == Op::Union
        || op == Op::Difference
        || op == Op::Intersection
        || op == Op::Hull
        || op == Op::Minkowski
        || op == Op::Module
        || op == Op::For;
}

GroupThumbnailCache::GroupThumbnailCache(QSize size, QObject *parent)
    : QObject(parent)
    , m_size(size)
    , m_renderTimer(new QTimer(this))
    , m_watcher(new QFutureWatcher<QHash<int, QImage>>(this))
{
    m_renderTimer->setSingleShot(true);
    connect(m_renderTimer, &QTimer::timeout,
            this, &GroupThumbnailCache::onRenderTimerTimeout);
    connect(m_watcher, &QFutureWatcher<QHash<int, QImage>>::finished,
            this, &GroupThumbnailCache::onRenderFinished);
}

GroupThumbnailCache::~GroupThumbnailCache()
{
    m_renderTimer->stop();
    if (m_watcher->isRunning()) {
        m_watcher->cancel();
        m_watcher->waitForFinished();
    }
}

void GroupThumbnailCache::syncGroups(const SceneDocument &)
{
    // No rendering in the debug stub — thumbnails stay null.
}

QImage GroupThumbnailCache::thumbnail(int groupNodeId) const
{
    return m_cache.value(groupNodeId); // always null in the stub
}

void GroupThumbnailCache::setSuspended(bool suspended)
{
    m_suspended = suspended;
}

void GroupThumbnailCache::onRenderTimerTimeout()
{
    // Intentionally empty — no CSG backend available in the debug app.
}

void GroupThumbnailCache::onRenderFinished()
{
    // Intentionally empty — onRenderTimerTimeout never launches any work.
}
