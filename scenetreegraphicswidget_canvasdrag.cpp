// scenetreegraphicswidget_canvasdrag.cpp
// Canvas-move drag helpers — magnetic snap and ghost outline.
// Compiled as part of the SceneTreeGraphicsWidget translation unit via the .pro file.

#include "scenetreegraphicswidget.h"
#include "scenetreegraphicshelpers.h"

#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QSet>

using namespace SceneTreeGraphics;

// ── Magnetic snap ─────────────────────────────────────────────────────────────
//
// For each neighbour block, generates 4 edge-to-edge candidate positions:
//
//   RIGHT  — our left edge at neighbour.right,  tops aligned.
//   LEFT   — our right edge at neighbour.left,  tops aligned.
//   BELOW  — our top edge at neighbour.bottom,  lefts aligned.
//   ABOVE  — our bottom edge at neighbour.top,  lefts aligned.
//
// Only candidates that do NOT overlap any other block are accepted.
// The closest valid candidate within kMagnetRadius wins.
// Returns true and fills *snapped when a valid candidate is found.

bool SceneTreeGraphicsWidget::applyMagneticSnap(const QPointF &candidate,
                                                const QSizeF  &size,
                                                int            excludeId,
                                                QPointF       *snapped,
                                                const QVector<int> &additionalExcludeIds) const
{
    // Pre-collect stationary neighbours. During slow cluster drag every moving
    // member is excluded, so a connected group cannot magnetise to itself.
    QVector<QRectF> neighbors;
    neighbors.reserve(m_canvasMoveHandles.size());
    for (const CanvasMoveHandle &h : m_canvasMoveHandles) {
        if (h.nodeId != excludeId
            && !additionalExcludeIds.contains(h.nodeId)
            && !h.blockRect.isEmpty()) {
            neighbors.append(h.blockRect);
        }
    }

    // Returns true if placing the dragged block at `pos` would overlap any neighbour.
    // A 0.5 px threshold allows edge-to-edge touching without triggering overlap.
    auto overlapsAny = [&](const QPointF &pos) -> bool {
        const QRectF placed(pos, size);
        for (const QRectF &nb : neighbors) {
            const QRectF isect = placed.intersected(nb);
            if (isect.width() > 0.5 && isect.height() > 0.5)
                return true;
        }
        return false;
    };

    // If the dragged block already overlaps a neighbour at the current candidate
    // position, force-snap to the nearest valid adjacent position regardless of
    // distance.  Otherwise use the normal kMagnetRadius pull.
    const bool alreadyOverlapping = overlapsAny(candidate);
    const qreal maxDist = alreadyOverlapping ? 1.0e9 : kMagnetRadius;

    qreal   bestDist = maxDist;
    bool    found    = false;
    QPointF best     = candidate;

    // All 4 corners of the neighbour block.
    // All 4 corner offsets of our dragged block from its top-left.
    // Combining every pair gives 16 corner-to-corner snap candidates.
    // overlapsAny filters out any that would collide with another block.

    for (const QRectF &nb : neighbors) {
        const QPointF nbCorners[4] = {
            { nb.left(),  nb.top()    },   // TL
            { nb.right(), nb.top()    },   // TR
            { nb.left(),  nb.bottom() },   // BL
            { nb.right(), nb.bottom() },   // BR
        };
        const QPointF ourOffsets[4] = {
            { 0,            0             }, // our TL
            { size.width(), 0             }, // our TR
            { 0,            size.height() }, // our BL
            { size.width(), size.height() }, // our BR
        };

        for (const QPointF &c : nbCorners) {
            for (const QPointF &o : ourOffsets) {
                const QPointF t = c - o;   // top-left of our block when corner-aligned
                const qreal d = QLineF(candidate, t).length();
                if (d >= bestDist)
                    continue;
                if (overlapsAny(t))
                    continue;
                bestDist = d;
                best     = t;
                found    = true;
            }
        }
    }

    if (found)
        *snapped = best;
    return found;
}

// ── Ghost outline / drag placeholder ─────────────────────────────────────────

