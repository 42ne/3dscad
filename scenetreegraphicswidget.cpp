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

#include <QApplication>
#include <QColorDialog>
#include <QMenu>
#include <QEasingCurve>
#include <QFontMetricsF>
#include <QGraphicsEllipseItem>
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
// ColorEditToggleItem — standalone vertical toggle switch for color-edit mode.
// Thumb slides to top (ON) or bottom (OFF); track darkens when active.
// ---------------------------------------------------------------------------
class ColorEditToggleItem : public QGraphicsItem
{
public:
    static constexpr qreal kW      = 16.0;
    static constexpr qreal kH      = 28.0;
    static constexpr qreal kThumbD = 12.0;
    static constexpr qreal kThumbM = (kW - kThumbD) * 0.5;  // side margin = 2 px

    ColorEditToggleItem(bool on,
                        std::function<void()> onClick,
                        std::function<void(bool)> onHover = {})
        : m_on(on), m_onClick(std::move(onClick)), m_onHover(std::move(onHover))
    {
        setAcceptedMouseButtons(Qt::LeftButton);
        setAcceptHoverEvents(true);
    }

    QRectF boundingRect() const override
    { return QRectF(-2.0, -2.0, kW + 4.0, kH + 4.0); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);

        // Track — dark when ON, muted when OFF
        const QColor track = m_on ? QColor(22, 22, 22, 225) : QColor(110, 115, 125, 135);
        QPainterPath trackPath;
        trackPath.addRoundedRect(QRectF(0, 0, kW, kH), kW * 0.5, kW * 0.5);
        painter->setPen(Qt::NoPen);
        painter->setBrush(track);
        painter->drawPath(trackPath);

        // Thumb: top = ON, bottom = OFF
        const qreal thumbY = m_on ? kThumbM : kH - kThumbM - kThumbD;
        const QRectF thumbRect(kThumbM, thumbY, kThumbD, kThumbD);

        // Thumb drop shadow
        painter->setBrush(QColor(0, 0, 0, 45));
        painter->drawEllipse(thumbRect.translated(0.5, 1.2));

        // Thumb fill
        painter->setPen(QPen(QColor(140, 140, 140, 55), 0.5));
        painter->setBrush(Qt::white);
        painter->drawEllipse(thumbRect);
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) { m_onClick(); e->accept(); }
        else e->ignore();
    }
    void hoverEnterEvent(QGraphicsSceneHoverEvent *) override
    { if (m_onHover) m_onHover(true); }
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *) override
    { if (m_onHover) m_onHover(false); }

private:
    bool m_on;
    std::function<void()> m_onClick;
    std::function<void(bool)> m_onHover;
};

