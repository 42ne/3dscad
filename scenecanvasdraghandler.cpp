// scenecanvasdraghandler.cpp
// Extracted from scenetreegraphicswidget_canvasdrag.cpp and SceneTreeGraphicsWidget mouse events.

#include "scenecanvasdraghandler.h"
#include "scenetreegraphicswidget.h"
#include "scenetreegraphicshelpers.h"
#include "scenetreelayout.h"

#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QLineF>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPen>
#include <QSet>
#include <QStringList>

using namespace SceneTreeGraphics;

// ── Constructor ───────────────────────────────────────────────────────────────

SceneCanvasDragHandler::SceneCanvasDragHandler(SceneTreeGraphicsWidget *widget)
    : QObject(widget)
    , m_widget(widget)
{
}

void SceneCanvasDragHandler::clearAfterSceneClear()
{
    m_canvasDragGhost = nullptr; // scene->clear() already deleted it
    m_canvasDragItems.clear();   // scene->clear() deleted these too
    m_clusterDragItems.clear();
}

// ── Mouse event handlers ──────────────────────────────────────────────────────

bool SceneCanvasDragHandler::handleMousePress(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return false;

    const QPointF scenePos = m_widget->mapToScene(event->pos());

    int groupId = 0;
    if (groupCollapseControlAt(scenePos, &groupId)) {
        toggleGroupCollapsed(groupId);
        event->accept();
        return true;
    }

    for (const SceneTreeGraphicsWidget::CanvasMoveHandle &h : m_widget->m_canvasMoveHandles) {
        if (h.gripRect.contains(scenePos)) {
            m_canvasDragPending    = true;
            m_canvasDragActive     = false;
            m_canvasDragNodeId     = h.nodeId;
            m_canvasDragPressScene = scenePos;
            m_canvasDragOrigPos    = h.blockRect.topLeft();
            m_canvasDragBlockSize  = h.blockRect.size();
            m_canvasDragCurrentPos = m_canvasDragOrigPos;
            m_canvasDragCluster.clear();
            m_canvasDragClusterOrigPos.clear();
            m_canvasDragDetached       = false;
            m_canvasDragPrevEventScene = scenePos;
            m_dbgPrevSnapped           = false;
            m_dbgLastLoggedPos         = m_canvasDragOrigPos;
            emit m_widget->canvasDrag(
                    QStringLiteral("[....] canvas-press   node=#%1  origPos=(%2,%3)"
                                   "  size=%4x%5  grip=(%6,%7 %8x%9)")
                    .arg(h.nodeId)
                    .arg(qRound(h.blockRect.x())).arg(qRound(h.blockRect.y()))
                    .arg(qRound(h.blockRect.width())).arg(qRound(h.blockRect.height()))
                    .arg(qRound(h.gripRect.x())).arg(qRound(h.gripRect.y()))
                    .arg(qRound(h.gripRect.width())).arg(qRound(h.gripRect.height())));
            m_widget->setCursor(Qt::SizeAllCursor);
            event->accept();
            return true;
        }
    }

    return false;
}