QPointF SceneTreeGraphicsWidget::nonOverlappingCanvasPosition(const QPointF &candidate,
                                                              const QSizeF &size,
                                                              const QVector<QRectF> &placedBlocks) const
{
    if (placedBlocks.isEmpty() || !size.isValid())
        return candidate;

    constexpr int MaxIterations = 80;
    QPointF pos = candidate;

    auto overlaps = [&](const QRectF &rect, QRectF *blockingRect) {
        for (const QRectF &placed : placedBlocks) {
            const QRectF overlap = rect.intersected(placed);
            if (overlap.width() > 0.5 && overlap.height() > 0.5) {
                if (blockingRect)
                    *blockingRect = placed;
                return true;
            }
        }
        return false;
    };

    for (int i = 0; i < MaxIterations; ++i) {
        const QRectF rect(pos, size);
        QRectF blocker;
        if (!overlaps(rect, &blocker))
            return pos;

        const qreal pushRight = blocker.right() - rect.left();
        const qreal pushDown = blocker.bottom() - rect.top();
        if (pushRight <= pushDown)
            pos.rx() += qMax<qreal>(1.0, pushRight);
        else
            pos.ry() += qMax<qreal>(1.0, pushDown);
    }

    return pos;
}

void SceneTreeGraphicsWidget::clearCanvasDragGhost()
{
    if (m_canvasDragGhost) {
        if (m_graphicsScene)
            m_graphicsScene->removeItem(m_canvasDragGhost);
        delete m_canvasDragGhost;
        m_canvasDragGhost = nullptr;
    }
}

// Shows a faint dashed placeholder at the block's ORIGINAL position so the user
// can see where the block was before dragging it away.
void SceneTreeGraphicsWidget::showDragPlaceholder(const QPointF &pos, const QSizeF &size)
{
    clearCanvasDragGhost();
    QPainterPath path;
    path.addRoundedRect(QRectF(pos, size), CornerRadius + 1.5, CornerRadius + 1.5);
    const QPen pen(QColor(110, 125, 150, 100), 1.5, Qt::DashLine);
    m_canvasDragGhost = m_graphicsScene->addPath(path, pen, Qt::NoBrush);
    m_canvasDragGhost->setZValue(5.0); // behind everything — just a subtle hint
}

// ── Cluster movement ──────────────────────────────────────────────────────────

// Returns true if block rects A and B are edge-to-edge touching (within tol px).
static bool edgeTouching(const QRectF &a, const QRectF &b, qreal tol = 1.5)
{
    const bool hOverlap = a.left() < b.right() + tol && b.left() < a.right() + tol;
    const bool vOverlap = a.top()  < b.bottom() + tol && b.top() < a.bottom() + tol;
    const bool hAdj = (qAbs(a.right() - b.left()) <= tol || qAbs(b.right() - a.left()) <= tol) && vOverlap;
    const bool vAdj = (qAbs(a.bottom() - b.top()) <= tol || qAbs(b.bottom() - a.top()) <= tol) && hOverlap;
    return hAdj || vAdj;
}

// Flood-fill all blocks edge-touching the starting block's connected component.
QVector<int> SceneTreeGraphicsWidget::findConnectedCluster(int startNodeId) const
{
    QRectF startRect;
    for (const CanvasMoveHandle &h : m_canvasMoveHandles) {
        if (h.nodeId == startNodeId) { startRect = h.blockRect; break; }
    }
    if (startRect.isEmpty())
        return { startNodeId };

    QVector<int>    cluster      = { startNodeId };
    QVector<QRectF> clusterRects = { startRect };
    bool changed = true;
    while (changed) {
        changed = false;
        for (const CanvasMoveHandle &h : m_canvasMoveHandles) {
            if (cluster.contains(h.nodeId))
                continue;
            for (const QRectF &cr : clusterRects) {
                if (edgeTouching(cr, h.blockRect)) {
                    cluster.append(h.nodeId);
                    clusterRects.append(h.blockRect);
                    changed = true;
                    break;
                }
            }
        }
    }
    return cluster;
}

// Returns the subset of m_treeItems whose scene-space centre lies inside blockRect.
// Used to identify which items belong to a given root block for live dragging.
//
// NOTE: m_treeItems may contain duplicates (grip-strip items are appended both
// explicitly and by the post-loop diff in drawTreeOrPlaceholder).  Deduplicate
// here so each item is moved at most once per mouse event.
QVector<QGraphicsItem *> SceneTreeGraphicsWidget::itemsInBlockRect(const QRectF &blockRect) const
{
    QSet<QGraphicsItem *> seen;
    QVector<QGraphicsItem *> result;
    result.reserve(32);
    for (QGraphicsItem *item : m_treeItems) {
        if (seen.contains(item))
            continue;
        seen.insert(item);
        if (blockRect.contains(item->sceneBoundingRect().center()))
            result.append(item);
    }
    return result;
}
