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
#include <QColorDialog>
#include <QMenu>
#include <QEasingCurve>
#include <QFontMetricsF>
#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
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
constexpr int CanvasBackgroundThemeCount = 6;

struct CanvasBackgroundTheme {
    QColor background;
    QColor minorGrid;
    QColor majorGrid;
};

CanvasBackgroundTheme canvasBackgroundTheme(int index)
{
    static const CanvasBackgroundTheme themes[CanvasBackgroundThemeCount] = {
        { QColor(31, 41, 55),   QColor(96, 106, 121),  QColor(139, 150, 166) },
        { QColor(16, 22, 32),   QColor(57, 69, 84),    QColor(92, 108, 128) },
        { QColor(50, 51, 56),   QColor(82, 84, 91),    QColor(119, 122, 131) },
        { QColor(231, 235, 241), QColor(197, 204, 214), QColor(160, 171, 185) },
        { QColor(246, 239, 226), QColor(217, 207, 190), QColor(183, 171, 151) },
        { QColor(220, 235, 241), QColor(185, 207, 216), QColor(145, 177, 190) },
    };
    return themes[qBound(0, index, CanvasBackgroundThemeCount - 1)];
}

CanvasBackgroundTheme activeCanvasBackgroundTheme(int index)
{
    if (SceneTreePalette::hasCustomTheme()) {
        const TreeAppearanceTheme theme = SceneTreePalette::customTheme();
        return {theme.canvas, theme.minorGrid, theme.majorGrid};
    }
    return canvasBackgroundTheme(index);
}

bool usesDarkOverlayGlass(int backgroundTheme)
{
    return activeCanvasBackgroundTheme(backgroundTheme).background.lightness() < 128;
}

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
    const qreal headerHeight = isVerticalHeaderOperation(operation) ? 0.0 : GroupHeaderHeight;
    const qreal minBottom = groupRect.top() + headerHeight + GroupPadding * 2.0 + PrimitiveHeight;
    const qreal contentBottom = content.isValid() ? content.bottom() + GroupPadding : minBottom;
    groupRect.setBottom(qMax(minBottom, contentBottom));
    if (content.isValid())
        groupRect.setRight(qMax(groupRect.right(), content.right() + GroupPadding));
    return groupRect;
}

