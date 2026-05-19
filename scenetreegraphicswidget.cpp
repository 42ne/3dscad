#include "scenetreegraphicswidget.h"
#include "scenetreegraphicshelpers.h"
#include "scenetreelayout.h"
#include "scenetreepreviewrenderer.h"
#include "scenetreenoderenderer.h"
#include "scenetreetoolbarrenderer.h"

#include <QGraphicsScene>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolTip>
#include <QWheelEvent>

using namespace SceneTreeGraphics;

namespace {

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

} // namespace


SceneTreeGraphicsWidget::SceneTreeGraphicsWidget(QWidget *parent)
    : QGraphicsView(parent)
    , m_graphicsScene(createTreeGraphicsScene(this))
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

void SceneTreeGraphicsWidget::setForLoopRangeAdjustedCallback(std::function<void(int, int, int, qreal)> callback)
{
    m_forLoopRangeAdjustedCallback = callback;
}

void SceneTreeGraphicsWidget::setCtrlReleasedCallback(std::function<void()> callback)
{
    m_ctrlReleasedCallback = callback;
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
    const QRectF toolbarRect = drawToolbar();
    drawTreeOrPlaceholder();
    updateSceneRect(toolbarRect);
}

void SceneTreeGraphicsWidget::resetGraphicsScene()
{
    clearDropPreview();
    m_graphicsScene->clear();
    m_treeLayout.clear();
    m_treeItems.clear();
}

void SceneTreeGraphicsWidget::drawTreeOrPlaceholder()
{
    if (!m_scene || m_scene->treeRoot().children.isEmpty()) {
        addLabel(m_graphicsScene,
                 QStringLiteral("Drop tree components here"),
                 QPointF(TreeX + 8.0, TreeY + 8.0),
                 QColor(105, 105, 105));
        return;
    }

    const QList<QGraphicsItem *> toolbarItems = m_graphicsScene->items();
    drawNode(m_scene->treeRoot(), QPointF(TreeX, TreeY), 0);

    const QList<QGraphicsItem *> allItems = m_graphicsScene->items();
    for (QGraphicsItem *item : allItems) {
        if (!toolbarItems.contains(item))
            m_treeItems.append(item);
    }
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

    if (event->button() == Qt::LeftButton && itemAt(event->pos()) == nullptr) {
        m_panning = true;
        m_lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
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

    if (m_panning) {
        const QPoint delta = event->pos() - m_lastPanPoint;
        m_lastPanPoint = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

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

void SceneTreeGraphicsWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Control) {
        updateControlTooltip(mapToGlobal(m_lastMousePosition), mapToScene(m_lastMousePosition), false);
        updateActiveTransformControl(QPointF(), false);
        updateActiveShapeParameterControl(QPointF(), false);
        updateActiveVariableNumberControl(QPointF(), false);
        updateActiveForLoopRangeControl(QPointF(), false);
        if (m_ctrlReleasedCallback)
            m_ctrlReleasedCallback();
    }

    QGraphicsView::keyReleaseEvent(event);
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
    event->accept();
}

QRectF SceneTreeGraphicsWidget::drawToolbar()
{
    return SceneTreeToolbarRenderer(m_graphicsScene)
        .render(
            [this](const QPointF &position, const QSizeF &previewSize, const QString &previewTool) {
                showDropPreview(position, previewSize, previewTool);
            },
            [this]() { clearDropPreview(); },
            [this](const QString &toolName, const QPointF &position) {
                handleToolDrop(toolName, position);
            });
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
        [this]() { clearDropPreview(); },
        [this](int droppedNodeId, const QPointF &position) { handleTreeNodeDrop(droppedNodeId, position); });

    m_graphicsScene->addItem(handle);
}

QRectF SceneTreeGraphicsWidget::drawNode(const SceneDocument::TreeNode &node, const QPointF &topLeft, int depth)
{
    if (node.type == SceneDocument::TreeNode::Primitive)
        return drawPrimitive(node, topLeft);
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

QRectF SceneTreeGraphicsWidget::drawGroup(const SceneDocument::TreeNode &node, const QPointF &topLeft, int depth)
{
    QVector<ChildLayout> children;
    const bool transformGroup = isTransformOperation(node.operation);
    const qreal headerWidth = transformGroup ? transformHeaderWidthForNode(node) : 0.0;
    const qreal headerHeight = transformGroup ? 0.0 : GroupHeaderHeight;
    QPointF childTopLeft(topLeft.x() + headerWidth + GroupPadding, topLeft.y() + headerHeight + GroupPadding);
    qreal maxChildWidth = 0.0;

    for (const SceneDocument::TreeNode &child : node.children) {
        const QRectF childRect = drawNode(child, childTopLeft, depth + 1);
        children.append({childRect, previewToolForNode(child), child.id});
        maxChildWidth = qMax(maxChildWidth, childRect.width());
        childTopLeft.ry() += childRect.height() + ChildGap;
    }

    qreal childrenHeight = children.isEmpty()
                               ? PrimitiveHeight
                               : childTopLeft.y() - topLeft.y() - headerHeight - GroupPadding - ChildGap;
    if (node.operation == SceneDocument::TreeNode::Difference)
        childrenHeight = qMax(childrenHeight, DifferenceMinContentHeight);
    const QSizeF size(qMax(minimumWidthForOperation(node.operation), headerWidth + maxChildWidth + GroupPadding * 2.0),
                      headerHeight + GroupPadding * 2.0 + childrenHeight);
    const QRectF rect(topLeft, size);
    qreal cutSeparatorY = 0.0;
    if (node.operation == SceneDocument::TreeNode::Difference) {
        cutSeparatorY = rect.top() + GroupHeaderHeight + GroupPadding + PrimitiveHeight + ChildGap * 0.5;
        if (!children.isEmpty())
            cutSeparatorY = children.first().rect.bottom() + ChildGap * 0.5;
    }
    m_treeLayout.addGroup({rect, node.id, depth, node.operation, cutSeparatorY, children});

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

    const QString groupLabel = labelForOperation(node.operation);
    const QRectF handleRect = transformGroup
                                  ? QRectF(rect.topLeft(), QSizeF(headerWidth, rect.height()))
                                  : QRectF(rect.topLeft(), QSizeF(rect.width(), GroupHeaderHeight));
    addNodeDragHandle(node.id, groupLabel, handleRect, rect, rect.size());

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
        m_toolDroppedCallback(toolName, target.parentGroupId, target.insertIndex);
    }
}