bool SceneCanvasDragHandler::handleMouseMove(QMouseEvent *event)
{
    if (!m_canvasDragPending && !m_canvasDragActive)
        return false;

    const QPointF scenePosition = m_widget->mapToScene(event->pos());
    const QPointF delta = scenePosition - m_canvasDragPressScene;
    const qreal dist = QLineF(QPointF(), delta).length();

    if (m_canvasDragPending && dist >= DragPreviewStartDistance) {
        m_canvasDragPending        = false;
        m_canvasDragActive         = true;
        m_canvasDragPrevEventScene = scenePosition;

        m_canvasDragCluster = findConnectedCluster(m_canvasDragNodeId);
        m_canvasDragClusterOrigPos.clear();
        m_clusterDragItems.clear();
        for (int nid : m_canvasDragCluster) {
            for (const SceneTreeGraphicsWidget::CanvasMoveHandle &h : m_widget->m_canvasMoveHandles) {
                if (h.nodeId == nid) {
                    m_canvasDragClusterOrigPos[nid] = h.blockRect.topLeft();
                    if (nid != m_canvasDragNodeId) {
                        m_clusterDragItems[nid] = itemsInBlockRect(h.blockRect);
                        for (QGraphicsItem *item : m_clusterDragItems[nid])
                            item->setZValue(item->zValue() + 195.0);
                    }
                    break;
                }
            }
        }

        m_canvasDragItems = itemsInBlockRect(QRectF(m_canvasDragOrigPos, m_canvasDragBlockSize));
        for (QGraphicsItem *item : m_canvasDragItems)
            item->setZValue(item->zValue() + 200.0);

        showDragPlaceholder(m_canvasDragOrigPos, m_canvasDragBlockSize);

        {
            QStringList clIds;
            for (int nid : m_canvasDragCluster) clIds << QStringLiteral("#%1").arg(nid);
            emit m_widget->canvasDrag(
                QStringLiteral("[....] canvas-start   node=#%1  cluster=[%2]"
                               "  items=%3  origPos=(%4,%5)")
                .arg(m_canvasDragNodeId)
                .arg(clIds.join(QStringLiteral(",")))
                .arg(m_canvasDragItems.size())
                .arg(qRound(m_canvasDragOrigPos.x())).arg(qRound(m_canvasDragOrigPos.y())));
        }
    }

    if (m_canvasDragActive) {
        const qreal velocity = QLineF(m_canvasDragPrevEventScene, scenePosition).length();
        m_canvasDragPrevEventScene = scenePosition;
        if (!m_canvasDragDetached && velocity > kClusterVelocityThreshold
                && m_canvasDragCluster.size() > 1) {
            m_canvasDragDetached = true;

            for (auto it = m_clusterDragItems.constBegin(); it != m_clusterDragItems.constEnd(); ++it) {
                for (QGraphicsItem *item : it.value())
                    item->setZValue(item->zValue() - 195.0);
            }
            m_clusterDragItems.clear();

            // Freeze cluster members at current visual position.
            // (a) refresh() draws them where they visually are, not pre-drag original.
            // (b) m_canvasMoveHandles is updated so magnetic snap uses correct rects.
            const QPointF detachOffset = m_canvasDragCurrentPos - m_canvasDragOrigPos;
            for (int nid : m_canvasDragCluster) {
                if (nid == m_canvasDragNodeId) continue;
                const QPointF frozenPos = m_canvasDragClusterOrigPos.value(nid) + detachOffset;
                m_widget->m_nodeCanvasPositions[nid] = frozenPos;
                for (SceneTreeGraphicsWidget::CanvasMoveHandle &h : m_widget->m_canvasMoveHandles) {
                    if (h.nodeId == nid) {
                        h.gripRect.moveTo(frozenPos);
                        h.blockRect.moveTo(frozenPos);
                        break;
                    }
                }
            }

            emit m_widget->canvasDrag(
                    QStringLiteral("[....] canvas-detach  node=#%1  vel=%2px"
                                   "  cluster had %3 members  detachOffset=(%4,%5)")
                    .arg(m_canvasDragNodeId)
                    .arg(qRound(velocity))
                    .arg(m_canvasDragCluster.size())
                    .arg(qRound(detachOffset.x())).arg(qRound(detachOffset.y())));
        }

        const QPointF rawCandidate = m_canvasDragOrigPos + delta;
        QPointF candidate = rawCandidate;

        QPointF snapped;
        const QVector<int> movingCluster = m_canvasDragDetached
                                               ? QVector<int>()
                                               : m_canvasDragCluster;
        bool magnetic = applyMagneticSnap(candidate, m_canvasDragBlockSize,
                                          m_canvasDragNodeId, &snapped,
                                          movingCluster);
        if (magnetic && QLineF(snapped, m_canvasDragOrigPos).length() < 4.0)
            magnetic = false;
        if (magnetic)
            candidate = snapped;

        {
            const bool snapChanged = (magnetic != m_dbgPrevSnapped);
            const qreal logDist = QLineF(m_dbgLastLoggedPos, candidate).length();
            if (snapChanged || logDist >= 25.0) {
                m_dbgPrevSnapped   = magnetic;
                m_dbgLastLoggedPos = candidate;
                QString msg = QStringLiteral(
                    "[....] canvas-move    node=#%1"
                    "  raw=(%2,%3)  final=(%4,%5)  snap=%6")
                    .arg(m_canvasDragNodeId)
                    .arg(qRound(rawCandidate.x())).arg(qRound(rawCandidate.y()))
                    .arg(qRound(candidate.x())).arg(qRound(candidate.y()))
                    .arg(magnetic ? QStringLiteral("YES  snappedTo=(%1,%2)")
                                        .arg(qRound(snapped.x())).arg(qRound(snapped.y()))
                                  : QStringLiteral("no"));
                if (!m_canvasDragDetached && m_canvasDragCluster.size() > 1) {
                    QStringList cl;
                    for (int nid : m_canvasDragCluster)
                        if (nid != m_canvasDragNodeId)
                            cl << QStringLiteral("#%1").arg(nid);
                    msg += QStringLiteral("  dragging-cluster=[%1]").arg(cl.join(QStringLiteral(",")));
                } else if (m_canvasDragDetached) {
                    msg += QStringLiteral("  (detached)");
                }
                emit m_widget->canvasDrag(msg);
            }
        }

        const QPointF frameDelta = candidate - m_canvasDragCurrentPos;
        m_canvasDragCurrentPos = candidate;
        m_canvasDragSnapped    = magnetic;

        for (QGraphicsItem *item : m_canvasDragItems)
            item->setPos(item->pos() + frameDelta);

        if (!m_canvasDragDetached) {
            for (auto it = m_clusterDragItems.constBegin(); it != m_clusterDragItems.constEnd(); ++it) {
                for (QGraphicsItem *item : it.value())
                    item->setPos(item->pos() + frameDelta);
            }
        }
    }

    event->accept();
    return true;
}

