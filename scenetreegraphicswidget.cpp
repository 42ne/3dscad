#include "scenetreegraphicswidget.h"
#include "expression.h"
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
#include "scenetreecoloreditmode.h"
#include "scenetreehovermanager.h"
#include "scenetreeinlineeditor.h"
#include "scenecanvasdraghandler.h"
#include "scenetreeoverlaycontroller.h"
#include "scenetreedroppreviewcontroller.h"
#include "scenetreehittestmanager.h"
#include "scenetreewheelhandler.h"

#include <QApplication>
#include <QColorDialog>
#include <QMenu>
#include <QEasingCurve>
#include <QFontMetricsF>
#include <QGraphicsEllipseItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsTextItem>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTextDocument>
#include <QTimer>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <QStringList>
#include <cmath>

using namespace SceneTreeGraphics;

namespace {

static bool isPolyhedronSelectableLabelCell(PolyhedronTableItem::Cell::Type type)
{
    return type == PolyhedronTableItem::Cell::PtLabel
           || type == PolyhedronTableItem::Cell::FaceLabel;
}

static QString polygonPointSelectionKey(int nodeId, int pointIndex)
{
    return QStringLiteral("%1:%2").arg(nodeId).arg(pointIndex);
}

// ---------------------------------------------------------------------------

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


qreal horizontalHeaderMinWidth(const SceneDocument::TreeNode &node)
{
    if (isVerticalHeaderOperation(node.operation)
        || node.operation == SceneDocument::TreeNode::For
        || node.operation == SceneDocument::TreeNode::LinearExtrude
        || node.operation == SceneDocument::TreeNode::RotateExtrude) {
        return minimumWidthForOperation(node.operation);
    }

    QString label = labelForOperation(node.operation);
    if (node.operation == SceneDocument::TreeNode::Module && !node.moduleName.trimmed().isEmpty())
        label += QStringLiteral(" ") + node.moduleName.trimmed();
    const QFontMetricsF metrics(sceneTreeGraphicsFont());
    const qreal iconSize = node.operation == SceneDocument::TreeNode::Module
        ? PrimitiveIconSize - 6.0
        : PrimitiveIconSize - 4.0;
    const qreal gripAndIconWidth = 30.0 + iconSize + 10.0;
    const qreal chevronAndRightPadding = node.operation == SceneDocument::TreeNode::Scene ? 10.0 : 28.0;
    return qMax(minimumWidthForOperation(node.operation),
                gripAndIconWidth + metrics.horizontalAdvance(label) + chevronAndRightPadding);
}

} // namespace


SceneTreeGraphicsWidget::SceneTreeGraphicsWidget(QWidget *parent)
    : QGraphicsView(parent)
    , m_graphicsScene(createTreeGraphicsScene(this))
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
    setBackgroundBrush(activeCanvasBackgroundTheme(m_canvasBackgroundTheme).background);
    setCacheMode(QGraphicsView::CacheNone);
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    setFocusPolicy(Qt::StrongFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setCursor(Qt::OpenHandCursor);
    if (viewport()) {
        viewport()->setAutoFillBackground(false);
        viewport()->setAttribute(Qt::WA_OpaquePaintEvent, true);
        viewport()->setAttribute(Qt::WA_NoSystemBackground, true);
    }

    m_colorEdit = new SceneTreeColorEditMode(this);
    connect(m_colorEdit, &SceneTreeColorEditMode::inlineThemeEdited,
            this, &SceneTreeGraphicsWidget::inlineThemeEdited);

    m_hoverManager      = new SceneTreeHoverManager(this);
    m_inlineEditor      = new SceneTreeInlineEditor(this);
    m_canvasDragHandler = new SceneCanvasDragHandler(this);
    m_overlay           = new SceneTreeOverlayController(this);
    m_dropPreview       = new SceneTreeDropPreviewController(this);
    m_hitTest           = new SceneTreeHitTestManager(this);
    m_canvasController  = new SceneTreeCanvasController(this);
    m_wheelHandler      = new SceneTreeWheelHandler(this);
    m_variableReferenceBlinkTimer = new QTimer(this);
    m_variableReferenceBlinkTimer->setInterval(420);
    connect(m_variableReferenceBlinkTimer, &QTimer::timeout, this, [this]() {
        if (m_hoveredVariableReferenceName.isEmpty()) {
            m_variableReferenceBlinkTimer->stop();
            return;
        }
        if (m_dragActive)
            return;
        m_variableReferenceBlinkOn = !m_variableReferenceBlinkOn;
        refresh();
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

void SceneTreeGraphicsWidget::setPolyhedronElementSelection(const QVector<int> &nodeIds)
{
    QSet<int> next;
    for (int nodeId : nodeIds) {
        if (nodeId > 0)
            next.insert(nodeId);
    }
    if (m_selectedPolyhedronElementNodeIds == next)
        return;

    m_selectedPolyhedronElementNodeIds = next;
    refresh();
}

void SceneTreeGraphicsWidget::focusSelectedNodeAnimated()
{
    if (!m_scene || m_selectedTreeNodeId <= 0 || !viewport()
        || m_dragActive || m_canvasDragHandler->isActive() || m_inlineEditor->m_inlineInputActive) {
        return;
    }

    QVector<int> collapsedAncestors;
    std::function<bool(const SceneDocument::TreeNode &)> revealNode =
        [&](const SceneDocument::TreeNode &parent) {
            for (const SceneDocument::TreeNode &child : parent.children) {
                if (child.id == m_selectedTreeNodeId)
                    return true;
                if (child.type != SceneDocument::TreeNode::Group || !revealNode(child))
                    continue;
                if (child.operation != SceneDocument::TreeNode::Scene
                    && m_collapsedGroupIds.contains(child.id))
                    collapsedAncestors.append(child.id);
                return true;
            }
            return false;
        };
    revealNode(m_scene->treeRoot());
    for (int groupId : collapsedAncestors)
        m_canvasDragHandler->toggleGroupCollapsed(groupId);

    QRectF targetRect = groupRectForNode(m_selectedTreeNodeId);
    if (!targetRect.isValid())
        targetRect = rectForChildNode(m_selectedTreeNodeId);
    if (!targetRect.isValid())
        return;

    if (m_focusAnimation) {
        m_focusAnimation->stop();
        m_focusAnimation->deleteLater();
        m_focusAnimation = nullptr;
    }
    m_canvasController->stopPanInertia();
    m_canvasController->snapZoom();

    const QPointF startCenter = mapToScene(viewport()->rect().center());
    const QPointF targetCenter = targetRect.center();
    const QTransform startTransform = transform();
    const qreal startScale = qMax<qreal>(0.001, startTransform.m11());
    const QSize viewportSize = viewport()->size();
    const qreal targetWidth = qMax<qreal>(1.0, viewportSize.width() / 3.0);
    const qreal targetHeight = qMax<qreal>(1.0, viewportSize.height() / 3.0);
    const qreal fitScale = qMin(targetWidth / qMax<qreal>(1.0, targetRect.width()),
                                targetHeight / qMax<qreal>(1.0, targetRect.height()));
    const qreal targetScale = qBound<qreal>(0.28, fitScale, 2.4);

    auto *animation = new QVariantAnimation(this);
    m_focusAnimation = animation;
    animation->setDuration(280);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation, &QVariantAnimation::valueChanged, this,
            [this, animation, startCenter, targetCenter, startTransform, startScale, targetScale](const QVariant &value) {
                if (animation != m_focusAnimation)
                    return;
                const qreal progress = value.toReal();
                const qreal scale = startScale + (targetScale - startScale) * progress;
                QTransform focusedTransform = startTransform;
                focusedTransform.scale(scale / startScale, scale / startScale);
                setTransform(focusedTransform);
                m_canvasController->setZoomLevel(transform().m11());
                centerOn(startCenter + (targetCenter - startCenter) * progress);
                m_inlineEditor->updateInlineInputGeometry();
                updateToolbarOverlay();
            });
    connect(animation, &QVariantAnimation::finished, this, [this, animation]() {
        if (m_focusAnimation == animation)
            m_focusAnimation = nullptr;
        animation->deleteLater();
    });
    animation->start();
}

void SceneTreeGraphicsWidget::setTreeTheme(int theme)
{
    const int clamped = qBound(0, theme, SceneTreePalette::ThemeCount - 1);
    if (m_treeTheme == clamped)
        return;
    m_treeTheme = clamped;
    refresh();
}

void SceneTreeGraphicsWidget::setCanvasBackgroundTheme(int theme)
{
    const int clamped = qBound(0, theme, CanvasBackgroundThemeCount - 1);
    if (m_canvasBackgroundTheme == clamped)
        return;

    m_canvasBackgroundTheme = clamped;
    setBackgroundBrush(canvasBackgroundTheme(clamped).background);
    viewport()->update();
    updateToolbarOverlay();
}

void SceneTreeGraphicsWidget::setCustomAppearanceTheme(const TreeAppearanceTheme &theme)
{
    SceneTreePalette::setCustomTheme(theme);
    setBackgroundBrush(theme.canvas);
    refresh();
}

void SceneTreeGraphicsWidget::clearCustomAppearanceTheme()
{
    if (!SceneTreePalette::hasCustomTheme())
        return;
    SceneTreePalette::clearCustomTheme();
    setBackgroundBrush(canvasBackgroundTheme(m_canvasBackgroundTheme).background);
    refresh();
}

void SceneTreeGraphicsWidget::refresh()
{
    QWidget *view = viewport();
    const bool updatesWereEnabled = view && view->updatesEnabled();
    if (view)
        view->setUpdatesEnabled(false);

    resetGraphicsScene();
    drawTreeOrPlaceholder();
    m_hoverManager->updateHighlightOverlay();
    updateSceneRect();
    updateToolbarOverlay();
    syncThumbnailCache();
    syncGroupThumbnailCache();

    if (view) {
        view->setUpdatesEnabled(updatesWereEnabled);
        if (updatesWereEnabled)
            view->update();
    }
}

void SceneTreeGraphicsWidget::updateHoveredVariableReference(const QPointF &scenePosition)
{
    QString hoveredName;
    for (const RenameZone &zone : m_renameZones) {
        if (zone.isModule)
            continue;
        if (zone.rect.adjusted(-2.0, -2.0, 2.0, 2.0).contains(scenePosition)) {
            hoveredName = zone.currentName;
            break;
        }
    }
    setHoveredVariableReferenceName(hoveredName);
}

void SceneTreeGraphicsWidget::setHoveredVariableReferenceName(const QString &name)
{
    if (m_hoveredVariableReferenceName == name)
        return;

    m_hoveredVariableReferenceName = name;
    m_variableReferenceBlinkOn = !name.isEmpty();
    if (m_variableReferenceBlinkTimer) {
        if (name.isEmpty())
            m_variableReferenceBlinkTimer->stop();
        else if (!m_variableReferenceBlinkTimer->isActive())
            m_variableReferenceBlinkTimer->start();
    }
    if (!m_dragActive)
        refresh();
}

void SceneTreeGraphicsWidget::requestAnimatedNodeDelete(int nodeId, const QRectF &preferredRect)
{
    if (nodeId <= 0) {
        emit treeNodeDeleteRequested(nodeId);
        return;
    }
    if (m_pendingAnimatedDeleteNodeIds.contains(nodeId))
        return;

    setHoveredVariableReferenceName(QString());
    const QRectF rect = preferredRect.isValid() ? preferredRect : nodeRectForDeleteAnimation(nodeId);
    if (!rect.isValid() || rect.isEmpty()) {
        emit treeNodeDeleteRequested(nodeId);
        return;
    }

    m_pendingAnimatedDeleteNodeIds.insert(nodeId);
    startDeletePixelAnimation(nodeId, rect);
}

QRectF SceneTreeGraphicsWidget::nodeRectForDeleteAnimation(int nodeId) const
{
    QRectF rect = groupRectForNode(nodeId);
    if (!rect.isValid())
        rect = rectForChildNode(nodeId);
    return rect;
}

void SceneTreeGraphicsWidget::startDeletePixelAnimation(int nodeId, const QRectF &rect)
{
    if (!m_graphicsScene || !viewport()) {
        m_pendingAnimatedDeleteNodeIds.remove(nodeId);
        emit treeNodeDeleteRequested(nodeId);
        return;
    }

    const QRectF clipped = rect.adjusted(1.0, 1.0, -1.0, -1.0);
    if (!clipped.isValid() || clipped.isEmpty()) {
        m_pendingAnimatedDeleteNodeIds.remove(nodeId);
        emit treeNodeDeleteRequested(nodeId);
        return;
    }

    // 1. Grab the card's appearance while it is still visible.
    const QRect vpRect = mapFromScene(clipped).boundingRect()
                             .intersected(viewport()->rect());
    if (vpRect.isEmpty()) {
        m_pendingAnimatedDeleteNodeIds.remove(nodeId);
        emit treeNodeDeleteRequested(nodeId);
        return;
    }
    const QPixmap capture = viewport()->grab(vpRect);
    const qreal renderScale = capture.devicePixelRatioF();

    // 2. Hide the node from the tree layout and rebuild.
    //    The node stays in the data model; m_hiddenForDeleteNodeId causes
    //    drawChild/drawTreeOrPlaceholder to skip it, so containers immediately
    //    reflow.  On heavy trees this rebuild may be slow but it runs before
    //    the animation timer starts, leaving the timer to run uncontested.
    m_hiddenForDeleteNodeId = nodeId;
    refresh();

    // 3. Build scatter tiles in viewport-pixel coordinates.
    //    Viewport pixels are independent of scene-coordinate shifts from reflow,
    //    so tiles always appear exactly where the card was on screen.
    const qreal vpW = vpRect.width();
    const qreal vpH = vpRect.height();
    const int columns = qBound(4, qRound(vpW / 10.0), 30);
    const int rows    = qBound(2, qRound(vpH / 10.0), 20);
    const qreal cellW = vpW / columns;
    const qreal cellH = vpH / rows;
    const QPointF vpCenter(vpRect.left() + vpW * 0.5, vpRect.top() + vpH * 0.5);

    m_deleteAnimTiles.clear();
    m_deleteAnimTiles.reserve(columns * rows);

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < columns; ++col) {
            const QRectF tileVpF(vpRect.left() + col * cellW,
                                 vpRect.top()  + row * cellH, cellW, cellH);
            const QRect tileVp = tileVpF.toRect()
                                     .translated(-vpRect.topLeft())
                                     .intersected(capture.rect());
            QPixmap px = capture.copy(tileVp);
            px.setDevicePixelRatio(renderScale);

            const int idx = row * columns + col;
            const QPointF startPos = tileVpF.topLeft();
            const QPointF fromCenter = startPos + QPointF(cellW * 0.5, cellH * 0.5) - vpCenter;
            const qreal len = qMax<qreal>(1.0, std::sqrt(fromCenter.x() * fromCenter.x()
                                                       + fromCenter.y() * fromCenter.y()));
            const QPointF outward = fromCenter / len;
            const qreal swirl = ((idx * 37) % 360) * 3.14159265358979323846 / 180.0;
            const QPointF jitter(std::cos(swirl) * 14.0, std::sin(swirl) * 14.0);

            DeleteAnimTile tile;
            tile.pixmap    = px;
            tile.startPos  = startPos;
            tile.pos       = startPos;
            tile.delta     = outward * (18.0 + (idx * 17) % 32) + jitter;
            tile.cellSize  = QSizeF(cellW, cellH);
            tile.fadeRate  = 1.0 + ((idx * 19 + col * 7 + row * 13) % 120) / 100.0;
            tile.rotatSign = (idx % 2 == 0) ? 1 : -1;
            m_deleteAnimTiles.append(tile);
        }
    }

    // 4. Run the scatter animation over the already-reflowed tree.
    auto *animation = new QVariantAnimation(this);
    animation->setDuration(480);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setEasingCurve(QEasingCurve::OutCubic);

    connect(animation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) {
        const qreal t = value.toReal();
        for (auto &tile : m_deleteAnimTiles) {
            tile.pos      = tile.startPos + tile.delta * t;
            tile.rotation = tile.rotatSign * 80.0 * t;
            tile.opacity  = qMax<qreal>(0.0, 1.0 - t * tile.fadeRate);
        }
        viewport()->update();
    });

    connect(animation, &QVariantAnimation::finished, this,
            [this, nodeId, animation]() {
        // 5. Animation done — commit deletion, final tree rebuild.
        m_hiddenForDeleteNodeId = 0;
        m_deleteAnimTiles.clear();
        animation->deleteLater();
        m_pendingAnimatedDeleteNodeIds.remove(nodeId);
        emit treeNodeDeleteRequested(nodeId);
    });

    animation->start();
}

