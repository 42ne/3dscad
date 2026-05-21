#include "scenetreegraphicswidget.h"
#include "scenetreegraphicshelpers.h"
#include "scenetreeinlinetextinput.h"
#include "scenetreelayout.h"
#include "scenetreepreviewrenderer.h"
#include "scenetreenoderenderer.h"
#include "scenetreetoolbarrenderer.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QGraphicsScene>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolTip>
#include <QTimer>
#include <QWheelEvent>
#include <QStringList>

using namespace SceneTreeGraphics;

namespace {

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

QStringList splitAtTopLevelCommas(const QString &text)
{
    QStringList result;
    int depth = 0;
    int start = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text[i];
        if (ch == QLatin1Char('(') || ch == QLatin1Char('['))
            ++depth;
        else if (ch == QLatin1Char(')') || ch == QLatin1Char(']'))
            --depth;
        else if (ch == QLatin1Char(',') && depth == 0) {
            const QString part = text.mid(start, i - start).trimmed();
            if (!part.isEmpty())
                result.append(part);
            start = i + 1;
        }
    }
    const QString tail = text.mid(start).trimmed();
    if (!tail.isEmpty())
        result.append(tail);
    return result;
}

// Resolves both named and positional call arguments against a module's parameter list.
static QHash<QString, QString> resolveModuleArguments(
    const QString &callArguments,
    const SceneDocument::TreeNode &moduleNode)
{
    QStringList paramOrder;
    for (const SceneDocument::TreeNode &child : moduleNode.children)
        if (child.type == SceneDocument::TreeNode::Variable && child.isParameter)
            paramOrder.append(child.variableName);

    QHash<QString, QString> result;
    int positionalIndex = 0;
    for (const QString &part : splitAtTopLevelCommas(callArguments)) {
        const int equal = part.indexOf(QLatin1Char('='));
        if (equal > 0) {
            const QString name = part.left(equal).trimmed();
            const QString expr  = part.mid(equal + 1).trimmed();
            if (!name.isEmpty() && !expr.isEmpty())
                result[name] = expr;
            // Named args don't consume positional slots in OpenSCAD
        } else {
            const QString expr = part.trimmed();
            if (!expr.isEmpty() && positionalIndex < paramOrder.size())
                result[paramOrder[positionalIndex]] = expr;
            ++positionalIndex;
        }
    }
    return result;
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
}

void SceneTreeGraphicsWidget::setSceneDocument(const SceneDocument *scene)
{
    m_scene = scene;
    refresh();
}

void SceneTreeGraphicsWidget::setToolDroppedCallback(std::function<void(const QString &, int, int)> callback)
{
    m_toolDroppedCallback = callback;
}

void SceneTreeGraphicsWidget::setModuleCallDroppedCallback(std::function<void(int, int, int)> callback)
{
    m_moduleCallDroppedCallback = callback;
}

void SceneTreeGraphicsWidget::setTreeNodeDroppedCallback(std::function<void(int, int, int)> callback)
{
    m_treeNodeDroppedCallback = callback;
}

void SceneTreeGraphicsWidget::setTreeNodeSelectedCallback(std::function<void(int)> callback)
{
    m_treeNodeSelectedCallback = callback;
}

void SceneTreeGraphicsWidget::setTreeNodeDeleteRequestedCallback(std::function<void(int)> callback)
{
    m_treeNodeDeleteRequestedCallback = callback;
}

void SceneTreeGraphicsWidget::setTransformValueAdjustedCallback(std::function<void(int, int, int, int, qreal)> callback)
{
    m_transformValueAdjustedCallback = callback;
}

void SceneTreeGraphicsWidget::setTransformControlHoveredCallback(std::function<void(int, SceneDocument::TreeNode::Operation, int)> callback)
{
    m_transformControlHoveredCallback = callback;
}

void SceneTreeGraphicsWidget::setShapeParameterAdjustedCallback(std::function<void(int, int, int, int, qreal)> callback)
{
    m_shapeParameterAdjustedCallback = callback;
}

void SceneTreeGraphicsWidget::setShapeParameterHoveredCallback(std::function<void(int, int)> callback)
{
    m_shapeParameterHoveredCallback = callback;
}

void SceneTreeGraphicsWidget::setVariableNumberAdjustedCallback(std::function<void(int, int, int, qreal)> callback)
{
    m_variableNumberAdjustedCallback = callback;
}

void SceneTreeGraphicsWidget::setModuleCallArgumentAdjustedCallback(std::function<void(int, int, int, int, qreal)> callback)
{
    m_moduleCallArgumentAdjustedCallback = callback;
}

