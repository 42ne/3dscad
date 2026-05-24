#include "nodethumbnailcache.h"
#include "scenedocument.h"
#include "viewportwidget.h"

// Primitive thumbnails are drawn inside the toolbar-like frame, so the render
// itself must not bring a second background rectangle.
static const QColor ThumbnailBg(0, 0, 0, 0);

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

void NodeThumbnailCache::syncPrimitives(const QHash<int, ShapeNode> &nodeShapes)
{
    // Remove cached data for nodes that no longer exist in the scene.
    const QList<int> cachedKeys = m_cache.keys();
    for (int id : cachedKeys) {
        if (!nodeShapes.contains(id)) {
            m_cache.remove(id);
            m_lastRendered.remove(id);
            m_pending.remove(id);
        }
    }

    // Mark nodes whose shape changed since the last successful render.
    bool anyDirty = false;
    for (auto it = nodeShapes.constBegin(); it != nodeShapes.constEnd(); ++it) {
        const int nodeId = it.key();
        const ShapeNode &shape = it.value();
        if (!m_lastRendered.contains(nodeId) || m_lastRendered[nodeId] != shape) {
            m_pending[nodeId] = shape;
            anyDirty = true;
        }
    }

    if (anyDirty && !m_suspended)
        m_renderTimer->start(RenderDelayMs); // (re-)start the debounce window
}

QImage NodeThumbnailCache::thumbnail(int nodeId) const
{
    return m_cache.value(nodeId);
}

void NodeThumbnailCache::setSuspended(bool suspended)
{
    if (m_suspended == suspended)
        return;

    m_suspended = suspended;
    if (m_suspended) {
        m_renderTimer->stop();
    } else if (!m_pending.isEmpty()) {
        m_renderTimer->start(RenderDelayMs);
    }
}

void NodeThumbnailCache::onRenderTimerTimeout()
{
    if (m_suspended)
        return;

    if (m_pending.isEmpty())
        return;

    // Snapshot pending before rendering. If syncPrimitives() runs again later,
    // it will queue a fresh render with the newer shape data.
    const QHash<int, ShapeNode> toRender = m_pending;
    m_pending.clear();

    for (auto it = toRender.constBegin(); it != toRender.constEnd(); ++it) {
        const int nodeId = it.key();
        const ShapeNode &shape = it.value();

        // Build a minimal single-shape document for the render.
        SceneDocument doc;
        ShapeNode copy = shape;
        copy.id = -1; // let addShape assign a fresh id
        doc.addShape(copy);

        m_cache[nodeId] = ViewportWidget::renderThumbnail(doc, m_size * 2, ThumbnailBg);
        m_lastRendered[nodeId] = shape;
    }

    emit thumbnailsUpdated();
}