bool SceneCanvasDragHandler::handleMouseRelease(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || (!m_canvasDragActive && !m_canvasDragPending))
        return false;

    const bool wasActive = m_canvasDragActive;
    const int  nodeId    = m_canvasDragNodeId;

    m_canvasDragActive  = false;
    m_canvasDragPending = false;
    m_canvasDragNodeId  = 0;
    clearDragGhost();
    m_widget->setCursor(Qt::OpenHandCursor);

    if (wasActive) {
        m_widget->m_nodeCanvasPositions[nodeId] = m_canvasDragCurrentPos;

        if (!m_canvasDragDetached && m_canvasDragCluster.size() > 1) {
            const QPointF offset = m_canvasDragCurrentPos - m_canvasDragOrigPos;
            for (int nid : m_canvasDragCluster) {
                if (nid == nodeId)
                    continue;
                m_widget->m_nodeCanvasPositions[nid] = m_canvasDragClusterOrigPos.value(nid) + offset;
            }
        }

        m_canvasDragItems.clear();
        m_clusterDragItems.clear();

        {
            QStringList movedIds;
            movedIds << QStringLiteral("#%1").arg(nodeId);
            if (!m_canvasDragDetached) {
                for (int nid : m_canvasDragCluster)
                    if (nid != nodeId) movedIds << QStringLiteral("#%1").arg(nid);
            }
            emit m_widget->canvasDrag(
                QStringLiteral("[....] canvas-commit  node=#%1"
                               "  finalPos=(%2,%3)  snap=%4"
                               "  moved=[%5]  detached=%6")
                .arg(nodeId)
                .arg(qRound(m_canvasDragCurrentPos.x())).arg(qRound(m_canvasDragCurrentPos.y()))
                .arg(m_canvasDragSnapped ? QStringLiteral("yes") : QStringLiteral("no"))
                .arg(movedIds.join(QStringLiteral(",")))
                .arg(m_canvasDragDetached ? QStringLiteral("yes") : QStringLiteral("no")));
        }

        m_canvasDragCluster.clear();
        m_canvasDragClusterOrigPos.clear();
        m_widget->refresh();
    } else {
        m_widget->handleTreeNodeSelected(nodeId);
    }

    event->accept();
    return true;
}

// ── Magnetic snap ─────────────────────────────────────────────────────────────
//
// For each neighbour block, generates 16 corner-to-corner snap candidates.
// Only candidates that do NOT overlap any other block are accepted.
// The closest valid candidate within kMagnetRadius wins.