qreal horizontalHeaderMinWidth(const SceneDocument::TreeNode &node)
{
    if (isVerticalHeaderOperation(node.operation) || node.operation == SceneDocument::TreeNode::For)
        return minimumWidthForOperation(node.operation);

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
    setBackgroundBrush(activeCanvasBackgroundTheme(m_canvasBackgroundTheme).background);
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

    m_panInertiaTimer = new QTimer(this);
    m_panInertiaTimer->setInterval(16);
    connect(m_panInertiaTimer, &QTimer::timeout, this, [this]() {
        if (qAbs(m_panVelocity.x()) < 2.0 && qAbs(m_panVelocity.y()) < 2.0) {
            m_panInertiaTimer->stop();
            m_panVelocity = QPointF();
            return;
        }
        horizontalScrollBar()->setValue(
            horizontalScrollBar()->value() - qRound(m_panVelocity.x()));
        verticalScrollBar()->setValue(
            verticalScrollBar()->value() - qRound(m_panVelocity.y()));
        m_panVelocity *= 0.92;
    });

    m_zoomAnimTimer = new QTimer(this);
    m_zoomAnimTimer->setInterval(24);

    m_zoomIdleTimer = new QTimer(this);
    m_zoomIdleTimer->setSingleShot(true);
    m_zoomIdleTimer->setInterval(80);

    connect(m_zoomIdleTimer, &QTimer::timeout, this, [this]() {
        m_zoomIdle = true;
    });

    connect(m_zoomAnimTimer, &QTimer::timeout, this, [this]() {
        // ── Physics step: accel → velocity → zoom ──────────────────────────
        constexpr qreal kAccelDecay    = 0.70;
        constexpr qreal kVelFriction   = 0.94;
        constexpr qreal kMaxVel        = 0.12;
        constexpr qreal kStepMin       = 0.89;
        constexpr qreal kStepMax       = 1.12;

        m_zoomVelocity += m_zoomAccel;
        m_zoomVelocity = qBound(-kMaxVel, m_zoomVelocity, kMaxVel);

        qreal step = 1.0 + m_zoomVelocity;
        step = qBound(kStepMin, step, kStepMax);

        // ── Damping ────────────────────────────────────────────────────────
        //   Active:  gentle decay — accel persists, speed builds
        //   Idle:    2× decay — fast stop when wheel stops
        if (m_zoomIdle) {
            m_zoomAccel    *= kAccelDecay  * kAccelDecay;   // 0.49
            m_zoomVelocity *= kVelFriction * kVelFriction;  // 0.88
        } else {
            m_zoomAccel    *= kAccelDecay;                   // 0.70
            m_zoomVelocity *= kVelFriction;                  // 0.94
        }

        // ── Apply zoom with stable anchor ───────────────────────────────────
        QPointF anchorVp = mapFromScene(m_zoomAnchorScene);
        scale(step, step);
        QPointF anchorVpAfter = mapFromScene(m_zoomAnchorScene);
        QPointF vpDelta = anchorVpAfter - anchorVp;
        horizontalScrollBar()->setValue(
            horizontalScrollBar()->value() + qRound(vpDelta.x()));
        verticalScrollBar()->setValue(
            verticalScrollBar()->value() + qRound(vpDelta.y()));

        const qreal newLevel = transform().m11();

        // ── Stop when both are negligible ───────────────────────────────────
        if (qAbs(m_zoomVelocity) < 0.002 && qAbs(m_zoomAccel) < 0.0001) {
            m_zoomAnimTimer->stop();
            m_zoomIdleTimer->stop();
            m_zoomVelocity = 0.0;
            m_zoomAccel    = 0.0;
        }

        // ── Only redraw UI when zoom actually changed noticeably ──────────
        if (qAbs(newLevel - m_zoomLevel) > 0.001) {
            m_zoomLevel = newLevel;
            updateInlineInputGeometry();
            updateToolbarOverlay();
        }
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

void SceneTreeGraphicsWidget::focusSelectedNodeAnimated()
{
    if (!m_scene || m_selectedTreeNodeId <= 0 || !viewport()
        || m_dragActive || m_canvasDragActive || m_inlineInputActive) {
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
        toggleGroupCollapsed(groupId);

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
    m_panInertiaTimer->stop();
    m_panVelocity = QPointF();
    snapZoom();

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
                m_zoomLevel = transform().m11();
                centerOn(startCenter + (targetCenter - startCenter) * progress);
                updateInlineInputGeometry();
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
    resetGraphicsScene();
    drawTreeOrPlaceholder();
    updateHoverHighlightOverlay();
    updateSceneRect();
    updateToolbarOverlay();
    syncThumbnailCache();
    syncGroupThumbnailCache();
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
    clearDropPreview();
    // scene->clear() will delete m_colorEditHighlight — null it out first.
    m_colorEditHighlight = nullptr;
    m_graphicsScene->clear();
    m_canvasDragGhost = nullptr;  // scene->clear() already deleted it
    m_canvasDragItems.clear();    // scene->clear() deleted these too
    m_clusterDragItems.clear();
    m_treeLayout.clear();
    m_treeItems.clear();
    m_renameZones.clear();
    m_toolbarItems.clear();
    m_hoverHighlightItems.clear();
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
    QVector<QRectF> placedRootBlocks;

    for (const SceneDocument::TreeNode &child : m_scene->treeRoot().children) {
        // Position: stored custom or auto-layout fallback.
        QPointF blockTopLeft = m_nodeCanvasPositions.value(child.id, autoPos);
        const QList<QGraphicsItem *> beforeBlockItems = m_graphicsScene->items();
        // Draw the node content kGripStripH px below the block top (grip strip occupies top).
        const QRectF drawn = drawNode(child, blockTopLeft + QPointF(0.0, kGripStripH), 0);
        QRectF fullBlock(blockTopLeft, QSizeF(drawn.width(), kGripStripH + drawn.height()));

        const QPointF resolvedTopLeft = nonOverlappingCanvasPosition(blockTopLeft,
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

void SceneTreeGraphicsWidget::clearHoverHighlightOverlay()
{
    for (QGraphicsItem *item : m_hoverHighlightItems) {
        if (!item)
            continue;
        m_graphicsScene->removeItem(item);
        delete item;
    }
    m_hoverHighlightItems.clear();
}

void SceneTreeGraphicsWidget::updateHoverHighlightOverlay()
{
    if (!m_graphicsScene)
        return;

    clearHoverHighlightOverlay();

    const bool hasActiveScrollControl = m_activeTransformControlNodeId > 0
                                        || m_activeColorNodeId > 0
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
        item->setAcceptedMouseButtons(Qt::NoButton);
        item->setZValue(180.0);
        m_hoverHighlightItems.append(item);
    };

    if (!hasActiveScrollControl) {
        addHoverOverlay(m_hoveredScrollRect,
                        QColor(100, 215, 240, 45), QColor(65, 180, 210, 155), 2.5, 5.0);
    }
    addHoverOverlay(m_hoveredRenameRect,
                    QColor(190, 160, 245, 40), QColor(145, 108, 215, 150), 2.0, 4.0);
}

void SceneTreeGraphicsWidget::drawBackground(QPainter *painter, const QRectF &rect)
{
    const CanvasBackgroundTheme background = activeCanvasBackgroundTheme(m_canvasBackgroundTheme);
    painter->fillRect(rect, background.background);
    drawCanvasGrid(painter, rect, 24.0, background.minorGrid, 1);
    drawCanvasGrid(painter, rect, 96.0, background.majorGrid, 1);
}

void SceneTreeGraphicsWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Control) {
        const QPointF scenePosition = mapToScene(m_lastMousePosition);
        updateControlTooltip(mapToGlobal(m_lastMousePosition), scenePosition, true);
        updateActiveTransformControl(scenePosition, true);
        updateActiveColorChannelControl(scenePosition, true);
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

void SceneTreeGraphicsWidget::snapZoom()
{
    m_zoomAnimTimer->stop();
    m_zoomIdleTimer->stop();
    m_zoomAccel    = 0.0;
    m_zoomVelocity = 0.0;
    m_zoomIdle     = true;
}

void SceneTreeGraphicsWidget::focusOutEvent(QFocusEvent *event)
{
    snapZoom();
    QGraphicsView::focusOutEvent(event);
}

void SceneTreeGraphicsWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    m_lastMousePosition = event->pos();

    // Stop any ongoing pan / zoom inertia — scene must be stable for drags
    if (event->button() == Qt::LeftButton) {
        m_panInertiaTimer->stop();
        m_panVelocity = QPointF();
        snapZoom();
    }

    // ── Color-edit mode: intercept left-clicks on card zones ─────────────────
    if (m_colorEditMode && event->button() == Qt::LeftButton) {
        const QPointF scenePos = mapToScene(event->pos());
        // Only toolbar items (theme swatches, the "✏ Colors" toggle button) are
        // allowed to handle their own clicks normally.  Everything else — tree
        // selection items, drag handles, node overlays — is suppressed so that
        // the color-edit click always fires, even when the user clicks on top of
        // an interactive card overlay.
        const QGraphicsItem *hitItem = itemAt(event->pos());
        const bool isToolbarItem = hitItem
            && m_toolbarItems.contains(const_cast<QGraphicsItem *>(hitItem));
        if (!isToolbarItem) {
            handleColorEditClick(scenePos);
            event->accept();
            return;
        }
        // Fall through for toolbar-only items (toggle button, swatches, etc.).
    }

    // ── Canvas-move drag: check grip strip before anything else ──────────────
    if (event->button() == Qt::LeftButton) {
        const QPointF scenePos = mapToScene(event->pos());
        int groupId = 0;
        if (groupCollapseControlAt(scenePos, &groupId)) {
            toggleGroupCollapsed(groupId);
            event->accept();
            return;
        }
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
            updateHoverHighlightOverlay();
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

            // Edge-touching root blocks stay together during a slow drag.
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

            // Snap against stationary blocks only. While the slow-drag cluster is
            // attached, none of its members may attract the group to itself.
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

    // ── Color-edit mode hover ─────────────────────────────────────────────────
    if (m_colorEditMode) {
        updateColorEditHighlight(scenePosition);
        event->accept();
        return;
    }

    // ── Normal flow ──────────────────────────────────────────────────────────
    updateControlTooltip(event->globalPos(), scenePosition, controlDown);
    updateActiveTransformControl(scenePosition, controlDown);
    updateActiveColorChannelControl(scenePosition, controlDown);
    updateActiveShapeParameterControl(scenePosition, controlDown);
    updateActiveVariableNumberControl(scenePosition, controlDown);
    updateActiveForLoopRangeControl(scenePosition, controlDown);
    updateActiveModuleCallParamControl(scenePosition, controlDown);

    if (m_panning) {
        const QPoint delta = event->pos() - m_lastPanPoint;
        m_lastPanPoint = event->pos();
        m_panVelocity = QPointF(delta.x(), delta.y());
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

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
        // Start inertia if velocity is significant
        if (qAbs(m_panVelocity.x()) > 8.0 || qAbs(m_panVelocity.y()) > 8.0)
            m_panInertiaTimer->start();
        else
            m_panVelocity = QPointF();
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
    clearColorEditHighlight();
    const bool changed = m_hoveredScrollRect.isValid() || m_hoveredRenameRect.isValid();
    m_hoveredScrollRect = QRectF();
    m_hoveredRenameRect = QRectF();
    updateHoverHint(QStringLiteral("canvas"),
                    QStringLiteral("Scene tree canvas\nWheel: zoom tree view\nDrag empty space: pan; drag toolbar icons to create blocks"));
    if (!m_panning)
        setCursor(Qt::OpenHandCursor);
    if (changed && !m_dragActive)
        updateHoverHighlightOverlay();
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
        updateHoverHighlightOverlay();
    updateToolbarOverlay();
}

void SceneTreeGraphicsWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Control) {
        updateControlTooltip(mapToGlobal(m_lastMousePosition), mapToScene(m_lastMousePosition), false);
        updateActiveTransformControl(QPointF(), false);
        updateActiveColorChannelControl(QPointF(), false);
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
        if (wheelSteps != 0 && handleColorChannelWheel(scenePosition, wheelSteps)) {
            event->accept();
            return;
        }
        if (wheelSteps != 0 && handleTransformWheel(scenePosition, wheelSteps)) {
            event->accept();
            return;
        }
    }

    // Zoom: acceleration model
    {
        constexpr qreal kAccelPerStep = 0.008;
        constexpr qreal kMaxAccel     = 0.020;

        m_zoomAccel += (event->angleDelta().y() > 0 ? 1 : -1) * kAccelPerStep;
        m_zoomAccel = qBound(-kMaxAccel, m_zoomAccel, kMaxAccel);
        m_zoomAnchorScene = mapToScene(event->position().toPoint());

        // Mark as active (triggers gentle decay in timer)
        m_zoomIdle = false;
        m_zoomIdleTimer->start();  // restart — will set idle after 80ms

        if (!m_zoomAnimTimer->isActive())
            m_zoomAnimTimer->start();
        event->accept();
    }
}

QRectF SceneTreeGraphicsWidget::drawToolbar()
{
    const QPointF viewportTopLeft = mapToScene(QPoint(0, 0));
    const qreal viewportWidth = viewport() ? viewport()->width() : 640.0;
    const qreal viewportScale = transform().m11();

    return SceneTreeToolbarRenderer(m_graphicsScene,
                                    &m_toolbarItems,
                                    m_treeTheme,
                                    usesDarkOverlayGlass(m_canvasBackgroundTheme))
        .render(
            [this](const QPointF &position, const QSizeF &previewSize, const QString &previewTool) {
                showDropPreview(position, previewSize, previewTool);
            },
            [this]() { finishDropPreview(); },
            [this](const QString &toolName, const QPointF &position) {
                handleToolDrop(toolName, position);
            },
            [this](const QString &toolName, bool hovered) {
                if (hovered) {
                    updateHoverHint(QStringLiteral("toolbar:%1").arg(toolName),
                                    toolbarToolTip(toolName));
                } else if (m_hoverHintKey == QStringLiteral("toolbar:%1").arg(toolName)) {
                    updateControlTooltip(mapToGlobal(m_lastMousePosition),
                                         mapToScene(m_lastMousePosition),
                                         QApplication::keyboardModifiers() & Qt::ControlModifier);
                }
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
    drawCanvasBackgroundSwitcher();
    drawThemeSwitcher();
    drawHoverHintOverlay();
}

void SceneTreeGraphicsWidget::handleThemeSwitcherClick(int themeIndex)
{
    const int clamped = qBound(0, themeIndex, SceneTreePalette::ThemeCount - 1);
    const bool customWasActive = SceneTreePalette::hasCustomTheme();
    if (customWasActive) {
        clearCustomAppearanceTheme();
        emit builtInAppearanceSelected();
    }
    if (m_treeTheme == clamped && !customWasActive)
        return;
    setTreeTheme(clamped);
    emit treeThemeChanged(m_treeTheme);
}

void SceneTreeGraphicsWidget::handleCanvasBackgroundSwitcherClick(int backgroundIndex)
{
    const int clamped = qBound(0, backgroundIndex, CanvasBackgroundThemeCount - 1);
    const bool customWasActive = SceneTreePalette::hasCustomTheme();
    if (customWasActive) {
        clearCustomAppearanceTheme();
        emit builtInAppearanceSelected();
    }
    if (m_canvasBackgroundTheme == clamped && !customWasActive)
        return;
    setCanvasBackgroundTheme(clamped);
    emit canvasBackgroundThemeChanged(m_canvasBackgroundTheme);
}

void SceneTreeGraphicsWidget::drawCanvasBackgroundSwitcher()
{
    if (!m_graphicsScene || !viewport())
        return;

    const QPointF viewportTopLeft = mapToScene(QPoint(0, 0));
    const qreal viewportHeight = viewport()->height();
    const qreal safeScale = qMax<qreal>(0.001, std::abs(transform().m11()));
    const auto scenePoint = [&](qreal x, qreal y) {
        return viewportTopLeft + QPointF(x / safeScale, y / safeScale);
    };

    constexpr qreal SwatchR = 7.0;
    constexpr qreal SwatchGap = 5.0;
    constexpr qreal PadH = 7.0;
    constexpr qreal PadV = 6.0;
    constexpr qreal BottomGap = 12.0;
    constexpr qreal RowGap = 8.0;
    constexpr qreal LocalOverlayZ = 10000.0;
    const qreal panelW = CanvasBackgroundThemeCount * (SwatchR * 2.0)
                         + (CanvasBackgroundThemeCount - 1) * SwatchGap + PadH * 2.0;
    const qreal panelH = SwatchR * 2.0 + PadV * 2.0;
    const QRectF panelLocal(0.0, 0.0, panelW, panelH);
    const QPointF panelTopLeft = scenePoint(12.0,
                                            viewportHeight - BottomGap - panelH * 2.0 - RowGap);
    const bool darkGlass = usesDarkOverlayGlass(m_canvasBackgroundTheme);
    const bool customGlass = SceneTreePalette::hasCustomTheme();
    const TreeAppearanceTheme customTheme = SceneTreePalette::customTheme();

    auto *shadow = m_graphicsScene->addRect(panelLocal.translated(2.0, 3.0),
                                             Qt::NoPen,
                                             QBrush(QColor(0, 0, 0, darkGlass ? 90 : 32)));
    shadow->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    shadow->setAcceptedMouseButtons(Qt::NoButton);
    shadow->setPos(panelTopLeft);
    shadow->setZValue(LocalOverlayZ - 2.0);
    shadow->setOpacity(darkGlass ? 0.65 : 0.40);
    m_toolbarItems.append(shadow);

    QPainterPath panelPath;
    panelPath.addRoundedRect(panelLocal, CornerRadius, CornerRadius);
    auto *panel = m_graphicsScene->addPath(panelPath,
                                           QPen(customGlass ? customTheme.glassBorder
                                                            : darkGlass ? QColor(148, 163, 184, 82)
                                                                        : QColor(118, 136, 156, 58), 1.0),
                                           QBrush(customGlass ? customTheme.glassBottom
                                                              : darkGlass ? QColor(10, 16, 24, 178)
                                                                          : QColor(250, 253, 255, 88)));
    panel->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    panel->setAcceptedMouseButtons(Qt::NoButton);
    panel->setPos(panelTopLeft);
    panel->setZValue(LocalOverlayZ - 1.0);
    m_toolbarItems.append(panel);

    for (int i = 0; i < CanvasBackgroundThemeCount; ++i) {
        const bool active = i == m_canvasBackgroundTheme;
        const QColor swatch = canvasBackgroundTheme(i).background;
        const QPointF center(PadH + i * (SwatchR * 2.0 + SwatchGap) + SwatchR,
                             PadV + SwatchR);
        if (active) {
            auto *ring = new ThemeSwitcherSwatchItem(
                center, SwatchR + 2.8,
                QPen(QColor(255, 255, 255, 210), 1.6), Qt::NoBrush,
                i, [this](int idx) { handleCanvasBackgroundSwitcherClick(idx); });
            ring->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
            ring->setPos(panelTopLeft);
            ring->setZValue(LocalOverlayZ);
            m_graphicsScene->addItem(ring);
            m_toolbarItems.append(ring);
        }
        auto *circle = new ThemeSwitcherSwatchItem(
            center, SwatchR,
            active ? QPen(Qt::NoPen) : QPen(swatch.darker(150), 1.0),
            QBrush(swatch),
            i, [this](int idx) { handleCanvasBackgroundSwitcherClick(idx); });
        circle->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        circle->setPos(panelTopLeft);
        circle->setZValue(LocalOverlayZ + 0.5);
        m_graphicsScene->addItem(circle);
        m_toolbarItems.append(circle);
    }
}

// ── Color-edit paint mode ──────────────────────────────────────────────────────

void SceneTreeGraphicsWidget::setColorEditMode(bool enabled)
{
    if (m_colorEditMode == enabled)
        return;
    m_colorEditMode = enabled;
    clearColorEditHighlight();
    if (enabled) {
        setCursor(Qt::PointingHandCursor);
        updateHoverHint(QStringLiteral("colorEdit:mode"),
                        QStringLiteral("✏ Edit colors\n"
                                       "Hover a card area then click to change its color.\n"
                                       "Click the button again to exit edit mode."));
    } else {
        m_colorEditZoneField.clear();
        setCursor(Qt::OpenHandCursor);
        updateHoverHint(QString(), QString());
    }
    updateToolbarOverlay();
    emit colorEditModeChanged(enabled);
}

SceneTreeGraphicsWidget::ColorZoneHit
SceneTreeGraphicsWidget::colorZoneAt(const QPointF &scenePos) const
{
    // Find the innermost (smallest-area) group hit area containing the position.
    const GroupHitArea *best = nullptr;
    for (const GroupHitArea &area : m_treeLayout.groupHitAreas()) {
        if (!area.rect.contains(scenePos))
            continue;
        const qreal area2 = area.rect.width() * area.rect.height();
        if (!best || area2 < best->rect.width() * best->rect.height())
            best = &area;
    }

    if (best) {
        const SceneDocument::TreeNode::Operation op = best->operation;
        // Children first — variable/param rows and primitive cards.
        for (const ChildLayout &child : best->children) {
            if (!child.rect.contains(scenePos))
                continue;
            if (m_scene) {
                const SceneDocument::TreeNode *node = m_scene->treeNodeById(child.nodeId);
                if (node && node->type == SceneDocument::TreeNode::Variable) {
                    const QString label = node->isParameter
                        ? QStringLiteral("Parameter row")
                        : QStringLiteral("Variable row");
                    return {QStringLiteral("input"), label, child.rect, true, op, true};
                }
            }
            return {QStringLiteral("card"), QStringLiteral("Card body"), child.rect, true, op, true};
        }

        // Header zone (top GroupHeaderHeight px of the group card).
        const QRectF hdr(best->rect.left(), best->rect.top(),
                         best->rect.width(), GroupHeaderHeight);
        if (hdr.contains(scenePos))
            return {QStringLiteral("header"), QStringLiteral("Card header"), hdr, true, op, true};

        // Rest of the group body.
        return {QStringLiteral("card"), QStringLiteral("Card body"), best->rect, true, op, true};
    }

    return {QStringLiteral("canvas"), QStringLiteral("Canvas background"), QRectF(), false,
            SceneDocument::TreeNode::Union, false};
}

void SceneTreeGraphicsWidget::updateColorEditHighlight(const QPointF &scenePos)
{
    clearColorEditHighlight();
    if (!m_colorEditMode || !m_graphicsScene)
        return;

    const ColorZoneHit hit = colorZoneAt(scenePos);
    m_colorEditZoneField = hit.fieldName;

    // Gold outline + subtle fill over the detected zone.
    if (hit.valid && !hit.rect.isNull()) {
        const qreal lineW = qMax(1.0, 2.0 / qMax(0.001, qAbs(transform().m11())));
        auto *overlay = m_graphicsScene->addRect(
            hit.rect.adjusted(lineW * 0.5, lineW * 0.5, -lineW * 0.5, -lineW * 0.5),
            QPen(QColor(255, 195, 40, 220), lineW),
            QBrush(QColor(255, 210, 70, 38)));
        overlay->setZValue(8800.0);
        m_colorEditHighlight = overlay;
    }

    const QString prefix = QStringLiteral("✏ Edit colors • ");
    if (hit.valid) {
        // Include the operation name so the user knows which card type will be affected.
        QString hintText = prefix + hit.label;
        if (hit.hasOperation) {
            // Map operation to a display name.
            static const QHash<int, QString> opNames = {
                {(int)SceneDocument::TreeNode::Union,        QStringLiteral("Union")},
                {(int)SceneDocument::TreeNode::Difference,   QStringLiteral("Difference")},
                {(int)SceneDocument::TreeNode::Intersection, QStringLiteral("Intersection")},
                {(int)SceneDocument::TreeNode::Module,       QStringLiteral("Module")},
                {(int)SceneDocument::TreeNode::Translate,    QStringLiteral("Translate")},
                {(int)SceneDocument::TreeNode::Rotate,       QStringLiteral("Rotate")},
                {(int)SceneDocument::TreeNode::Scale,        QStringLiteral("Scale")},
                {(int)SceneDocument::TreeNode::Mirror,       QStringLiteral("Mirror")},
                {(int)SceneDocument::TreeNode::Hull,         QStringLiteral("Hull")},
                {(int)SceneDocument::TreeNode::For,          QStringLiteral("For")},
                {(int)SceneDocument::TreeNode::Scene,        QStringLiteral("Scene")},
            };
            const QString opName = opNames.value(static_cast<int>(hit.operation), QStringLiteral("Group"));
            hintText += QStringLiteral(" [") + opName + QStringLiteral("]");
        }
        hintText += QStringLiteral(" — click to edit colors");
        updateHoverHint(QStringLiteral("colorEdit:%1").arg(hit.fieldName), hintText);
    } else {
        updateHoverHint(QStringLiteral("colorEdit:canvas"),
                        prefix + QStringLiteral("Canvas — click to change background color"));
    }
}

void SceneTreeGraphicsWidget::clearColorEditHighlight()
{
    if (m_colorEditHighlight && m_graphicsScene) {
        m_graphicsScene->removeItem(m_colorEditHighlight);
        delete m_colorEditHighlight;
        m_colorEditHighlight = nullptr;
    }
}

void SceneTreeGraphicsWidget::handleColorEditClick(const QPointF &scenePos)
{
    const ColorZoneHit hit = colorZoneAt(scenePos);

    // ── Canvas background click ───────────────────────────────────────────────
    if (!hit.hasOperation) {
        TreeAppearanceTheme theme = SceneTreePalette::customTheme();
        const QColor chosen = QColorDialog::getColor(theme.canvas, this,
                                                      QStringLiteral("Canvas background"),
                                                      QColorDialog::ShowAlphaChannel);
        if (chosen.isValid()) {
            theme.canvas = chosen;
            SceneTreePalette::setCustomTheme(theme);
            refresh();
            updateToolbarOverlay();
            updateColorEditHighlight(scenePos);
        }
        return;
    }

    // ── Group card click — per-operation color menu ──────────────────────────
    const SceneDocument::TreeNode::Operation op = hit.operation;

    // Build the operation display name.
    static const QHash<int, QString> opDisplayNames = {
        {(int)SceneDocument::TreeNode::Union,        QStringLiteral("Union")},
        {(int)SceneDocument::TreeNode::Difference,   QStringLiteral("Difference")},
        {(int)SceneDocument::TreeNode::Intersection, QStringLiteral("Intersection")},
        {(int)SceneDocument::TreeNode::Module,       QStringLiteral("Module")},
        {(int)SceneDocument::TreeNode::Translate,    QStringLiteral("Translate")},
        {(int)SceneDocument::TreeNode::Rotate,       QStringLiteral("Rotate")},
        {(int)SceneDocument::TreeNode::Scale,        QStringLiteral("Scale")},
        {(int)SceneDocument::TreeNode::Mirror,       QStringLiteral("Mirror")},
        {(int)SceneDocument::TreeNode::Hull,         QStringLiteral("Hull")},
        {(int)SceneDocument::TreeNode::For,          QStringLiteral("For")},
        {(int)SceneDocument::TreeNode::Scene,        QStringLiteral("Scene")},
    };
    const QString opName = opDisplayNames.value(static_cast<int>(op), QStringLiteral("Group"));

    // Resolve current colors (override or derived default).
    const auto pt = static_cast<SceneTreePalette::Theme>(m_treeTheme);
    const OperationCardPalette pal = SceneTreePalette::operationCardPalette(op);
    const QColor currentCard   = pal.card.isValid()   ? pal.card   : SceneTreePalette::groupFill(op, 0, pt);
    const QColor currentHeader = pal.header.isValid() ? pal.header : SceneTreePalette::headerFill(currentCard, pt);
    const QColor currentText   = pal.text.isValid()   ? pal.text   : SceneTreePalette::textPrimary(pt);
    const QColor currentBorder = pal.border.isValid() ? pal.border : SceneTreePalette::cardBorder(op, currentCard, pt);
    const QColor currentActive = pal.numBorderActive.isValid() ? pal.numBorderActive : SceneTreePalette::pillBorderActive();
    const QColor currentActiveFill = pal.numFillActive.isValid() ? pal.numFillActive : SceneTreePalette::pillFillActive();

    // Helper: add a color-picking action with a swatch icon.
    QMenu menu(this);

    // Title (non-interactive).
    auto *titleAction = menu.addAction(QStringLiteral("✏ ") + opName + QStringLiteral(" colors"));
    titleAction->setEnabled(false);
    menu.addSeparator();

    const auto addColorEntry = [&](const QString &label, const QColor &current,
                                   std::function<void(const QColor &)> apply)
    {
        QPixmap swatch(14, 14);
        swatch.fill(current.isValid() ? QColor(current.red(), current.green(), current.blue()) : Qt::gray);
        auto *act = menu.addAction(QIcon(swatch), label);
        QObject::connect(act, &QAction::triggered, this, [this, current, opName, label, apply]() {
            const QColor chosen = QColorDialog::getColor(current, this,
                                                          opName + QStringLiteral(" — ") + label,
                                                          QColorDialog::ShowAlphaChannel);
            if (chosen.isValid())
                apply(chosen);
        });
    };

    addColorEntry(QStringLiteral("Card body"), currentCard, [this, op, &scenePos](const QColor &c) {
        OperationCardPalette p = SceneTreePalette::operationCardPalette(op);
        p.card = c;
        SceneTreePalette::setOperationCardPalette(op, p);
        refresh(); updateToolbarOverlay(); updateColorEditHighlight(scenePos);
    });
    addColorEntry(QStringLiteral("Header"), currentHeader, [this, op, &scenePos](const QColor &c) {
        OperationCardPalette p = SceneTreePalette::operationCardPalette(op);
        p.header = c;
        SceneTreePalette::setOperationCardPalette(op, p);
        refresh(); updateToolbarOverlay(); updateColorEditHighlight(scenePos);
    });
    addColorEntry(QStringLiteral("Text"), currentText, [this, op, &scenePos](const QColor &c) {
        OperationCardPalette p = SceneTreePalette::operationCardPalette(op);
        p.text = c;
        SceneTreePalette::setOperationCardPalette(op, p);
        refresh(); updateToolbarOverlay(); updateColorEditHighlight(scenePos);
    });
    addColorEntry(QStringLiteral("Border"), currentBorder, [this, op, &scenePos](const QColor &c) {
        OperationCardPalette p = SceneTreePalette::operationCardPalette(op);
        p.border = c;
        SceneTreePalette::setOperationCardPalette(op, p);
        refresh(); updateToolbarOverlay(); updateColorEditHighlight(scenePos);
    });
    menu.addSeparator();
    addColorEntry(QStringLiteral("Active highlight"), currentActive, [this, op, &scenePos, currentActiveFill](const QColor &c) {
        OperationCardPalette p = SceneTreePalette::operationCardPalette(op);
        p.numBorderActive = c;
        // Also update fill as a 50%-alpha version of the same hue if not already customised.
        if (!p.numFillActive.isValid())
            p.numFillActive = QColor(c.red(), c.green(), c.blue(), 128);
        SceneTreePalette::setOperationCardPalette(op, p);
        refresh(); updateToolbarOverlay(); updateColorEditHighlight(scenePos);
    });
    addColorEntry(QStringLiteral("Highlight fill"), currentActiveFill, [this, op, &scenePos](const QColor &c) {
        OperationCardPalette p = SceneTreePalette::operationCardPalette(op);
        p.numFillActive = c;
        SceneTreePalette::setOperationCardPalette(op, p);
        refresh(); updateToolbarOverlay(); updateColorEditHighlight(scenePos);
    });
    menu.addSeparator();

    // Reset this operation to defaults.
    auto *resetAct = menu.addAction(QStringLiteral("Reset ") + opName + QStringLiteral(" to default"));
    QObject::connect(resetAct, &QAction::triggered, this, [this, op, &scenePos]() {
        TreeAppearanceTheme theme = SceneTreePalette::customTheme();
        theme.operationCards.remove(static_cast<int>(op));
        SceneTreePalette::setCustomTheme(theme);
        refresh(); updateToolbarOverlay(); updateColorEditHighlight(scenePos);
    });

    menu.exec(QCursor::pos());
}

// ── End of color-edit section ─────────────────────────────────────────────────

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
    const bool darkGlass = usesDarkOverlayGlass(m_canvasBackgroundTheme);
    const bool customGlass = SceneTreePalette::hasCustomTheme();
    const TreeAppearanceTheme customTheme = SceneTreePalette::customTheme();

    // Drop shadow.
    auto *shadow = m_graphicsScene->addRect(panelLocal.translated(2.0, 3.0),
                                             Qt::NoPen,
                                             QBrush(QColor(0, 0, 0, darkGlass ? 90 : 32)));
    shadow->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    shadow->setAcceptedMouseButtons(Qt::NoButton);
    shadow->setPos(panelTopLeft);
    shadow->setZValue(LocalOverlayZ - 2.0);
    shadow->setOpacity(darkGlass ? 0.65 : 0.40);
    m_toolbarItems.append(shadow);

    // Glass panel background — same style as toolbar.
    QPainterPath panelPath;
    panelPath.addRoundedRect(panelLocal, CornerRadius, CornerRadius);
    auto *panel = m_graphicsScene->addPath(panelPath,
                                            QPen(customGlass ? customTheme.glassBorder
                                                             : darkGlass ? QColor(148, 163, 184, 82)
                                                                         : QColor(118, 136, 156, 58), 1.0),
                                            QBrush(customGlass ? customTheme.glassBottom
                                                               : darkGlass ? QColor(10, 16, 24, 178)
                                                                           : QColor(250, 253, 255, 88)));
    panel->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    panel->setAcceptedMouseButtons(Qt::NoButton);
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

    // ── Color-edit mode toggle button ─────────────────────────────────────────
    // A small "✏ Colors" button to the right of the swatch panel, with the
    // same glass style.  When active it gets a warm gold outline.
    {
        constexpr qreal BtnGap  = 6.0;   // gap between swatch panel right edge and button
        constexpr qreal BtnPadH = 8.0;

        // Measure the button label.
        const QString btnLabel = m_colorEditMode
            ? QStringLiteral("✏ Colors ✓")   // ✏ Colors ✓
            : QStringLiteral("✏ Colors");           // ✏ Colors

        QFont btnFont = sceneTreeGraphicsFont();
        btnFont.setPointSizeF(qMax(7.5, btnFont.pointSizeF() - 0.5));
        const QFontMetricsF fm(btnFont);
        const qreal textW = fm.horizontalAdvance(btnLabel);
        const qreal btnW  = textW + BtnPadH * 2.0;
        const qreal btnH  = panelH;   // same height as swatch panel

        // Position: immediately to the right of the swatch panel.
        const QPointF btnTopLeft = scenePoint(12.0 + panelW + BtnGap,
                                               viewportHeight - BottomGap - btnH);
        const QRectF  btnLocal(0.0, 0.0, btnW, btnH);

        // Shadow.
        auto *btnShadow = m_graphicsScene->addRect(btnLocal.translated(2.0, 3.0),
                                                    Qt::NoPen,
                                                    QBrush(QColor(0, 0, 0, darkGlass ? 90 : 32)));
        btnShadow->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        btnShadow->setAcceptedMouseButtons(Qt::NoButton);
        btnShadow->setPos(btnTopLeft);
        btnShadow->setZValue(LocalOverlayZ - 2.0);
        btnShadow->setOpacity(darkGlass ? 0.65 : 0.40);
        m_toolbarItems.append(btnShadow);

        // Panel background — highlighted when active.
        QPainterPath btnPath;
        btnPath.addRoundedRect(btnLocal, CornerRadius, CornerRadius);
        const QColor btnBorder = m_colorEditMode
            ? QColor(255, 195, 40, 230)
            : (customGlass ? customTheme.glassBorder
                           : darkGlass ? QColor(148, 163, 184, 82)
                                       : QColor(118, 136, 156, 58));
        const QColor btnBg = m_colorEditMode
            ? QColor(255, 200, 50, 55)
            : (customGlass ? customTheme.glassBottom
                           : darkGlass ? QColor(10, 16, 24, 178)
                                       : QColor(250, 253, 255, 88));
        auto *btnPanel = m_graphicsScene->addPath(btnPath,
                                                   QPen(btnBorder, m_colorEditMode ? 1.5 : 1.0),
                                                   QBrush(btnBg));
        btnPanel->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        btnPanel->setAcceptedMouseButtons(Qt::NoButton);
        btnPanel->setPos(btnTopLeft);
        btnPanel->setZValue(LocalOverlayZ - 1.0);
        m_toolbarItems.append(btnPanel);

        // Button label text.
        auto *btnText = m_graphicsScene->addText(btnLabel, btnFont);
        btnText->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        btnText->setAcceptedMouseButtons(Qt::NoButton);
        btnText->setDefaultTextColor(m_colorEditMode
            ? QColor(255, 195, 40, 240)
            : (darkGlass ? QColor(210, 225, 248, 210) : QColor(34, 45, 58, 200)));
        // Center text inside the button rect.
        btnText->setPos(btnTopLeft + QPointF(BtnPadH,
                                              (btnH - fm.height()) * 0.5 - fm.descent() * 0.5 + 0.5));
        btnText->setZValue(LocalOverlayZ + 0.5);
        m_toolbarItems.append(btnText);

        // Invisible click-catcher rect on top (so the whole button area is clickable).
        class ColorEditBtnItem : public QGraphicsRectItem {
        public:
            ColorEditBtnItem(const QRectF &r, std::function<void()> fn)
                : QGraphicsRectItem(r), m_fn(std::move(fn))
            { setPen(Qt::NoPen); setBrush(Qt::NoBrush);
              setAcceptedMouseButtons(Qt::LeftButton); }
        protected:
            void mousePressEvent(QGraphicsSceneMouseEvent *e) override
            { if (e->button() == Qt::LeftButton) { m_fn(); e->accept(); } else e->ignore(); }
        private:
            std::function<void()> m_fn;
        };
        auto *clickArea = new ColorEditBtnItem(btnLocal,
                                               [this]{ setColorEditMode(!m_colorEditMode); });
        clickArea->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        clickArea->setPos(btnTopLeft);
        clickArea->setZValue(LocalOverlayZ + 1.0);
        m_graphicsScene->addItem(clickArea);
        m_toolbarItems.append(clickArea);
    }
}

void SceneTreeGraphicsWidget::drawHoverHintOverlay()
{
    if (!m_graphicsScene || !viewport())
        return;

    const QString hint = m_hoverHintText.trimmed().isEmpty()
                             ? QStringLiteral("Scene tree\nHover blocks, values, gaps, or handles to see available actions.")
                             : m_hoverHintText;

    const QPointF viewportTopLeft = mapToScene(QPoint(0, 0));
    const qreal viewportWidth = viewport()->width();
    const qreal viewportHeight = viewport()->height();
    const qreal viewportScale = transform().m11();
    const qreal safeScale = qMax<qreal>(0.001, std::abs(viewportScale));

    const auto scenePoint = [&](qreal x, qreal y) {
        return viewportTopLeft + QPointF(x / safeScale, y / safeScale);
    };

    constexpr qreal OverlayMargin = 12.0;
    constexpr qreal ThemePanelWidth = 123.0;
    constexpr qreal ThemePanelHeight = 26.0;
    constexpr qreal BackgroundPanelHeight = 26.0;
    constexpr qreal SwitcherRowGap = 8.0;
    constexpr qreal Gap = 14.0;
    constexpr qreal PadH = 12.0;
    constexpr qreal PadV = 9.0;
    constexpr qreal BottomGap = 12.0;
    constexpr qreal LocalOverlayZ = 10020.0;

    qreal panelX = OverlayMargin + ThemePanelWidth + Gap;
    qreal availableW = viewportWidth - panelX - OverlayMargin;
    if (availableW < 260.0) {
        panelX = OverlayMargin;
        availableW = viewportWidth - OverlayMargin * 2.0;
    }
    availableW = qBound<qreal>(220.0, availableW, 640.0);

    QFont font = sceneTreeGraphicsFont();
    font.setPointSizeF(qMax<qreal>(8.0, font.pointSizeF() - 0.2));
    const qreal textW = availableW - PadH * 2.0;
    QTextDocument textMeasure;
    textMeasure.setDefaultFont(font);
    textMeasure.setDocumentMargin(0.0);
    textMeasure.setTextWidth(textW);
    textMeasure.setPlainText(hint);
    const qreal maxPanelH = qMax<qreal>(84.0, viewportHeight * 0.38);
    const qreal panelH = qBound<qreal>(42.0,
                                       textMeasure.size().height() + PadV * 2.0 + 4.0,
                                       maxPanelH);
    qreal panelY = viewportHeight - BottomGap - panelH;
    if (panelX <= OverlayMargin + 0.5)
        panelY -= ThemePanelHeight + BackgroundPanelHeight + SwitcherRowGap + Gap;

    const QRectF panelLocal(0.0, 0.0, availableW, panelH);
    const QPointF panelTopLeft = scenePoint(panelX, qMax<qreal>(OverlayMargin, panelY));
    const bool darkGlass = usesDarkOverlayGlass(m_canvasBackgroundTheme);
    const bool customGlass = SceneTreePalette::hasCustomTheme();
    const TreeAppearanceTheme customTheme = SceneTreePalette::customTheme();

    auto *shadow = m_graphicsScene->addRect(panelLocal.translated(3.0, 4.0),
                                            Qt::NoPen,
                                            QBrush(QColor(0, 0, 0, darkGlass ? 115 : 38)));
    shadow->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    shadow->setAcceptedMouseButtons(Qt::NoButton);
    shadow->setPos(panelTopLeft);
    shadow->setZValue(LocalOverlayZ - 2.0);
    shadow->setOpacity(darkGlass ? 0.72 : 0.42);
    m_toolbarItems.append(shadow);

    QPainterPath path;
    path.addRoundedRect(panelLocal, 8.0, 8.0);
    QLinearGradient glass(QPointF(0.0, 0.0), QPointF(0.0, panelH));
    glass.setColorAt(0.0, customGlass ? customTheme.glassTop
                                      : darkGlass ? QColor(24, 34, 50, 218) : QColor(255, 255, 255, 116));
    glass.setColorAt(1.0, customGlass ? customTheme.glassBottom
                                      : darkGlass ? QColor(8, 13, 22, 196) : QColor(237, 244, 249, 74));
    auto *panel = m_graphicsScene->addPath(path,
                                           QPen(customGlass ? customTheme.glassBorder
                                                            : darkGlass ? QColor(142, 178, 215, 120)
                                                                        : QColor(116, 141, 166, 70), 1.0),
                                           QBrush(glass));
    panel->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    panel->setAcceptedMouseButtons(Qt::NoButton);
    panel->setPos(panelTopLeft);
    panel->setZValue(LocalOverlayZ - 1.0);
    m_toolbarItems.append(panel);

    auto *text = m_graphicsScene->addText(hint, font);
    text->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    text->setAcceptedMouseButtons(Qt::NoButton);
    text->document()->setDocumentMargin(0.0);
    text->setTextWidth(textW);
    text->setDefaultTextColor(customGlass ? customTheme.text
                                          : darkGlass ? QColor(232, 242, 255) : QColor(39, 51, 66));
    text->setPos(scenePoint(panelX + PadH, qMax<qreal>(OverlayMargin, panelY) + PadV));
    text->setZValue(LocalOverlayZ);
    m_toolbarItems.append(text);
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

    const QImage callThumbnail = m_groupThumbnailCache
        ? m_groupThumbnailCache->thumbnail(node.id)
        : QImage();

    SceneTreeNodeRenderer(m_graphicsScene,
                          m_selectedTreeNodeId,
                          [this](int nodeId) { handleTreeNodeSelected(nodeId); },
                          0, -1, -1, 0, -1, -1, 0, -1, 0, -1,
                          node.id, activeMCVarNodeId, activeMCNumberStart)
        .renderModuleCall(node, rect, params, callThumbnail);

    // Pass "call" (canonical tool name) not the module name — same reason as "var".
    addNodeDragHandle(node.id, QStringLiteral("call"), rect, rect, rect.size());
    return rect;
}

QRectF SceneTreeGraphicsWidget::drawGroup(const SceneDocument::TreeNode &node, const QPointF &topLeft, int depth)
{
    QVector<ChildLayout> children;
    const bool collapsedGroup = node.operation != SceneDocument::TreeNode::Scene
                                && m_collapsedGroupIds.contains(node.id);
    const bool verticalHeaderGroup = isVerticalHeaderOperation(node.operation);
    const qreal headerWidth = isTransformOperation(node.operation)
                                  ? transformHeaderWidthForNode(node)
                                  : verticalHeaderGroup ? TransformHeaderWidth : 0.0;
    const qreal headerHeight = verticalHeaderGroup ? 0.0 : GroupHeaderHeight;
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
            moduleCallTemplateLabelY = moduleParameterSeparatorY + 4.0;
            moduleCallTemplateRect = QRectF(childTopLeft, moduleCallPreviewSize(node.moduleName, moduleCallTemplateParams));
            maxChildWidth = qMax(maxChildWidth, moduleCallTemplateRect.width());
            childTopLeft.ry() += moduleCallTemplateRect.height() + ChildGap;

            childTopLeft.ry() += labelSpace + ChildGap;
            for (const SceneDocument::TreeNode &child : node.children) {
                if (!(child.type == SceneDocument::TreeNode::Variable && child.isParameter))
                    drawChild(child);
            }
        }
    } else if (!collapsedGroup) {
        for (const SceneDocument::TreeNode &child : node.children)
            drawChild(child);
    }

    qreal childrenHeight = children.isEmpty()
                               ? PrimitiveHeight
                               : childTopLeft.y() - topLeft.y() - headerHeight - GroupPadding - ChildGap;
    if (node.operation == SceneDocument::TreeNode::Module && !collapsedGroup)
        childrenHeight = qMax(childrenHeight + 10.0, VariableHeight * 2.0 + ChildGap * 7.0);
    if (node.operation == SceneDocument::TreeNode::Difference)
        childrenHeight = qMax(childrenHeight, DifferenceMinContentHeight);

    // For a for-loop the header text can be much wider than the children.
    // Measure it so the card is never narrower than the rendered range expression.
    qreal forLoopHeaderMinWidth = 0.0;
    if (node.operation == SceneDocument::TreeNode::For) {
        forLoopHeaderMinWidth = SceneTreeGraphics::forLoopHeaderMinWidth(
            forLoopVariableName(node),
            forLoopRangeExpression(node),
            QFontMetricsF(sceneTreeGraphicsFont()));
    }

    const QSizeF size = collapsedGroup
        ? QSizeF(qMax(horizontalHeaderMinWidth(node), forLoopHeaderMinWidth), GroupHeaderHeight)
        : QSizeF(qMax(qMax(horizontalHeaderMinWidth(node),
                           headerWidth + maxChildWidth + GroupPadding * 2.0),
                      forLoopHeaderMinWidth),
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

    const int activeVerticalNodeId = m_activeColorNodeId > 0
                                     ? m_activeColorNodeId
                                     : m_activeTransformControlNodeId;
    const int activeVerticalAxis = m_activeColorNodeId > 0
                                   ? m_activeColorChannel
                                   : m_activeTransformControlAxis;
    const int activeVerticalNumberStart = m_activeColorNodeId > 0
                                          ? 0
                                          : m_activeTransformControlNumberStart;

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
                          m_activeForLoopNodeId,
                          m_activeForLoopNumberStart)
        .setTheme(m_treeTheme)
        .renderGroup(node, rect, depth, cutSeparatorY, groupThumbnail, collapsedGroup);

    if (node.operation == SceneDocument::TreeNode::Module && !collapsedGroup) {
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
        const QRectF handleRect = verticalHeaderGroup && !collapsedGroup
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

    scheduleDropCommit([this, moduleGroupId, target]() {
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

    // ── Root-level guard ──────────────────────────────────────────────────────
    // Only Module and Scene groups may live as direct children of the invisible
    // root node.  All other drops that land at root level are silently rejected:
    // the node shows the "no valid slot" animation but is not moved or deleted.
    // (Module tool drops are handled by the isRootOnlyTreeTool branch above and
    //  never reach this point, so we need not special-case them here.)
    if (target.hasTarget && m_scene) {
        const int rootId = m_scene->treeRoot().id;
        if (target.parentGroupId == rootId) {
            bool rootEligible = false;
            if (movingNodeId > 0) {
                const SceneDocument::TreeNode *n = m_scene->treeNodeById(movingNodeId);
                rootEligible = n
                    && n->type == SceneDocument::TreeNode::Group
                    && (n->operation == SceneDocument::TreeNode::Module
                     || n->operation == SceneDocument::TreeNode::Scene);
            }
            if (!rootEligible) {
                // Preserve source fields so the drop-preview animation can still
                // show the source group collapsing back smoothly.
                DropTarget noTarget;
                noTarget.sourceGroupRect      = target.sourceGroupRect;
                noTarget.sourceGroupOperation = target.sourceGroupOperation;
                noTarget.sourceCutSeparatorY  = target.sourceCutSeparatorY;
                noTarget.sourceChildren       = target.sourceChildren;
                noTarget.sourceRect           = target.sourceRect;
                noTarget.zoneRect             = target.zoneRect;
                return noTarget;
            }
        }
    }

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
                || area.operation != SceneDocument::TreeNode::Module) {
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

        scheduleDropCommit([this, nodeId]() {
            emit treeNodeDeleteRequested(nodeId);
        });
        return;
    }

    scheduleDropCommit([this, nodeId, target]() {
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

bool SceneTreeGraphicsWidget::handleColorChannelWheel(const QPointF &scenePosition, int wheelSteps)
{
    if (!m_scene)
        return false;

    int groupId = 0;
    int channel = -1;
    if (!colorChannelControlAt(scenePosition, &groupId, &channel))
        return false;

    emit colorChannelAdjusted(groupId, channel, static_cast<qreal>(wheelSteps));
    updateActiveColorChannelControl(scenePosition, true);
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

bool SceneTreeGraphicsWidget::colorChannelControlAt(const QPointF &scenePosition,
                                                    int *groupId,
                                                    int *channel) const
{
    const GroupHitArea *bestArea = nullptr;
    int bestChannel = -1;
    for (const GroupHitArea &area : m_treeLayout.groupHitAreas()) {
        if (area.collapsed || area.operation != SceneDocument::TreeNode::Color || !area.rect.contains(scenePosition))
            continue;

        int hitChannel = -1;
        for (int i = 0; i < 3; ++i) {
            if (transformParameterControlRect(area.rect, i, TransformHeaderWidth).contains(scenePosition)) {
                hitChannel = i;
                break;
            }
        }
        if (hitChannel < 0)
            continue;

        if (!bestArea || area.depth > bestArea->depth) {
            bestArea = &area;
            bestChannel = hitChannel;
        }
    }

    if (!bestArea)
        return false;

    if (groupId) *groupId = bestArea->groupId;
    if (channel) *channel = bestChannel;
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
        if (area.collapsed)
            continue;
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
        if (area.collapsed || area.operation != SceneDocument::TreeNode::For || !area.rect.contains(scenePosition))
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
    Q_UNUSED(globalPosition);
    if (m_dragActive)
        return;

    QString key;
    const QString message = hoverHintTextForPosition(scenePosition, controlDown, &key);
    updateHoverHint(key, message);
}

void SceneTreeGraphicsWidget::updateHoverHint(const QString &key, const QString &text)
{
    if (key == m_hoverHintKey && text == m_hoverHintText)
        return;

    m_hoverHintKey = key;
    m_hoverHintText = text;
    if (m_dragActive)
        return;

    updateToolbarOverlay();
}

QString SceneTreeGraphicsWidget::hoverHintTextForPosition(const QPointF &scenePosition,
                                                          bool controlDown,
                                                          QString *key) const
{
    auto setKey = [key](const QString &value) {
        if (key)
            *key = value;
    };

    int collapseGroupId = 0;
    if (groupCollapseControlAt(scenePosition, &collapseGroupId)) {
        const bool collapsed = m_collapsedGroupIds.contains(collapseGroupId);
        setKey(QStringLiteral("group-collapse:%1:%2").arg(collapseGroupId).arg(collapsed));
        return collapsed
            ? QStringLiteral("Group contents hidden\nClick to expand this group")
            : QStringLiteral("Group contents visible\nClick to collapse this group");
    }

    int forNodeId = 0;
    int forNumberStart = -1;
    int forNumberLength = 0;
    if (forLoopRangeControlAt(scenePosition, &forNodeId, &forNumberStart, &forNumberLength)) {
        setKey(QStringLiteral("for:%1:%2:%3").arg(forNodeId).arg(forNumberStart).arg(controlDown));
        return controlDown
            ? QStringLiteral("For range number\nMouse wheel: change this range value\nDouble-click module/variable labels to rename")
            : QStringLiteral("For range number\nHold Ctrl + mouse wheel: change this value\nDrag child blocks into the loop body");
    }

    int groupId = 0;
    int axis = -1;
    SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
    int transformNumberStart = -1;
    int transformNumberLength = 0;
    if (transformControlAt(scenePosition, &groupId, &operation, &axis, &transformNumberStart, &transformNumberLength)
        && transformNumberStart >= 0
        && transformNumberLength > 0) {
        static const char *AxisNames[] = {"X", "Y", "Z"};
        const QString axisName = QString::fromLatin1(AxisNames[qBound(0, axis, 2)]);
        setKey(QStringLiteral("transform:%1:%2:%3").arg(groupId).arg(transformNumberStart).arg(controlDown));
        return controlDown
            ? QStringLiteral("%1 %2 value\nMouse wheel: change value\nDrag selected viewport axes: move/rotate when supported")
                  .arg(labelForOperation(operation), axisName)
            : QStringLiteral("%1 %2 value\nHold Ctrl + mouse wheel: change value\nClick block: select the transform group")
                  .arg(labelForOperation(operation), axisName);
    }

    int colorGroupId = 0;
    int colorChannel = -1;
    if (colorChannelControlAt(scenePosition, &colorGroupId, &colorChannel)) {
        static const char *ChannelNames[] = {"R", "G", "B"};
        const QString channelName = QString::fromLatin1(ChannelNames[qBound(0, colorChannel, 2)]);
        setKey(QStringLiteral("color:%1:%2:%3").arg(colorGroupId).arg(colorChannel).arg(controlDown));
        return controlDown
            ? QStringLiteral("Color %1 channel\nMouse wheel: change channel\nShift makes bigger steps").arg(channelName)
            : QStringLiteral("Color %1 channel\nHold Ctrl + mouse wheel: change channel\nDrop blocks into the color body").arg(channelName);
    }

    int variableNodeId = 0;
    int numberStart = -1;
    int numberLength = 0;
    if (variableNumberControlAt(scenePosition, &variableNodeId, &numberStart, &numberLength)) {
        setKey(QStringLiteral("variable:%1:%2:%3").arg(variableNodeId).arg(numberStart).arg(controlDown));
        return controlDown
            ? QStringLiteral("Variable value\nMouse wheel: change this number\nDouble-click variable name: rename")
            : QStringLiteral("Variable value\nHold Ctrl + mouse wheel: change this number\nDrag VAR into module parameters/body");
    }

    int moduleCallNodeId = 0;
    int moduleCallVarNodeId = 0;
    int moduleCallStart = -1;
    int moduleCallLength = 0;
    if (moduleCallParamControlAt(scenePosition, &moduleCallNodeId, &moduleCallVarNodeId, &moduleCallStart, &moduleCallLength)) {
        setKey(QStringLiteral("modulecall:%1:%2:%3").arg(moduleCallNodeId).arg(moduleCallStart).arg(controlDown));
        return controlDown
            ? QStringLiteral("Module call argument\nMouse wheel: change this argument\nDrop the call into groups like any block")
            : QStringLiteral("Module call argument\nHold Ctrl + mouse wheel: change this argument\nDrag module call handles from module blocks");
    }

    int shapeId = -1;
    int nodeId = 0;
    int parameter = -1;
    int shapeNumberStart = -1;
    int shapeNumberLength = 0;
    if (shapeParameterControlAt(scenePosition, &shapeId, &nodeId, &parameter, &shapeNumberStart, &shapeNumberLength)) {
        const ShapeNode *shape = m_scene ? m_scene->shapeById(shapeId) : nullptr;
        const QVector<ShapeParameterControl> controls = shape ? shapeParameterControls(*shape) : QVector<ShapeParameterControl>();
        const QString label = parameter >= 0 && parameter < controls.size() ? controls[parameter].label : QStringLiteral("parameter");
        setKey(QStringLiteral("shape:%1:%2:%3").arg(nodeId).arg(parameter).arg(controlDown));
        return controlDown
            ? QStringLiteral("Shape %1\nMouse wheel: change value\nDrag card: reorder or move into a group").arg(label)
            : QStringLiteral("Shape %1\nHold Ctrl + mouse wheel: change value\nClick card: select object").arg(label);
    }

    int renameNodeId = 0;
    QRectF renameRect;
    if (hoverRenameZoneAt(scenePosition, &renameNodeId, &renameRect)) {
        const SceneDocument::TreeNode *node = m_scene ? m_scene->treeNodeById(renameNodeId) : nullptr;
        const bool isModule = node && node->type == SceneDocument::TreeNode::Group
                              && node->operation == SceneDocument::TreeNode::Module;
        setKey(QStringLiteral("rename:%1").arg(renameNodeId));
        return isModule
            ? QStringLiteral("Module name\nDouble-click: rename module\nModule calls update through the tree")
            : QStringLiteral("Variable name\nDouble-click: rename variable\nCtrl + wheel works on numeric values");
    }

    const QRectF scrollRect = hoverScrollZoneRect(scenePosition);
    if (scrollRect.isValid()) {
        setKey(QStringLiteral("scrollzone:%1:%2").arg(qRound(scrollRect.x())).arg(qRound(scrollRect.y())));
        return QStringLiteral("Editable number\nHold Ctrl + mouse wheel: change value\nThe highlighted field shows the active target");
    }

    for (const CanvasMoveHandle &handle : m_canvasMoveHandles) {
        if (handle.gripRect.contains(scenePosition)) {
            setKey(QStringLiteral("grip:%1").arg(handle.nodeId));
            return QStringLiteral("Canvas block handle\nDrag: move block on the tree canvas\nSlow drag moves touching blocks; fast drag detaches");
        }
    }

    for (const GroupHitArea &area : m_treeLayout.groupHitAreas()) {
        for (const ChildLayout &child : area.children) {
            if (child.rect.contains(scenePosition)) {
                setKey(QStringLiteral("node:%1").arg(child.nodeId));
                return QStringLiteral("Tree block\nClick: select\nDrag: move between groups; Delete removes selected");
            }
        }
    }

    for (const GroupHitArea &area : m_treeLayout.groupHitAreas()) {
        if (area.rect.contains(scenePosition)) {
            setKey(QStringLiteral("group:%1").arg(area.groupId));
            return QStringLiteral("%1 group\nDrop blocks into gaps to reorder or nest\nRight-click/left-click: select; Delete: remove selected")
                .arg(labelForOperation(area.operation));
        }
    }

    setKey(QStringLiteral("canvas"));
    return QStringLiteral("Scene tree canvas\nWheel: zoom tree view\nDrag empty space: pan; drag toolbar icons to create blocks");
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

void SceneTreeGraphicsWidget::updateActiveColorChannelControl(const QPointF &scenePosition, bool enabled)
{
    int groupId = 0;
    int channel = -1;
    if (!enabled || !colorChannelControlAt(scenePosition, &groupId, &channel)) {
        groupId = 0;
        channel = -1;
    }

    if (m_activeColorNodeId == groupId && m_activeColorChannel == channel)
        return;

    m_activeColorNodeId = groupId;
    m_activeColorChannel = channel;
    if (!m_dragActive)
        refresh();
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

    int collapseGroupId = 0;
    const bool onCollapseControl = groupCollapseControlAt(scenePosition, &collapseGroupId);
    bool onCanvasGrip = false;
    for (const CanvasMoveHandle &handle : m_canvasMoveHandles) {
        if (handle.gripRect.contains(scenePosition)) {
            onCanvasGrip = true;
            break;
        }
    }

    // Update cursor.
    if (newRenameRect.isValid())
        setCursor(Qt::IBeamCursor);
    else if (newScrollRect.isValid())
        setCursor(Qt::SizeVerCursor);
    else if (onCollapseControl)
        setCursor(Qt::PointingHandCursor);
    else if (onCanvasGrip)
        setCursor(Qt::SizeAllCursor);
    else
        setCursor(Qt::OpenHandCursor);

    if (newScrollRect == m_hoveredScrollRect && newRenameRect == m_hoveredRenameRect)
        return;

    m_hoveredScrollRect = newScrollRect;
    m_hoveredRenameRect = newRenameRect;
    updateHoverHighlightOverlay();
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
        int groupId = 0, channel = -1;
        if (colorChannelControlAt(scenePosition, &groupId, &channel) && channel >= 0) {
            const SceneDocument::TreeNode *node = m_scene ? m_scene->treeNodeById(groupId) : nullptr;
            if (node) {
                const QColor color = node->color.isValid() ? node->color : QColor(79, 163, 255);
                const int channelValues[3] = {color.red(), color.green(), color.blue()};
                const QString expr = QString::number(channelValues[qBound(0, channel, 2)]);
                const QFontMetricsF metrics(sceneTreeGraphicsFont());
                const auto controls = transformParameterNumberControls(
                    groupRectForNode(groupId), channel, expr, metrics, TransformHeaderWidth);
                if (!controls.isEmpty())
                    return controls.first().rect;
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