void SceneTreeGraphicsWidget::compactRootBlocksAndFit()
{
    if (!m_scene)
        return;

    if (m_canvasMoveHandles.size() != m_scene->treeRoot().children.size())
        refresh();

    QHash<int, QSizeF> blockSizes;
    qreal totalArea = 0.0;
    qreal maxBlockWidth = 0.0;
    qreal maxBlockHeight = 0.0;
    for (const CanvasMoveHandle &handle : m_canvasMoveHandles) {
        if (handle.nodeId <= 0 || handle.blockRect.isEmpty())
            continue;
        const QSizeF size = handle.blockRect.size();
        blockSizes.insert(handle.nodeId, size);
        totalArea += size.width() * size.height();
        maxBlockWidth = qMax(maxBlockWidth, size.width());
        maxBlockHeight = qMax(maxBlockHeight, size.height());
    }

    if (blockSizes.isEmpty())
        return;

    const qreal viewportTarget = viewport() ? viewport()->height() * 0.82 / qMax<qreal>(0.25, transform().m11()) : 620.0;
    const qreal areaTarget = std::sqrt(qMax<qreal>(1.0, totalArea) * 1.25);
    const qreal targetColumnHeight = qMax(maxBlockHeight, qMax<qreal>(viewportTarget, areaTarget));

    QPointF cursor(TreeX, TreeY);
    qreal columnWidth = 0.0;
    bool columnHasBlocks = false;
    bool changed = false;
    QHash<int, QPointF> compactPositions;

    for (const SceneDocument::TreeNode &child : m_scene->treeRoot().children) {
        const QSizeF size = blockSizes.value(child.id);
        if (!size.isValid())
            continue;

        if (columnHasBlocks && cursor.y() + size.height() > TreeY + targetColumnHeight) {
            cursor.setX(cursor.x() + columnWidth);
            cursor.setY(TreeY);
            columnWidth = 0.0;
            columnHasBlocks = false;
        }

        compactPositions.insert(child.id, cursor);
        if (m_nodeCanvasPositions.value(child.id, QPointF()) != cursor)
            changed = true;

        cursor.ry() += size.height();
        columnWidth = qMax(columnWidth, size.width());
        columnHasBlocks = true;
    }

    if (changed) {
        for (auto it = compactPositions.constBegin(); it != compactPositions.constEnd(); ++it)
            m_nodeCanvasPositions[it.key()] = it.value();
        refresh();
    }

    QRectF bounds;
    bool hasBounds = false;
    for (const CanvasMoveHandle &handle : m_canvasMoveHandles) {
        bounds = hasBounds ? bounds.united(handle.blockRect) : handle.blockRect;
        hasBounds = true;
    }
    bounds = bounds.adjusted(-36.0, -36.0, 36.0, 36.0);
    if (bounds.isValid() && viewport()) {
        fitInView(bounds, Qt::KeepAspectRatio);
        updateToolbarOverlay();
    }
}