void SceneTreeGraphicsWidget::setForLoopRangeAdjustedCallback(std::function<void(int, int, int, qreal)> callback)
{
    m_forLoopRangeAdjustedCallback = callback;
}

void SceneTreeGraphicsWidget::setCtrlReleasedCallback(std::function<void()> callback)
{
    m_ctrlReleasedCallback = callback;
}

void SceneTreeGraphicsWidget::setModuleRenameRequestedCallback(std::function<void(int, const QString &)> callback)
{
    m_moduleRenameRequestedCallback = callback;
}

void SceneTreeGraphicsWidget::setVariableRenameRequestedCallback(std::function<void(int, const QString &)> callback)
{
    m_variableRenameRequestedCallback = callback;
}

void SceneTreeGraphicsWidget::setSelectedTreeNodeId(int nodeId)
{
    if (m_selectedTreeNodeId == nodeId)
        return;

    m_selectedTreeNodeId = nodeId;
    refresh();
}

void SceneTreeGraphicsWidget::refresh()
{
    resetGraphicsScene();
    drawTreeOrPlaceholder();
    updateSceneRect();
    updateToolbarOverlay();
}

void SceneTreeGraphicsWidget::resetGraphicsScene()
{
    clearDropPreview();
    m_graphicsScene->clear();
    m_treeLayout.clear();
    m_treeItems.clear();
    m_renameZones.clear();
    m_toolbarItems.clear();
    m_treeItemsVisible = true;
}

void SceneTreeGraphicsWidget::drawTreeOrPlaceholder()
{
    if (!m_scene || m_scene->isEmpty()) {
        addLabel(m_graphicsScene,
                 QStringLiteral("Drop tree components here"),
                 QPointF(TreeX + 8.0, TreeY + 8.0),
                 QColor(105, 105, 105));
        return;
    }

    const QList<QGraphicsItem *> existingItems = m_graphicsScene->items();
    QPointF topLeft(TreeX, TreeY);

    for (const SceneDocument::TreeNode &child : m_scene->treeRoot().children)
        topLeft.ry() += drawNode(child, topLeft, 0).height() + ChildGap * 2.0;

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
        if (m_treeNodeDeleteRequestedCallback)
            m_treeNodeDeleteRequestedCallback(m_selectedTreeNodeId);
        event->accept();
        return;
    }

    QGraphicsView::keyPressEvent(event);
}

void SceneTreeGraphicsWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    m_lastMousePosition = event->pos();

    if (event->button() == Qt::RightButton && itemAt(event->pos()) == nullptr) {
        handleTreeNodeSelected(0);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && itemAt(event->pos()) == nullptr) {
        m_panning = true;
        m_lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
        // Clear hover highlights when a pan begins.
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

    updateHoverHighlights(scenePosition);

    QGraphicsView::mouseMoveEvent(event);
}

