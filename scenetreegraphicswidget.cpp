#include "scenetreegraphicswidget.h"
#include "groupthumbnailcache.h"
#include "nodethumbnailcache.h"
#include "scenetreegraphicshelpers.h"
#include "scenetreepalette.h"
#include "scenetreeinlinetextinput.h"
#include "scenetreelayout.h"
#include "scenetreepreviewrenderer.h"
#include "scenetreenoderenderer.h"
#include "scenetreetoolbarrenderer.h"
#include "scenestringutils.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QToolTip>
#include <QTimer>
#include <QWheelEvent>
#include <QStringList>

using namespace SceneTreeGraphics;

namespace {

// ---------------------------------------------------------------------------
// ThemeSwitcherSwatchItem — a single clickable swatch circle in the theme
// switcher overlay at the bottom of the viewport.
// ---------------------------------------------------------------------------
class ThemeSwitcherSwatchItem : public QGraphicsEllipseItem
{
public:
    ThemeSwitcherSwatchItem(const QPointF &center,
                             qreal radius,
                             const QPen &pen,
                             const QBrush &brush,
                             int themeIndex,
                             std::function<void(int)> onClick)
        : QGraphicsEllipseItem(center.x() - radius, center.y() - radius,
                                radius * 2.0, radius * 2.0)
        , m_themeIndex(themeIndex)
        , m_onClick(std::move(onClick))
    {
        setPen(pen);
        setBrush(brush);
        setAcceptedMouseButtons(Qt::LeftButton);
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (m_onClick)
                m_onClick(m_themeIndex);
            event->accept();
        } else {
            event->ignore();
        }
    }

private:
    int m_themeIndex = 0;
    std::function<void(int)> m_onClick;
};

// ---------------------------------------------------------------------------

constexpr qreal LiveDropPreviewDurationMs = 210.0;
constexpr qreal ReleaseDropPreviewDurationMs = 95.0;
constexpr qreal DropPreviewRectTolerance = 0.5;

bool isRootOnlyTreeTool(const QString &tool)
{
    return tool == QStringLiteral("module");
}

SceneTreeLayout::DropTarget freeFloatingDropTarget(const QPointF &scenePosition,
                                                   const QSizeF &previewSize,
                                                   bool allowInsertion)
{
    SceneTreeLayout::DropTarget target;
    target.zoneRect = QRectF(scenePosition - QPointF(previewSize.width() * 0.5,
                                                     previewSize.height() * 0.5),
                             previewSize);
    if (allowInsertion) {
        target.placeholderRect = target.zoneRect;
        target.hasTarget = true;
    }
    return target;
}


qreal easeOutCubic(qreal value)
{
    const qreal t = qBound<qreal>(0.0, value, 1.0);
    const qreal inverse = 1.0 - t;
    return 1.0 - inverse * inverse * inverse;
}

qreal lerp(qreal from, qreal to, qreal progress)
{
    return from + (to - from) * progress;
}

QRectF interpolatedRect(const QRectF &from, const QRectF &to, qreal progress)
{
    if (!from.isValid())
        return to;
    if (!to.isValid())
        return from;

    return QRectF(lerp(from.left(), to.left(), progress),
                  lerp(from.top(), to.top(), progress),
                  lerp(from.width(), to.width(), progress),
                  lerp(from.height(), to.height(), progress));
}

bool rectNear(const QRectF &left, const QRectF &right)
{
    if (left.isValid() != right.isValid())
        return false;
    if (!left.isValid())
        return true;

    return qAbs(left.left() - right.left()) <= DropPreviewRectTolerance
           && qAbs(left.top() - right.top()) <= DropPreviewRectTolerance
           && qAbs(left.width() - right.width()) <= DropPreviewRectTolerance
           && qAbs(left.height() - right.height()) <= DropPreviewRectTolerance;
}

bool childListNear(const QVector<SceneTreeLayout::ChildLayout> &left,
                   const QVector<SceneTreeLayout::ChildLayout> &right)
{
    if (left.size() != right.size())
        return false;

    for (int i = 0; i < left.size(); ++i) {
        if (left[i].nodeId != right[i].nodeId
            || left[i].tool != right[i].tool
            || !rectNear(left[i].rect, right[i].rect)) {
            return false;
        }
    }

    return true;
}

bool groupPreviewListNear(const QVector<SceneTreeLayout::GroupPreview> &left,
                          const QVector<SceneTreeLayout::GroupPreview> &right)
{
    if (left.size() != right.size())
        return false;

    for (int i = 0; i < left.size(); ++i) {
        if (left[i].operation != right[i].operation
            || !rectNear(left[i].rect, right[i].rect)
            || !childListNear(left[i].children, right[i].children)) {
            return false;
        }
    }

    return true;
}

bool dropTargetNear(const SceneTreeLayout::DropTarget &left,
                    const SceneTreeLayout::DropTarget &right)
{
    return left.hasTarget == right.hasTarget
           && left.parentGroupId == right.parentGroupId
           && left.insertIndex == right.insertIndex
           && left.moduleParameterZone == right.moduleParameterZone
           && left.previewGroupOperation == right.previewGroupOperation
           && rectNear(left.zoneRect, right.zoneRect)
           && rectNear(left.sourceRect, right.sourceRect)
           && rectNear(left.sourceGroupRect, right.sourceGroupRect)
           && rectNear(left.placeholderRect, right.placeholderRect)
           && rectNear(left.slotMarkerRect, right.slotMarkerRect)
           && rectNear(left.previewGroupRect, right.previewGroupRect)
           && childListNear(left.sourceChildren, right.sourceChildren)
           && childListNear(left.previewChildren, right.previewChildren)
           && groupPreviewListNear(left.expandedGroups, right.expandedGroups);
}

QRectF previewContentBounds(const QVector<SceneTreeLayout::ChildLayout> &children,
                            const QRectF &placeholderRect)
{
    QRectF bounds = placeholderRect;
    bool hasBounds = bounds.isValid();

    for (const SceneTreeLayout::ChildLayout &child : children) {
        if (!child.rect.isValid())
            continue;
        bounds = hasBounds ? bounds.united(child.rect) : child.rect;
        hasBounds = true;
    }

    return hasBounds ? bounds : QRectF();
}

QRectF groupRectForPreviewContent(QRectF groupRect,
                                  SceneDocument::TreeNode::Operation operation,
                                  const QVector<SceneTreeLayout::ChildLayout> &children,
                                  const QRectF &placeholderRect)
{
    if (!groupRect.isValid())
        return groupRect;

    const QRectF content = previewContentBounds(children, placeholderRect);
    const qreal headerHeight = isTransformOperation(operation) ? 0.0 : GroupHeaderHeight;
    const qreal minBottom = groupRect.top() + headerHeight + GroupPadding * 2.0 + PrimitiveHeight;
    const qreal contentBottom = content.isValid() ? content.bottom() + GroupPadding : minBottom;
    groupRect.setBottom(qMax(minBottom, contentBottom));
    if (content.isValid())
        groupRect.setRight(qMax(groupRect.right(), content.right() + GroupPadding));
    return groupRect;
}

SceneTreeLayout::ChildLayout interpolatedChild(const SceneTreeLayout::ChildLayout &from,
                                               const SceneTreeLayout::ChildLayout &to,
                                               qreal progress)
{
    SceneTreeLayout::ChildLayout child = to;
    child.rect = interpolatedRect(from.rect, to.rect, progress);
    return child;
}

QVector<SceneTreeLayout::ChildLayout> interpolatedChildren(const QVector<SceneTreeLayout::ChildLayout> &from,
                                                          const QVector<SceneTreeLayout::ChildLayout> &to,
                                                          qreal progress)
{
    if (from.size() != to.size())
        return to;

    QVector<SceneTreeLayout::ChildLayout> children;
    children.reserve(to.size());
    for (int i = 0; i < to.size(); ++i)
        children.append(interpolatedChild(from[i], to[i], progress));
    return children;
}

SceneTreeLayout::DropTarget collapsedDropTarget(SceneTreeLayout::DropTarget target)
{
    if (!target.hasTarget || !target.placeholderRect.isValid())
        return target;

    const qreal slotY = target.slotMarkerRect.isValid()
                            ? target.slotMarkerRect.center().y()
                            : target.placeholderRect.center().y();
    const qreal shift = target.placeholderRect.height() + ChildGap;
    target.placeholderRect = QRectF(target.placeholderRect.left(),
                                    slotY,
                                    target.placeholderRect.width(),
                                    1.0);
    target.slotMarkerRect = target.placeholderRect;

    for (int i = qMax(0, target.insertIndex); i < target.previewChildren.size(); ++i)
        target.previewChildren[i].rect.translate(0.0, -shift);

    for (SceneTreeLayout::GroupPreview &group : target.expandedGroups) {
        for (SceneTreeLayout::ChildLayout &child : group.children) {
            if (child.rect.top() >= slotY)
                child.rect.translate(0.0, -shift);
        }
        group.rect = groupRectForPreviewContent(group.rect,
                                                group.operation,
                                                group.children,
                                                target.placeholderRect);
    }

    target.previewGroupRect = groupRectForPreviewContent(target.previewGroupRect,
                                                         target.previewGroupOperation,
                                                         target.previewChildren,
                                                         target.placeholderRect);
    return target;
}

SceneTreeLayout::GroupPreview interpolatedGroupPreview(const SceneTreeLayout::GroupPreview &from,
                                                       const SceneTreeLayout::GroupPreview &to,
                                                       qreal progress)
{
    SceneTreeLayout::GroupPreview group = to;
    group.rect = interpolatedRect(from.rect, to.rect, progress);
    group.children = interpolatedChildren(from.children, to.children, progress);
    return group;
}

SceneTreeLayout::DropTarget interpolatedDropTarget(const SceneTreeLayout::DropTarget &from,
                                                   const SceneTreeLayout::DropTarget &to,
                                                   qreal rawProgress)
{
    const qreal progress = easeOutCubic(rawProgress);
    SceneTreeLayout::DropTarget target = to;
    target.zoneRect = interpolatedRect(from.zoneRect, to.zoneRect, progress);
    target.sourceRect = interpolatedRect(from.sourceRect, to.sourceRect, progress);
    target.sourceGroupRect = interpolatedRect(from.sourceGroupRect, to.sourceGroupRect, progress);
    target.placeholderRect = interpolatedRect(from.placeholderRect, to.placeholderRect, progress);
    target.slotMarkerRect = interpolatedRect(from.slotMarkerRect, to.slotMarkerRect, progress);
    target.previewGroupRect = interpolatedRect(from.previewGroupRect, to.previewGroupRect, progress);
    target.sourceCutSeparatorY = lerp(from.sourceCutSeparatorY, to.sourceCutSeparatorY, progress);
    target.previewCutSeparatorY = lerp(from.previewCutSeparatorY, to.previewCutSeparatorY, progress);
    target.sourceChildren = interpolatedChildren(from.sourceChildren, to.sourceChildren, progress);
    target.previewChildren = interpolatedChildren(from.previewChildren, to.previewChildren, progress);

    if (from.expandedGroups.size() == to.expandedGroups.size()) {
        target.expandedGroups.clear();
        for (int i = 0; i < to.expandedGroups.size(); ++i)
            target.expandedGroups.append(interpolatedGroupPreview(from.expandedGroups[i], to.expandedGroups[i], progress));
    }

    return target;
}

} // namespace


