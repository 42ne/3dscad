#include "scenetreedroppreviewcontroller.h"
#include "scenetreegraphicswidget.h"
#include "scenetreegraphicshelpers.h"
#include "scenetreepreviewrenderer.h"
#include "nodethumbnailcache.h"
#include "groupthumbnailcache.h"

#include <QTimer>
#include <QtGlobal>
#include <cmath>

using namespace SceneTreeGraphics;

// ── Local animation helpers ───────────────────────────────────────────────────
// These mirror the identical helpers in scenetreegraphicswidget.cpp's anonymous
// namespace. They will be consolidated when audit #1/#10 (constants/helpers
// extraction) is implemented.
namespace {

constexpr qreal LiveDropPreviewDurationMs    = 210.0;
constexpr qreal ReleaseDropPreviewDurationMs = 95.0;
constexpr qreal DropPreviewRectTolerance     = 0.5;

qreal easeOutCubic(qreal v)
{
    const qreal t = qBound<qreal>(0.0, v, 1.0);
    const qreal inv = 1.0 - t;
    return 1.0 - inv * inv * inv;
}

qreal lerp(qreal from, qreal to, qreal p) { return from + (to - from) * p; }

QRectF interpolatedRect(const QRectF &from, const QRectF &to, qreal p)
{
    if (!from.isValid()) return to;
    if (!to.isValid())   return from;
    return QRectF(lerp(from.left(), to.left(), p), lerp(from.top(), to.top(), p),
                  lerp(from.width(), to.width(), p), lerp(from.height(), to.height(), p));
}

bool rectNear(const QRectF &a, const QRectF &b)
{
    if (a.isValid() != b.isValid()) return false;
    if (!a.isValid()) return true;
    return qAbs(a.left() - b.left())     <= DropPreviewRectTolerance
        && qAbs(a.top() - b.top())       <= DropPreviewRectTolerance
        && qAbs(a.width() - b.width())   <= DropPreviewRectTolerance
        && qAbs(a.height() - b.height()) <= DropPreviewRectTolerance;
}

bool childListNear(const QVector<SceneTreeLayout::ChildLayout> &a,
                   const QVector<SceneTreeLayout::ChildLayout> &b)
{
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i)
        if (a[i].nodeId != b[i].nodeId || !rectNear(a[i].rect, b[i].rect)) return false;
    return true;
}

bool groupPreviewListNear(const QVector<SceneTreeLayout::GroupPreview> &a,
                          const QVector<SceneTreeLayout::GroupPreview> &b)
{
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i)
        if (a[i].operation != b[i].operation || !rectNear(a[i].rect, b[i].rect)
            || !childListNear(a[i].children, b[i].children)) return false;
    return true;
}

bool dropTargetNear(const SceneTreeLayout::DropTarget &l, const SceneTreeLayout::DropTarget &r)
{
    return l.hasTarget == r.hasTarget
        && l.parentGroupId == r.parentGroupId
        && l.insertIndex == r.insertIndex
        && l.moduleParameterZone == r.moduleParameterZone
        && l.previewGroupOperation == r.previewGroupOperation
        && rectNear(l.zoneRect, r.zoneRect)
        && rectNear(l.sourceRect, r.sourceRect)
        && rectNear(l.sourceGroupRect, r.sourceGroupRect)
        && rectNear(l.placeholderRect, r.placeholderRect)
        && rectNear(l.slotMarkerRect, r.slotMarkerRect)
        && rectNear(l.previewGroupRect, r.previewGroupRect)
        && childListNear(l.sourceChildren, r.sourceChildren)
        && childListNear(l.previewChildren, r.previewChildren)
        && groupPreviewListNear(l.expandedGroups, r.expandedGroups);
}

QRectF previewContentBounds(const QVector<SceneTreeLayout::ChildLayout> &ch,
                             const QRectF &placeholderRect)
{
    QRectF bounds = placeholderRect;
    bool hasBounds = bounds.isValid();
    for (const SceneTreeLayout::ChildLayout &c : ch) {
        if (!c.rect.isValid()) continue;
        bounds = hasBounds ? bounds.united(c.rect) : c.rect;
        hasBounds = true;
    }
    return hasBounds ? bounds : QRectF();
}