void SceneTreeGraphicsWidget::resetGraphicsScene()
{
    // Delete animations keep tiles in m_deleteAnimTiles (widget member, not scene items)
    // so they survive scene resets safely — don't stop them here.
    // m_hiddenForDeleteNodeId is intentionally preserved too: if refresh() is
    // called while an animation is pending, the node must stay hidden in the
    // rebuilt layout until the animation commits the deletion.
    m_pendingAnimatedDeleteNodeIds.clear();

    clearDropPreview();
    // Remove color-edit overlays before clear() deletes them under us.
    clearColorEditHighlight();
    m_treeZoomSnapshotItem = nullptr;
    m_graphicsScene->clear();
    m_canvasDragHandler->clearAfterSceneClear();
    m_treeLayout.clear();
    m_treeItems.clear();
    m_renameZones.clear();
    m_overlay->m_items.clear();
    m_overlay->m_itemOffsets.clear();
    m_hoverManager->m_hoverHighlightItems.clear();
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
    for (auto it = m_collapsedGroupIds.begin(); it != m_collapsedGroupIds.end(); ) {
        if (!m_scene->treeNodeById(*it))
            it = m_collapsedGroupIds.erase(it);
        else
            ++it;
    }

    // Apply the pending toolbar-drop canvas position to the most-recently-inserted
    // child that does not yet have a custom position (typically the last child).
    if (m_dropPreview->m_hasPendingInsertPos) {
        m_dropPreview->m_hasPendingInsertPos = false;
        const auto &children = m_scene->treeRoot().children;
        for (int ci = children.size() - 1; ci >= 0; --ci) {
            const int id = children[ci].id;
            if (!m_nodeCanvasPositions.contains(id)) {
                m_nodeCanvasPositions[id] = m_dropPreview->m_pendingInsertCanvasPosition;
                break;
            }
        }
    }

    const QList<QGraphicsItem *> existingItems = m_graphicsScene->items();
    QPointF autoPos(TreeX, TreeY);
    QVector<QRectF> placedRootBlocks;

    for (const SceneDocument::TreeNode &child : m_scene->treeRoot().children) {
        if (child.id == m_hiddenForDeleteNodeId)
            continue;
        // Position: stored custom or auto-layout fallback.
        QPointF blockTopLeft = m_nodeCanvasPositions.value(child.id, autoPos);
        const QList<QGraphicsItem *> beforeBlockItems = m_graphicsScene->items();
        // Draw the node content kGripStripH px below the block top (grip strip occupies top).
        const QRectF drawn = drawNode(child, blockTopLeft + QPointF(0.0, kGripStripH), 0);
        QRectF fullBlock(blockTopLeft, QSizeF(drawn.width(), kGripStripH + drawn.height()));

        const QPointF resolvedTopLeft = m_canvasDragHandler->nonOverlappingCanvasPosition(blockTopLeft,
                                                                                          fullBlock.size(),
                                                                                          placedRootBlocks);
        if (!qFuzzyCompare(resolvedTopLeft.x() + 1.0, blockTopLeft.x() + 1.0)
            || !qFuzzyCompare(resolvedTopLeft.y() + 1.0, blockTopLeft.y() + 1.0)) {
            const QPointF delta = resolvedTopLeft - blockTopLeft;
            const QList<QGraphicsItem *> afterBlockItems = m_graphicsScene->items();
            for (QGraphicsItem *item : afterBlockItems) {
                if (!beforeBlockItems.contains(item))
                    item->setPos(item->pos() + delta);
            }
            m_treeLayout.translateRootBlock(child.id, delta);
            for (RenameZone &zone : m_renameZones) {
                if (fullBlock.contains(zone.rect.center()))
                    zone.rect.translate(delta);
            }
            blockTopLeft = resolvedTopLeft;
            fullBlock.moveTo(blockTopLeft);
            m_nodeCanvasPositions[child.id] = blockTopLeft;
        }

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
        placedRootBlocks.append(fullBlock);

        // Advance auto-layout cursor (whether this node is custom-positioned or not).
        autoPos.ry() += fullBlock.height() + ChildGap * 2.0;
    }

    const QList<QGraphicsItem *> allItems = m_graphicsScene->items();
    for (QGraphicsItem *item : allItems) {
        if (!existingItems.contains(item))
            m_treeItems.append(item);
    }

}



void SceneTreeGraphicsWidget::drawBackground(QPainter *painter, const QRectF &rect)
{
    if (!m_fpsTimer.isValid()) {
        m_fpsTimer.start();
        m_lastFpsSampleNs = m_fpsTimer.nsecsElapsed();
    } else {
        const qint64 nowNs = m_fpsTimer.nsecsElapsed();
        const qint64 deltaNs = nowNs - m_lastFpsSampleNs;
        m_lastFpsSampleNs = nowNs;
        if (deltaNs > 0) {
            const qreal frameMs = qreal(deltaNs) / 1000000.0;
            if (frameMs < 1000.0)
                m_averageFrameMs = m_averageFrameMs <= 0.0
                    ? frameMs
                    : m_averageFrameMs * 0.9 + frameMs * 0.1;
        }
    }

    const CanvasBackgroundTheme background = activeCanvasBackgroundTheme(m_canvasBackgroundTheme);
    painter->fillRect(rect, background.background);
    const qreal scale = qMax(0.001, std::abs(transform().m11()));
    // Skip minor grid when zoomed out so far that lines would be < 4px apart on screen.
    if (24.0 * scale >= 4.0)
        drawCanvasGrid(painter, rect, 24.0, background.minorGrid, 1);
    drawCanvasGrid(painter, rect, 120.0, background.majorGrid, 1);
}

void SceneTreeGraphicsWidget::drawForeground(QPainter *painter, const QRectF &)
{
    if (m_deleteAnimTiles.isEmpty())
        return;
    // Tiles are stored in viewport-pixel coordinates — reset the scene
    // transform so we draw directly in device pixels.
    painter->save();
    painter->resetTransform();
    for (const DeleteAnimTile &tile : m_deleteAnimTiles) {
        if (tile.opacity <= 0.0) continue;
        painter->save();
        painter->setOpacity(tile.opacity);
        const QPointF tileCenter = tile.pos + QPointF(tile.cellSize.width()  * 0.5,
                                                      tile.cellSize.height() * 0.5);
        painter->translate(tileCenter);
        painter->rotate(tile.rotation);
        painter->translate(-tileCenter);
        painter->drawPixmap(QRectF(tile.pos, tile.cellSize), tile.pixmap,
                            QRectF(QPointF(), QSizeF(tile.pixmap.size())));
        painter->restore();
    }
    painter->restore();
}

void SceneTreeGraphicsWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && colorEditMode()) {
        setColorEditMode(false);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Control) {
        const QPointF scenePosition = mapToScene(m_lastMousePosition);
        m_hoverManager->updateTooltip(mapToGlobal(m_lastMousePosition), scenePosition, true);
        m_hoverManager->updateActiveTransformControl(scenePosition, true);
        m_hoverManager->updateActiveColorChannelControl(scenePosition, true);
        m_hoverManager->updateActiveShapeParameterControl(scenePosition, true);
        m_hoverManager->updateActiveVariableNumberControl(scenePosition, true);
        m_hoverManager->updateActiveForLoopRangeControl(scenePosition, true);
        m_hoverManager->updateActiveModuleCallParamControl(scenePosition, true);
        m_hoverManager->updateHighlights(scenePosition);
    }

    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && m_selectedTreeNodeId > 0) {
        requestAnimatedNodeDelete(m_selectedTreeNodeId);
        event->accept();
        return;
    }

    QGraphicsView::keyPressEvent(event);
}


int  SceneTreeGraphicsWidget::toolbarSnapSideForVpPos(QPoint vpPos) const
{ return m_overlay->toolbarSnapSideForVpPos(vpPos); }

bool SceneTreeGraphicsWidget::isOnToolbarBackground(QPointF vpPos) const
{ return m_overlay->isOnToolbarBackground(vpPos); }

void SceneTreeGraphicsWidget::repositionToolbarItemsSync()
{ m_overlay->repositionToolbarItemsSync(); }

void SceneTreeGraphicsWidget::focusOutEvent(QFocusEvent *event)
{
    m_canvasController->snapZoom();
    QGraphicsView::focusOutEvent(event);
}

void SceneTreeGraphicsWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    m_lastMousePosition = event->pos();

    // Stop any ongoing pan / zoom inertia — scene must be stable for drags
    if (event->button() == Qt::LeftButton) {
        m_canvasController->stopPanInertia();
        m_canvasController->snapZoom();
    }

    // ── Color-edit mode: intercept left-clicks on card zones ─────────────────
    if (colorEditMode() && event->button() == Qt::LeftButton) {
        const QPointF scenePos = mapToScene(event->pos());
        // Only toolbar items (theme swatches, the "✏ Colors" toggle button) are
        // allowed to handle their own clicks normally.  Everything else — tree
        // selection items, drag handles, node overlays — is suppressed so that
        // the color-edit click always fires, even when the user clicks on top of
        // an interactive card overlay.
        const QGraphicsItem *hitItem = itemAt(event->pos());
        const bool isToolbarItem = hitItem
            && m_overlay->m_items.contains(const_cast<QGraphicsItem *>(hitItem));
        // Swatches and the toggle button handle their own clicks; everything else
        // (card zones, glass panels, empty canvas) routes through handleColorEditClick.
        const QString hitZone = isToolbarItem ? hitItem->data(0).toString() : QString();
        const bool isToolbarControl = (hitZone == QLatin1String("swatch")
                                       || hitZone == QLatin1String("toggle"));
        if (!isToolbarControl) {
            if (m_colorEdit) m_colorEdit->handleClick(scenePos);
            event->accept();
            return;
        }
        // Fall through for swatches and the toggle button.
    }

    // ── Table labels/buttons ────────────────────────────────
    if (!colorEditMode() && event->button() == Qt::LeftButton) {
        const QPointF scenePos = mapToScene(event->pos());
        Polygon2DTableItem::Cell polygonCell;
        if (polygon2DTableControlAt(scenePos, &polygonCell)) {
            if (polygonCell.type == Polygon2DTableItem::Cell::PtLabel
                && polygonCell.nodeId > 0
                && polygonCell.index >= 0) {
                const QString key = polygonPointSelectionKey(polygonCell.nodeId, polygonCell.index);
                const bool multi = event->modifiers() & Qt::ShiftModifier;
                if (multi) {
                    if (m_selectedPolygonPointKeys.contains(key))
                        m_selectedPolygonPointKeys.remove(key);
                    else
                        m_selectedPolygonPointKeys.insert(key);
                } else if (m_selectedPolygonPointKeys.size() == 1
                           && m_selectedPolygonPointKeys.contains(key)) {
                    m_selectedPolygonPointKeys.clear();
                } else {
                    m_selectedPolygonPointKeys.clear();
                    m_selectedPolygonPointKeys.insert(key);
                }
                refresh();
                event->accept();
                return;
            }
            if (polygonCell.type == Polygon2DTableItem::Cell::RemovePt && polygonCell.index >= 0) {
                const QString nodePrefix = QStringLiteral("%1:").arg(polygonCell.nodeId);
                for (auto it = m_selectedPolygonPointKeys.begin(); it != m_selectedPolygonPointKeys.end(); ) {
                    if (it->startsWith(nodePrefix))
                        it = m_selectedPolygonPointKeys.erase(it);
                    else
                        ++it;
                }
                emit polygon2DPointRemoveRequested(polygonCell.nodeId, polygonCell.index);
                event->accept();
                return;
            }
            if (polygonCell.type == Polygon2DTableItem::Cell::AddPt) {
                emit polygon2DPointAddRequested(polygonCell.nodeId);
                event->accept();
                return;
            }
        }

        PolyhedronTableItem::Cell cell;
        if (polyhedronTableControlAt(scenePos, &cell)) {
            if (isPolyhedronSelectableLabelCell(cell.type) && cell.nodeId > 0) {
                const bool multi = event->modifiers() & Qt::ShiftModifier;
                if (multi) {
                    if (m_selectedPolyhedronElementNodeIds.contains(cell.nodeId))
                        m_selectedPolyhedronElementNodeIds.remove(cell.nodeId);
                    else
                        m_selectedPolyhedronElementNodeIds.insert(cell.nodeId);
                } else if (m_selectedPolyhedronElementNodeIds.size() == 1
                           && m_selectedPolyhedronElementNodeIds.contains(cell.nodeId)) {
                    m_selectedPolyhedronElementNodeIds.clear();
                } else {
                    m_selectedPolyhedronElementNodeIds.clear();
                    m_selectedPolyhedronElementNodeIds.insert(cell.nodeId);
                }

                emit polyhedronElementSelectionChanged(m_selectedPolyhedronElementNodeIds.values().toVector());
                refresh();
                event->accept();
                return;
            }
            if (cell.type == PolyhedronTableItem::Cell::RemovePt
                || cell.type == PolyhedronTableItem::Cell::RemoveFace) {
                m_selectedPolyhedronElementNodeIds.remove(cell.nodeId);
                emit polyhedronElementSelectionChanged(m_selectedPolyhedronElementNodeIds.values().toVector());
                requestAnimatedNodeDelete(cell.nodeId, cell.rect);
                event->accept();
                return;
            }
            if (cell.type == PolyhedronTableItem::Cell::AddPt) {
                const int groupId = polyhedronGroupIdForCell(scenePos);
                if (groupId > 0) {
                    emit polyhedronAddPointRequested(groupId);
                    event->accept();
                    return;
                }
            }
            if (cell.type == PolyhedronTableItem::Cell::AddFace) {
                const int groupId = polyhedronGroupIdForCell(scenePos);
                if (groupId > 0) {
                    emit polyhedronAddFaceRequested(groupId);
                    event->accept();
                    return;
                }
            }
            if (cell.type == PolyhedronTableItem::Cell::AutoFace) {
                const int groupId = polyhedronGroupIdForCell(scenePos);
                if (groupId > 0) {
                    emit polyhedronAutofaceRequested(groupId);
                    event->accept();
                    return;
                }
            }
            if (cell.type == PolyhedronTableItem::Cell::TemplateButton) {
                const int groupId = polyhedronGroupIdForCell(scenePos);
                if (groupId > 0 && cell.index >= 0) {
                    emit polyhedronTemplateRequested(groupId, cell.index);
                    event->accept();
                    return;
                }
            }
            if (cell.type == PolyhedronTableItem::Cell::ClearPolyhedron) {
                const int groupId = polyhedronGroupIdForCell(scenePos);
                if (groupId > 0) {
                    emit polyhedronClearRequested(groupId);
                    event->accept();
                    return;
                }
            }
        }
    }

    // ── Center checkbox ───────────────────────────────────
    if (!colorEditMode() && event->button() == Qt::LeftButton) {
        const QPointF scenePos = mapToScene(event->pos());
        for (const GroupHitArea &area : m_treeLayout.groupHitAreas()) {
            for (const ChildLayout &child : area.children) {
                if (!child.rect.contains(scenePos)) continue;
                const SceneDocument::TreeNode *node = m_scene ? m_scene->treeNodeById(child.nodeId) : nullptr;
                if (!node || node->type != SceneDocument::TreeNode::Primitive) continue;
                const ShapeNode *shape = m_scene ? m_scene->shapeById(node->shapeId) : nullptr;
                if (!shape || !shapeSupportsCenter(static_cast<int>(shape->type))) continue;
                const QRectF cbRect = centerCheckboxRect(child.rect);
                if (cbRect.contains(scenePos)) {
                    emit shapeCenterToggled(node->id, node->shapeId, !shape->center);
                    event->accept();
                    return;
                }
            }
        }
    }

    // ── Canvas-move drag: check grip strip before anything else ──────────────
    if (!colorEditMode() && event->button() == Qt::LeftButton) {
        const QPointF scenePos = mapToScene(event->pos());
        ExpressionEditTarget target;
        if (m_hoverManager->expressionEditTargetAt(scenePos, &target)) {
            m_inlineEditor->startInlineExpressionEdit(target);
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::LeftButton) {
        if (m_canvasDragHandler->handleMousePress(event))
            return;
    }

    if (event->button() == Qt::RightButton && itemAt(event->pos()) == nullptr) {
        handleTreeNodeSelected(0);
        event->accept();
        return;
    }

    // ── Toolbar drag: intercept clicks on the panel background ───────────────
    // itemAt() returns panel even with Qt::NoButton, so we check VP rects
    // directly — do NOT gate on itemAt == nullptr here.
    if (event->button() == Qt::LeftButton
        && isOnToolbarBackground(QPointF(event->pos()))) {
        m_overlay->m_dragPending = true;
        m_overlay->m_dragActive  = false;
        m_overlay->m_dragPressVp = event->pos();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && itemAt(event->pos()) == nullptr) {
        m_canvasController->startPan(event->pos());
        const bool changed = m_hoverManager->m_hoveredScrollRect.isValid() || m_hoverManager->m_hoveredRenameRect.isValid()
                             || m_hoverManager->m_hoveredExpressionRect.isValid();
        m_hoverManager->m_hoveredScrollRect = QRectF();
        m_hoverManager->m_hoveredRenameRect = QRectF();
        m_hoverManager->m_hoveredExpressionRect = QRectF();
        if (changed)
            m_hoverManager->updateHighlightOverlay();
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

    // ── Toolbar drag ─────────────────────────────────────────────────────────
    if (m_overlay->m_dragPending || m_overlay->m_dragActive) {
        setHoveredVariableReferenceName(QString());
        if (m_overlay->m_dragPending) {
            const int dist = (event->pos() - m_overlay->m_dragPressVp).manhattanLength();
            if (dist >= 8) {
                m_overlay->m_dragPending = false;
                m_overlay->m_dragActive  = true;
                setCursor(Qt::ClosedHandCursor);
            }
        }
        if (m_overlay->m_dragActive) {
            const int targetSide = toolbarSnapSideForVpPos(event->pos());
            if (targetSide != m_overlay->m_side) {
                m_overlay->m_side = targetSide;
                updateToolbarOverlay();
            }
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        event->accept();
        return;
    }

    // ── Canvas-move drag ─────────────────────────────────────────────────────
    if (m_canvasDragHandler->handleMouseMove(event))
        return;

    // ── Toggle overlay — highest priority ────────────────────────────────────
    // Compute the toggle's viewport rect directly (ItemIgnoresTransformations means
    // the item's bounding rect is in viewport pixels, not scene units).
    if (m_overlay->m_colorEditToggleItem) {
        const QPointF togVp = mapFromScene(m_overlay->m_colorEditToggleItem->pos());
        if (m_overlay->m_colorEditToggleItem->boundingRect().translated(togVp)
                .contains(QPointF(event->pos()))) {
            setHoveredVariableReferenceName(QString());
            clearColorEditHighlight();                  // remove any stale blink overlay
            QGraphicsView::mouseMoveEvent(event);       // deliver hover enter/move to toggle
            event->accept();
            return;
        }
    }

    // ── Color-edit mode hover ─────────────────────────────────────────────────
    if (colorEditMode()) {
        setHoveredVariableReferenceName(QString());
        updateColorEditHighlight(scenePosition);
        if (!(event->buttons() & Qt::LeftButton))
            QGraphicsView::mouseMoveEvent(event);       // hover events only — block drag propagation
        event->accept();
        return;
    }

    // ── Normal flow ──────────────────────────────────────────────────────────
    m_hoverManager->updateTooltip(event->globalPos(), scenePosition, controlDown);
    m_hoverManager->updateActiveTransformControl(scenePosition, controlDown);
    m_hoverManager->updateActiveColorChannelControl(scenePosition, controlDown);
    m_hoverManager->updateActiveShapeParameterControl(scenePosition, controlDown);
    m_hoverManager->updateActiveVariableNumberControl(scenePosition, controlDown);
    m_hoverManager->updateActiveForLoopRangeControl(scenePosition, controlDown);
    m_hoverManager->updateActiveModuleCallParamControl(scenePosition, controlDown);

    if (m_canvasController->updatePan(event->pos())) {
        setHoveredVariableReferenceName(QString());
        event->accept();
        return;
    }

    m_hoverManager->updateHighlights(scenePosition);
    if (!m_dragActive)
        updateHoveredVariableReference(scenePosition);

    // Palette-tool drag: update the preview directly at the view level so it
    // keeps tracking the cursor even if interactive tree items (expression
    // fields, scroll zones) absorb the event before it reaches the
    // QGraphicsScene grabber.  The call is cheap — startAnimation skips a
    // re-render when the target hasn't changed.
    if (m_dragActive && m_dropPreview->m_movingNodeId == 0
        && !m_dropPreview->m_tool.isEmpty() && !m_dropPreview->m_finishing) {
        showDropPreview(scenePosition,
                        previewSizeForTool(m_dropPreview->m_tool),
                        m_dropPreview->m_tool, 0);
    }

    QGraphicsView::mouseMoveEvent(event);
}

void SceneTreeGraphicsWidget::mouseReleaseEvent(QMouseEvent *event)
{
    // ── Toolbar drag release ──────────────────────────────────────────────────
    if (event->button() == Qt::LeftButton
        && (m_overlay->m_dragActive || m_overlay->m_dragPending)) {
        if (m_overlay->m_dragActive) {
            const int newSide = toolbarSnapSideForVpPos(event->pos());
            if (newSide != m_overlay->m_side) {
                m_overlay->m_side = newSide;
                updateToolbarOverlay();
            }
        }
        m_overlay->m_dragActive  = false;
        m_overlay->m_dragPending = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }

    // ── Canvas-move drag release ──────────────────────────────────────────────
    if (m_canvasDragHandler->handleMouseRelease(event))
        return;

    if (event->button() == Qt::LeftButton && m_canvasController->isPanning()) {
        m_canvasController->stopPan();
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);

    // Fallback: if a palette-tool drag was active but the QGraphicsScene
    // grabber was already gone (stolen by an interactive field), the
    // PaletteToolItem::mouseReleaseEvent never fires.  Handle the drop here.
    if (event->button() == Qt::LeftButton && m_dragActive
        && m_dropPreview->m_movingNodeId == 0
        && !m_dropPreview->m_tool.isEmpty()
        && !m_dropPreview->m_finishing) {
        const QString tool = m_dropPreview->m_tool;
        finishDropPreview();
        handleToolDrop(tool, mapToScene(event->pos()));
    }
}

void SceneTreeGraphicsWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QPointF scenePos = mapToScene(event->pos());
        int renameNodeId = 0;
        QRectF renameRect;
        if (m_hoverManager->hoverRenameZoneAt(scenePos, &renameNodeId, &renameRect)) {
            const SceneDocument::TreeNode *node = m_scene ? m_scene->treeNodeById(renameNodeId) : nullptr;
            if (node) {
                const bool isModule = node->type == SceneDocument::TreeNode::Group
                                      && node->operation == SceneDocument::TreeNode::Module;
                const QString currentName = isModule ? node->moduleName : node->variableName;
                m_inlineEditor->startInlineRename(renameNodeId, isModule, renameRect, currentName);
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
    setHoveredVariableReferenceName(QString());
    clearColorEditHighlight();
    const bool changed = m_hoverManager->m_hoveredScrollRect.isValid() || m_hoverManager->m_hoveredRenameRect.isValid()
                         || m_hoverManager->m_hoveredExpressionRect.isValid();
    m_hoverManager->m_hoveredScrollRect = QRectF();
    m_hoverManager->m_hoveredRenameRect = QRectF();
    m_hoverManager->m_hoveredExpressionRect = QRectF();
    m_hoverManager->updatePolyhedronElementHover(QPointF(), false);
    m_hoverManager->updatePolygonPointHover(QPointF(), false);
    m_hoverManager->updateHoverHint(QStringLiteral("canvas"),
                    QStringLiteral("Scene tree canvas\nWheel: zoom tree view\nDrag empty space: pan; drag toolbar icons to create blocks"));
    if (!m_canvasController->isPanning())
        setCursor(Qt::OpenHandCursor);
    if (changed && !m_dragActive)
        m_hoverManager->updateHighlightOverlay();
}

void SceneTreeGraphicsWidget::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    m_inlineEditor->updateInlineInputGeometry();
    // Clear hover state when the canvas scrolls (positions shift under the cursor).
    const bool changed = m_hoverManager->m_hoveredScrollRect.isValid() || m_hoverManager->m_hoveredRenameRect.isValid()
                         || m_hoverManager->m_hoveredExpressionRect.isValid();
    m_hoverManager->m_hoveredScrollRect = QRectF();
    m_hoverManager->m_hoveredRenameRect = QRectF();
    m_hoverManager->m_hoveredExpressionRect = QRectF();
    m_hoverManager->updatePolyhedronElementHover(QPointF(), false);
    m_hoverManager->updatePolygonPointHover(QPointF(), false);
    if (changed && !m_dragActive)
        m_hoverManager->updateHighlightOverlay();
    // QGraphicsView::scrollContentsBy uses QWidget::scroll() which shifts ALL
    // viewport pixels including ItemIgnoresTransformations overlay items.
    // Repositioning synchronously ensures the queued paint event fires after
    // pos() is corrected, preventing the one-frame "dancing" flicker.
    // repositionToolbarItemsSync() only calls setPos() — no item removal,
    // safe to call here. The fallback for stale sizes is deferred to avoid
    // re-entrancy in updateToolbarOverlay during hover dispatch.
    if (m_overlay->m_items.size() == m_overlay->m_itemOffsets.size()) {
        repositionToolbarItemsSync();
        if (viewport())
            viewport()->update();
    } else if (!m_overlay->m_repositionPending) {
        m_overlay->m_repositionPending = true;
        QTimer::singleShot(0, this, [this]() {
            m_overlay->m_repositionPending = false;
            repositionToolbarItems();
            if (viewport())
                viewport()->update();
        });
    }
}

void SceneTreeGraphicsWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Control) {
        m_hoverManager->updateTooltip(mapToGlobal(m_lastMousePosition), mapToScene(m_lastMousePosition), false);
        m_hoverManager->updateActiveTransformControl(QPointF(), false);
        m_hoverManager->updateActiveColorChannelControl(QPointF(), false);
        m_hoverManager->updateActiveShapeParameterControl(QPointF(), false);
        m_hoverManager->updateActiveVariableNumberControl(QPointF(), false);
        m_hoverManager->updateActiveForLoopRangeControl(QPointF(), false);
        m_hoverManager->updateActiveModuleCallParamControl(QPointF(), false);
        m_hoverManager->updateHighlights(mapToScene(m_lastMousePosition));
        emit ctrlReleased();
    }

    QGraphicsView::keyReleaseEvent(event);
}