SceneTreeGraphicsWidget::SceneTreeGraphicsWidget(QWidget *parent)
    : QGraphicsView(parent)
    , m_graphicsScene(createTreeGraphicsScene(this))
    , m_dropPreviewAnimationTimer(new QTimer(this))
{
    const QFont treeFont = sceneTreeGraphicsFont();
    setFont(treeFont);
    m_graphicsScene->setFont(treeFont);

    setScene(m_graphicsScene);
    setMinimumHeight(280);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(CanvasBackground);
    setCacheMode(QGraphicsView::CacheNone);
    setFocusPolicy(Qt::StrongFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setCursor(Qt::OpenHandCursor);

    m_inlineInput = new SceneTreeInlineTextInput(this);

    m_dropPreviewAnimationTimer->setInterval(16);
    connect(m_dropPreviewAnimationTimer, &QTimer::timeout, this, [this]() {
        advanceDropPreviewAnimation();
    });

    m_thumbnailCache = new NodeThumbnailCache(QSize(68, 68), this);
    connect(m_thumbnailCache, &NodeThumbnailCache::thumbnailsUpdated,
            this, &SceneTreeGraphicsWidget::refresh);

    m_groupThumbnailCache = new GroupThumbnailCache(QSize(64, 64), this);
    connect(m_groupThumbnailCache, &GroupThumbnailCache::thumbnailsUpdated,
            this, &SceneTreeGraphicsWidget::refresh);

}

void SceneTreeGraphicsWidget::setSceneDocument(const SceneDocument *scene)
{
    m_scene = scene;
    refresh();
}


void SceneTreeGraphicsWidget::setSelectedTreeNodeId(int nodeId)
{
    if (m_selectedTreeNodeId == nodeId)
        return;

    m_selectedTreeNodeId = nodeId;
    refresh();
}

void SceneTreeGraphicsWidget::setTreeTheme(int theme)
{
    const int clamped = qBound(0, theme, SceneTreePalette::ThemeCount - 1);
    if (m_treeTheme == clamped)
        return;
    m_treeTheme = clamped;
    refresh();
}

void SceneTreeGraphicsWidget::refresh()
{
    resetGraphicsScene();
    drawTreeOrPlaceholder();
    updateSceneRect();
    updateToolbarOverlay();
    syncThumbnailCache();
    syncGroupThumbnailCache();
}

void SceneTreeGraphicsWidget::resetGraphicsScene()
{
    clearDropPreview();
    m_graphicsScene->clear();
    m_canvasDragGhost = nullptr;  // scene->clear() already deleted it
    m_canvasDragItems.clear();    // scene->clear() deleted these too
    m_clusterDragItems.clear();
    m_treeLayout.clear();
    m_treeItems.clear();
    m_renameZones.clear();
    m_toolbarItems.clear();
    m_canvasMoveHandles.clear();
    m_treeItemsVisible = true;
}

void SceneTreeGraphicsWidget::drawTreeOrPlaceholder()
{
    if (!m_scene) {
        addLabel(m_graphicsScene,
                 QStringLiteral("Drop tree components here"),
                 QPointF(TreeX + 8.0, TreeY + 8.0),
                 QColor(105, 105, 105));
        return;
    }

    // Prune positions for nodes that no longer exist.
    {
        QSet<int> liveIds;
        for (const SceneDocument::TreeNode &c : m_scene->treeRoot().children)
            liveIds.insert(c.id);
        for (auto it = m_nodeCanvasPositions.begin(); it != m_nodeCanvasPositions.end(); ) {
            if (!liveIds.contains(it.key()))
                it = m_nodeCanvasPositions.erase(it);
            else
                ++it;
        }
    }

    // Apply the pending toolbar-drop canvas position to the most-recently-inserted
    // child that does not yet have a custom position (typically the last child).
    if (m_hasPendingInsertPos) {
        m_hasPendingInsertPos = false;
        const auto &children = m_scene->treeRoot().children;
        for (int ci = children.size() - 1; ci >= 0; --ci) {
            const int id = children[ci].id;
            if (!m_nodeCanvasPositions.contains(id)) {
                m_nodeCanvasPositions[id] = m_pendingInsertCanvasPosition;
                break;
            }
        }
    }

    const QList<QGraphicsItem *> existingItems = m_graphicsScene->items();
    QPointF autoPos(TreeX, TreeY);

    for (const SceneDocument::TreeNode &child : m_scene->treeRoot().children) {
        // Position: stored custom or auto-layout fallback.
        const QPointF blockTopLeft = m_nodeCanvasPositions.value(child.id, autoPos);
        // Draw the node content kGripStripH px below the block top (grip strip occupies top).
        const QRectF drawn = drawNode(child, blockTopLeft + QPointF(0.0, kGripStripH), 0);
        const QRectF fullBlock(blockTopLeft, QSizeF(drawn.width(), kGripStripH + drawn.height()));

        // ── Grip strip ───────────────────────────────────────────────────────
        const QRectF gripRect(blockTopLeft, QSizeF(drawn.width(), kGripStripH));

        // Background of the grip strip.
        QPainterPath gripPath;
        gripPath.addRoundedRect(gripRect.adjusted(0, 0, 0, 0), 3.0, 3.0);
        auto *gripBg = m_graphicsScene->addPath(gripPath,
                                                QPen(Qt::NoPen),
                                                QBrush(QColor(40, 50, 66, 140)));
        gripBg->setZValue(25.0);
        m_treeItems.append(gripBg);

        // Four subtle grip dots centred in the strip.
        const qreal cx = gripRect.center().x();
        const qreal cy = gripRect.center().y();
        const QColor dotCol(154, 166, 184, 215);
        for (int i = 0; i < 4; ++i) {
            const qreal x = cx - 10.5 + i * 7.0;
            auto *dot = m_graphicsScene->addEllipse(x - 1.55, cy - 1.55, 3.1, 3.1,
                                                    QPen(Qt::NoPen), QBrush(dotCol));
            dot->setZValue(26.0);
            m_treeItems.append(dot);
        }

        // Record handle and block rect for hit-testing and magnetic snap.
        m_canvasMoveHandles.append({gripRect, fullBlock, child.id});

        // Advance auto-layout cursor (whether this node is custom-positioned or not).
        autoPos.ry() += fullBlock.height() + ChildGap * 2.0;
    }

    const QList<QGraphicsItem *> allItems = m_graphicsScene->items();
    for (QGraphicsItem *item : allItems) {
        if (!existingItems.contains(item))
            m_treeItems.append(item);
    }

    // Hover overlays — drawn on top of all tree items.
    const bool hasActiveScrollControl = m_activeTransformControlNodeId > 0
                                        || m_activeShapeParameterNodeId > 0
                                        || m_activeVariableNodeId > 0
                                        || m_activeForLoopNodeId > 0
                                        || m_activeModuleCallNodeId > 0;

    auto addHoverOverlay = [this](const QRectF &rect,
                                  const QColor &fill,
                                  const QColor &border,
                                  qreal inflate,
                                  qreal radius) {
        if (!rect.isValid() || rect.isEmpty())
            return;
        QPainterPath path;
        path.addRoundedRect(rect.adjusted(-inflate, -inflate, inflate, inflate), radius, radius);
        auto *item = m_graphicsScene->addPath(path, QPen(border, 1.5), QBrush(fill));
        item->setZValue(180.0);
        m_treeItems.append(item);
    };

    if (!hasActiveScrollControl)
        addHoverOverlay(m_hoveredScrollRect,
                        QColor(100, 215, 240, 45), QColor(65, 180, 210, 155), 2.5, 5.0);
    addHoverOverlay(m_hoveredRenameRect,
                    QColor(190, 160, 245, 40), QColor(145, 108, 215, 150), 2.0, 4.0);
}

void SceneTreeGraphicsWidget::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->fillRect(rect, CanvasBackground);
    drawCanvasGrid(painter, rect, 24.0, MinorGridColor, 1);
    drawCanvasGrid(painter, rect, 96.0, MajorGridColor, 1);
}

void SceneTreeGraphicsWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Control) {
        const QPointF scenePosition = mapToScene(m_lastMousePosition);
        updateControlTooltip(mapToGlobal(m_lastMousePosition), scenePosition, true);
        updateActiveTransformControl(scenePosition, true);
        updateActiveShapeParameterControl(scenePosition, true);
        updateActiveVariableNumberControl(scenePosition, true);
        updateActiveForLoopRangeControl(scenePosition, true);
        updateActiveModuleCallParamControl(scenePosition, true);
    }

    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && m_selectedTreeNodeId > 0) {
        emit treeNodeDeleteRequested(m_selectedTreeNodeId);
        event->accept();
        return;
    }

    QGraphicsView::keyPressEvent(event);
}

void SceneTreeGraphicsWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    m_lastMousePosition = event->pos();

    // ── Canvas-move drag: check grip strip before anything else ──────────────
    if (event->button() == Qt::LeftButton) {
        const QPointF scenePos = mapToScene(event->pos());
        for (const CanvasMoveHandle &h : m_canvasMoveHandles) {
            if (h.gripRect.contains(scenePos)) {
                m_canvasDragPending    = true;
                m_canvasDragActive     = false;
                m_canvasDragNodeId     = h.nodeId;
                m_canvasDragPressScene = scenePos;
                m_canvasDragOrigPos    = h.blockRect.topLeft();
                m_canvasDragBlockSize  = h.blockRect.size();
                m_canvasDragCurrentPos = m_canvasDragOrigPos;
                // Reset cluster state for the new drag.
                m_canvasDragCluster.clear();
                m_canvasDragClusterOrigPos.clear();
                m_canvasDragDetached       = false;
                m_canvasDragPrevEventScene = scenePos;
                m_dbgPrevSnapped           = false;
                m_dbgLastLoggedPos         = m_canvasDragOrigPos;
                emit canvasDrag(
                        QStringLiteral("[....] canvas-press   node=#%1  origPos=(%2,%3)"
                                       "  size=%4x%5  grip=(%6,%7 %8x%9)")
                        .arg(h.nodeId)
                        .arg(qRound(h.blockRect.x())).arg(qRound(h.blockRect.y()))
                        .arg(qRound(h.blockRect.width())).arg(qRound(h.blockRect.height()))
                        .arg(qRound(h.gripRect.x())).arg(qRound(h.gripRect.y()))
                        .arg(qRound(h.gripRect.width())).arg(qRound(h.gripRect.height())));
                setCursor(Qt::SizeAllCursor);
                event->accept();
                return;
            }
        }
    }

    if (event->button() == Qt::RightButton && itemAt(event->pos()) == nullptr) {
        handleTreeNodeSelected(0);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && itemAt(event->pos()) == nullptr) {
        m_panning = true;
        m_lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
        const bool changed = m_hoveredScrollRect.isValid() || m_hoveredRenameRect.isValid();
        m_hoveredScrollRect = QRectF();
        m_hoveredRenameRect = QRectF();
        if (changed)
            refresh();
        event->accept();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void SceneTreeGraphicsWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_lastMousePosition = event->pos();
    const bool controlDown = event->modifiers() & Qt::ControlModifier;
    const QPointF scenePosition = mapToScene(event->pos());

    // ── Canvas-move drag ─────────────────────────────────────────────────────
    if (m_canvasDragPending || m_canvasDragActive) {
        const QPointF delta = scenePosition - m_canvasDragPressScene;
        const qreal dist = QLineF(QPointF(), delta).length();

        if (m_canvasDragPending && dist >= DragPreviewStartDistance) {
            m_canvasDragPending        = false;
            m_canvasDragActive         = true;
            m_canvasDragPrevEventScene = scenePosition; // start velocity tracking here

            // Find cluster (blocks edge-touching the dragged block).
            m_canvasDragCluster = findConnectedCluster(m_canvasDragNodeId);
            m_canvasDragClusterOrigPos.clear();
            m_clusterDragItems.clear();
            for (int nid : m_canvasDragCluster) {
                for (const CanvasMoveHandle &h : m_canvasMoveHandles) {
                    if (h.nodeId == nid) {
                        m_canvasDragClusterOrigPos[nid] = h.blockRect.topLeft();
                        if (nid != m_canvasDragNodeId) {
                            m_clusterDragItems[nid] = itemsInBlockRect(h.blockRect);
                            // Raise cluster members slightly so they render above neighbours.
                            for (QGraphicsItem *item : m_clusterDragItems[nid])
                                item->setZValue(item->zValue() + 195.0);
                        }
                        break;
                    }
                }
            }

            // Collect primary block items and raise them to the top.
            m_canvasDragItems = itemsInBlockRect(QRectF(m_canvasDragOrigPos, m_canvasDragBlockSize));
            for (QGraphicsItem *item : m_canvasDragItems)
                item->setZValue(item->zValue() + 200.0);

            // Leave a faint placeholder outline where the block started.
            showDragPlaceholder(m_canvasDragOrigPos, m_canvasDragBlockSize);

            {
                QStringList clIds;
                for (int nid : m_canvasDragCluster) clIds << QStringLiteral("#%1").arg(nid);
                emit canvasDrag(
                    QStringLiteral("[....] canvas-start   node=#%1  cluster=[%2]"
                                   "  items=%3  origPos=(%4,%5)")
                    .arg(m_canvasDragNodeId)
                    .arg(clIds.join(QStringLiteral(",")))
                    .arg(m_canvasDragItems.size())
                    .arg(qRound(m_canvasDragOrigPos.x())).arg(qRound(m_canvasDragOrigPos.y())));
            }
        }

        if (m_canvasDragActive) {
            // Velocity check — fast drag detaches block from cluster.
            const qreal velocity = QLineF(m_canvasDragPrevEventScene, scenePosition).length();
            m_canvasDragPrevEventScene = scenePosition;
            if (!m_canvasDragDetached && velocity > kClusterVelocityThreshold
                    && m_canvasDragCluster.size() > 1) {
                m_canvasDragDetached = true;

                // Restore cluster members' Z so they stop appearing "lifted".
                for (auto it = m_clusterDragItems.constBegin(); it != m_clusterDragItems.constEnd(); ++it) {
                    for (QGraphicsItem *item : it.value())
                        item->setZValue(item->zValue() - 195.0);
                }
                m_clusterDragItems.clear();

                // Freeze cluster members at their current visual position so that:
                //   (a) refresh() after commit draws them where they visually are now,
                //       not at their pre-drag original → prevents the "snap-back" bounce.
                //   (b) m_canvasMoveHandles is updated → magnetic snap during continued
                //       drag of the primary block uses the correct neighbour rects.
                const QPointF detachOffset = m_canvasDragCurrentPos - m_canvasDragOrigPos;
                for (int nid : m_canvasDragCluster) {
                    if (nid == m_canvasDragNodeId) continue;
                    const QPointF frozenPos = m_canvasDragClusterOrigPos.value(nid) + detachOffset;
                    m_nodeCanvasPositions[nid] = frozenPos;
                    for (CanvasMoveHandle &h : m_canvasMoveHandles) {
                        if (h.nodeId == nid) {
                            h.gripRect.moveTo(frozenPos);
                            h.blockRect.moveTo(frozenPos);
                            break;
                        }
                    }
                }

                emit canvasDrag(
                        QStringLiteral("[....] canvas-detach  node=#%1  vel=%2px"
                                       "  cluster had %3 members  detachOffset=(%4,%5)")
                        .arg(m_canvasDragNodeId)
                        .arg(qRound(velocity))
                        .arg(m_canvasDragCluster.size())
                        .arg(qRound(detachOffset.x())).arg(qRound(detachOffset.y())));
            }

            const QPointF rawCandidate = m_canvasDragOrigPos + delta;
            QPointF candidate = rawCandidate;

            // Magnetic snap — but skip candidates that would put the block back at its
            // original position (a neighbour that was adjacent generates exactly that).
            QPointF snapped;
            bool magnetic = applyMagneticSnap(candidate, m_canvasDragBlockSize,
                                              m_canvasDragNodeId, &snapped);
            if (magnetic && QLineF(snapped, m_canvasDragOrigPos).length() < 4.0)
                magnetic = false;
            if (magnetic)
                candidate = snapped;

            // Debug: log on snap state change or every ≥25 px of movement.
            {
                const bool snapChanged = (magnetic != m_dbgPrevSnapped);
                const qreal logDist = QLineF(m_dbgLastLoggedPos, candidate).length();
                if (snapChanged || logDist >= 25.0) {
                    m_dbgPrevSnapped    = magnetic;
                    m_dbgLastLoggedPos  = candidate;
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
                    emit canvasDrag(msg);
                }
            }

            // Move all primary-block items by the per-frame delta.
            const QPointF frameDelta = candidate - m_canvasDragCurrentPos;
            m_canvasDragCurrentPos = candidate;
            m_canvasDragSnapped    = magnetic;

            for (QGraphicsItem *item : m_canvasDragItems)
                item->setPos(item->pos() + frameDelta);

            // Move cluster members by the same delta (unless detached).
            if (!m_canvasDragDetached) {
                for (auto it = m_clusterDragItems.constBegin(); it != m_clusterDragItems.constEnd(); ++it) {
                    for (QGraphicsItem *item : it.value())
                        item->setPos(item->pos() + frameDelta);
                }
            }

            event->accept();
            return;
        }
        event->accept();
        return;
    }

    // ── Normal flow ──────────────────────────────────────────────────────────
    updateControlTooltip(event->globalPos(), scenePosition, controlDown);
    updateActiveTransformControl(scenePosition, controlDown);
    updateActiveShapeParameterControl(scenePosition, controlDown);
    updateActiveVariableNumberControl(scenePosition, controlDown);
    updateActiveForLoopRangeControl(scenePosition, controlDown);
    updateActiveModuleCallParamControl(scenePosition, controlDown);

    if (m_panning) {
        const QPoint delta = event->pos() - m_lastPanPoint;
        m_lastPanPoint = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    // Update grip cursor when hovering over a strip.
    bool onGrip = false;
    for (const CanvasMoveHandle &h : m_canvasMoveHandles) {
        if (h.gripRect.contains(scenePosition)) { onGrip = true; break; }
    }
    setCursor(onGrip ? Qt::SizeAllCursor : Qt::OpenHandCursor);

    updateHoverHighlights(scenePosition);
    QGraphicsView::mouseMoveEvent(event);
}

void SceneTreeGraphicsWidget::mouseReleaseEvent(QMouseEvent *event)
{
    // ── Canvas-move drag release ──────────────────────────────────────────────
    if (event->button() == Qt::LeftButton && (m_canvasDragActive || m_canvasDragPending)) {
        const bool wasActive = m_canvasDragActive;
        const int  nodeId    = m_canvasDragNodeId;

        m_canvasDragActive  = false;
        m_canvasDragPending = false;
        m_canvasDragNodeId  = 0;
        clearCanvasDragGhost();
        setCursor(Qt::OpenHandCursor);

        if (wasActive) {
            // Commit position of the primary block.
            m_nodeCanvasPositions[nodeId] = m_canvasDragCurrentPos;

            // Commit cluster members' final positions.
            // Slow drag (not detached): move all by the same offset as the primary.
            // Fast drag (detached): members were already frozen in m_nodeCanvasPositions
            //   at the moment of detach — nothing to do here.
            if (!m_canvasDragDetached && m_canvasDragCluster.size() > 1) {
                const QPointF offset = m_canvasDragCurrentPos - m_canvasDragOrigPos;
                for (int nid : m_canvasDragCluster) {
                    if (nid == nodeId)
                        continue;
                    m_nodeCanvasPositions[nid] = m_canvasDragClusterOrigPos.value(nid) + offset;
                }
            }

            // Clear item vectors before refresh() destroys the scene.
            m_canvasDragItems.clear();
            m_clusterDragItems.clear();

            {
                QStringList movedIds;
                movedIds << QStringLiteral("#%1").arg(nodeId);
                if (!m_canvasDragDetached) {
                    for (int nid : m_canvasDragCluster)
                        if (nid != nodeId) movedIds << QStringLiteral("#%1").arg(nid);
                }
                emit canvasDrag(
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
            refresh(); // rebuilds scene from scratch
        } else {
            // No drag — treat as block selection.
            handleTreeNodeSelected(nodeId);
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_panning) {
        m_panning = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void SceneTreeGraphicsWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QPointF scenePos = mapToScene(event->pos());
        int renameNodeId = 0;
        QRectF renameRect;
        if (hoverRenameZoneAt(scenePos, &renameNodeId, &renameRect)) {
            const SceneDocument::TreeNode *node = m_scene ? m_scene->treeNodeById(renameNodeId) : nullptr;
            if (node) {
                const bool isModule = node->type == SceneDocument::TreeNode::Group
                                      && node->operation == SceneDocument::TreeNode::Module;
                const QString currentName = isModule ? node->moduleName : node->variableName;
                startInlineRename(renameNodeId, isModule, renameRect, currentName);
                event->accept();
                return;
            }
        }
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void SceneTreeGraphicsWidget::leaveEvent(QEvent *event)
{
    QGraphicsView::leaveEvent(event);
    const bool changed = m_hoveredScrollRect.isValid() || m_hoveredRenameRect.isValid();
    m_hoveredScrollRect = QRectF();
    m_hoveredRenameRect = QRectF();
    if (!m_panning)
        setCursor(Qt::OpenHandCursor);
    if (changed && !m_dragActive)
        refresh();
}

void SceneTreeGraphicsWidget::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    updateInlineInputGeometry();
    // Clear hover state when the canvas scrolls (positions shift under the cursor).
    const bool changed = m_hoveredScrollRect.isValid() || m_hoveredRenameRect.isValid();
    m_hoveredScrollRect = QRectF();
    m_hoveredRenameRect = QRectF();
    if (changed && !m_dragActive)
        refresh(); // refresh() already calls updateToolbarOverlay()
    else
        updateToolbarOverlay();
}

void SceneTreeGraphicsWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Control) {
        updateControlTooltip(mapToGlobal(m_lastMousePosition), mapToScene(m_lastMousePosition), false);
        updateActiveTransformControl(QPointF(), false);
        updateActiveShapeParameterControl(QPointF(), false);
        updateActiveVariableNumberControl(QPointF(), false);
        updateActiveForLoopRangeControl(QPointF(), false);
        updateActiveModuleCallParamControl(QPointF(), false);
        emit ctrlReleased();
    }

    QGraphicsView::keyReleaseEvent(event);
}

void SceneTreeGraphicsWidget::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    updateInlineInputGeometry();
    updateToolbarOverlay();
}

void SceneTreeGraphicsWidget::showEvent(QShowEvent *event)
{
    QGraphicsView::showEvent(event);
    updateToolbarOverlay();
}

void SceneTreeGraphicsWidget::wheelEvent(QWheelEvent *event)
{
    if ((event->modifiers() & Qt::ControlModifier) && event->angleDelta().y() != 0) {
        const int wheelSteps = event->angleDelta().y() / 120;
        const QPointF scenePosition = mapToScene(event->position().toPoint());
        if (wheelSteps != 0 && handleForLoopRangeWheel(scenePosition, wheelSteps)) {
            event->accept();
            return;
        }
        if (wheelSteps != 0 && handleModuleCallParamWheel(scenePosition, wheelSteps)) {
            event->accept();
            return;
        }
        if (wheelSteps != 0 && handleVariableNumberWheel(scenePosition, wheelSteps)) {
            event->accept();
            return;
        }
        if (wheelSteps != 0 && handleShapeParameterWheel(scenePosition, wheelSteps)) {
            event->accept();
            return;
        }
        if (wheelSteps != 0 && handleTransformWheel(scenePosition, wheelSteps)) {
            event->accept();
            return;
        }
    }

    const qreal factor = event->angleDelta().y() > 0 ? 1.12 : 1.0 / 1.12;
    scale(factor, factor);
    updateInlineInputGeometry();
    updateToolbarOverlay();
    event->accept();
}

QRectF SceneTreeGraphicsWidget::drawToolbar()
{
    const QPointF viewportTopLeft = mapToScene(QPoint(0, 0));
    const qreal viewportWidth = viewport() ? viewport()->width() : 640.0;
    const qreal viewportScale = transform().m11();

    return SceneTreeToolbarRenderer(m_graphicsScene, &m_toolbarItems)
        .render(
            [this](const QPointF &position, const QSizeF &previewSize, const QString &previewTool) {
                showDropPreview(position, previewSize, previewTool);
            },
            [this]() { finishDropPreview(); },
            [this](const QString &toolName, const QPointF &position) {
                handleToolDrop(toolName, position);
            },
            viewportTopLeft,
            viewportWidth,
            viewportScale);
}

void SceneTreeGraphicsWidget::clearToolbar()
{
    for (QGraphicsItem *item : m_toolbarItems) {
        if (!item)
            continue;
        m_graphicsScene->removeItem(item);
        delete item;
    }
    m_toolbarItems.clear();
}

void SceneTreeGraphicsWidget::updateToolbarOverlay()
{
    if (!m_graphicsScene)
        return;

    clearToolbar();
    drawToolbar();
    drawThemeSwitcher();
}

void SceneTreeGraphicsWidget::handleThemeSwitcherClick(int themeIndex)
{
    const int clamped = qBound(0, themeIndex, SceneTreePalette::ThemeCount - 1);
    if (m_treeTheme == clamped)
        return;
    m_treeTheme = clamped;
    // refresh() rebuilds the whole scene including the toolbar overlay.
    refresh();
}

void SceneTreeGraphicsWidget::drawThemeSwitcher()
{
    if (!m_graphicsScene || !viewport())
        return;

    const QPointF viewportTopLeft = mapToScene(QPoint(0, 0));
    const qreal viewportHeight = viewport()->height();
    const qreal viewportScale  = transform().m11();
    const qreal safeScale = qMax<qreal>(0.001, std::abs(viewportScale));

    const auto scenePoint = [&](qreal x, qreal y) {
        return viewportTopLeft + QPointF(x / safeScale, y / safeScale);
    };

    // Geometry constants (all in logical viewport pixels before scale application).
    constexpr qreal SwatchR     =  7.0;   // circle radius
    constexpr qreal SwatchGap   =  5.0;   // gap between circles
    constexpr qreal PadH        =  7.0;   // panel left/right padding
    constexpr qreal PadV        =  6.0;   // panel top/bottom padding
    constexpr qreal BottomGap   = 12.0;   // distance from bottom viewport edge

    // Use the same OverlayZ as the toolbar (defined in scenetreetoolbarrenderer.cpp).
    // We replicate the constant here to keep the file self-contained.
    constexpr qreal LocalOverlayZ = 10000.0;

    const int n = SceneTreePalette::ThemeCount;
    const qreal panelW = n * (SwatchR * 2.0) + (n - 1) * SwatchGap + PadH * 2.0;
    const qreal panelH = SwatchR * 2.0 + PadV * 2.0;

    const QRectF panelLocal(0.0, 0.0, panelW, panelH);
    const QPointF panelTopLeft = scenePoint(12.0,                           // OverlayMargin
                                             viewportHeight - BottomGap - panelH);

    // Drop shadow.
    auto *shadow = m_graphicsScene->addRect(panelLocal.translated(2.0, 3.0),
                                             Qt::NoPen,
                                             QBrush(QColor(0, 0, 0, 90)));
    shadow->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    shadow->setPos(panelTopLeft);
    shadow->setZValue(LocalOverlayZ - 2.0);
    shadow->setOpacity(0.65);
    m_toolbarItems.append(shadow);

    // Glass panel background — same style as toolbar.
    QPainterPath panelPath;
    panelPath.addRoundedRect(panelLocal, CornerRadius, CornerRadius);
    auto *panel = m_graphicsScene->addPath(panelPath,
                                            QPen(QColor(148, 163, 184, 82), 1.0),
                                            QBrush(QColor(10, 16, 24, 178)));
    panel->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    panel->setPos(panelTopLeft);
    panel->setZValue(LocalOverlayZ - 1.0);
    m_toolbarItems.append(panel);

    // One swatch circle per theme.
    for (int i = 0; i < n; ++i) {
        const auto  th      = static_cast<SceneTreePalette::Theme>(i);
        const bool  active  = (i == m_treeTheme);
        const QColor swatch = SceneTreePalette::swatchColor(th);
        const qreal cx = PadH + i * (SwatchR * 2.0 + SwatchGap) + SwatchR;
        const qreal cy = PadV + SwatchR;

        // Active ring (drawn first, below the fill circle).
        if (active) {
            const qreal ringR = SwatchR + 2.8;
            auto *ring = new ThemeSwitcherSwatchItem(
                QPointF(cx, cy), ringR,
                QPen(QColor(255, 255, 255, 210), 1.6),
                Qt::NoBrush,
                i, [this](int idx) { handleThemeSwitcherClick(idx); });
            ring->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
            ring->setPos(panelTopLeft);
            ring->setZValue(LocalOverlayZ);
            m_graphicsScene->addItem(ring);
            m_toolbarItems.append(ring);
        }

        // Filled swatch circle.
        const QPen swatchPen = active
            ? QPen(Qt::NoPen)
            : QPen(swatch.darker(150), 1.0);
        auto *circle = new ThemeSwitcherSwatchItem(
            QPointF(cx, cy), SwatchR,
            swatchPen,
            QBrush(swatch),
            i, [this](int idx) { handleThemeSwitcherClick(idx); });
        circle->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        circle->setPos(panelTopLeft);
        circle->setZValue(LocalOverlayZ + 0.5);
        m_graphicsScene->addItem(circle);
        m_toolbarItems.append(circle);
    }
}

void SceneTreeGraphicsWidget::addNodeDragHandle(int nodeId,
                                                const QString &label,
                                                const QRectF &handleRect,
                                                const QRectF &sourceRect,
                                                const QSizeF &previewSize)
{
    auto *handle = createTreeNodeDragHandleItem(
        nodeId,
        label,
        handleRect,
        sourceRect,
        [this](int selectedNodeId) { handleTreeNodeSelected(selectedNodeId); },
        previewSize,
        [this, nodeId](const QPointF &position, const QSizeF &size, const QString &tool) {
            showDropPreview(position, size, tool, nodeId);
        },
        [this]() { finishDropPreview(); },
        [this](int droppedNodeId, const QPointF &position) { handleTreeNodeDrop(droppedNodeId, position); });

    m_graphicsScene->addItem(handle);
}

QRectF SceneTreeGraphicsWidget::drawNode(const SceneDocument::TreeNode &node, const QPointF &topLeft, int depth)
{
    if (node.type == SceneDocument::TreeNode::Primitive)
        return drawPrimitive(node, topLeft);
    if (node.type == SceneDocument::TreeNode::ModuleCall)
        return drawModuleCall(node, topLeft);
    if (node.type == SceneDocument::TreeNode::Variable) {
        const QRectF rect(topLeft, variablePreviewSize(node.variableName, node.variableExpression));
        SceneTreeNodeRenderer(m_graphicsScene,
                              m_selectedTreeNodeId,
                              nullptr,
                              0,   // activeTransformNodeId
                              -1,  // activeTransformAxis
                              -1,  // activeTransformNumberStart
                              0,   // activeShapeNodeId
                              -1,  // activeShapeParameter
                              -1,  // activeShapeParamNumberStart
                              m_activeVariableNodeId,
                              m_activeVariableNumberStart,
                              0,
                              -1)
            .setTheme(m_treeTheme)
            .renderVariable(node, rect);
        // Pass the canonical tool name ("var"/"par"), not the variable name.
        // renderPreviewTool() does not recognise arbitrary variable names and falls
        // back to drawing a cube — the user would see a cube ghost when dragging a variable.
        addNodeDragHandle(node.id,
                          node.isParameter ? QStringLiteral("par") : QStringLiteral("var"),
                          rect, rect, rect.size());

        // Register rename zone for the variable name text (badge = 38px, name follows).
        const QFontMetricsF metrics(sceneTreeGraphicsFont());
        const qreal nameW = qMax(metrics.horizontalAdvance(node.variableName), 24.0);
        m_renameZones.append({QRectF(rect.left() + 38.0, rect.top(), nameW, VariableHeight),
                              node.id, false, node.variableName});

        return rect;
    }

    return drawGroup(node, topLeft, depth);
}

QRectF SceneTreeGraphicsWidget::drawPrimitive(const SceneDocument::TreeNode &node, const QPointF &topLeft)
{
    const ShapeNode *shape = m_scene ? m_scene->shapeById(node.shapeId) : nullptr;
    const QSizeF size = shape ? primitivePreviewSize(*shape) : QSizeF(PrimitiveWidth, PrimitiveHeight);
    const QRectF rect(topLeft, size);
    const QString label = labelForPrimitive(node.shapeId);
    const QImage thumbnail = m_thumbnailCache ? m_thumbnailCache->thumbnail(node.id) : QImage();

    SceneTreeNodeRenderer(m_graphicsScene,
                          m_selectedTreeNodeId,
                          nullptr,
                          0,
                          -1,
                          -1,
                          m_activeShapeParameterNodeId,
                          m_activeShapeParameter,
                          m_activeShapeParameterNumberStart,
                          0,
                          -1,
                          0,
                          -1)
        .renderPrimitive(node, rect, label, shape, thumbnail);

    addNodeDragHandle(node.id, label, rect, rect, size);
    return rect;
}

QRectF SceneTreeGraphicsWidget::drawModuleCall(const SceneDocument::TreeNode &node, const QPointF &topLeft)
{
    QVector<ModuleCallParam> params;
    if (m_scene) {
        const SceneDocument::TreeNode *modGroup = m_scene->treeNodeById(node.shapeId);
        if (modGroup && modGroup->operation == SceneDocument::TreeNode::Module) {
            const QHash<QString, QString> overrides = resolveModuleArguments(node.moduleCallArguments, *modGroup);
            for (const SceneDocument::TreeNode &child : modGroup->children) {
                if (child.type == SceneDocument::TreeNode::Variable && child.isParameter) {
                    const QString expr = overrides.value(child.variableName,
                                                         child.variableExpression.trimmed().isEmpty()
                                                             ? QString::number(child.variableValue)
                                                             : child.variableExpression.trimmed());
                    params.append({child.id, child.variableName, expr});
                }
            }
        }
    }

    const QSizeF size = moduleCallPreviewSize(node.moduleName, params);
    const QRectF rect(topLeft, size);

    const int activeMCVarNodeId = (m_activeModuleCallNodeId == node.id) ? m_activeModuleCallVarNodeId : 0;
    const int activeMCNumberStart = (m_activeModuleCallNodeId == node.id) ? m_activeModuleCallNumberStart : -1;

    SceneTreeNodeRenderer(m_graphicsScene,
                          m_selectedTreeNodeId,
                          [this](int nodeId) { handleTreeNodeSelected(nodeId); },
                          0, -1, -1, 0, -1, -1, 0, -1, 0, -1,
                          node.id, activeMCVarNodeId, activeMCNumberStart)
        .renderModuleCall(node, rect, params);

    // Pass "call" (canonical tool name) not the module name — same reason as "var".
    addNodeDragHandle(node.id, QStringLiteral("call"), rect, rect, rect.size());
    return rect;
}

QRectF SceneTreeGraphicsWidget::drawGroup(const SceneDocument::TreeNode &node, const QPointF &topLeft, int depth)
{
    QVector<ChildLayout> children;
    const bool transformGroup = isTransformOperation(node.operation);
    const qreal headerWidth = transformGroup ? transformHeaderWidthForNode(node) : 0.0;
    const qreal headerHeight = transformGroup ? 0.0 : GroupHeaderHeight;
    QPointF childTopLeft(topLeft.x() + headerWidth + GroupPadding, topLeft.y() + headerHeight + GroupPadding);
    qreal maxChildWidth = 0.0;
    int moduleParameterCount = 0;
    qreal moduleParameterSeparatorY = 0.0;
    qreal moduleCallTemplateLabelY = 0.0;
    QRectF moduleCallTemplateRect;
    QVector<ModuleCallParam> moduleCallTemplateParams;

    auto drawChild = [&](const SceneDocument::TreeNode &child) {
        const QRectF childRect = drawNode(child, childTopLeft, depth + 1);
        children.append({childRect, previewToolForNode(child), child.id});
        maxChildWidth = qMax(maxChildWidth, childRect.width());
        childTopLeft.ry() += childRect.height() + ChildGap;
    };

    if (node.operation == SceneDocument::TreeNode::Module) {
        const qreal labelSpace = 16.0;
        childTopLeft.ry() += labelSpace;
        for (const SceneDocument::TreeNode &child : node.children) {
            if (child.type == SceneDocument::TreeNode::Variable && child.isParameter) {
                drawChild(child);
                ++moduleParameterCount;
            }
        }
        if (moduleParameterCount == 0)
            childTopLeft.ry() += VariableHeight + ChildGap;
        moduleParameterSeparatorY = childTopLeft.y() + ChildGap * 0.5;

        for (const SceneDocument::TreeNode &child : node.children) {
            if (child.type == SceneDocument::TreeNode::Variable && child.isParameter) {
                const QString expr = child.variableExpression.trimmed().isEmpty()
                                         ? QString::number(child.variableValue)
                                         : child.variableExpression.trimmed();
                moduleCallTemplateParams.append({child.id, child.variableName, expr});
            }
        }
        childTopLeft.ry() += 16.0 + ChildGap;
        moduleCallTemplateLabelY = moduleParameterSeparatorY + 4.0;
        moduleCallTemplateRect = QRectF(childTopLeft, moduleCallPreviewSize(node.moduleName, moduleCallTemplateParams));
        maxChildWidth = qMax(maxChildWidth, moduleCallTemplateRect.width());
        childTopLeft.ry() += moduleCallTemplateRect.height() + ChildGap;

        childTopLeft.ry() += labelSpace + ChildGap;
        for (const SceneDocument::TreeNode &child : node.children) {
            if (!(child.type == SceneDocument::TreeNode::Variable && child.isParameter))
                drawChild(child);
        }
    } else {
        for (const SceneDocument::TreeNode &child : node.children)
            drawChild(child);
    }

    qreal childrenHeight = children.isEmpty()
                               ? PrimitiveHeight
                               : childTopLeft.y() - topLeft.y() - headerHeight - GroupPadding - ChildGap;
    if (node.operation == SceneDocument::TreeNode::Module)
        childrenHeight = qMax(childrenHeight + 10.0, VariableHeight * 2.0 + ChildGap * 7.0);
    if (node.operation == SceneDocument::TreeNode::Difference)
        childrenHeight = qMax(childrenHeight, DifferenceMinContentHeight);

    // For a for-loop the header text can be much wider than the children.
    // Measure it so the card is never narrower than the rendered range expression.
    qreal forLoopHeaderMinWidth = 0.0;
    if (node.operation == SceneDocument::TreeNode::For) {
        const QFontMetricsF fm(sceneTreeGraphicsFont());
        const QString varName   = forLoopVariableName(node);
        const QString rangeExpr = forLoopRangeExpression(node);
        const QString prefix    = QStringLiteral("for (%1 = ").arg(varName);
        // 68 = grip + operation icon + label gap, 8 = right padding inside card
        forLoopHeaderMinWidth = 68.0
            + fm.horizontalAdvance(prefix)
            + fm.horizontalAdvance(rangeExpr)
            + fm.horizontalAdvance(QStringLiteral(")"))
            + 8.0;
    }

    const QSizeF size(qMax(qMax(minimumWidthForOperation(node.operation),
                                headerWidth + maxChildWidth + GroupPadding * 2.0),
                           forLoopHeaderMinWidth),
                      headerHeight + GroupPadding * 2.0 + childrenHeight);
    const QRectF rect(topLeft, size);
    qreal cutSeparatorY = 0.0;
    if (node.operation == SceneDocument::TreeNode::Difference) {
        cutSeparatorY = rect.top() + GroupHeaderHeight + GroupPadding + PrimitiveHeight + ChildGap * 0.5;
        if (!children.isEmpty())
            cutSeparatorY = children.first().rect.bottom() + ChildGap * 0.5;
    }
    m_treeLayout.addGroup({rect, node.id, depth, node.operation, cutSeparatorY, moduleParameterSeparatorY, moduleParameterCount, children});

    const QImage groupThumbnail = (m_groupThumbnailCache && GroupThumbnailCache::isEligibleOperation(node.operation))
                                      ? m_groupThumbnailCache->thumbnail(node.id)
                                      : QImage();

    SceneTreeNodeRenderer(m_graphicsScene,
                          m_selectedTreeNodeId,
                          [this](int nodeId) { handleTreeNodeSelected(nodeId); },
                          m_activeTransformControlNodeId,
                          m_activeTransformControlAxis,
                          m_activeTransformControlNumberStart,
                          0,
                          -1,
                          -1,
                          0,
                          -1,
                          m_activeForLoopNodeId,
                          m_activeForLoopNumberStart)
        .setTheme(m_treeTheme)
        .renderGroup(node, rect, depth, cutSeparatorY, groupThumbnail);

    if (node.operation == SceneDocument::TreeNode::Module) {
        const qreal labelLeft = rect.left() + GroupPadding + PrimitiveIconSize + 10.0;
        const qreal labelRight = rect.right() - GroupPadding;
        auto *paramsLabel = m_graphicsScene->addSimpleText(QStringLiteral("parameters"));
        paramsLabel->setBrush(QColor(84, 95, 116));
        if (paramsLabel->boundingRect().width() > labelRight - labelLeft)
            paramsLabel->setScale(qMax<qreal>(0.72, (labelRight - labelLeft) / paramsLabel->boundingRect().width()));
        paramsLabel->setPos(labelLeft, rect.top() + GroupHeaderHeight + 4.0);
        paramsLabel->setZValue(depth * 10.0 + 8.0);

        auto *callLabel = m_graphicsScene->addSimpleText(QStringLiteral("call handle"));
        callLabel->setBrush(QColor(84, 95, 116));
        if (callLabel->boundingRect().width() > labelRight - (rect.left() + GroupPadding))
            callLabel->setScale(qMax<qreal>(0.72, (labelRight - rect.left() - GroupPadding) / callLabel->boundingRect().width()));
        const qreal callLabelY = qMax(moduleCallTemplateLabelY,
                                      moduleCallTemplateRect.top() - callLabel->boundingRect().height() - 3.0);
        callLabel->setPos(rect.left() + GroupPadding, callLabelY);
        callLabel->setZValue(depth * 10.0 + 8.0);

        SceneDocument::TreeNode callTemplate;
        callTemplate.id = 0;
        callTemplate.type = SceneDocument::TreeNode::ModuleCall;
        callTemplate.shapeId = node.id;
        callTemplate.moduleName = node.moduleName;
        SceneTreeNodeRenderer(m_graphicsScene,
                              -1,
                              nullptr,
                              0,
                              -1,
                              -1,
                              0,
                              -1,
                              -1,
                              0,
                              -1,
                              0,
                              -1)
            .renderModuleCall(callTemplate, moduleCallTemplateRect, moduleCallTemplateParams);
        addNodeDragHandle(-node.id,
                          QStringLiteral("call"),
                          moduleCallTemplateRect,
                          moduleCallTemplateRect,
                          moduleCallTemplateRect.size());

        // Register rename zone for the module name inside the call-handle card.
        // The module name text starts at x = cardLeft + 46 (after the CALL badge).
        {
            const QFontMetricsF metrics(sceneTreeGraphicsFont());
            const qreal nameW = qMax(metrics.horizontalAdvance(node.moduleName), 24.0);
            m_renameZones.append({QRectF(moduleCallTemplateRect.left() + 46.0,
                                         moduleCallTemplateRect.top(),
                                         nameW,
                                         VariableHeight),
                                  node.id, true, node.moduleName});
        }

        const qreal bodyTop = moduleCallTemplateRect.bottom() + ChildGap + 4.0;
        auto *bodyLabel = m_graphicsScene->addSimpleText(QStringLiteral("body"));
        bodyLabel->setBrush(QColor(84, 95, 116));
        if (bodyTop + bodyLabel->boundingRect().height() <= rect.bottom() - GroupPadding) {
            bodyLabel->setPos(rect.left() + GroupPadding, bodyTop);
            bodyLabel->setZValue(depth * 10.0 + 8.0);
        } else {
            delete bodyLabel;
        }

        auto *separator = m_graphicsScene->addLine(rect.left() + GroupPadding,
                                                  moduleParameterSeparatorY,
                                                  rect.right() - GroupPadding,
                                                  moduleParameterSeparatorY,
                                                  QPen(QColor(142, 151, 166), 1, Qt::DashLine));
        separator->setZValue(depth * 10.0 + 7.0);
    }

    const QString groupLabel = labelForOperation(node.operation);
    // Scene is the permanent top-level container — it cannot be dragged or moved.
    // Root-level groups (depth == 0) are repositioned via the canvas-move grip strip,
    // not via the tree-structure drag handle — which always returns no-target for them.
    if (node.operation != SceneDocument::TreeNode::Scene && depth > 0) {
        const QRectF handleRect = transformGroup
                                      ? QRectF(rect.topLeft(), QSizeF(headerWidth, rect.height()))
                                      : QRectF(rect.topLeft(), QSizeF(rect.width(), GroupHeaderHeight));
        addNodeDragHandle(node.id, groupLabel, handleRect, rect, rect.size());
    }

    return rect;
}

void SceneTreeGraphicsWidget::handleToolDrop(const QString &toolName, const QPointF &scenePosition)
{
    const DropTarget target = dropTargetForToolAt(scenePosition,
                                                  previewSizeForTool(toolName),
                                                  toolName,
                                                  0,
                                                  false);
    if (!target.hasTarget && !isRootOnlyTreeTool(toolName)) {
        clearDropPreview();
        return;
    }

    // For root-level tools (modules) store the snapped canvas position so that
    // drawTreeOrPlaceholder() can place the newly-created block there.
    if (isRootOnlyTreeTool(toolName) && target.zoneRect.isValid()) {
        m_pendingInsertCanvasPosition = target.zoneRect.topLeft();
        m_hasPendingInsertPos = true;
    }

    scheduleDropCommit([this, toolName, target]() {
        emit toolDropped(toolName,
                         target.parentGroupId,
                         target.moduleParameterZone ? -100000 - target.insertIndex : target.insertIndex);
    });
}

void SceneTreeGraphicsWidget::handleModuleCallTemplateDrop(int moduleGroupId, const QPointF &scenePosition)
{
    if (moduleGroupId <= 0 || !m_scene) {
        clearDropPreview();
        return;
    }

    const SceneDocument::TreeNode *module = m_scene->treeNodeById(moduleGroupId);
    if (!module || module->type != SceneDocument::TreeNode::Group
        || module->operation != SceneDocument::TreeNode::Module) {
        clearDropPreview();
        return;
    }

    QVector<ModuleCallParam> params;
    for (const SceneDocument::TreeNode &child : module->children) {
        if (child.type != SceneDocument::TreeNode::Variable || !child.isParameter)
            continue;
        const QString expression = child.variableExpression.trimmed().isEmpty()
                                       ? QString::number(child.variableValue)
                                       : child.variableExpression.trimmed();
        params.append({child.id, child.variableName, expression});
    }

    const DropTarget target = dropTargetForToolAt(scenePosition,
                                                  moduleCallPreviewSize(module->moduleName, params),
                                                  QStringLiteral("call"),
                                                  0,
                                                  false);
    if (!target.hasTarget) {
        clearDropPreview();
        return;
    }

    scheduleDropCommit([this, moduleGroupId, target]() {
        emit moduleCallDropped(moduleGroupId,
                               target.parentGroupId,
                               target.moduleParameterZone ? -100000 - target.insertIndex : target.insertIndex);
    });
}

QString SceneTreeGraphicsWidget::previewToolForNode(const SceneDocument::TreeNode &node) const
{
    if (node.type == SceneDocument::TreeNode::Variable)
        return node.isParameter ? QStringLiteral("par") : QStringLiteral("var");
    if (node.type == SceneDocument::TreeNode::ModuleCall)
        return QStringLiteral("call");
    if (node.type != SceneDocument::TreeNode::Primitive)
        return labelForOperation(node.operation);

    const ShapeNode *shape = m_scene ? m_scene->shapeById(node.shapeId) : nullptr;
    return toolNameForPrimitiveType(shape ? shape->type : ShapeNode::Cube);
}

SceneTreeGraphicsWidget::DropTarget SceneTreeGraphicsWidget::dropTargetForToolAt(const QPointF &scenePosition,
                                                                                 const QSizeF &previewSize,
                                                                                 const QString &previewTool,
                                                                                 int movingNodeId,
                                                                                 bool allowFreeFloatingInsertion) const
{
    const QSizeF effectivePreviewSize = previewSize.isValid() ? previewSize : defaultPreviewSize();
    if (isRootOnlyTreeTool(previewTool)) {
        // Compute candidate top-left (centered on cursor), then apply magnetic snap
        // so the drop preview snaps to nearby blocks just like canvas-move drag does.
        const QPointF candidateTL = scenePosition - QPointF(effectivePreviewSize.width() * 0.5,
                                                             effectivePreviewSize.height() * 0.5);
        QPointF snappedTL;
        const bool magnetic = applyMagneticSnap(candidateTL, effectivePreviewSize, 0, &snappedTL);
        const QPointF finalTL = magnetic ? snappedTL : candidateTL;
        DropTarget target;
        target.zoneRect = QRectF(finalTL, effectivePreviewSize);
        if (allowFreeFloatingInsertion) {
            target.placeholderRect = target.zoneRect;
            target.hasTarget = true;
        }
        return target;
    }

    DropTarget target = m_treeLayout.dropTargetAt(scenePosition,
                                                  effectivePreviewSize,
                                                  movingNodeId,
                                                  isVariableToolName(previewTool));
    if (!target.zoneRect.isValid())
        target = freeFloatingDropTarget(scenePosition, effectivePreviewSize, allowFreeFloatingInsertion);

    if (isVariableToolName(previewTool) && m_scene) {
        const int rootId = m_scene->treeRoot().id;
        const SceneDocument::TreeNode *targetNode = m_scene->treeNodeById(target.parentGroupId);
        const bool targetIsModule = targetNode
                                    && targetNode->type == SceneDocument::TreeNode::Group
                                    && targetNode->operation == SceneDocument::TreeNode::Module;
        const bool targetIsScene = targetNode
                                   && targetNode->type == SceneDocument::TreeNode::Group
                                   && targetNode->operation == SceneDocument::TreeNode::Scene;
        if (target.parentGroupId > 0 && target.parentGroupId != rootId && !targetIsModule && !targetIsScene) {
            // Reject the drop target but preserve source preview data so the
            // animation can smoothly show the source group collapsing rather
            // than freezing on a phantom union rectangle.
            DropTarget noTarget;
            noTarget.sourceGroupRect    = target.sourceGroupRect;
            noTarget.sourceGroupOperation = target.sourceGroupOperation;
            noTarget.sourceCutSeparatorY = target.sourceCutSeparatorY;
            noTarget.sourceChildren     = target.sourceChildren;
            noTarget.sourceRect         = target.sourceRect;
            return noTarget;
        }
    }

    if (previewTool == QStringLiteral("call") && m_scene) {
        const SceneDocument::TreeNode *targetNode = m_scene->treeNodeById(target.parentGroupId);
        if (!targetNode || targetNode->type != SceneDocument::TreeNode::Group)
            return DropTarget();

        for (const GroupHitArea &area : m_treeLayout.groupHitAreas()) {
            if (area.groupId != target.parentGroupId
                || area.operation != SceneDocument::TreeNode::Module
                || area.moduleParameterSeparatorY <= 0.0) {
                continue;
            }

            if (scenePosition.y() < area.moduleParameterSeparatorY)
                return DropTarget();

            target.insertIndex = qMax(target.insertIndex, area.moduleParameterCount);
            break;
        }
    }

    if (movingNodeId > 0 && m_scene) {
        const SceneDocument::TreeNode *movingNode = m_scene->treeNodeById(movingNodeId);
        if (movingNode && movingNode->type == SceneDocument::TreeNode::ModuleCall) {
            const SceneDocument::TreeNode *targetNode = m_scene->treeNodeById(target.parentGroupId);
            if (!targetNode || targetNode->type != SceneDocument::TreeNode::Group)
                return DropTarget();
        }
    }

    return target;
}

void SceneTreeGraphicsWidget::handleTreeNodeDrop(int nodeId, const QPointF &scenePosition)
{
    if (nodeId < 0) {
        handleModuleCallTemplateDrop(-nodeId, scenePosition);
        return;
    }

    QSizeF previewSize = defaultPreviewSize();
    QString previewTool;
    if (m_scene) {
        const SceneDocument::TreeNode *node = m_scene->treeNodeById(nodeId);
        if (node) {
            previewTool = previewToolForNode(*node);
            if (node->type == SceneDocument::TreeNode::Variable) {
                previewSize = variablePreviewSize(node->variableName, node->variableExpression);
            } else if (node->type == SceneDocument::TreeNode::ModuleCall) {
                QVector<ModuleCallParam> params;
                const SceneDocument::TreeNode *modGroup = m_scene->treeNodeById(node->shapeId);
                if (modGroup && modGroup->operation == SceneDocument::TreeNode::Module) {
                    const QHash<QString, QString> overrides = resolveModuleArguments(node->moduleCallArguments, *modGroup);
                    for (const SceneDocument::TreeNode &paramNode : modGroup->children) {
                        if (paramNode.type != SceneDocument::TreeNode::Variable || !paramNode.isParameter)
                            continue;
                        const QString expression = overrides.value(paramNode.variableName,
                                                                   paramNode.variableExpression.trimmed().isEmpty()
                                                                       ? QString::number(paramNode.variableValue)
                                                                       : paramNode.variableExpression.trimmed());
                        params.append({paramNode.id, paramNode.variableName, expression});
                    }
                }
                previewSize = moduleCallPreviewSize(node->moduleName, params);
            } else {
                previewSize = previewSizeForTool(previewTool);
            }
        }
    }

    const DropTarget target = dropTargetForToolAt(scenePosition,
                                                  previewSize,
                                                  previewTool,
                                                  nodeId,
                                                  false);
    if (!target.hasTarget) {
        if (!target.zoneRect.isValid()) {
            clearDropPreview();
            return;
        }

        const SceneDocument::TreeNode *node = m_scene ? m_scene->treeNodeById(nodeId) : nullptr;
        const bool moduleDeclaration = node
                                       && node->type == SceneDocument::TreeNode::Group
                                       && node->operation == SceneDocument::TreeNode::Module;
        if (moduleDeclaration) {
            clearDropPreview();
            return;
        }

        scheduleDropCommit([this, nodeId]() {
            emit treeNodeDeleteRequested(nodeId);
        });
        return;
    }

    scheduleDropCommit([this, nodeId, target]() {
        emit treeNodeDropped(nodeId,
                             target.parentGroupId,
                             target.moduleParameterZone ? -100000 - target.insertIndex : target.insertIndex);
    });
}

void SceneTreeGraphicsWidget::handleTreeNodeSelected(int nodeId)
{
    setFocus();
    m_selectedTreeNodeId = nodeId;
    refresh();
    emit treeNodeSelected(nodeId);
}

bool SceneTreeGraphicsWidget::handleTransformWheel(const QPointF &scenePosition, int wheelSteps)
{
    if (!m_scene)
        return false;

    int groupId = 0;
    int axis = -1;
    int start = -1;
    int length = 0;
    SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
    if (!transformControlAt(scenePosition, &groupId, &operation, &axis, &start, &length))
        return false;
    if (start < 0 || length <= 0)
        return false;

    emit transformValueAdjusted(groupId, axis, start, length, static_cast<qreal>(wheelSteps));
    updateActiveTransformControl(scenePosition, true);
    return true;
}

bool SceneTreeGraphicsWidget::handleShapeParameterWheel(const QPointF &scenePosition, int wheelSteps)
{
    if (!m_scene)
        return false;

    int shapeId = -1;
    int nodeId = 0;
    int paramIndex = -1;
    int start = -1;
    int length = 0;
    if (!shapeParameterControlAt(scenePosition, &shapeId, &nodeId, &paramIndex, &start, &length))
        return false;

    emit shapeParameterAdjusted(nodeId, paramIndex, start, length, static_cast<qreal>(wheelSteps));
    updateActiveShapeParameterControl(scenePosition, true);
    Q_UNUSED(shapeId);
    return true;
}

bool SceneTreeGraphicsWidget::handleVariableNumberWheel(const QPointF &scenePosition, int wheelSteps)
{
    if (!m_scene)
        return false;

    int nodeId = 0;
    int start = -1;
    int length = 0;
    if (!variableNumberControlAt(scenePosition, &nodeId, &start, &length))
        return false;

    emit variableNumberAdjusted(nodeId, start, length, wheelSteps);
    updateActiveVariableNumberControl(scenePosition, true);
    return true;
}

bool SceneTreeGraphicsWidget::handleForLoopRangeWheel(const QPointF &scenePosition, int wheelSteps)
{
    if (!m_scene)
        return false;

    int nodeId = 0;
    int start = -1;
    int length = 0;
    if (!forLoopRangeControlAt(scenePosition, &nodeId, &start, &length))
        return false;

    emit forLoopRangeAdjusted(nodeId, start, length, wheelSteps);
    updateActiveForLoopRangeControl(scenePosition, true);
    return true;
}

bool SceneTreeGraphicsWidget::transformControlAt(const QPointF &scenePosition,
                                                 int *groupId,
                                                 SceneDocument::TreeNode::Operation *operation,
                                                 int *axisOut,
                                                 int *numberStart,
                                                 int *numberLength) const
{
    const GroupHitArea *bestArea = nullptr;
    for (const GroupHitArea &area : m_treeLayout.groupHitAreas()) {
        if (area.operation != SceneDocument::TreeNode::Translate
            && area.operation != SceneDocument::TreeNode::Rotate
            && area.operation != SceneDocument::TreeNode::Scale
            && area.operation != SceneDocument::TreeNode::Mirror) {
            continue;
        }

        if (!area.rect.contains(scenePosition))
            continue;

        if (!bestArea || area.depth > bestArea->depth)
            bestArea = &area;
    }

    if (!bestArea)
        return false;

    qreal headerWidth = TransformHeaderWidth;
    if (!bestArea->children.isEmpty()) {
        headerWidth = qMax<qreal>(TransformHeaderWidth,
                                  bestArea->children.first().rect.left() - bestArea->rect.left() - GroupPadding);
    }

    // Find which axis row the mouse is over
    int hitAxis = -1;
    for (int i = 0; i < 3; ++i) {
        if (transformParameterControlRect(bestArea->rect, i, headerWidth).contains(scenePosition)) {
            hitAxis = i;
            break;
        }
    }

    if (hitAxis < 0)
        return false;

    // Two-level: find which number span within the expression
    if (m_scene && (numberStart || numberLength)) {
        const SceneDocument::TreeNode *node = m_scene->treeNodeById(bestArea->groupId);
        if (node) {
            const QString expr = transformAxisExpression(*node, hitAxis);
            const QFontMetricsF hitMetrics(sceneTreeGraphicsFont());
            const QVector<ExpressionNumberControl> numControls =
                transformParameterNumberControls(bestArea->rect, hitAxis, expr, hitMetrics, headerWidth);
            for (const ExpressionNumberControl &nc : numControls) {
                if (nc.rect.contains(scenePosition)) {
                    if (numberStart)  *numberStart  = nc.start;
                    if (numberLength) *numberLength = nc.length;
                    break;
                }
            }
        }
    }

    if (groupId)    *groupId    = bestArea->groupId;
    if (operation)  *operation  = bestArea->operation;
    if (axisOut)    *axisOut    = hitAxis;
    return true;
}

bool SceneTreeGraphicsWidget::shapeParameterControlAt(const QPointF &scenePosition,
                                                      int *shapeId,
                                                      int *nodeId,
                                                      int *parameter,
                                                      int *numberStart,
                                                      int *numberLength) const
{
    if (!m_scene)
        return false;

    const SceneDocument::TreeNode *bestNode = nullptr;
    QRectF bestRect;
    int bestDepth = -1;
    for (const GroupHitArea &area : m_treeLayout.groupHitAreas()) {
        for (const ChildLayout &child : area.children) {
            if (!child.rect.contains(scenePosition))
                continue;

            const SceneDocument::TreeNode *node = m_scene->treeNodeById(child.nodeId);
            if (!node || node->type != SceneDocument::TreeNode::Primitive)
                continue;

            if (area.depth > bestDepth) {
                bestNode = node;
                bestRect = child.rect;
                bestDepth = area.depth;
            }
        }
    }

    if (!bestNode)
        return false;

    const ShapeNode *shape = m_scene->shapeById(bestNode->shapeId);
    if (!shape)
        return false;

    const QFontMetricsF hitMetrics(sceneTreeGraphicsFont());
    const QVector<ShapeParameterControl> controls = shapeParameterControls(*shape);
    for (int i = 0; i < controls.size(); ++i) {
        if (!shapeParameterControlRect(bestRect, i, controls.size()).contains(scenePosition))
            continue;

        const QVector<ExpressionNumberControl> numControls =
            shapeParameterNumberControls(bestRect, i, controls.size(), controls[i].expression, hitMetrics);
        for (const ExpressionNumberControl &nc : numControls) {
            if (!nc.rect.contains(scenePosition))
                continue;

            if (shapeId)    *shapeId    = bestNode->shapeId;
            if (nodeId)     *nodeId     = bestNode->id;
            if (parameter)  *parameter  = i;
            if (numberStart)  *numberStart  = nc.start;
            if (numberLength) *numberLength = nc.length;
            return true;
        }
    }

    return false;
}

bool SceneTreeGraphicsWidget::variableNumberControlAt(const QPointF &scenePosition,
                                                      int *nodeId,
                                                      int *start,
                                                      int *length) const
{
    if (!m_scene)
        return false;

    const SceneDocument::TreeNode *bestNode = nullptr;
    QRectF bestRect;
    int bestDepth = -1;
    for (const GroupHitArea &area : m_treeLayout.groupHitAreas()) {
        for (const ChildLayout &child : area.children) {
            if (!child.rect.contains(scenePosition))
                continue;

            const SceneDocument::TreeNode *node = m_scene->treeNodeById(child.nodeId);
            if (!node || node->type != SceneDocument::TreeNode::Variable)
                continue;

            if (area.depth > bestDepth) {
                bestNode = node;
                bestRect = child.rect;
                bestDepth = area.depth;
            }
        }
    }

    if (!bestNode)
        return false;

    const QFontMetricsF hitMetrics(sceneTreeGraphicsFont());
    const qreal hitNameW = hitMetrics.horizontalAdvance(bestNode->variableName);
    const QVector<ExpressionNumberControl> controls = expressionNumberControls(bestRect, bestNode->variableExpression, hitMetrics, hitNameW);
    for (const ExpressionNumberControl &control : controls) {
        if (!control.rect.contains(scenePosition))
            continue;

        if (nodeId)
            *nodeId = bestNode->id;
        if (start)
            *start = control.start;
        if (length)
            *length = control.length;
        return true;
    }

    return false;
}

bool SceneTreeGraphicsWidget::forLoopRangeControlAt(const QPointF &scenePosition,
                                                    int *nodeId,
                                                    int *start,
                                                    int *length) const
{
    if (!m_scene)
        return false;

    const GroupHitArea *bestArea = nullptr;
    for (const GroupHitArea &area : m_treeLayout.groupHitAreas()) {
        if (area.operation != SceneDocument::TreeNode::For || !area.rect.contains(scenePosition))
            continue;

        if (!bestArea || area.depth > bestArea->depth)
            bestArea = &area;
    }

    if (!bestArea)
        return false;

    const SceneDocument::TreeNode *node = m_scene->treeNodeById(bestArea->groupId);
    if (!node || node->type != SceneDocument::TreeNode::Group || node->operation != SceneDocument::TreeNode::For)
        return false;

    const QFontMetricsF hitMetrics(sceneTreeGraphicsFont());
    const QString variableName = forLoopVariableName(*node);
    const QString rangeExpression = forLoopRangeExpression(*node);
    const QVector<ExpressionNumberControl> controls =
        forLoopRangeNumberControls(bestArea->rect, variableName, rangeExpression, hitMetrics);
    for (const ExpressionNumberControl &control : controls) {
        if (!control.rect.contains(scenePosition))
            continue;

        if (nodeId)
            *nodeId = node->id;
        if (start)
            *start = control.start;
        if (length)
            *length = control.length;
        return true;
    }

    return false;
}

void SceneTreeGraphicsWidget::updateControlTooltip(const QPoint &globalPosition,
                                                   const QPointF &scenePosition,
                                                   bool controlDown)
{
    QString key;
    QString message;

    int groupId = 0;
    int axis = -1;
    SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
    int forNodeId = 0;
    int forNumberStart = -1;
    int forNumberLength = 0;
    if (forLoopRangeControlAt(scenePosition, &forNodeId, &forNumberStart, &forNumberLength)) {
        key = QStringLiteral("for:%1:%2").arg(forNodeId).arg(forNumberStart);
        message = controlDown
                      ? QStringLiteral("Use mouse wheel to change this for range number")
                      : QStringLiteral("Hold Ctrl and use mouse wheel to change this for range number");
    } else {
        int transformNumberStart = -1;
        int transformNumberLength = 0;
        if (transformControlAt(scenePosition,
                               &groupId,
                               &operation,
                               &axis,
                               &transformNumberStart,
                               &transformNumberLength)
            && transformNumberStart >= 0
            && transformNumberLength > 0) {
        static const char *AxisNames[] = {"X", "Y", "Z"};
        const QString axisName = QString::fromLatin1(AxisNames[axis]);
        key = QStringLiteral("transform:%1:%2").arg(groupId).arg(transformNumberStart);
        message = controlDown
                      ? QStringLiteral("Use mouse wheel to change %1 %2").arg(labelForOperation(operation), axisName)
                      : QStringLiteral("Hold Ctrl and use mouse wheel to change %1 %2").arg(labelForOperation(operation), axisName);
        }
    }
    if (key.isEmpty()) {
        int variableNodeId = 0;
        int numberStart = -1;
        int numberLength = 0;
        if (variableNumberControlAt(scenePosition, &variableNodeId, &numberStart, &numberLength)) {
            key = QStringLiteral("variable:%1:%2").arg(variableNodeId).arg(numberStart);
            message = controlDown
                          ? QStringLiteral("Use mouse wheel to change this number")
                          : QStringLiteral("Hold Ctrl and use mouse wheel to change this number");
        } else {
            int mcNodeId = 0;
            int mcVarNodeId = 0;
            int mcStart = -1;
            int mcLength = 0;
            if (moduleCallParamControlAt(scenePosition, &mcNodeId, &mcVarNodeId, &mcStart, &mcLength)) {
                key = QStringLiteral("modulecall:%1:%2").arg(mcNodeId).arg(mcStart);
                message = controlDown
                              ? QStringLiteral("Use mouse wheel to change this parameter")
                              : QStringLiteral("Hold Ctrl and use mouse wheel to change this parameter");
            }
        }
        if (key.isEmpty()) {
            int shapeId = -1;
            int nodeId = 0;
            int parameter = -1;
            int numStart = -1;
            int numLen = 0;
            if (shapeParameterControlAt(scenePosition, &shapeId, &nodeId, &parameter, &numStart, &numLen)) {
            Q_UNUSED(numStart); Q_UNUSED(numLen);
            const ShapeNode *shape = m_scene ? m_scene->shapeById(shapeId) : nullptr;
            const QVector<ShapeParameterControl> controls = shape ? shapeParameterControls(*shape) : QVector<ShapeParameterControl>();
            if (parameter >= 0 && parameter < controls.size()) {
                key = QStringLiteral("shape:%1:%2").arg(nodeId).arg(parameter);
                message = controlDown
                              ? QStringLiteral("Use mouse wheel to change %1").arg(controls[parameter].label)
                              : QStringLiteral("Hold Ctrl and use mouse wheel to change %1").arg(controls[parameter].label);
            }
            }
        }
    }

    if (key.isEmpty()) {
        if (!m_lastControlTooltipKey.isEmpty()) {
            QToolTip::hideText();
            m_lastControlTooltipKey.clear();
        }
        return;
    }

    if (key == m_lastControlTooltipKey)
        return;

    m_lastControlTooltipKey = key;
    QToolTip::showText(globalPosition + QPoint(12, 18), message, this);
}

void SceneTreeGraphicsWidget::updateActiveTransformControl(const QPointF &scenePosition, bool enabled)
{
    int groupId = 0;
    int axis = -1;
    int numberStart = -1;
    int numberLength = 0;
    SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
    if (!enabled
        || !transformControlAt(scenePosition, &groupId, &operation, &axis, &numberStart, &numberLength)
        || numberStart < 0
        || numberLength <= 0) {
        groupId = 0;
        axis = -1;
        numberStart = -1;
        operation = SceneDocument::TreeNode::Union;
    }

    if (m_activeTransformControlNodeId == groupId
        && m_activeTransformControlAxis == axis
        && m_activeTransformControlNumberStart == numberStart
        && m_activeTransformControlOperation == operation) {
        return;
    }

    m_activeTransformControlNodeId = groupId;
    m_activeTransformControlAxis = axis;
    m_activeTransformControlNumberStart = numberStart;
    m_activeTransformControlOperation = operation;
    if (!m_dragActive)
        refresh();

    emit transformControlHovered(groupId, operation, axis);
}

void SceneTreeGraphicsWidget::updateActiveShapeParameterControl(const QPointF &scenePosition, bool enabled)
{
    int shapeId = -1;
    int nodeId = 0;
    int paramIndex = -1;
    int numberStart = -1;
    int numberLength = 0;
    if (!enabled || !shapeParameterControlAt(scenePosition, &shapeId, &nodeId, &paramIndex, &numberStart, &numberLength)) {
        nodeId = 0;
        paramIndex = -1;
        numberStart = -1;
    }

    if (m_activeShapeParameterNodeId == nodeId
        && m_activeShapeParameter == paramIndex
        && m_activeShapeParameterNumberStart == numberStart) {
        return;
    }

    m_activeShapeParameterNodeId = nodeId;
    m_activeShapeParameter = paramIndex;
    m_activeShapeParameterNumberStart = numberStart;
    if (!m_dragActive)
        refresh();

    emit shapeParameterHovered(shapeId, paramIndex);
}

void SceneTreeGraphicsWidget::updateActiveVariableNumberControl(const QPointF &scenePosition, bool enabled)
{
    int nodeId = 0;
    int start = -1;
    int length = 0;
    if (!enabled || !variableNumberControlAt(scenePosition, &nodeId, &start, &length)) {
        nodeId = 0;
        start = -1;
    }

    Q_UNUSED(length);

    if (m_activeVariableNodeId == nodeId
        && m_activeVariableNumberStart == start) {
        return;
    }

    m_activeVariableNodeId = nodeId;
    m_activeVariableNumberStart = start;
    if (!m_dragActive)
        refresh();
    emit variableNumberHovered(nodeId, start);
}

void SceneTreeGraphicsWidget::updateActiveForLoopRangeControl(const QPointF &scenePosition, bool enabled)
{
    int nodeId = 0;
    int start = -1;
    int length = 0;
    if (!enabled || !forLoopRangeControlAt(scenePosition, &nodeId, &start, &length)) {
        nodeId = 0;
        start = -1;
    }

    Q_UNUSED(length);

    if (m_activeForLoopNodeId == nodeId
        && m_activeForLoopNumberStart == start) {
        return;
    }

    m_activeForLoopNodeId = nodeId;
    m_activeForLoopNumberStart = start;
    if (!m_dragActive)
        refresh();
    emit forLoopRangeHovered(nodeId, start);
}

bool SceneTreeGraphicsWidget::moduleCallParamControlAt(const QPointF &scenePosition,
                                                        int *moduleCallNodeId,
                                                        int *paramVarNodeId,
                                                        int *start,
                                                        int *length) const
{
    if (!m_scene)
        return false;

    for (const GroupHitArea &area : m_treeLayout.groupHitAreas()) {
        for (const ChildLayout &child : area.children) {
            if (!child.rect.contains(scenePosition))
                continue;

            const SceneDocument::TreeNode *node = m_scene->treeNodeById(child.nodeId);
            if (!node || node->type != SceneDocument::TreeNode::ModuleCall)
                continue;

            const SceneDocument::TreeNode *modGroup = m_scene->treeNodeById(node->shapeId);
            if (!modGroup || modGroup->operation != SceneDocument::TreeNode::Module)
                continue;

            QVector<ModuleCallParam> params;
            const QHash<QString, QString> overrides = resolveModuleArguments(node->moduleCallArguments, *modGroup);
            for (const SceneDocument::TreeNode &pChild : modGroup->children) {
                if (pChild.type == SceneDocument::TreeNode::Variable && pChild.isParameter) {
                    const QString expr = overrides.value(pChild.variableName,
                                                         pChild.variableExpression.trimmed().isEmpty()
                                                             ? QString::number(pChild.variableValue)
                                                             : pChild.variableExpression.trimmed());
                    params.append({pChild.id, pChild.variableName, expr});
                }
            }

            if (params.isEmpty())
                continue;

            const QFontMetricsF hitMetrics(sceneTreeGraphicsFont());
            const QVector<ModuleCallParamControl> controls =
                moduleCallParamControls(child.rect, node->moduleName, params, hitMetrics);
            for (const ModuleCallParamControl &ctrl : controls) {
                if (!ctrl.rect.contains(scenePosition))
                    continue;
                if (moduleCallNodeId)
                    *moduleCallNodeId = node->id;
                if (paramVarNodeId)
                    *paramVarNodeId = ctrl.paramVarNodeId;
                if (start)
                    *start = ctrl.numberStart;
                if (length)
                    *length = ctrl.numberLength;
                return true;
            }
        }
    }
    return false;
}

bool SceneTreeGraphicsWidget::handleModuleCallParamWheel(const QPointF &scenePosition, int wheelSteps)
{
    if (!m_scene)
        return false;

    int moduleCallNodeId = 0;
    int paramVarNodeId = 0;
    int start = -1;
    int length = 0;
    if (!moduleCallParamControlAt(scenePosition, &moduleCallNodeId, &paramVarNodeId, &start, &length))
        return false;

    emit moduleCallArgumentAdjusted(moduleCallNodeId, paramVarNodeId, start, length, wheelSteps);
    updateActiveModuleCallParamControl(scenePosition, true);
    return true;
}

void SceneTreeGraphicsWidget::updateActiveModuleCallParamControl(const QPointF &scenePosition, bool enabled)
{
    int moduleCallNodeId = 0;
    int varNodeId = 0;
    int start = -1;
    int length = 0;
    if (!enabled || !moduleCallParamControlAt(scenePosition, &moduleCallNodeId, &varNodeId, &start, &length)) {
        moduleCallNodeId = 0;
        varNodeId = 0;
        start = -1;
    }

    Q_UNUSED(length);

    if (m_activeModuleCallNodeId == moduleCallNodeId
        && m_activeModuleCallVarNodeId == varNodeId
        && m_activeModuleCallNumberStart == start) {
        return;
    }

    m_activeModuleCallNodeId = moduleCallNodeId;
    m_activeModuleCallVarNodeId = varNodeId;
    m_activeModuleCallNumberStart = start;
    if (!m_dragActive)
        refresh();
    emit moduleCallParamHovered(moduleCallNodeId, varNodeId, start);
}

void SceneTreeGraphicsWidget::updateHoverHighlights(const QPointF &scenePosition)
{
    if (m_dragActive || m_inlineInputActive)
        return;

    const bool controlDown = QApplication::keyboardModifiers() & Qt::ControlModifier;

    // --- Scroll zone hover (teal) ---
    QRectF newScrollRect;
    if (controlDown) {
        // When Ctrl is held, active scroll controls have their own highlight; no extra glow.
        newScrollRect = QRectF();
    } else {
        newScrollRect = hoverScrollZoneRect(scenePosition);
    }

    // --- Rename zone hover (lavender) ---
    int renameNodeId = 0;
    QRectF renameRect;
    hoverRenameZoneAt(scenePosition, &renameNodeId, &renameRect);
    const QRectF newRenameRect = renameNodeId > 0 ? renameRect : QRectF();

    // Update cursor.
    if (newRenameRect.isValid())
        setCursor(Qt::IBeamCursor);
    else if (newScrollRect.isValid())
        setCursor(Qt::SizeVerCursor);
    else
        setCursor(Qt::OpenHandCursor);

    if (newScrollRect == m_hoveredScrollRect && newRenameRect == m_hoveredRenameRect)
        return;

    m_hoveredScrollRect = newScrollRect;
    m_hoveredRenameRect = newRenameRect;
    refresh();
    emit hoverScrollZoneChanged(m_hoveredScrollRect);
}

QRectF SceneTreeGraphicsWidget::hoverScrollZoneRect(const QPointF &scenePosition) const
{
    // Check each type of scroll control and return the first rect that contains scenePosition.
    {
        int groupId = 0;
        SceneDocument::TreeNode::Operation op = SceneDocument::TreeNode::Union;
        int axis = -1, numStart = -1, numLen = -1;
        if (transformControlAt(scenePosition, &groupId, &op, &axis, &numStart, &numLen) && numStart >= 0) {
            const SceneDocument::TreeNode *node = m_scene ? m_scene->treeNodeById(groupId) : nullptr;
            if (node) {
                const QString expr = transformAxisExpression(*node, axis);
                const qreal hw = transformHeaderWidthForNode(*node);
                const QFontMetricsF metrics(sceneTreeGraphicsFont());
                const auto controls = transformParameterNumberControls(
                    groupRectForNode(groupId), axis, expr, metrics, hw);
                for (const auto &ctl : controls) {
                    if (ctl.start == numStart)
                        return ctl.rect;
                }
            }
        }
    }
    {
        int shapeId = 0, nodeId2 = 0, param = -1, numStart = -1, numLen = -1;
        if (shapeParameterControlAt(scenePosition, &shapeId, &nodeId2, &param, &numStart, &numLen) && numStart >= 0) {
            const ShapeNode *shape = (m_scene && shapeId > 0) ? m_scene->shapeById(shapeId) : nullptr;
            if (shape && param >= 0) {
                // Find the primitive card rect in the layout children.
                for (const GroupHitArea &area : m_treeLayout.groupHitAreas()) {
                    for (const ChildLayout &child : area.children) {
                        if (child.nodeId != nodeId2)
                            continue;
                        const auto paramControls = shapeParameterControls(*shape);
                        if (param < paramControls.size()) {
                            const QFontMetricsF metrics(sceneTreeGraphicsFont());
                            const auto numCtrls = shapeParameterNumberControls(
                                child.rect, param, paramControls.size(),
                                paramControls[param].expression, metrics);
                            for (const auto &ctl : numCtrls) {
                                if (ctl.start == numStart)
                                    return ctl.rect;
                            }
                        }
                        break;
                    }
                }
            }
        }
    }
    {
        int nodeId2 = 0, start = -1, length = 0;
        if (variableNumberControlAt(scenePosition, &nodeId2, &start, &length) && start >= 0) {
            const SceneDocument::TreeNode *node = m_scene ? m_scene->treeNodeById(nodeId2) : nullptr;
            if (node) {
                // Find the variable card rect via the layout children.
                for (const GroupHitArea &area : m_treeLayout.groupHitAreas()) {
                    for (const ChildLayout &child : area.children) {
                        if (child.nodeId != nodeId2)
                            continue;
                        const QFontMetricsF metrics(sceneTreeGraphicsFont());
                        const qreal nameW = metrics.horizontalAdvance(node->variableName);
                        const auto controls = expressionNumberControls(
                            child.rect, node->variableExpression, metrics, nameW);
                        for (const auto &ctl : controls) {
                            if (ctl.start == start)
                                return ctl.rect;
                        }
                        break;
                    }
                }
            }
        }
    }
    {
        int nodeId2 = 0, start = -1, length = 0;
        if (forLoopRangeControlAt(scenePosition, &nodeId2, &start, &length) && start >= 0) {
            const SceneDocument::TreeNode *node = m_scene ? m_scene->treeNodeById(nodeId2) : nullptr;
            if (node) {
                const QFontMetricsF metrics(sceneTreeGraphicsFont());
                for (const GroupHitArea &area : m_treeLayout.groupHitAreas()) {
                    if (area.groupId == nodeId2) {
                        const QString varName = forLoopVariableName(*node);
                        const QString rangeExpr = forLoopRangeExpression(*node);
                        const auto controls = forLoopRangeNumberControls(area.rect, varName, rangeExpr, metrics);
                        for (const auto &ctl : controls) {
                            if (ctl.start == start)
                                return ctl.rect;
                        }
                        break;
                    }
                }
            }
        }
    }
    {
        int moduleCallId = 0, varId = 0, start = -1, length = 0;
        if (moduleCallParamControlAt(scenePosition, &moduleCallId, &varId, &start, &length) && start >= 0) {
            const SceneDocument::TreeNode *callNode = m_scene ? m_scene->treeNodeById(moduleCallId) : nullptr;
            const SceneDocument::TreeNode *moduleNode = (callNode && m_scene)
                                                            ? m_scene->treeNodeById(callNode->shapeId)
                                                            : nullptr;
            if (callNode && moduleNode) {
                QVector<ModuleCallParam> params;
                for (const SceneDocument::TreeNode &child : moduleNode->children) {
                    if (child.type == SceneDocument::TreeNode::Variable && child.isParameter) {
                        params.append({child.id, child.variableName,
                                       child.variableExpression.trimmed().isEmpty()
                                           ? QString::number(child.variableValue)
                                           : child.variableExpression.trimmed()});
                    }
                }
                // Find the call card rect from treeLayout children.
                for (const GroupHitArea &area : m_treeLayout.groupHitAreas()) {
                    for (const ChildLayout &child : area.children) {
                        if (child.nodeId == moduleCallId) {
                            const QFontMetricsF metrics(sceneTreeGraphicsFont());
                            const auto controls = moduleCallParamControls(
                                child.rect, moduleNode->moduleName, params, metrics);
                            for (const auto &ctl : controls) {
                                if (ctl.paramVarNodeId == varId && ctl.numberStart == start)
                                    return ctl.rect;
                            }
                        }
                    }
                }
            }
        }
    }
    return QRectF();
}

bool SceneTreeGraphicsWidget::hoverRenameZoneAt(const QPointF &scenePosition, int *nodeId, QRectF *zoneRect) const
{
    for (const RenameZone &zone : m_renameZones) {
        // Inflate the hit area a little for usability.
        if (zone.rect.adjusted(-2, -2, 2, 2).contains(scenePosition)) {
            if (nodeId)   *nodeId   = zone.nodeId;
            if (zoneRect) *zoneRect = zone.rect;
            return true;
        }
    }
    if (nodeId)   *nodeId   = 0;
    if (zoneRect) *zoneRect = QRectF();
    return false;
}

void SceneTreeGraphicsWidget::startInlineRename(int nodeId,
                                                bool isModule,
                                                const QRectF &sceneRect,
                                                const QString &currentName)
{
    if (!m_inlineInput)
        return;

    m_inlineInputActive = true;
    m_inlineInputSceneRect = sceneRect;

    m_inlineInput->startEditing(
        mapFromScene(sceneRect).boundingRect(),
        currentName,
        [this, nodeId, isModule](const QString &newName) {
            m_inlineInputActive = false;
            m_inlineInputSceneRect = QRectF();
            if (newName.isEmpty() || newName == (isModule
                    ? (m_scene ? m_scene->treeNodeById(nodeId) ?
                           m_scene->treeNodeById(nodeId)->moduleName : QString() : QString())
                    : (m_scene ? m_scene->treeNodeById(nodeId) ?
                           m_scene->treeNodeById(nodeId)->variableName : QString() : QString())))
                return;
            if (isModule) {
                emit moduleRenameRequested(nodeId, newName);
            } else {
                emit variableRenameRequested(nodeId, newName);
            }
        },
        [this]() {
            m_inlineInputActive = false;
            m_inlineInputSceneRect = QRectF();
        });
}

void SceneTreeGraphicsWidget::updateInlineInputGeometry()
{
    if (!m_inlineInput || !m_inlineInputActive || !m_inlineInputSceneRect.isValid())
        return;

    m_inlineInput->reposition(mapFromScene(m_inlineInputSceneRect).boundingRect());
    m_inlineInput->raise();
}

QRectF SceneTreeGraphicsWidget::debugGroupRect(int groupId) const { return groupRectForNode(groupId); }
QRectF SceneTreeGraphicsWidget::debugChildRect(int nodeId)  const { return rectForChildNode(nodeId); }

QRectF SceneTreeGraphicsWidget::groupRectForNode(int groupId) const
{
    for (const GroupHitArea &area : m_treeLayout.groupHitAreas()) {
        if (area.groupId == groupId)
            return area.rect;
    }
    return QRectF();
}

QRectF SceneTreeGraphicsWidget::rectForChildNode(int nodeId) const
{
    for (const GroupHitArea &area : m_treeLayout.groupHitAreas()) {
        for (const ChildLayout &child : area.children) {
            if (child.nodeId == nodeId)
                return child.rect;
        }
    }
    return QRectF();
}

void SceneTreeGraphicsWidget::showDropPreview(const QPointF &scenePosition, const QSizeF &previewSize, const QString &previewTool, int movingNodeId)
{
    m_dragActive = true;
    if (m_thumbnailCache)
        m_thumbnailCache->setSuspended(true);
    if (m_groupThumbnailCache)
        m_groupThumbnailCache->setSuspended(true);

    const QSizeF effectivePreviewSize = previewSize.isValid() ? previewSize : defaultPreviewSize();
    const DropTarget target = dropTargetForToolAt(scenePosition,
                                                  effectivePreviewSize,
                                                  previewTool,
                                                  movingNodeId,
                                                  movingNodeId <= 0);

    emit dropPreviewChanged(previewTool, movingNodeId, target, scenePosition);

    startDropPreviewAnimation(target, previewTool, movingNodeId, LiveDropPreviewDurationMs);
}

void SceneTreeGraphicsWidget::finishDropPreview()
{
    if (!m_dropPreviewActive) {
        clearDropPreview();
        return;
    }

    m_dropPreviewFinishing = true;
    startDropPreviewAnimation(m_dropPreviewTarget,
                              m_dropPreviewTool,
                              m_dropPreviewMovingNodeId,
                              ReleaseDropPreviewDurationMs);
}

void SceneTreeGraphicsWidget::clearDropPreview()
{
    if (m_dropPreviewAnimationTimer)
        m_dropPreviewAnimationTimer->stop();
    m_dragActive = false;
    m_dropPreviewActive = false;
    m_dropPreviewFinishing = false;
    m_dropPreviewProgress = 0.0;
    m_dropPreviewStartTarget = DropTarget();
    m_dropPreviewTarget = DropTarget();
    m_dropPreviewCurrentTarget = DropTarget();
    m_dropPreviewTool.clear();
    m_dropPreviewMovingNodeId = 0;
    SceneTreePreviewRenderer(m_graphicsScene, &m_dropPreviewItems, m_scene, &m_treeLayout, m_treeTheme).clear();
    setTreeItemsVisible(true);
    if (m_thumbnailCache)
        m_thumbnailCache->setSuspended(false);
    if (m_groupThumbnailCache)
        m_groupThumbnailCache->setSuspended(false);
}

void SceneTreeGraphicsWidget::startDropPreviewAnimation(const DropTarget &target,
                                                        const QString &previewTool,
                                                        int movingNodeId,
                                                        qreal durationMs)
{
    const bool samePreviewKind = m_dropPreviewActive
                                 && m_dropPreviewTool == previewTool
                                 && m_dropPreviewMovingNodeId == movingNodeId;
    if (samePreviewKind && dropTargetNear(target, m_dropPreviewTarget))
        return;

    m_dropPreviewStartTarget = samePreviewKind
                                   ? m_dropPreviewCurrentTarget
                                   : collapsedDropTarget(target);
    m_dropPreviewTarget = target;
    m_dropPreviewTool = previewTool;
    m_dropPreviewMovingNodeId = movingNodeId;
    m_dropPreviewDurationMs = qMax<qreal>(1.0, durationMs);
    m_dropPreviewProgress = 0.0;
    m_dropPreviewActive = true;

    if (!samePreviewKind)
        renderDropPreviewFrame(m_dropPreviewStartTarget);
    if (m_dropPreviewAnimationTimer)
        m_dropPreviewAnimationTimer->start();
}

void SceneTreeGraphicsWidget::advanceDropPreviewAnimation()
{
    if (!m_dropPreviewActive)
        return;

    m_dropPreviewProgress = qMin<qreal>(1.0, m_dropPreviewProgress + 16.0 / m_dropPreviewDurationMs);
    const DropTarget frame = interpolatedDropTarget(m_dropPreviewStartTarget,
                                                    m_dropPreviewTarget,
                                                    m_dropPreviewProgress);
    renderDropPreviewFrame(frame);

    if (m_dropPreviewProgress >= 1.0 && m_dropPreviewAnimationTimer)
        m_dropPreviewAnimationTimer->stop();
}

void SceneTreeGraphicsWidget::renderDropPreviewFrame(const DropTarget &target)
{
    m_dropPreviewCurrentTarget = target;
    SceneTreePreviewRenderer(m_graphicsScene, &m_dropPreviewItems, m_scene, &m_treeLayout, m_treeTheme).clear();
    setTreeItemsVisible(true);

    SceneTreePreviewRenderer(m_graphicsScene, &m_dropPreviewItems, m_scene, &m_treeLayout, m_treeTheme)
        .render(target, m_dropPreviewTool, m_dropPreviewMovingNodeId);
}

void SceneTreeGraphicsWidget::scheduleDropCommit(std::function<void()> action)
{
    if (!action)
        return;

    if (!m_dropPreviewFinishing) {
        clearDropPreview();
        action();
        return;
    }

    // Defer the actual scene mutation until the current mouse-release handler
    // returns, so the QGraphicsItem that emitted the drop is not deleted while
    // still executing.  Do not wait for the release preview animation here:
    // keeping a transient preview alive during refresh/toolbar rebuilds can
    // leave the tree canvas stuck in drag state.
    QTimer::singleShot(0, this, [this, action = std::move(action)]() mutable {
        clearDropPreview();
        action();
    });
}

void SceneTreeGraphicsWidget::setTreeItemsVisible(bool visible)
{
    if (m_treeItemsVisible == visible)
        return;

    m_treeItemsVisible = visible;
    for (QGraphicsItem *item : m_treeItems) {
        if (item)
            item->setOpacity(visible ? 1.0 : 0.0);
    }
}

void SceneTreeGraphicsWidget::updateSceneRect()
{
    QRectF bounds = m_graphicsScene->itemsBoundingRect()
                        .adjusted(-CanvasMargin, -CanvasMargin, CanvasMargin, CanvasMargin);

    if (bounds.width() < 420.0)
        bounds.setWidth(420.0);
    if (bounds.height() < 260.0)
        bounds.setHeight(260.0);

    m_graphicsScene->setSceneRect(bounds);
}

QString SceneTreeGraphicsWidget::labelForPrimitive(int shapeId) const
{
    if (!m_scene)
        return "shape";

    const ShapeNode *shape = m_scene->shapeById(shapeId);
    if (!shape)
        return "shape";

    if (!shape->name.isEmpty())
        return shape->name;

    return toolNameForPrimitiveType(shape->type);
}

ShapeNode::Type SceneTreeGraphicsWidget::typeForPrimitive(int shapeId) const
{
    if (!m_scene)
        return ShapeNode::Cube;

    const ShapeNode *shape = m_scene->shapeById(shapeId);
    return shape ? shape->type : ShapeNode::Cube;
}

void SceneTreeGraphicsWidget::syncThumbnailCache()
{
    if (!m_thumbnailCache || !m_scene)
        return;

    QHash<int, ShapeNode> primitiveShapes;
    collectPrimitiveNodeShapes(m_scene->treeRoot(), &primitiveShapes);
    m_thumbnailCache->syncPrimitives(primitiveShapes);
}

void SceneTreeGraphicsWidget::syncGroupThumbnailCache()
{
    if (!m_groupThumbnailCache || !m_scene)
        return;

    m_groupThumbnailCache->syncGroups(*m_scene);
}

void SceneTreeGraphicsWidget::collectPrimitiveNodeShapes(const SceneDocument::TreeNode &node,
                                                         QHash<int, ShapeNode> *out) const
{
    if (!out)
        return;

    if (node.type == SceneDocument::TreeNode::Primitive) {
        const ShapeNode *shape = m_scene ? m_scene->shapeById(node.shapeId) : nullptr;
        if (shape)
            out->insert(node.id, *shape);
        return;
    }

    for (const SceneDocument::TreeNode &child : node.children)
        collectPrimitiveNodeShapes(child, out);
}