QRectF groupRectForPreviewContent(QRectF groupRect,
                                   SceneDocument::TreeNode::Operation op,
                                   const QVector<SceneTreeLayout::ChildLayout> &children,
                                   const QRectF &placeholder)
{
    if (!groupRect.isValid()) return groupRect;
    const QRectF content = previewContentBounds(children, placeholder);
    const qreal headerHeight = isVerticalHeaderOperation(op) ? 0.0 : GroupHeaderHeight;
    const qreal minBottom = groupRect.top() + headerHeight + GroupPadding * 2.0 + PrimitiveHeight;
    const qreal contentBottom = content.isValid() ? content.bottom() + GroupPadding : minBottom;
    groupRect.setBottom(qMax(minBottom, contentBottom));
    if (content.isValid())
        groupRect.setRight(qMax(groupRect.right(), content.right() + GroupPadding));
    return groupRect;
}

SceneTreeLayout::ChildLayout interpolatedChild(const SceneTreeLayout::ChildLayout &from,
                                               const SceneTreeLayout::ChildLayout &to,
                                               qreal p)
{
    SceneTreeLayout::ChildLayout ch = to;
    ch.rect = interpolatedRect(from.rect, to.rect, p);
    return ch;
}

QVector<SceneTreeLayout::ChildLayout> interpolatedChildren(
    const QVector<SceneTreeLayout::ChildLayout> &from,
    const QVector<SceneTreeLayout::ChildLayout> &to, qreal p)
{
    if (from.size() != to.size()) return to;
    QVector<SceneTreeLayout::ChildLayout> out;
    out.reserve(to.size());
    for (int i = 0; i < to.size(); ++i) out.append(interpolatedChild(from[i], to[i], p));
    return out;
}

SceneTreeLayout::DropTarget collapsedDropTarget(SceneTreeLayout::DropTarget t)
{
    if (!t.hasTarget || !t.placeholderRect.isValid()) return t;
    const qreal slotY = t.slotMarkerRect.isValid()
        ? t.slotMarkerRect.center().y() : t.placeholderRect.center().y();
    const qreal shift = t.placeholderRect.height() + ChildGap;
    t.placeholderRect = QRectF(t.placeholderRect.left(), slotY, t.placeholderRect.width(), 1.0);
    t.slotMarkerRect  = t.placeholderRect;
    for (int i = qMax(0, t.insertIndex); i < t.previewChildren.size(); ++i)
        t.previewChildren[i].rect.translate(0.0, -shift);
    for (SceneTreeLayout::GroupPreview &g : t.expandedGroups) {
        for (SceneTreeLayout::ChildLayout &ch : g.children)
            if (ch.rect.top() >= slotY) ch.rect.translate(0.0, -shift);
        g.rect = groupRectForPreviewContent(g.rect, g.operation, g.children, t.placeholderRect);
    }
    t.previewGroupRect = groupRectForPreviewContent(t.previewGroupRect, t.previewGroupOperation,
                                                     t.previewChildren, t.placeholderRect);
    return t;
}

SceneTreeLayout::GroupPreview interpolatedGroupPreview(
    const SceneTreeLayout::GroupPreview &from,
    const SceneTreeLayout::GroupPreview &to, qreal p)
{
    SceneTreeLayout::GroupPreview g = to;
    g.rect     = interpolatedRect(from.rect, to.rect, p);
    g.children = interpolatedChildren(from.children, to.children, p);
    return g;
}