void SceneTreeGraphicsWidget::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    m_inlineEditor->updateInlineInputGeometry();
    updateToolbarOverlay();
}

void SceneTreeGraphicsWidget::showEvent(QShowEvent *event)
{
    QGraphicsView::showEvent(event);
    updateToolbarOverlay();
}

void SceneTreeGraphicsWidget::wheelEvent(QWheelEvent *event)
{
    if (colorEditMode() && event->angleDelta().y() != 0) {
        const QPointF scenePos = mapToScene(event->position().toPoint());
        if (m_colorEdit) m_colorEdit->handleWheel(scenePos, event->angleDelta().y());
        event->accept();
        return;
    }

    if ((event->modifiers() & Qt::ControlModifier) && event->angleDelta().y() != 0) {
        const int wheelSteps = event->angleDelta().y() / 120;
        if (wheelSteps != 0) {
            const QPointF scenePosition = mapToScene(event->position().toPoint());
            if (m_wheelHandler->handleCtrlWheel(scenePosition, wheelSteps)) {
                event->accept();
                return;
            }
        }
    }

    m_canvasController->handleWheelZoom(event);
}

void SceneTreeGraphicsWidget::updateToolbarOverlay()
{
    // During an active palette-tool drag, m_dragActive is true and the
    // PaletteToolItem is the QGraphicsScene mouse grabber.  A full
    // updateToolbarOverlay() would call clearToolbar() → removeItem(grabber),
    // which releases the grab and stops mouseMoveEvent delivery, causing the
    // drag ghost to freeze or disappear.  A cheap reposition is safe here and
    // keeps the toolbar visually in the right place until the drag ends.
    if (m_dragActive) {
        repositionToolbarItemsSync();
        return;
    }
    m_overlay->updateToolbarOverlay();
}

void SceneTreeGraphicsWidget::repositionToolbarItems()
{ m_overlay->repositionToolbarItems(); }

void SceneTreeGraphicsWidget::beginTreeZoomSnapshot()
{
    if (!m_treeZoomSnapshotEnabled || m_treeZoomSnapshotItem || !m_graphicsScene || m_treeItems.isEmpty())
        return;

    QRectF bounds;
    qreal maxTreeZ = 0.0;
    for (QGraphicsItem *item : m_treeItems) {
        if (!item || !item->isVisible())
            continue;
        bounds = bounds.united(item->sceneBoundingRect());
        maxTreeZ = qMax(maxTreeZ, item->zValue());
    }
    if (!bounds.isValid() || bounds.isEmpty())
        return;

    bounds = bounds.adjusted(-2.0, -2.0, 2.0, 2.0);

    qreal renderScale = qBound<qreal>(0.5, std::abs(transform().m11()), 2.0);
    QSize pixelSize(int(std::ceil(bounds.width() * renderScale)),
                    int(std::ceil(bounds.height() * renderScale)));
    if (pixelSize.width() <= 0 || pixelSize.height() <= 0)
        return;

    constexpr qint64 MaxSnapshotPixels = 12 * 1024 * 1024;
    const qint64 pixels = qint64(pixelSize.width()) * qint64(pixelSize.height());
    if (pixels > MaxSnapshotPixels) {
        const qreal downscale = std::sqrt(qreal(MaxSnapshotPixels) / qreal(pixels));
        renderScale *= downscale;
        pixelSize = QSize(qMax(1, int(std::ceil(bounds.width() * renderScale))),
                          qMax(1, int(std::ceil(bounds.height() * renderScale))));
    }

    QPixmap snapshot(pixelSize);
    const CanvasBackgroundTheme bgTheme =
        activeCanvasBackgroundTheme(m_canvasBackgroundTheme);
    snapshot.fill(bgTheme.background);

    QSet<QGraphicsItem *> treeSet;
    for (QGraphicsItem *item : m_treeItems)
        if (item)
            treeSet.insert(item);

    QVector<QGraphicsItem *> hiddenItems;
    const QList<QGraphicsItem *> allItems = m_graphicsScene->items();
    for (QGraphicsItem *item : allItems) {
        if (!item || treeSet.contains(item) || !item->isVisible())
            continue;
        hiddenItems.append(item);
        item->setVisible(false);
    }

    // Suppress the scene's hardcoded background brush so the pixmap fill
    // is the only background; the grid is still drawn by drawBackground().
    const QBrush savedBg = m_graphicsScene->backgroundBrush();
    m_graphicsScene->setBackgroundBrush(Qt::NoBrush);

    QPainter painter(&snapshot);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    m_graphicsScene->render(&painter,
        QRectF(QPointF(0.0, 0.0), QSizeF(pixelSize)),
        bounds, Qt::IgnoreAspectRatio);
    painter.end();
    m_graphicsScene->setBackgroundBrush(savedBg);

    for (QGraphicsItem *item : hiddenItems)
        if (item)
            item->setVisible(true);

    auto *snapshotItem = new QGraphicsPixmapItem(snapshot);
    snapshotItem->setPos(bounds.topLeft());
    snapshotItem->setScale(1.0 / renderScale);
    snapshotItem->setZValue(maxTreeZ + 10.0);
    snapshotItem->setAcceptedMouseButtons(Qt::NoButton);
    snapshotItem->setAcceptHoverEvents(false);
    m_graphicsScene->addItem(snapshotItem);
    m_treeZoomSnapshotItem = snapshotItem;

    setTreeItemsVisible(false);
}