bool SceneCanvasDragHandler::applyMagneticSnap(const QPointF &candidate,
                                                const QSizeF  &size,
                                                int            excludeId,
                                                QPointF       *snapped,
                                                const QVector<int> &additionalExcludeIds) const
{
    QVector<QRectF> neighbors;
    neighbors.reserve(m_widget->m_canvasMoveHandles.size());
    for (const SceneTreeGraphicsWidget::CanvasMoveHandle &h : m_widget->m_canvasMoveHandles) {
        if (h.nodeId != excludeId
            && !additionalExcludeIds.contains(h.nodeId)
            && !h.blockRect.isEmpty()) {
            neighbors.append(h.blockRect);
        }
    }

    auto overlapsAny = [&](const QPointF &pos) -> bool {
        const QRectF placed(pos, size);
        for (const QRectF &nb : neighbors) {
            const QRectF isect = placed.intersected(nb);
            if (isect.width() > 0.5 && isect.height() > 0.5)
                return true;
        }
        return false;
    };

    const bool alreadyOverlapping = overlapsAny(candidate);
    const qreal maxDist = alreadyOverlapping ? 1.0e9 : kMagnetRadius;

    qreal   bestDist = maxDist;
    bool    found    = false;
    QPointF best     = candidate;

    for (const QRectF &nb : neighbors) {
        const QPointF nbCorners[4] = {
            { nb.left(),  nb.top()    },
            { nb.right(), nb.top()    },
            { nb.left(),  nb.bottom() },
            { nb.right(), nb.bottom() },
        };
        const QPointF ourOffsets[4] = {
            { 0,            0             },
            { size.width(), 0             },
            { 0,            size.height() },
            { size.width(), size.height() },
        };

        for (const QPointF &c : nbCorners) {
            for (const QPointF &o : ourOffsets) {
                const QPointF t = c - o;
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

QPointF SceneCanvasDragHandler::nonOverlappingCanvasPosition(const QPointF &candidate,
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

void SceneCanvasDragHandler::clearDragGhost()
{
    if (m_canvasDragGhost) {
        if (m_widget->m_graphicsScene)
            m_widget->m_graphicsScene->removeItem(m_canvasDragGhost);
        delete m_canvasDragGhost;
        m_canvasDragGhost = nullptr;
    }
}

void SceneCanvasDragHandler::showDragPlaceholder(const QPointF &pos, const QSizeF &size)
{
    clearDragGhost();
    QPainterPath path;
    path.addRoundedRect(QRectF(pos, size), CornerRadius + 1.5, CornerRadius + 1.5);
    const QPen pen(QColor(110, 125, 150, 100), 1.5, Qt::DashLine);
    m_canvasDragGhost = m_widget->m_graphicsScene->addPath(path, pen, Qt::NoBrush);
    m_canvasDragGhost->setZValue(5.0);
}

// ── Cluster movement ──────────────────────────────────────────────────────────

static bool edgeTouching(const QRectF &a, const QRectF &b, qreal tol = 1.5)
{
    const bool hOverlap = a.left() < b.right() + tol && b.left() < a.right() + tol;
    const bool vOverlap = a.top()  < b.bottom() + tol && b.top() < a.bottom() + tol;
    const bool hAdj = (qAbs(a.right() - b.left()) <= tol || qAbs(b.right() - a.left()) <= tol) && vOverlap;
    const bool vAdj = (qAbs(a.bottom() - b.top()) <= tol || qAbs(b.bottom() - a.top()) <= tol) && hOverlap;
    return hAdj || vAdj;
}

QVector<int> SceneCanvasDragHandler::findConnectedCluster(int startNodeId) const
{
    QRectF startRect;
    for (const SceneTreeGraphicsWidget::CanvasMoveHandle &h : m_widget->m_canvasMoveHandles) {
        if (h.nodeId == startNodeId) { startRect = h.blockRect; break; }
    }
    if (startRect.isEmpty())
        return { startNodeId };

    QVector<int>    cluster      = { startNodeId };
    QVector<QRectF> clusterRects = { startRect };
    bool changed = true;
    while (changed) {
        changed = false;
        for (const SceneTreeGraphicsWidget::CanvasMoveHandle &h : m_widget->m_canvasMoveHandles) {
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

QVector<QGraphicsItem *> SceneCanvasDragHandler::itemsInBlockRect(const QRectF &blockRect) const
{
    QSet<QGraphicsItem *> seen;
    QVector<QGraphicsItem *> result;
    result.reserve(32);
    for (QGraphicsItem *item : m_widget->m_treeItems) {
        if (seen.contains(item))
            continue;
        seen.insert(item);
        if (blockRect.contains(item->sceneBoundingRect().center()))
            result.append(item);
    }
    return result;
}

// ── Group collapse ────────────────────────────────────────────────────────────

bool SceneCanvasDragHandler::groupCollapseControlAt(const QPointF &scenePosition, int *groupId) const
{
    const SceneTreeLayout::GroupHitArea *best = nullptr;
    for (const SceneTreeLayout::GroupHitArea &area : m_widget->m_treeLayout.groupHitAreas()) {
        if (area.operation == SceneDocument::TreeNode::Scene)
            continue;
        const bool verticalExpanded = isVerticalHeaderOperation(area.operation) && !area.collapsed;
        const QRectF chevronRect = verticalExpanded
            ? QRectF(area.rect.left(), area.rect.bottom() - GroupHeaderHeight,
                     TransformIconWidth, GroupHeaderHeight)
            : QRectF(area.rect.right() - 34.0, area.rect.top(),
                     34.0, GroupHeaderHeight);
        if (!chevronRect.contains(scenePosition) || (best && best->depth >= area.depth))
            continue;
        best = &area;
    }
    if (!best)
        return false;
    if (groupId)
        *groupId = best->groupId;
    return true;
}

void SceneCanvasDragHandler::toggleGroupCollapsed(int groupId)
{
    if (groupId <= 0)
        return;

    const QRectF groupRect = m_widget->groupRectForNode(groupId);
    SceneTreeGraphicsWidget::CanvasMoveHandle oldRoot;
    bool hasRoot = false;
    for (const SceneTreeGraphicsWidget::CanvasMoveHandle &handle : m_widget->m_canvasMoveHandles) {
        if (handle.blockRect.contains(groupRect.center())) {
            oldRoot = handle;
            hasRoot = true;
            break;
        }
    }

    struct AttachedBlock {
        int id = 0;
        QPointF topLeft;
        bool followsRight = false;
        bool followsBottom = false;
    };
    QVector<AttachedBlock> attachedBlocks;
    if (hasRoot) {
        constexpr qreal tolerance = 1.5;
        for (const SceneTreeGraphicsWidget::CanvasMoveHandle &handle : m_widget->m_canvasMoveHandles) {
            if (handle.nodeId == oldRoot.nodeId)
                continue;
            const bool verticalOverlap = handle.blockRect.top() < oldRoot.blockRect.bottom() + tolerance
                                         && oldRoot.blockRect.top() < handle.blockRect.bottom() + tolerance;
            const bool horizontalOverlap = handle.blockRect.left() < oldRoot.blockRect.right() + tolerance
                                           && oldRoot.blockRect.left() < handle.blockRect.right() + tolerance;
            const bool followsRight = qAbs(handle.blockRect.left() - oldRoot.blockRect.right()) <= tolerance
                                      && verticalOverlap;
            const bool followsBottom = qAbs(handle.blockRect.top() - oldRoot.blockRect.bottom()) <= tolerance
                                       && horizontalOverlap;
            if (followsRight || followsBottom)
                attachedBlocks.append({handle.nodeId, handle.blockRect.topLeft(), followsRight, followsBottom});
        }
    }

    if (m_widget->m_collapsedGroupIds.contains(groupId))
        m_widget->m_collapsedGroupIds.remove(groupId);
    else
        m_widget->m_collapsedGroupIds.insert(groupId);
    m_widget->refresh();

    if (!hasRoot || attachedBlocks.isEmpty())
        return;

    QRectF newRootRect;
    for (const SceneTreeGraphicsWidget::CanvasMoveHandle &handle : m_widget->m_canvasMoveHandles) {
        if (handle.nodeId == oldRoot.nodeId) {
            newRootRect = handle.blockRect;
            break;
        }
    }
    if (!newRootRect.isValid())
        return;

    const QPointF sizeDelta(newRootRect.width() - oldRoot.blockRect.width(),
                            newRootRect.height() - oldRoot.blockRect.height());
    if (qFuzzyIsNull(sizeDelta.x()) && qFuzzyIsNull(sizeDelta.y()))
        return;
    for (const AttachedBlock &block : attachedBlocks) {
        QPointF adjusted = block.topLeft;
        if (block.followsRight)
            adjusted.rx() += sizeDelta.x();
        if (block.followsBottom)
            adjusted.ry() += sizeDelta.y();
        m_widget->m_nodeCanvasPositions[block.id] = adjusted;
    }
    m_widget->refresh();
}