QString SceneTreeGraphicsWidget::previewToolForNode(const SceneDocument::TreeNode &node) const
{
    if (node.type == SceneDocument::TreeNode::Variable)
        return QStringLiteral("var");
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

    DropTarget target = m_treeLayout.dropTargetAt(scenePosition, effectivePreviewSize, movingNodeId);
    if (!target.zoneRect.isValid())
        target = freeFloatingDropTarget(scenePosition, effectivePreviewSize, allowFreeFloatingInsertion);

    if (isVariableToolName(previewTool) && m_scene) {
        const int rootId = m_scene->treeRoot().id;
        if (target.parentGroupId > 0 && target.parentGroupId != rootId)
            return DropTarget();
    }

    return target;
}

void SceneTreeGraphicsWidget::handleTreeNodeDrop(int nodeId, const QPointF &scenePosition)
{
    if (m_treeNodeDroppedCallback) {
        QSizeF previewSize = defaultPreviewSize();
        QString previewTool;
        if (m_scene) {
            const SceneDocument::TreeNode *node = m_scene->treeNodeById(nodeId);
            if (node) {
                previewTool = previewToolForNode(*node);
                previewSize = node->type == SceneDocument::TreeNode::Variable
                                  ? variablePreviewSize(node->variableName, node->variableExpression)
                                  : previewSizeForTool(previewTool);
            }
        }

        const DropTarget target = dropTargetForToolAt(scenePosition,
                                                      previewSize,
                                                      previewTool,
                                                      nodeId,
                                                      false);
        m_treeNodeDroppedCallback(nodeId, target.parentGroupId, target.insertIndex);
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
    } else if (transformControlAt(scenePosition, &groupId, &operation, &axis)) {
        static const char *AxisNames[] = {"X", "Y", "Z"};
        const QString axisName = QString::fromLatin1(AxisNames[axis]);
        key = QStringLiteral("transform:%1:%2").arg(groupId).arg(axis);
        message = controlDown
                      ? QStringLiteral("Use mouse wheel to change %1 %2").arg(labelForOperation(operation), axisName)
                      : QStringLiteral("Hold Ctrl and use mouse wheel to change %1 %2").arg(labelForOperation(operation), axisName);
    } else {
        int variableNodeId = 0;
        int numberStart = -1;
        int numberLength = 0;
        if (variableNumberControlAt(scenePosition, &variableNodeId, &numberStart, &numberLength)) {
            key = QStringLiteral("variable:%1:%2").arg(variableNodeId).arg(numberStart);
            message = controlDown
                          ? QStringLiteral("Use mouse wheel to change this number")
                          : QStringLiteral("Hold Ctrl and use mouse wheel to change this number");
        } else {
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
    if (!enabled || !transformControlAt(scenePosition, &groupId, &operation, &axis, &numberStart, &numberLength)) {
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

void SceneTreeGraphicsWidget::showDropPreview(const QPointF &scenePosition, const QSizeF &previewSize, const QString &previewTool, int movingNodeId)
{
    if (movingNodeId > 0)
        m_dragActive = true;
    clearDropPreview();

    const QSizeF effectivePreviewSize = previewSize.isValid() ? previewSize : defaultPreviewSize();
    const DropTarget target = dropTargetForToolAt(scenePosition,
                                                  effectivePreviewSize,
                                                  previewTool,
                                                  movingNodeId,
                                                  movingNodeId <= 0);

    const bool hasTreePreview = target.sourceGroupRect.isValid()
                                || target.previewGroupRect.isValid()
                                || !target.expandedGroups.isEmpty();
    setTreeItemsVisible(!hasTreePreview);

    SceneTreePreviewRenderer(m_graphicsScene, &m_dropPreviewItems, m_scene, &m_treeLayout)
        .render(target, previewTool, movingNodeId);
}

void SceneTreeGraphicsWidget::clearDropPreview()
{
    m_dragActive = false;
    SceneTreePreviewRenderer(m_graphicsScene, &m_dropPreviewItems, m_scene, &m_treeLayout).clear();
    setTreeItemsVisible(true);
}

void SceneTreeGraphicsWidget::setTreeItemsVisible(bool visible)
{
    for (QGraphicsItem *item : m_treeItems) {
        if (item)
            item->setOpacity(visible ? 1.0 : 0.0);
    }
}

void SceneTreeGraphicsWidget::updateSceneRect(const QRectF &toolbarRect)
{
    QRectF bounds = m_graphicsScene->itemsBoundingRect()
                        .united(toolbarRect)
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