void SceneTreeGraphicsWidget::endTreeZoomSnapshot()
{
    setTreeItemsVisible(true);
    if (!m_treeZoomSnapshotItem)
        return;

    m_graphicsScene->removeItem(m_treeZoomSnapshotItem);
    delete m_treeZoomSnapshotItem;
    m_treeZoomSnapshotItem = nullptr;
}


// ── Color-edit paint mode ──────────────────────────────────────────────────────

void SceneTreeGraphicsWidget::setColorEditMode(bool enabled)
{
    if (!m_colorEdit) return;
    m_colorEdit->setEnabled(enabled);
    emit colorEditModeChanged(enabled);
}

bool SceneTreeGraphicsWidget::colorEditMode() const
{
    return m_colorEdit && m_colorEdit->isEnabled();
}

void SceneTreeGraphicsWidget::setTreeZoomSnapshotCacheEnabled(bool enabled)
{
    if (m_treeZoomSnapshotEnabled == enabled)
        return;

    m_treeZoomSnapshotEnabled = enabled;
    if (!enabled)
        endTreeZoomSnapshot();

    QTimer::singleShot(0, this, [this]() {
        updateToolbarOverlay();
    });
}

qreal SceneTreeGraphicsWidget::averageViewportFps() const
{
    return m_averageFrameMs > 0.0 ? 1000.0 / m_averageFrameMs : 0.0;
}

void SceneTreeGraphicsWidget::clearColorEditHighlight()
{
    if (m_colorEdit) m_colorEdit->clearHighlight();
}

void SceneTreeGraphicsWidget::updateColorEditHighlight(const QPointF &scenePos)
{
    if (m_colorEdit) m_colorEdit->updateHighlight(scenePos);
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
                              m_hoverManager->m_activeVariableNodeId,
                              m_hoverManager->m_activeVariableNumberStart,
                              0,
                              -1)
            .setTheme(m_treeTheme)
            .setHighlightedVariableReference(m_hoveredVariableReferenceName, m_variableReferenceBlinkOn)
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
        constexpr qreal RenameTextHeight = 16.0;
        m_renameZones.append({QRectF(rect.left() + 38.0,
                                     rect.top() + (VariableHeight - RenameTextHeight) * 0.5,
                                     nameW,
                                     RenameTextHeight),
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
                          m_hoverManager->m_activeShapeParameterNodeId,
                          m_hoverManager->m_activeShapeParameter,
                          m_hoverManager->m_activeShapeParameterNumberStart,
                          0,
                          -1,
                          0,
                          -1)
        .setTheme(m_treeTheme)
        .setHighlightedVariableReference(m_hoveredVariableReferenceName, m_variableReferenceBlinkOn)
        .renderPrimitive(node, rect, label, shape, thumbnail);

    if (shape && shape->type == ShapeNode::Polygon2D) {
        const QRectF tableRect(rect.left(), rect.top() + PrimitiveHeight + 8.0,
                               rect.width(), qMax<qreal>(0.0, rect.height() - PrimitiveHeight - 8.0));
        QSet<int> selectedPointIndices;
        for (const QString &key : m_selectedPolygonPointKeys) {
            const QStringList parts = key.split(QLatin1Char(':'));
            if (parts.size() != 2)
                continue;
            if (parts[0].toInt() == node.id)
                selectedPointIndices.insert(parts[1].toInt());
        }
        const int hoveredPointIndex = m_hoverManager->m_hoveredPolygonPointNodeId == node.id
                                          ? m_hoverManager->m_hoveredPolygonPointIndex
                                          : -1;
        auto *tableItem = new Polygon2DTableItem(tableRect, node.id, shape, m_treeTheme,
                                                 m_hoverManager->m_activeShapeParameterNodeId,
                                                 m_hoverManager->m_activeShapeParameter,
                                                 selectedPointIndices,
                                                 hoveredPointIndex);
        tableItem->setPos(tableRect.topLeft());
        tableItem->setZValue(6.0);
        m_graphicsScene->addItem(tableItem);
        m_treeItems.append(tableItem);
    }

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

    const int activeMCVarNodeId = (m_hoverManager->m_activeModuleCallNodeId == node.id) ? m_hoverManager->m_activeModuleCallVarNodeId : 0;
    const int activeMCNumberStart = (m_hoverManager->m_activeModuleCallNodeId == node.id) ? m_hoverManager->m_activeModuleCallNumberStart : -1;

    const QImage callThumbnail = m_groupThumbnailCache
        ? m_groupThumbnailCache->thumbnail(node.id)
        : QImage();

    SceneTreeNodeRenderer(m_graphicsScene,
                          m_selectedTreeNodeId,
                          [this](int nodeId) { handleTreeNodeSelected(nodeId); },
                          0, -1, -1, 0, -1, -1, 0, -1, 0, -1,
                          node.id, activeMCVarNodeId, activeMCNumberStart)
        .setTheme(m_treeTheme)
        .setHighlightedVariableReference(m_hoveredVariableReferenceName, m_variableReferenceBlinkOn)
        .renderModuleCall(node, rect, params, callThumbnail);

    // Pass "call" (canonical tool name) not the module name — same reason as "var".
    addNodeDragHandle(node.id, QStringLiteral("call"), rect, rect, rect.size());
    return rect;
}

void SceneTreeGraphicsWidget::drawModuleSectionLabels(
    const QRectF &rect, qreal sepY, int depth,
    const QColor &color, QVector<QGraphicsItem *> *outItems)
{
    const bool blinkMode   = (outItems != nullptr);
    const qreal labelLeft  = rect.left() + GroupPadding + PrimitiveIconSize + 10.0;
    const qreal callLeft   = rect.left() + GroupPadding;
    const qreal labelRight = rect.right() - GroupPadding;
    // moduleCallTemplateRect.top() derivation: after sep is set, drawGroup does
    //   childTopLeft.ry() += 16 + ChildGap, so tmplTop = sepY - ChildGap/2 + 16 + ChildGap = sepY + 16 + ChildGap/2
    const qreal tmplTop    = sepY + 16.0 + ChildGap * 0.5;
    const qreal tmplBottom = tmplTop + VariableHeight;

    // "parameters" label
    auto *paramsLabel = m_graphicsScene->addSimpleText(QStringLiteral("parameters"));
    paramsLabel->setBrush(color);
    if (paramsLabel->boundingRect().width() > labelRight - labelLeft)
        paramsLabel->setScale(qMax<qreal>(0.72, (labelRight - labelLeft) / paramsLabel->boundingRect().width()));
    paramsLabel->setPos(labelLeft, rect.top() + GroupHeaderHeight + 4.0);
    paramsLabel->setZValue(blinkMode ? 8801.0 : depth * 10.0 + 8.0);
    if (blinkMode) outItems->append(paramsLabel);

    // "call handle" label — scale first, then compute Y (same as renderer)
    auto *callLabel = m_graphicsScene->addSimpleText(QStringLiteral("call handle"));
    callLabel->setBrush(color);
    if (callLabel->boundingRect().width() > labelRight - callLeft)
        callLabel->setScale(qMax<qreal>(0.72, (labelRight - callLeft) / callLabel->boundingRect().width()));
    const qreal callLabelY = qMax(sepY + 4.0,
                                   tmplTop - callLabel->boundingRect().height() - 3.0);
    callLabel->setPos(callLeft, callLabelY);
    callLabel->setZValue(blinkMode ? 8801.0 : depth * 10.0 + 8.0);
    if (blinkMode) outItems->append(callLabel);

    // "body" label
    auto *bodyLabel = m_graphicsScene->addSimpleText(QStringLiteral("body"));
    const qreal bodyTop = tmplBottom + ChildGap + 4.0;
    if (bodyTop + bodyLabel->boundingRect().height() <= rect.bottom() - GroupPadding) {
        bodyLabel->setBrush(color);
        bodyLabel->setPos(callLeft, bodyTop);
        bodyLabel->setZValue(blinkMode ? 8801.0 : depth * 10.0 + 8.0);
        if (blinkMode) outItems->append(bodyLabel);
    } else {
        delete bodyLabel;
    }

    // Separator dashed line — same pen width in both modes so dash pattern aligns.
    auto *separator = m_graphicsScene->addLine(
        callLeft, sepY, labelRight, sepY,
        QPen(color, 1.0, Qt::DashLine));
    separator->setZValue(blinkMode ? 8801.0 : depth * 10.0 + 7.0);
    if (blinkMode) outItems->append(separator);
}