void SceneTreeGraphicsWidget::mouseReleaseEvent(QMouseEvent *event)
{
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
        if (m_ctrlReleasedCallback)
            m_ctrlReleasedCallback();
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
            .renderVariable(node, rect);
        addNodeDragHandle(node.id, node.variableName, rect, rect, rect.size());

        // Register rename zone for the variable name text (badge = 38px, name follows).
        const QFontMetricsF metrics(font());
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
        .renderPrimitive(node, rect, label, shape);

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

    addNodeDragHandle(node.id, node.moduleName, rect, rect, rect.size());
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
    qreal moduleBodyLabelY = 0.0;
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

        moduleBodyLabelY = childTopLeft.y() + 4.0;
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
        childrenHeight = qMax(childrenHeight, VariableHeight * 2.0 + ChildGap * 5.0);
    if (node.operation == SceneDocument::TreeNode::Difference)
        childrenHeight = qMax(childrenHeight, DifferenceMinContentHeight);

    // For a for-loop the header text can be much wider than the children.
    // Measure it so the card is never narrower than the rendered range expression.
    qreal forLoopHeaderMinWidth = 0.0;
    if (node.operation == SceneDocument::TreeNode::For) {
        const QFontMetricsF fm(font());
        const QString varName   = forLoopVariableName(node);
        const QString rangeExpr = forLoopRangeExpression(node);
        const QString prefix    = QStringLiteral("for (%1 = ").arg(varName);
        // 52 = badge area, 8 = right padding inside card
        forLoopHeaderMinWidth = 52.0
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
        .renderGroup(node, rect, depth, cutSeparatorY);

    if (node.operation == SceneDocument::TreeNode::Module) {
        auto *paramsLabel = m_graphicsScene->addSimpleText(QStringLiteral("parameters"));
        paramsLabel->setBrush(QColor(84, 95, 116));
        paramsLabel->setPos(rect.left() + GroupPadding, rect.top() + GroupHeaderHeight + 4.0);
        paramsLabel->setZValue(depth * 10.0 + 8.0);

        auto *callLabel = m_graphicsScene->addSimpleText(QStringLiteral("call handle"));
        callLabel->setBrush(QColor(84, 95, 116));
        callLabel->setPos(rect.left() + GroupPadding, moduleCallTemplateLabelY);
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
        // The module name text starts at x = cardLeft + 42 (after the CALL badge).
        {
            const QFontMetricsF metrics(font());
            const qreal nameW = qMax(metrics.horizontalAdvance(node.moduleName), 24.0);
            m_renameZones.append({QRectF(moduleCallTemplateRect.left() + 42.0,
                                         moduleCallTemplateRect.top(),
                                         nameW,
                                         VariableHeight),
                                  node.id, true, node.moduleName});
        }

        auto *bodyLabel = m_graphicsScene->addSimpleText(QStringLiteral("body"));
        bodyLabel->setBrush(QColor(84, 95, 116));
        bodyLabel->setPos(rect.left() + GroupPadding, moduleBodyLabelY);
        bodyLabel->setZValue(depth * 10.0 + 8.0);

        auto *separator = m_graphicsScene->addLine(rect.left() + GroupPadding,
                                                  moduleParameterSeparatorY,
                                                  rect.right() - GroupPadding,
                                                  moduleParameterSeparatorY,
                                                  QPen(QColor(142, 151, 166), 1, Qt::DashLine));
        separator->setZValue(depth * 10.0 + 7.0);
    }

    const QString groupLabel = labelForOperation(node.operation);
    // Scene is the permanent top-level container — it cannot be dragged or moved.
    if (node.operation != SceneDocument::TreeNode::Scene) {
        const QRectF handleRect = transformGroup
                                      ? QRectF(rect.topLeft(), QSizeF(headerWidth, rect.height()))
                                      : QRectF(rect.topLeft(), QSizeF(rect.width(), GroupHeaderHeight));
        addNodeDragHandle(node.id, groupLabel, handleRect, rect, rect.size());
    }

    return rect;
}

void SceneTreeGraphicsWidget::handleToolDrop(const QString &toolName, const QPointF &scenePosition)
{
    if (m_toolDroppedCallback) {
        const DropTarget target = dropTargetForToolAt(scenePosition,
                                                      previewSizeForTool(toolName),
                                                      toolName,
                                                      0,
                                                      false);
        scheduleDropCommit([this, toolName, target]() {
            if (m_toolDroppedCallback)
                m_toolDroppedCallback(toolName,
                                      target.parentGroupId,
                                      target.moduleParameterZone ? -100000 - target.insertIndex : target.insertIndex);
        });
    }
}

void SceneTreeGraphicsWidget::handleModuleCallTemplateDrop(int moduleGroupId, const QPointF &scenePosition)
{
    if (!m_moduleCallDroppedCallback || moduleGroupId <= 0 || !m_scene)
        return;

    const SceneDocument::TreeNode *module = m_scene->treeNodeById(moduleGroupId);
    if (!module || module->type != SceneDocument::TreeNode::Group
        || module->operation != SceneDocument::TreeNode::Module) {
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
        if (m_moduleCallDroppedCallback)
            m_moduleCallDroppedCallback(moduleGroupId,
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
    if (isRootOnlyTreeTool(previewTool))
        return freeFloatingDropTarget(scenePosition, effectivePreviewSize, allowFreeFloatingInsertion);

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

    if (m_treeNodeDroppedCallback) {
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
                if (m_treeNodeDeleteRequestedCallback)
                    m_treeNodeDeleteRequestedCallback(nodeId);
            });
            return;
        }

        scheduleDropCommit([this, nodeId, target]() {
            if (m_treeNodeDroppedCallback)
                m_treeNodeDroppedCallback(nodeId,
                                          target.parentGroupId,
                                          target.moduleParameterZone ? -100000 - target.insertIndex : target.insertIndex);
        });
    }
}

void SceneTreeGraphicsWidget::handleTreeNodeSelected(int nodeId)
{
    setFocus();
    m_selectedTreeNodeId = nodeId;
    refresh();
    if (m_treeNodeSelectedCallback)
        m_treeNodeSelectedCallback(nodeId);
}