// ---------------------------------------------------------------------------
// ThemeSwitcherSwatchItem — a single clickable swatch circle in the theme
// switcher overlay at the bottom of the viewport.
// ---------------------------------------------------------------------------
class ThemeSwitcherSwatchItem : public QGraphicsEllipseItem
{
public:
    // hoverPen: pen applied while the cursor is over this item (optional).
    ThemeSwitcherSwatchItem(const QPointF &center,
                             qreal radius,
                             const QPen &pen,
                             const QBrush &brush,
                             int themeIndex,
                             std::function<void(int)> onClick,
                             const QPen &hoverPen = QPen(Qt::NoPen))
        : QGraphicsEllipseItem(center.x() - radius, center.y() - radius,
                                radius * 2.0, radius * 2.0)
        , m_themeIndex(themeIndex)
        , m_onClick(std::move(onClick))
        , m_normalPen(pen)
        , m_hoverPen(hoverPen)
    {
        setPen(pen);
        setBrush(brush);
        setAcceptedMouseButtons(Qt::LeftButton);
        if (m_hoverPen.style() != Qt::NoPen)
            setAcceptHoverEvents(true);
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

    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override
    {
        setPen(m_hoverPen);
        update();
        event->accept();
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override
    {
        setPen(m_normalPen);
        update();
        event->accept();
    }

private:
    int m_themeIndex = 0;
    std::function<void(int)> m_onClick;
    QPen m_normalPen;
    QPen m_hoverPen;
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

void collectVariableValues(const SceneDocument::TreeNode &node, QHash<QString, qreal> *values, int excludedNodeId = 0)
{
    if (!values)
        return;
    if (node.type == SceneDocument::TreeNode::Variable) {
        if (node.id != excludedNodeId)
            (*values)[node.variableName] = node.variableValue;
        return;
    }
    for (const SceneDocument::TreeNode &child : node.children)
        collectVariableValues(child, values, excludedNodeId);
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
    if (isVerticalHeaderOperation(node.operation)
        || node.operation == SceneDocument::TreeNode::For
        || node.operation == SceneDocument::TreeNode::LinearExtrude) {
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

    m_colorEdit = new SceneTreeColorEditMode(this);
    connect(m_colorEdit, &SceneTreeColorEditMode::inlineThemeEdited,
            this, &SceneTreeGraphicsWidget::inlineThemeEdited);

    m_hoverManager      = new SceneTreeHoverManager(this);
    m_inlineEditor      = new SceneTreeInlineEditor(this);
    m_canvasDragHandler = new SceneCanvasDragHandler(this);

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

        // ── Apply zoom with stable anchor (clamped to safe range) ──────────
        // kMinZoom guards against scene-scale ~0 which causes GDI font-engine
        // failures (GetGlyphOutline error 1003) when toolbar text items are
        // rebuilt at an extreme inverse transform.
        constexpr qreal kMinZoom = 0.05;
        constexpr qreal kMaxZoom = 8.0;
        const qreal currentScale = transform().m11();
        const qreal clampedStep  = qBound(kMinZoom / currentScale, step,
                                          kMaxZoom / currentScale);
        if (qAbs(clampedStep - step) > 1e-9) {
            m_zoomVelocity = 0.0;
            m_zoomAccel    = 0.0;
        }
        QPointF anchorVp = mapFromScene(m_zoomAnchorScene);
        scale(clampedStep, clampedStep);
        QPointF anchorVpAfter = mapFromScene(m_zoomAnchorScene);
        QPointF vpDelta = anchorVpAfter - anchorVp;
        horizontalScrollBar()->setValue(
            horizontalScrollBar()->value() + qRound(vpDelta.x()));
        verticalScrollBar()->setValue(
            verticalScrollBar()->value() + qRound(vpDelta.y()));

        const qreal newLevel = transform().m11();

        // ── Stop when both are negligible ───────────────────────────────────
        const bool zoomJustStopped = qAbs(m_zoomVelocity) < 0.002 && qAbs(m_zoomAccel) < 0.0001;
        if (zoomJustStopped) {
            m_zoomAnimTimer->stop();
            m_zoomIdleTimer->stop();
            m_zoomVelocity = 0.0;
            m_zoomAccel    = 0.0;
            for (QGraphicsItem *item : m_treeItems)
                item->setCacheMode(QGraphicsItem::NoCache);
            setRenderHint(QPainter::Antialiasing, true);
            setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
            viewport()->update();
        }

        // ── Reposition UI when zoom actually changed noticeably ──────────
        // During animation: cheap reposition only — avoids addText() calls
        // (drawHintOverlay) at extreme scene scales that trigger GDI failures.
        // After stop: full rebuild at a stable, clamped scale.
        if (qAbs(newLevel - m_zoomLevel) > 0.001) {
            m_zoomLevel = newLevel;
            m_inlineEditor->updateInlineInputGeometry();
            if (zoomJustStopped)
                updateToolbarOverlay();
            else
                repositionToolbarItems();
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
    // Remove color-edit overlays before clear() deletes them under us.
    clearColorEditHighlight();
    m_graphicsScene->clear();
    m_canvasDragHandler->clearAfterSceneClear();
    m_treeLayout.clear();
    m_treeItems.clear();
    m_renameZones.clear();
    m_toolbarItems.clear();
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
    const CanvasBackgroundTheme background = activeCanvasBackgroundTheme(m_canvasBackgroundTheme);
    painter->fillRect(rect, background.background);
    const qreal scale = qMax(0.001, std::abs(transform().m11()));
    // Skip minor grid when zoomed out so far that lines would be < 4px apart on screen.
    if (24.0 * scale >= 4.0)
        drawCanvasGrid(painter, rect, 24.0, background.minorGrid, 1);
    drawCanvasGrid(painter, rect, 120.0, background.majorGrid, 1);
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

int SceneTreeGraphicsWidget::toolbarSnapSideForVpPos(QPoint vpPos) const
{
    const qreal vw = viewport() ? viewport()->width()  : 640.0;
    const qreal vh = viewport() ? viewport()->height() : 480.0;
    if (vpPos.y() < vh * 0.25)
        return 0;  // top (horizontal)
    return (vpPos.x() < vw * 0.5) ? 1 : 2;  // left or right (vertical)
}

bool SceneTreeGraphicsWidget::isOnToolbarBackground(QPointF vpPos) const
{
    if (!m_toolbarPanelVpRect.contains(vpPos))
        return false;
    for (const QRectF &r : m_toolbarToolVpRects)
        if (r.contains(vpPos))
            return false;
    return true;
}

void SceneTreeGraphicsWidget::repositionToolbarItemsSync()
{
    if (m_toolbarItems.isEmpty() || m_toolbarItems.size() != m_toolbarVpOffsets.size())
        return;
    const QPointF vtl = mapToScene(QPoint(0, 0));
    const qreal s = qMax(0.001, std::abs(transform().m11()));
    for (int i = 0; i < m_toolbarItems.size(); ++i)
        m_toolbarItems[i]->setPos(vtl + QPointF(m_toolbarVpOffsets[i].x() / s,
                                                 m_toolbarVpOffsets[i].y() / s));
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
    if (colorEditMode() && event->button() == Qt::LeftButton) {
        const QPointF scenePos = mapToScene(event->pos());
        // Only toolbar items (theme swatches, the "✏ Colors" toggle button) are
        // allowed to handle their own clicks normally.  Everything else — tree
        // selection items, drag handles, node overlays — is suppressed so that
        // the color-edit click always fires, even when the user clicks on top of
        // an interactive card overlay.
        const QGraphicsItem *hitItem = itemAt(event->pos());
        const bool isToolbarItem = hitItem
            && m_toolbarItems.contains(const_cast<QGraphicsItem *>(hitItem));
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
                emit treeNodeDeleteRequested(cell.nodeId);
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
        m_toolbarDragPending = true;
        m_toolbarDragActive  = false;
        m_toolbarDragPressVp = event->pos();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && itemAt(event->pos()) == nullptr) {
        m_panning = true;
        m_lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
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
    if (m_toolbarDragPending || m_toolbarDragActive) {
        if (m_toolbarDragPending) {
            const int dist = (event->pos() - m_toolbarDragPressVp).manhattanLength();
            if (dist >= 8) {
                m_toolbarDragPending = false;
                m_toolbarDragActive  = true;
                setCursor(Qt::ClosedHandCursor);
            }
        }
        if (m_toolbarDragActive) {
            const int targetSide = toolbarSnapSideForVpPos(event->pos());
            if (targetSide != m_toolbarSide) {
                m_toolbarSide = targetSide;
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
    if (m_colorEditToggleItem) {
        const QPointF togVp = mapFromScene(m_colorEditToggleItem->pos());
        if (m_colorEditToggleItem->boundingRect().translated(togVp)
                .contains(QPointF(event->pos()))) {
            clearColorEditHighlight();                  // remove any stale blink overlay
            QGraphicsView::mouseMoveEvent(event);       // deliver hover enter/move to toggle
            event->accept();
            return;
        }
    }

    // ── Color-edit mode hover ─────────────────────────────────────────────────
    if (colorEditMode()) {
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

    if (m_panning) {
        const QPoint delta = event->pos() - m_lastPanPoint;
        m_lastPanPoint = event->pos();
        m_panVelocity = QPointF(delta.x(), delta.y());
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    m_hoverManager->updateHighlights(scenePosition);
    QGraphicsView::mouseMoveEvent(event);
}

void SceneTreeGraphicsWidget::mouseReleaseEvent(QMouseEvent *event)
{
    // ── Toolbar drag release ──────────────────────────────────────────────────
    if (event->button() == Qt::LeftButton && (m_toolbarDragActive || m_toolbarDragPending)) {
        if (m_toolbarDragActive) {
            const int newSide = toolbarSnapSideForVpPos(event->pos());
            if (newSide != m_toolbarSide) {
                m_toolbarSide = newSide;
                updateToolbarOverlay();
            }
        }
        m_toolbarDragActive  = false;
        m_toolbarDragPending = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }

    // ── Canvas-move drag release ──────────────────────────────────────────────
    if (m_canvasDragHandler->handleMouseRelease(event))
        return;

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
    if (!m_panning)
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
    if (m_toolbarItems.size() == m_toolbarVpOffsets.size()) {
        repositionToolbarItemsSync();
        if (viewport())
            viewport()->update();
    } else if (!m_toolbarRepositionPending) {
        m_toolbarRepositionPending = true;
        QTimer::singleShot(0, this, [this]() {
            m_toolbarRepositionPending = false;
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
        if (wheelSteps != 0 && handlePolygon2DTableWheel(scenePosition, wheelSteps)) {
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
        if (wheelSteps != 0 && handlePolyhedronTableWheel(scenePosition, wheelSteps)) {
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

        if (!m_zoomAnimTimer->isActive()) {
            for (QGraphicsItem *item : m_treeItems)
                item->setCacheMode(QGraphicsItem::ItemCoordinateCache);
            setRenderHint(QPainter::Antialiasing, false);
            setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
            m_zoomAnimTimer->start();
        }
        event->accept();
    }
}

QRectF SceneTreeGraphicsWidget::drawToolbar()
{
    const QPointF viewportTopLeft = mapToScene(QPoint(0, 0));
    const qreal viewportWidth  = viewport() ? viewport()->width()  : 640.0;
    const qreal viewportHeight = viewport() ? viewport()->height() : 480.0;
    const qreal viewportScale  = transform().m11();

    m_toolbarToolVpRects.clear();
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
                    m_hoverManager->updateHoverHint(QStringLiteral("toolbar:%1").arg(toolName),
                                    toolbarToolTip(toolName));
                } else if (m_hoverManager->hoverHintKey() == QStringLiteral("toolbar:%1").arg(toolName)) {
                    m_hoverManager->updateTooltip(mapToGlobal(m_lastMousePosition),
                                         mapToScene(m_lastMousePosition),
                                         QApplication::keyboardModifiers() & Qt::ControlModifier);
                }
            },
            viewportTopLeft,
            viewportWidth,
            viewportHeight,
            viewportScale,
            m_toolbarSide,
            &m_toolbarPanelVpRect,
            &m_toolbarToolVpRects);
}

void SceneTreeGraphicsWidget::clearToolbar()
{
    m_colorEditToggleItem = nullptr;
    const QVector<QGraphicsItem *> toDelete = m_toolbarItems;
    for (QGraphicsItem *item : toDelete) {
        if (!item)
            continue;
        m_graphicsScene->removeItem(item);
    }
    m_toolbarItems.clear();
    m_toolbarVpOffsets.clear();
    // Defer actual deletion: removeItem() already prevents new events from reaching
    // these items, but QGraphicsScene's hover-tracking may still hold the raw pointer
    // until the current event-loop iteration finishes.  Deleting here causes a
    // use-after-free when scrollContentsBy -> dispatchHoverEvent fires synchronously.
    QTimer::singleShot(0, this, [toDelete]() { qDeleteAll(toDelete); });
}

void SceneTreeGraphicsWidget::updateToolbarOverlay()
{
    if (!m_graphicsScene)
        return;

    clearToolbar();
    drawToolbar();
    drawCanvasBackgroundSwitcher();
    drawThemeSwitcher();
    m_hoverManager->drawHintOverlay();

    // Compute VP-pixel offsets from each item's current scene position so that
    // repositionToolbarItems() can cheaply update positions without rebuilding.
    const QPointF vtl = mapToScene(QPoint(0, 0));
    const qreal s = qMax(0.001, std::abs(transform().m11()));
    m_toolbarVpOffsets.resize(m_toolbarItems.size());
    for (int i = 0; i < m_toolbarItems.size(); ++i) {
        const QPointF delta = m_toolbarItems[i]->pos() - vtl;
        m_toolbarVpOffsets[i] = QPointF(delta.x() * s, delta.y() * s);
    }
}

void SceneTreeGraphicsWidget::repositionToolbarItems()
{
    if (m_toolbarItems.isEmpty())
        return;
    if (m_toolbarItems.size() != m_toolbarVpOffsets.size()) {
        updateToolbarOverlay();
        return;
    }
    const QPointF vtl = mapToScene(QPoint(0, 0));
    const qreal s = qMax(0.001, std::abs(transform().m11()));
    for (int i = 0; i < m_toolbarItems.size(); ++i)
        m_toolbarItems[i]->setPos(vtl + QPointF(m_toolbarVpOffsets[i].x() / s,
                                                 m_toolbarVpOffsets[i].y() / s));
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
    shadow->setData(0, QStringLiteral("shadow"));
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
    panel->setData(0, QStringLiteral("glass_toolbar"));
    m_toolbarItems.append(panel);

    // Active ring: high-contrast vs. the glass panel to make the selection clear.
    const QColor ringColor = darkGlass ? QColor(255, 255, 255, 210) : QColor(40, 50, 65, 200);

    for (int i = 0; i < CanvasBackgroundThemeCount; ++i) {
        const bool active = i == m_canvasBackgroundTheme;
        const QColor swatch = canvasBackgroundTheme(i).background;
        const QPointF center(PadH + i * (SwatchR * 2.0 + SwatchGap) + SwatchR,
                             PadV + SwatchR);
        if (active) {
            auto *ring = new ThemeSwitcherSwatchItem(
                center, SwatchR + 2.8,
                QPen(ringColor, 1.6), Qt::NoBrush,
                i, [this](int idx) { handleCanvasBackgroundSwitcherClick(idx); });
            ring->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
            ring->setPos(panelTopLeft);
            ring->setZValue(LocalOverlayZ);
            ring->setData(0, QStringLiteral("swatch"));
            m_graphicsScene->addItem(ring);
            m_toolbarItems.append(ring);
        }
        // Border and hover adapt to each swatch's own lightness: dark swatch → lighter ring,
        // light swatch → darker ring. Stays in the same tonal family, never jarring.
        const bool swatchDark  = swatch.lightness() < 128;
        const QColor borderCol = swatchDark ? swatch.lighter(145) : swatch.darker(118);
        const QColor hoverCol  = swatchDark ? swatch.lighter(170) : swatch.darker(135);
        const QPen normalPen = active ? QPen(Qt::NoPen) : QPen(borderCol, 1.0);
        const QPen hoverPen  = active ? QPen(Qt::NoPen) : QPen(hoverCol,  2.0);
        auto *circle = new ThemeSwitcherSwatchItem(
            center, SwatchR,
            normalPen, QBrush(swatch),
            i, [this](int idx) { handleCanvasBackgroundSwitcherClick(idx); },
            hoverPen);
        circle->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        circle->setPos(panelTopLeft);
        circle->setZValue(LocalOverlayZ + 0.5);
        circle->setData(0, QStringLiteral("swatch"));
        m_graphicsScene->addItem(circle);
        m_toolbarItems.append(circle);
    }
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

void SceneTreeGraphicsWidget::clearColorEditHighlight()
{
    if (m_colorEdit) m_colorEdit->clearHighlight();
}

void SceneTreeGraphicsWidget::updateColorEditHighlight(const QPointF &scenePos)
{
    if (m_colorEdit) m_colorEdit->updateHighlight(scenePos);
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
    const qreal panelW = n * (SwatchR * 2.0) + (n - 1) * SwatchGap + PadH * 2.0; // = 123
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
    shadow->setData(0, QStringLiteral("shadow"));
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
    panel->setData(0, QStringLiteral("glass_toolbar"));
    m_toolbarItems.append(panel);

    // Active ring: high-contrast vs. glass panel for clear selection indication.
    const QColor ringColor = darkGlass ? QColor(255, 255, 255, 210) : QColor(40, 50, 65, 200);

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
                QPen(ringColor, 1.6),
                Qt::NoBrush,
                i, [this](int idx) { handleThemeSwitcherClick(idx); });
            ring->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
            ring->setPos(panelTopLeft);
            ring->setZValue(LocalOverlayZ);
            ring->setData(0, QStringLiteral("swatch"));
            m_graphicsScene->addItem(ring);
            m_toolbarItems.append(ring);
        }

        // Border and hover adapt to each swatch's own lightness.
        const bool swatchDark  = swatch.lightness() < 128;
        const QColor borderCol = swatchDark ? swatch.lighter(145) : swatch.darker(118);
        const QColor hoverCol  = swatchDark ? swatch.lighter(170) : swatch.darker(135);
        const QPen normalPen = active ? QPen(Qt::NoPen) : QPen(borderCol, 1.0);
        const QPen hoverPen  = active ? QPen(Qt::NoPen) : QPen(hoverCol,  2.0);
        auto *circle = new ThemeSwitcherSwatchItem(
            QPointF(cx, cy), SwatchR,
            normalPen,
            QBrush(swatch),
            i, [this](int idx) { handleThemeSwitcherClick(idx); },
            hoverPen);
        circle->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        circle->setPos(panelTopLeft);
        circle->setZValue(LocalOverlayZ + 0.5);
        circle->setData(0, QStringLiteral("swatch"));
        m_graphicsScene->addItem(circle);
        m_toolbarItems.append(circle);
    }

    // ── Standalone vertical toggle for color-edit mode ───────────────────────
    {
        constexpr qreal ToggleGap = 7.0;   // gap from swatch panel right edge
        constexpr qreal TW = ColorEditToggleItem::kW;
        constexpr qreal TH = ColorEditToggleItem::kH;

        // Centre the toggle vertically with the swatch panel.
        const qreal toggleVpX = 12.0 + panelW + ToggleGap;
        const qreal toggleVpY = viewportHeight - BottomGap - panelH * 0.5 - TH * 0.5;
        const QPointF toggleTopLeft = scenePoint(toggleVpX, toggleVpY);

        // Drop shadow (rounded pill, offset slightly).
        QPainterPath shadowPath;
        shadowPath.addRoundedRect(QRectF(1.5, 2.5, TW, TH), TW * 0.5, TW * 0.5);
        auto *toggleShadow = m_graphicsScene->addPath(shadowPath,
            Qt::NoPen, QBrush(QColor(0, 0, 0, darkGlass ? 85 : 28)));
        toggleShadow->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        toggleShadow->setAcceptedMouseButtons(Qt::NoButton);
        toggleShadow->setPos(toggleTopLeft);
        toggleShadow->setZValue(LocalOverlayZ - 2.0);
        toggleShadow->setOpacity(darkGlass ? 0.62 : 0.38);
        toggleShadow->setData(0, QStringLiteral("shadow"));
        m_toolbarItems.append(toggleShadow);

        // The toggle itself.
        auto *tog = new ColorEditToggleItem(
            colorEditMode(),
            [this] { setColorEditMode(!colorEditMode()); },
            [this](bool enter) {
                if (enter) {
                    const QString stateStr = colorEditMode()
                        ? QStringLiteral("ON  — click to exit")
                        : QStringLiteral("OFF — click to enter");
                    m_hoverManager->updateHoverHint(QStringLiteral("colorEdit:toggle"),
                                    QStringLiteral("✏ Color edit  ") + stateStr
                                    + QStringLiteral("\nHover blocks to inspect and edit theme colors."));
                } else {
                    if (colorEditMode())
                        m_hoverManager->updateHoverHint(QStringLiteral("colorEdit:mode"),
                                        QStringLiteral("✏ Color edit\n"
                                                       "Hover a card, then scroll ↕ to cycle properties.\n"
                                                       "Click to pick a color.  Esc to exit."));
                    else
                        m_hoverManager->updateHoverHint(QString(), QString());
                }
            });
        tog->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        tog->setPos(toggleTopLeft);
        tog->setZValue(LocalOverlayZ + 0.5);
        tog->setData(0, QStringLiteral("toggle"));
        m_graphicsScene->addItem(tog);
        m_toolbarItems.append(tog);
        m_colorEditToggleItem = tog;
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
                              m_hoverManager->m_activeVariableNodeId,
                              m_hoverManager->m_activeVariableNumberStart,
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
                          m_hoverManager->m_activeShapeParameterNodeId,
                          m_hoverManager->m_activeShapeParameter,
                          m_hoverManager->m_activeShapeParameterNumberStart,
                          0,
                          -1,
                          0,
                          -1)
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

    // Parameter headers can be wider than the children. Measure them so the
    // card is never narrower than the rendered expression.
    qreal parameterHeaderMinWidth = 0.0;
    if (node.operation == SceneDocument::TreeNode::For) {
        parameterHeaderMinWidth = SceneTreeGraphics::forLoopHeaderMinWidth(
            forLoopVariableName(node),
            forLoopRangeExpression(node),
            QFontMetricsF(sceneTreeGraphicsFont()));
    } else if (node.operation == SceneDocument::TreeNode::LinearExtrude) {
        parameterHeaderMinWidth = SceneTreeGraphics::linearExtrudeHeaderMinWidth(
            linearExtrudeHeightExpression(node),
            QFontMetricsF(sceneTreeGraphicsFont()));
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
        const bool magnetic = m_canvasDragHandler->applyMagneticSnap(candidateTL, effectivePreviewSize, 0, &snappedTL);
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
    m_hoverManager->updateActiveTransformControl(scenePosition, true);
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
    m_hoverManager->updateActiveColorChannelControl(scenePosition, true);
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
    m_hoverManager->updateActiveShapeParameterControl(scenePosition, true);
    Q_UNUSED(shapeId);
    return true;
}

bool SceneTreeGraphicsWidget::handlePolyhedronTableWheel(const QPointF &scenePosition, int wheelSteps)
{
    PolyhedronTableItem::Cell cell;
    if (!polyhedronTableControlAt(scenePosition, &cell))
        return false;

    // ── FaceParticipate (custom participation editing) ──
    if (cell.type == PolyhedronTableItem::Cell::FaceParticipate) {
        if (!m_scene) return false;
        const SceneDocument::TreeNode *faceNode = m_scene->treeNodeById(cell.nodeId);
        if (!faceNode || faceNode->type != SceneDocument::TreeNode::Primitive) return false;
        const ShapeNode *shape = m_scene->shapeById(faceNode->shapeId);
        if (!shape || shape->type != ShapeNode::Face3D) return false;

        const QVector<int> face = shape->polyhedronFaces.isEmpty()
                                      ? QVector<int>()
                                      : shape->polyhedronFaces.first();
        const int oldPos = face.indexOf(cell.sub);

        int newPos = oldPos;
        if (wheelSteps > 0) {
            // Cycle forward through states: - -> 0 -> 1 -> ... -> -
            if (oldPos < 0) {
                newPos = 0;
            } else if (oldPos >= face.size() - 1) {
                newPos = -1;
            } else {
                newPos = oldPos + 1;
            }
        } else {
            // Cycle backward through states: - -> last -> ... -> 0 -> -
            if (oldPos < 0) {
                newPos = face.size();
            } else if (oldPos == 0) {
                newPos = -1;
            } else {
                newPos = oldPos - 1;
            }
        }

        // Find the point nodeId via the table item
        int ptNodeId = 0;
        for (QGraphicsItem *item : m_treeItems) {
            auto *tableItem = dynamic_cast<PolyhedronTableItem *>(item);
            if (!tableItem) continue;
            ptNodeId = tableItem->pointNodeIdForIndex(cell.sub);
            break;
        }
        if (ptNodeId <= 0) return false;

        emit polyhedronFaceParticipationAdjusted(cell.nodeId, ptNodeId, newPos);
        return true;
    }

    int paramIndex = -1;
    switch (cell.type) {
    case PolyhedronTableItem::Cell::PtX: paramIndex = 0; break;
    case PolyhedronTableItem::Cell::PtY: paramIndex = 1; break;
    case PolyhedronTableItem::Cell::PtZ: paramIndex = 2; break;
    default: return false;
    }

    // Resolve the shape to get the parameter expression,
    // so we can emit a valid numberStart/numberLength for the handler.
    const SceneDocument::TreeNode *node = m_scene ? m_scene->treeNodeById(cell.nodeId) : nullptr;
    if (!node || node->type != SceneDocument::TreeNode::Primitive) return false;
    const ShapeNode *shape = m_scene->shapeById(node->shapeId);
    if (!shape) return false;

    const QVector<ShapeParameterControl> controls = shapeParameterControls(*shape);
    if (paramIndex >= controls.size()) return false;

    const QString &expr = controls[paramIndex].expression;
    // Point3D coords and Face3D indices are always simple numeric expressions,
    // so the entire expression string is the number to adjust.
    const int numberStart = 0;
    const int numberLength = expr.size();

    emit shapeParameterAdjusted(cell.nodeId, paramIndex, numberStart, numberLength, static_cast<qreal>(wheelSteps));
    m_hoverManager->updateActiveShapeParameterControl(scenePosition, true);
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
    m_hoverManager->updateActiveVariableNumberControl(scenePosition, true);
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
    m_hoverManager->updateActiveForLoopRangeControl(scenePosition, true);
    return true;
}

bool SceneTreeGraphicsWidget::handlePolygon2DTableWheel(const QPointF &scenePosition, int wheelSteps)
{
    Polygon2DTableItem::Cell cell;
    if (!polygon2DTableControlAt(scenePosition, &cell))
        return false;

    int coord = -1;
    if (cell.type == Polygon2DTableItem::Cell::PtX)
        coord = 0;
    else if (cell.type == Polygon2DTableItem::Cell::PtY)
        coord = 1;
    if (coord < 0 || cell.index < 0)
        return false;

    emit polygon2DPointAdjusted(cell.nodeId, cell.index, coord, wheelSteps);
    m_hoverManager->updateActiveShapeParameterControl(scenePosition, true);
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

bool SceneTreeGraphicsWidget::polyhedronTableControlAt(const QPointF &scenePosition,
                                                       PolyhedronTableItem::Cell *cell) const
{
    if (!cell) return false;
    // Iterate all tree items looking for a PolyhedronTableItem under the cursor
    for (QGraphicsItem *item : m_treeItems) {
        auto *tableItem = dynamic_cast<PolyhedronTableItem *>(item);
        if (!tableItem) continue;
        if (!tableItem->boundingRect().contains(tableItem->mapFromScene(scenePosition)))
            continue;
        *cell = tableItem->cellAt(tableItem->mapFromScene(scenePosition));
        return cell->type != PolyhedronTableItem::Cell::None;
    }
    return false;
}

int SceneTreeGraphicsWidget::polyhedronGroupIdForCell(const QPointF &scenePosition) const
{
    for (QGraphicsItem *item : m_treeItems) {
        auto *tableItem = dynamic_cast<PolyhedronTableItem *>(item);
        if (!tableItem) continue;
        if (!tableItem->boundingRect().contains(tableItem->mapFromScene(scenePosition)))
            continue;
        return tableItem->groupNodeId();
    }
    return 0;
}

bool SceneTreeGraphicsWidget::polygon2DTableControlAt(const QPointF &scenePosition,
                                                      Polygon2DTableItem::Cell *cell) const
{
    if (!cell) return false;
    for (QGraphicsItem *item : m_treeItems) {
        auto *tableItem = dynamic_cast<Polygon2DTableItem *>(item);
        if (!tableItem) continue;
        const QPointF local = tableItem->mapFromScene(scenePosition);
        if (!tableItem->boundingRect().contains(local))
            continue;
        *cell = tableItem->cellAt(local);
        return cell->type != Polygon2DTableItem::Cell::None;
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
    m_hoverManager->updateActiveModuleCallParamControl(scenePosition, true);
    return true;
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