QRectF SceneTreeGraphicsWidget::drawGroup(const SceneDocument::TreeNode &node, const QPointF &topLeft, int depth)
{
    QVector<ChildLayout> children;
    const bool collapsedGroup = node.operation != SceneDocument::TreeNode::Scene
                                && m_collapsedGroupIds.contains(node.id);
    const bool verticalHeaderGroup = isVerticalHeaderOperation(node.operation);
    const qreal headerWidth = isTransformOperation(node.operation)
                                  ? transformHeaderWidthForNode(node)
                                  : node.operation == SceneDocument::TreeNode::Resize
                                      ? transformHeaderWidthForNode(node)
                                      : verticalHeaderGroup ? TransformHeaderWidth : 0.0;
    const qreal headerHeight = verticalHeaderGroup ? 0.0 : GroupHeaderHeight;
    QPointF childTopLeft(topLeft.x() + headerWidth + GroupPadding, topLeft.y() + headerHeight + GroupPadding);
    qreal maxChildWidth = 0.0;
    int moduleParameterCount = 0;
    qreal moduleParameterSeparatorY = 0.0;
    QRectF moduleCallTemplateRect;
    QVector<ModuleCallParam> moduleCallTemplateParams;

    const bool isPolyhedron = (node.operation == SceneDocument::TreeNode::Polyhedron);
    const QSizeF polyhedronTableSize = (isPolyhedron && !collapsedGroup && m_scene)
        ? PolyhedronTableItem::estimateSize(node.id, m_scene)
        : QSizeF();
    const qreal polyhedronTableHeight = polyhedronTableSize.height();
    const qreal polyhedronTableWidth  = polyhedronTableSize.width();

    auto drawChild = [&](const SceneDocument::TreeNode &child) {
        if (child.id == m_hiddenForDeleteNodeId)
            return;
        const QRectF childRect = drawNode(child, childTopLeft, depth + 1);
        children.append({childRect, previewToolForNode(child), child.id});
        maxChildWidth = qMax(maxChildWidth, childRect.width());
        childTopLeft.ry() += childRect.height() + ChildGap;
    };

    if (node.operation == SceneDocument::TreeNode::Module) {
        if (!collapsedGroup) {
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
            moduleCallTemplateRect = QRectF(childTopLeft, moduleCallPreviewSize(node.moduleName, moduleCallTemplateParams));
            maxChildWidth = qMax(maxChildWidth, moduleCallTemplateRect.width());
            childTopLeft.ry() += moduleCallTemplateRect.height() + ChildGap;

            childTopLeft.ry() += labelSpace + ChildGap;
            for (const SceneDocument::TreeNode &child : node.children) {
                if (!(child.type == SceneDocument::TreeNode::Variable && child.isParameter))
                    drawChild(child);
            }
        }
    } else if (isPolyhedron && !collapsedGroup) {
        // Reserve space for the polyhedron table — no individual children
        childTopLeft.ry() += polyhedronTableHeight;
        maxChildWidth = qMax(maxChildWidth, polyhedronTableWidth);
    } else if (!collapsedGroup) {
        for (const SceneDocument::TreeNode &child : node.children)
            drawChild(child);
    }

    qreal childrenHeight = children.isEmpty()
                               ? (isPolyhedron && !collapsedGroup
                                      ? polyhedronTableHeight
                                      : PrimitiveHeight)
                               : childTopLeft.y() - topLeft.y() - headerHeight - GroupPadding - ChildGap;
    if (node.operation == SceneDocument::TreeNode::Module && !collapsedGroup) {
        // Modules have complex internal layout (labels, separator, call template) that
        // advance childTopLeft even when children is empty — always derive from actual position.
        // +PrimitiveHeight ensures a visible body drop zone at the bottom.
        const qreal actual = childTopLeft.y() - topLeft.y() - headerHeight - GroupPadding - ChildGap;
        childrenHeight = qMax(actual + PrimitiveHeight, VariableHeight * 2.0 + ChildGap * 7.0);
    }
    if (node.operation == SceneDocument::TreeNode::Difference
        || node.operation == SceneDocument::TreeNode::Union
        || node.operation == SceneDocument::TreeNode::Intersection)
        childrenHeight = qMax(childrenHeight, DifferenceMinContentHeight);
    // Vertical-header nodes (Translate/Rotate/Scale/Mirror/Color) paint 3 parameter
    // rows; the bottom of row Z/B sits at rect.top() + 51.  The group height equals
    // GroupPadding*2 + childrenHeight (= 18 + childrenHeight), so childrenHeight must
    // be at least 37 to keep the last row from clipping (51 - 18 + 4px margin = 37).
    if (verticalHeaderGroup && !collapsedGroup)
        childrenHeight = qMax(childrenHeight, 37.0);

    // Parameter headers can be wider than the children. Measure them so the
    // card is never narrower than the rendered expression.
    qreal parameterHeaderMinWidth = 0.0;
    if (node.operation == SceneDocument::TreeNode::For) {
        parameterHeaderMinWidth = SceneTreeGraphics::forLoopHeaderMinWidth(
            forLoopVariableName(node),
            forLoopRangeExpression(node),
            QFontMetricsF(sceneTreeGraphicsFont()));
    } else if (node.operation == SceneDocument::TreeNode::LinearExtrude) {
        const QString centerExpr = node.linearExtrudeCenter
            ? QStringLiteral("true") : QStringLiteral("false");
        const QString twistExpr = SceneTreeGraphics::linearExtrudeParam(node, 1,
            QString::number(node.linearExtrudeTwist, 'g'));
        const QString slicesExpr = SceneTreeGraphics::linearExtrudeParam(node, 2,
            QString::number(node.linearExtrudeSlices));
        const QString scaleExpr = SceneTreeGraphics::linearExtrudeParam(node, 3,
            QString::number(node.linearExtrudeScaleVal, 'g'));
        parameterHeaderMinWidth = SceneTreeGraphics::linearExtrudeHeaderMinWidth(
            linearExtrudeHeightExpression(node),
            QFontMetricsF(sceneTreeGraphicsFont()),
            centerExpr, twistExpr, slicesExpr, scaleExpr);
    } else if (node.operation == SceneDocument::TreeNode::RotateExtrude) {
        const QString angleExpr = SceneTreeGraphics::rotateExtrudeAngleExpression(node);
        parameterHeaderMinWidth = SceneTreeGraphics::rotateExtrudeHeaderMinWidth(
            angleExpr, QFontMetricsF(sceneTreeGraphicsFont()));
    }

    const QSizeF size = collapsedGroup
        ? QSizeF(qMax(horizontalHeaderMinWidth(node), parameterHeaderMinWidth), GroupHeaderHeight)
        : QSizeF(qMax(qMax(horizontalHeaderMinWidth(node),
                           headerWidth + maxChildWidth + GroupPadding * 2.0),
                      parameterHeaderMinWidth),
                 headerHeight + GroupPadding * 2.0 + childrenHeight);
    const QRectF rect(topLeft, size);
    qreal cutSeparatorY = 0.0;
    if (node.operation == SceneDocument::TreeNode::Difference && !collapsedGroup) {
        cutSeparatorY = rect.top() + GroupHeaderHeight + GroupPadding + PrimitiveHeight + ChildGap * 0.5;
        if (!children.isEmpty())
            cutSeparatorY = children.first().rect.bottom() + ChildGap * 0.5;
    }
    m_treeLayout.addGroup({rect, node.id, depth, node.operation, cutSeparatorY, moduleParameterSeparatorY, moduleParameterCount, collapsedGroup, children});

    const QImage groupThumbnail = (m_groupThumbnailCache && GroupThumbnailCache::isEligibleOperation(node.operation))
                                      ? m_groupThumbnailCache->thumbnail(node.id)
                                      : QImage();

    const int activeVerticalNodeId = m_hoverManager->m_activeColorNodeId > 0
                                     ? m_hoverManager->m_activeColorNodeId
                                     : m_hoverManager->m_activeTransformControlNodeId;
    const int activeVerticalAxis = m_hoverManager->m_activeColorNodeId > 0
                                   ? m_hoverManager->m_activeColorChannel
                                   : m_hoverManager->m_activeTransformControlAxis;
    const int activeVerticalNumberStart = m_hoverManager->m_activeColorNodeId > 0
                                          ? 0
                                          : m_hoverManager->m_activeTransformControlNumberStart;

    SceneTreeNodeRenderer(m_graphicsScene,
                          m_selectedTreeNodeId,
                          [this](int nodeId) { handleTreeNodeSelected(nodeId); },
                          activeVerticalNodeId,
                          activeVerticalAxis,
                          activeVerticalNumberStart,
                          0,
                          -1,
                          -1,
                          0,
                          -1,
                          m_hoverManager->m_activeForLoopNodeId,
                          m_hoverManager->m_activeForLoopNumberStart)
        .setTheme(m_treeTheme)
        .setHighlightedVariableReference(m_hoveredVariableReferenceName, m_variableReferenceBlinkOn)
        .renderGroup(node, rect, depth, cutSeparatorY, groupThumbnail, collapsedGroup);

    // ── Polyhedron table ──────────────────────────────────
    if (isPolyhedron && !collapsedGroup && m_scene) {
        const QRectF tableRect(
            rect.left() + GroupPadding,
            rect.top() + GroupHeaderHeight + GroupPadding,
            rect.width() - GroupPadding * 2,
            polyhedronTableHeight
        );
        auto *tableItem = new PolyhedronTableItem(tableRect, node.id, m_scene, nullptr, m_treeTheme,
                                                       m_hoverManager->m_activeShapeParameterNodeId,
                                                       m_hoverManager->m_activeShapeParameter,
                                                       m_hoverManager->m_activeShapeParameterNumberStart,
                                                       m_selectedPolyhedronElementNodeIds,
                                                       m_hoverManager->m_hoveredPolyhedronElementNodeId);
        m_graphicsScene->addItem(tableItem);
        m_treeItems.append(tableItem);
    }

    if (node.operation == SceneDocument::TreeNode::Module && !collapsedGroup) {
        const QColor moduleLabelColor = SceneTreePalette::textMuted(static_cast<SceneTreePalette::Theme>(m_treeTheme));
        drawModuleSectionLabels(rect, moduleParameterSeparatorY, depth, moduleLabelColor);

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
            .setTheme(m_treeTheme)
            .setHighlightedVariableReference(m_hoveredVariableReferenceName, m_variableReferenceBlinkOn)
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

    }

    const QString groupLabel = labelForOperation(node.operation);
    // Scene is the permanent top-level container — it cannot be dragged or moved.
    // Root-level groups (depth == 0) are repositioned via the canvas-move grip strip,
    // not via the tree-structure drag handle — which always returns no-target for them.
    if (node.operation != SceneDocument::TreeNode::Scene && depth > 0) {
        const QRectF handleRect = verticalHeaderGroup && !collapsedGroup
                                      ? QRectF(rect.topLeft(), QSizeF(headerWidth, rect.height()))
                                      : QRectF(rect.topLeft(), QSizeF(rect.width(), GroupHeaderHeight));
        addNodeDragHandle(node.id, groupLabel, handleRect, rect, rect.size());
    }

    return rect;
}