SceneTreeLayout::DropTarget interpolatedDropTarget(const SceneTreeLayout::DropTarget &from,
                                                   const SceneTreeLayout::DropTarget &to,
                                                   qreal rawProgress)
{
    const qreal p = easeOutCubic(rawProgress);
    SceneTreeLayout::DropTarget t = to;
    t.zoneRect          = interpolatedRect(from.zoneRect,          to.zoneRect,          p);
    t.sourceRect        = interpolatedRect(from.sourceRect,        to.sourceRect,        p);
    t.sourceGroupRect   = interpolatedRect(from.sourceGroupRect,   to.sourceGroupRect,   p);
    t.placeholderRect   = interpolatedRect(from.placeholderRect,   to.placeholderRect,   p);
    t.slotMarkerRect    = interpolatedRect(from.slotMarkerRect,    to.slotMarkerRect,    p);
    t.previewGroupRect  = interpolatedRect(from.previewGroupRect,  to.previewGroupRect,  p);
    t.sourceCutSeparatorY  = lerp(from.sourceCutSeparatorY,  to.sourceCutSeparatorY,  p);
    t.previewCutSeparatorY = lerp(from.previewCutSeparatorY, to.previewCutSeparatorY, p);
    t.sourceChildren  = interpolatedChildren(from.sourceChildren,  to.sourceChildren,  p);
    t.previewChildren = interpolatedChildren(from.previewChildren, to.previewChildren, p);
    if (from.expandedGroups.size() == to.expandedGroups.size()) {
        t.expandedGroups.clear();
        for (int i = 0; i < to.expandedGroups.size(); ++i)
            t.expandedGroups.append(interpolatedGroupPreview(from.expandedGroups[i],
                                                              to.expandedGroups[i], p));
    }
    return t;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

SceneTreeDropPreviewController::SceneTreeDropPreviewController(SceneTreeGraphicsWidget *widget)
    : m_widget(widget)
{
    m_animTimer = new QTimer(m_widget);
    m_animTimer->setInterval(16);
    QObject::connect(m_animTimer, &QTimer::timeout, m_widget, [this]() { advance(); });
}

// ── Public API ────────────────────────────────────────────────────────────────

void SceneTreeDropPreviewController::show(const QPointF &scenePosition,
                                          const QSizeF &previewSize,
                                          const QString &previewTool,
                                          int movingNodeId)
{
    m_widget->m_dragActive = true;
    if (m_widget->m_thumbnailCache)
        m_widget->m_thumbnailCache->setSuspended(true);
    if (m_widget->m_groupThumbnailCache)
        m_widget->m_groupThumbnailCache->setSuspended(true);

    const DropTarget target = m_widget->dropTargetForToolAt(
        scenePosition, previewSize, previewTool, movingNodeId, movingNodeId <= 0);
    emit m_widget->dropPreviewChanged(previewTool, movingNodeId, target, scenePosition);
    startAnimation(target, previewTool, movingNodeId, LiveDropPreviewDurationMs);
}

void SceneTreeDropPreviewController::finish()
{
    if (!m_active) { clear(); return; }
    m_finishing = true;
    startAnimation(m_target, m_tool, m_movingNodeId, ReleaseDropPreviewDurationMs);
}

void SceneTreeDropPreviewController::clear()
{
    m_animTimer->stop();
    m_widget->m_dragActive = false;
    m_active = m_finishing = false;
    m_progress = 0.0;
    m_startTarget = m_target = m_currentTarget = DropTarget();
    m_tool.clear();
    m_movingNodeId = 0;
    SceneTreePreviewRenderer(m_widget->m_graphicsScene, &m_items,
                             m_widget->m_scene, &m_widget->m_treeLayout,
                             m_widget->m_treeTheme).clear();
    m_widget->setTreeItemsVisible(true);
    if (m_widget->m_thumbnailCache)
        m_widget->m_thumbnailCache->setSuspended(false);
    if (m_widget->m_groupThumbnailCache)
        m_widget->m_groupThumbnailCache->setSuspended(false);
}

void SceneTreeDropPreviewController::scheduleCommit(std::function<void()> action)
{
    if (!action) return;
    if (!m_finishing) { clear(); action(); return; }
    QTimer::singleShot(0, m_widget, [this, action = std::move(action)]() mutable {
        clear(); action();
    });
}

// ── Private ───────────────────────────────────────────────────────────────────

void SceneTreeDropPreviewController::startAnimation(const DropTarget &target,
                                                     const QString &previewTool,
                                                     int movingNodeId,
                                                     qreal durationMs)
{
    const bool sameKind = m_active && m_tool == previewTool && m_movingNodeId == movingNodeId;
    if (sameKind && dropTargetNear(target, m_target))
        return;
    m_startTarget  = sameKind ? m_currentTarget : collapsedDropTarget(target);
    m_target       = target;
    m_tool         = previewTool;
    m_movingNodeId = movingNodeId;
    m_durationMs   = qMax<qreal>(1.0, durationMs);
    m_progress     = 0.0;
    m_active       = true;
    if (!sameKind) renderFrame(m_startTarget);
    m_animTimer->start();
}

void SceneTreeDropPreviewController::advance()
{
    if (!m_active) return;
    m_progress = qMin<qreal>(1.0, m_progress + 16.0 / m_durationMs);
    renderFrame(interpolatedDropTarget(m_startTarget, m_target, m_progress));
    if (m_progress >= 1.0) m_animTimer->stop();
}

void SceneTreeDropPreviewController::renderFrame(const DropTarget &target)
{
    m_currentTarget = target;
    SceneTreePreviewRenderer(m_widget->m_graphicsScene, &m_items,
                             m_widget->m_scene, &m_widget->m_treeLayout,
                             m_widget->m_treeTheme).clear();
    m_widget->setTreeItemsVisible(true);
    SceneTreePreviewRenderer(m_widget->m_graphicsScene, &m_items,
                             m_widget->m_scene, &m_widget->m_treeLayout,
                             m_widget->m_treeTheme)
        .render(target, m_tool, m_movingNodeId);
}
