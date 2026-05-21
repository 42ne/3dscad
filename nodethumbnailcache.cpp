#include "nodethumbnailcache.h"
#include "scenedocument.h"
#include "viewportwidget.h"

#include <QtConcurrent>

// Background color matching the primitive card background (QColor(219, 231, 246)).
// Using the card's own colour means the thumbnail blends seamlessly into the card.
static const QColor ThumbnailBg(219, 231, 246);

NodeThumbnailCache::NodeThumbnailCache(QSize size, QObject *parent)
    : QObject(parent)
    , m_size(size)
    , m_renderTimer(new QTimer(this))
    , m_watcher(new QFutureWatcher<QVector<QPair<int, QImage>>>(this))
{
    m_renderTimer->setSingleShot(true);
    connect(m_renderTimer, &QTimer::timeout,
            this, &NodeThumbnailCache::onRenderTimerTimeout);
    connect(m_watcher,
            &QFutureWatcher<QVector<QPair<int, QImage>>>::finished,
            this, &NodeThumbnailCache::onRenderFinished);
}

NodeThumbnailCache::~NodeThumbnailCache()
{
    m_renderTimer->stop();
    m_watcher->cancel();
    m_watcher->waitForFinished();
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
        const int nodeId      = it.key();
        const ShapeNode &shape = it.value();
        if (!m_lastRendered.contains(nodeId) || m_lastRendered[nodeId] != shape) {
            m_pending[nodeId] = shape;
            anyDirty = true;
        }
    }

    if (anyDirty)
        m_renderTimer->start(RenderDelayMs); // (re-)start the debounce window
}

QImage NodeThumbnailCache::thumbnail(int nodeId) const
{
    return m_cache.value(nodeId);
}

void NodeThumbnailCache::onRenderTimerTimeout()
{
    if (m_watcher->isRunning()) {
        // A previous render is still running; check again shortly after it finishes.
        m_renderTimer->start(RetryDelayMs);
        return;
    }

    if (m_pending.isEmpty())
        return;

    // Snapshot pending → in-progress, then launch the background render.
    m_inProgress = m_pending;
    m_pending.clear();

    QVector<QPair<int, ShapeNode>> toRender;
    toRender.reserve(m_inProgress.size());
    for (auto it = m_inProgress.constBegin(); it != m_inProgress.constEnd(); ++it)
        toRender.append(QPair<int, ShapeNode>(it.key(), it.value()));

    const QSize  size = m_size;
    const QColor bg   = ThumbnailBg;

    m_watcher->setFuture(QtConcurrent::run(
        [toRender, size, bg]() -> QVector<QPair<int, QImage>> {
            QVector<QPair<int, QImage>> results;
            results.reserve(toRender.size());

            for (const QPair<int, ShapeNode> &pair : toRender) {
                const int       nodeId = pair.first;
                const ShapeNode &shape = pair.second;

                // Build a minimal single-shape document for the render.
                SceneDocument doc;
                ShapeNode copy = shape;
                copy.id = -1; // let addShape assign a fresh id
                doc.addShape(copy);

                const QImage img = ViewportWidget::renderThumbnail(doc, size, bg);
                results.append(QPair<int, QImage>(nodeId, img));
            }
            return results;
        }));
}

void NodeThumbnailCache::onRenderFinished()
{
    if (m_watcher->isCanceled()) {
        m_inProgress.clear();
        return;
    }

    const QVector<QPair<int, QImage>> results = m_watcher->result();
    for (const QPair<int, QImage> &pair : results) {
        const int nodeId = pair.first;
        m_cache[nodeId]        = pair.second;
        m_lastRendered[nodeId] = m_inProgress.value(nodeId);
    }
    m_inProgress.clear();

    emit thumbnailsUpdated();

    // If more changes arrived while the render was running, schedule them promptly.
    if (!m_pending.isEmpty())
        m_renderTimer->start(RetryDelayMs);
}