SceneTreeGraphicsWidget::DropTarget
SceneTreeGraphicsWidget::dropTargetForToolAt(const QPointF &scenePosition,
                                             const QSizeF &previewSize,
                                             const QString &previewTool,
                                             int movingNodeId,
                                             bool allowFreeFloatingInsertion) const
{
    const QSizeF eff = previewSize.isValid() ? previewSize : defaultPreviewSize();
    if (isRootOnlyTreeTool(previewTool)) {
        const QPointF candidateTL = scenePosition - QPointF(eff.width() * 0.5, eff.height() * 0.5);
        QPointF snappedTL;
        const bool magnetic = m_canvasDragHandler->applyMagneticSnap(candidateTL, eff, 0, &snappedTL);
        const QPointF finalTL = magnetic ? snappedTL : candidateTL;
        DropTarget t; t.zoneRect = QRectF(finalTL, eff);
        if (allowFreeFloatingInsertion) { t.placeholderRect = t.zoneRect; t.hasTarget = true; }
        return t;
    }
    DropTarget target = m_treeLayout.dropTargetAt(scenePosition, eff, movingNodeId,
                                                  isVariableToolName(previewTool));
    if (!target.zoneRect.isValid())
        target = freeFloatingDropTarget(scenePosition, eff, allowFreeFloatingInsertion);
    if (target.hasTarget && m_scene) {
        const int rootId = m_scene->treeRoot().id;
        if (target.parentGroupId == rootId) {
            bool rootEligible = false;
            if (movingNodeId > 0) {
                const SceneDocument::TreeNode *n = m_scene->treeNodeById(movingNodeId);
                rootEligible = n && n->type == SceneDocument::TreeNode::Group
                    && (n->operation == SceneDocument::TreeNode::Module
                     || n->operation == SceneDocument::TreeNode::Scene);
            }
            if (!rootEligible) {
                DropTarget no; no.sourceGroupRect = target.sourceGroupRect;
                no.sourceGroupOperation = target.sourceGroupOperation;
                no.sourceCutSeparatorY = target.sourceCutSeparatorY;
                no.sourceChildren = target.sourceChildren;
                no.sourceRect = target.sourceRect;
                no.zoneRect = target.zoneRect;
                return no;
            }
        }
    }
    if (isVariableToolName(previewTool) && m_scene) {
        const int rootId = m_scene->treeRoot().id;
        const SceneDocument::TreeNode *tn = m_scene->treeNodeById(target.parentGroupId);
        const bool isModule = tn && tn->type == SceneDocument::TreeNode::Group
            && tn->operation == SceneDocument::TreeNode::Module;
        const bool isScene = tn && tn->type == SceneDocument::TreeNode::Group
            && tn->operation == SceneDocument::TreeNode::Scene;
        if (target.parentGroupId > 0 && target.parentGroupId != rootId && !isModule && !isScene) {
            DropTarget no; no.sourceGroupRect = target.sourceGroupRect;
            no.sourceGroupOperation = target.sourceGroupOperation;
            no.sourceCutSeparatorY = target.sourceCutSeparatorY;
            no.sourceChildren = target.sourceChildren;
            no.sourceRect = target.sourceRect;
            return no;
        }
    }
    if (previewTool == QStringLiteral("call") && m_scene) {
        const SceneDocument::TreeNode *tn = m_scene->treeNodeById(target.parentGroupId);
        if (!tn || tn->type != SceneDocument::TreeNode::Group) return DropTarget();
        for (const GroupHitArea &area : m_treeLayout.groupHitAreas()) {
            if (area.groupId != target.parentGroupId || area.operation != SceneDocument::TreeNode::Module) continue;
            if (scenePosition.y() < area.moduleParameterSeparatorY) return DropTarget();
            target.insertIndex = qMax(target.insertIndex, area.moduleParameterCount);
            break;
        }
    }
    if (movingNodeId > 0 && m_scene) {
        const SceneDocument::TreeNode *mn = m_scene->treeNodeById(movingNodeId);
        if (mn && mn->type == SceneDocument::TreeNode::ModuleCall) {
            const SceneDocument::TreeNode *tn = m_scene->treeNodeById(target.parentGroupId);
            if (!tn || tn->type != SceneDocument::TreeNode::Group) return DropTarget();
        }
    }
    return target;
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
        m_dropPreview->m_pendingInsertCanvasPosition = target.zoneRect.topLeft();
        m_dropPreview->m_hasPendingInsertPos = true;
    }

    m_dropPreview->scheduleCommit([this, toolName, target]() {
        emit toolDropped(toolName,
                         target.parentGroupId,
                         target.insertIndex,
                         target.moduleParameterZone);
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

    m_dropPreview->scheduleCommit([this, moduleGroupId, target]() {
        emit moduleCallDropped(moduleGroupId,
                               target.parentGroupId,
                               target.insertIndex);
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

        // Delete only when the drop lands in truly empty canvas space.
        // If the preview rect overlaps an existing root-level block the user was
        // probably trying to attach the node to something but missed a valid slot
        // — cancel silently so the node stays where it was.
        const bool overlapsExistingBlock = [&]() {
            for (const CanvasMoveHandle &h : m_canvasMoveHandles) {
                const QRectF isect = target.zoneRect.intersected(h.blockRect);
                if (isect.width() > 1.0 && isect.height() > 1.0)
                    return true;
            }
            return false;
        }();

        if (overlapsExistingBlock) {
            clearDropPreview();
            return;
        }

        m_dropPreview->scheduleCommit([this, nodeId]() {
            requestAnimatedNodeDelete(nodeId);
        });
        return;
    }

    m_dropPreview->scheduleCommit([this, nodeId, target]() {
        emit treeNodeDropped(nodeId,
                             target.parentGroupId,
                             target.insertIndex,
                             target.moduleParameterZone);
    });
}

void SceneTreeGraphicsWidget::handleTreeNodeSelected(int nodeId)
{
    setFocus();
    m_selectedTreeNodeId = nodeId;
    refresh();
    emit treeNodeSelected(nodeId);
}

// ── Hit-test wrappers (delegate to SceneTreeHitTestManager) ──────────────────
bool SceneTreeGraphicsWidget::transformControlAt(const QPointF &p, int *gId, SceneDocument::TreeNode::Operation *op, int *ax, int *ns, int *nl) const
{ return m_hitTest->transformControlAt(p, gId, op, ax, ns, nl); }
bool SceneTreeGraphicsWidget::colorChannelControlAt(const QPointF &p, int *gId, int *ch) const
{ return m_hitTest->colorChannelControlAt(p, gId, ch); }
bool SceneTreeGraphicsWidget::shapeParameterControlAt(const QPointF &p, int *sId, int *nId, int *param, int *ns, int *nl) const
{ return m_hitTest->shapeParameterControlAt(p, sId, nId, param, ns, nl); }
bool SceneTreeGraphicsWidget::polyhedronTableControlAt(const QPointF &p, PolyhedronTableItem::Cell *cell) const
{ return m_hitTest->polyhedronTableControlAt(p, cell); }
int  SceneTreeGraphicsWidget::polyhedronGroupIdForCell(const QPointF &p) const
{ return m_hitTest->polyhedronGroupIdForCell(p); }
bool SceneTreeGraphicsWidget::polygon2DTableControlAt(const QPointF &p, Polygon2DTableItem::Cell *cell) const
{ return m_hitTest->polygon2DTableControlAt(p, cell); }
bool SceneTreeGraphicsWidget::variableNumberControlAt(const QPointF &p, int *nId, int *s, int *l) const
{ return m_hitTest->variableNumberControlAt(p, nId, s, l); }
bool SceneTreeGraphicsWidget::forLoopRangeControlAt(const QPointF &p, int *nId, int *s, int *l) const
{ return m_hitTest->forLoopRangeControlAt(p, nId, s, l); }
bool SceneTreeGraphicsWidget::moduleCallParamControlAt(const QPointF &p, int *mcId, int *pvId, int *s, int *l) const
{ return m_hitTest->moduleCallParamControlAt(p, mcId, pvId, s, l); }

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

void SceneTreeGraphicsWidget::showDropPreview(const QPointF &p, const QSizeF &sz,
                                              const QString &tool, int nodeId)
{ m_dropPreview->show(p, sz, tool, nodeId); }

void SceneTreeGraphicsWidget::finishDropPreview()
{ m_dropPreview->finish(); }

void SceneTreeGraphicsWidget::clearDropPreview()
{ m_dropPreview->clear(); }

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
    // Use only tree items for bounds — toolbar overlay items have
    // ItemIgnoresTransformations and live in viewport-pixel space, so their
    // sceneBoundingRect() is misleading and must not affect the scene rect.
    QRectF bounds;
    for (QGraphicsItem *item : m_treeItems)
        if (item)
            bounds = bounds.united(item->sceneBoundingRect());

    bounds = bounds.adjusted(-CanvasMargin, -CanvasMargin, CanvasMargin, CanvasMargin);

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