bool SceneTreeGraphicsWidget::handleTransformWheel(const QPointF &scenePosition, int wheelSteps)
{
    if (!m_scene || !m_transformValueAdjustedCallback)
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

    m_transformValueAdjustedCallback(groupId, axis, start, length, static_cast<qreal>(wheelSteps));
    updateActiveTransformControl(scenePosition, true);
    return true;
}

bool SceneTreeGraphicsWidget::handleShapeParameterWheel(const QPointF &scenePosition, int wheelSteps)
{
    if (!m_scene || !m_shapeParameterAdjustedCallback)
        return false;

    int shapeId = -1;
    int nodeId = 0;
    int paramIndex = -1;
    int start = -1;
    int length = 0;
    if (!shapeParameterControlAt(scenePosition, &shapeId, &nodeId, &paramIndex, &start, &length))
        return false;

    m_shapeParameterAdjustedCallback(nodeId, paramIndex, start, length, static_cast<qreal>(wheelSteps));
    updateActiveShapeParameterControl(scenePosition, true);
    Q_UNUSED(shapeId);
    return true;
}

bool SceneTreeGraphicsWidget::handleVariableNumberWheel(const QPointF &scenePosition, int wheelSteps)
{
    if (!m_scene || !m_variableNumberAdjustedCallback)
        return false;

    int nodeId = 0;
    int start = -1;
    int length = 0;
    if (!variableNumberControlAt(scenePosition, &nodeId, &start, &length))
        return false;

    m_variableNumberAdjustedCallback(nodeId, start, length, wheelSteps);
    updateActiveVariableNumberControl(scenePosition, true);
    return true;
}

bool SceneTreeGraphicsWidget::handleForLoopRangeWheel(const QPointF &scenePosition, int wheelSteps)
{
    if (!m_scene || !m_forLoopRangeAdjustedCallback)
        return false;

    int nodeId = 0;
    int start = -1;
    int length = 0;
    if (!forLoopRangeControlAt(scenePosition, &nodeId, &start, &length))
        return false;

    m_forLoopRangeAdjustedCallback(nodeId, start, length, wheelSteps);
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
            && area.operation != SceneDocument::TreeNode::Scale) {
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
            const QFontMetricsF hitMetrics(font());
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

    const QFontMetricsF hitMetrics(font());
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

    const QFontMetricsF hitMetrics(font());
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

    const QFontMetricsF hitMetrics(font());
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

    if (m_transformControlHoveredCallback)
        m_transformControlHoveredCallback(groupId, operation, axis);
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

    if (m_shapeParameterHoveredCallback)
        m_shapeParameterHoveredCallback(shapeId, paramIndex);
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

            const QFontMetricsF hitMetrics(font());
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
    if (!m_scene || !m_moduleCallArgumentAdjustedCallback)
        return false;

    int moduleCallNodeId = 0;
    int paramVarNodeId = 0;
    int start = -1;
    int length = 0;
    if (!moduleCallParamControlAt(scenePosition, &moduleCallNodeId, &paramVarNodeId, &start, &length))
        return false;

    m_moduleCallArgumentAdjustedCallback(moduleCallNodeId, paramVarNodeId, start, length, wheelSteps);
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
                const QFontMetricsF metrics(font());
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
                            const QFontMetricsF metrics(font());
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
                        const QFontMetricsF metrics(font());
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
                const QFontMetricsF metrics(font());
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
                            const QFontMetricsF metrics(font());
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
                if (m_moduleRenameRequestedCallback)
                    m_moduleRenameRequestedCallback(nodeId, newName);
            } else {
                if (m_variableRenameRequestedCallback)
                    m_variableRenameRequestedCallback(nodeId, newName);
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

    const QSizeF effectivePreviewSize = previewSize.isValid() ? previewSize : defaultPreviewSize();
    const DropTarget target = dropTargetForToolAt(scenePosition,
                                                  effectivePreviewSize,
                                                  previewTool,
                                                  movingNodeId,
                                                  movingNodeId <= 0);

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
    SceneTreePreviewRenderer(m_graphicsScene, &m_dropPreviewItems, m_scene, &m_treeLayout).clear();
    setTreeItemsVisible(true);
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
    SceneTreePreviewRenderer(m_graphicsScene, &m_dropPreviewItems, m_scene, &m_treeLayout).clear();
    setTreeItemsVisible(true);

    SceneTreePreviewRenderer(m_graphicsScene, &m_dropPreviewItems, m_scene, &m_treeLayout)
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

    QTimer::singleShot(static_cast<int>(ReleaseDropPreviewDurationMs), this, [this, action = std::move(action)]() mutable {
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
