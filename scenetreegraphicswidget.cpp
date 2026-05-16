#include "scenetreegraphicswidget.h"
#include "scenetreegraphicshelpers.h"

#include <QBrush>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QScrollBar>
#include <QWheelEvent>

using namespace SceneTreeGraphics;

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

void SceneTreeGraphicsWidget::setSelectedTreeNodeId(int nodeId)
{
    if (m_selectedTreeNodeId == nodeId)
        return;

    m_selectedTreeNodeId = nodeId;
    refresh();
}

void SceneTreeGraphicsWidget::refresh()
{
    clearDropPreview();
    m_graphicsScene->clear();
    m_groupHitAreas.clear();
    m_treeItems.clear();

    const QRectF toolbarRect = drawToolbar();

    if (m_scene && !m_scene->treeRoot().children.isEmpty()) {
        const QList<QGraphicsItem *> toolbarItems = m_graphicsScene->items();
        drawNode(m_scene->treeRoot(), QPointF(TreeX, TreeY), 0);
        const QList<QGraphicsItem *> allItems = m_graphicsScene->items();
        for (QGraphicsItem *item : allItems) {
            if (!toolbarItems.contains(item))
                m_treeItems.append(item);
        }
    } else {
        addLabel(m_graphicsScene, "Drop tree components here", QPointF(TreeX + 8.0, TreeY + 8.0), QColor(105, 105, 105));
    }

    QRectF bounds = m_graphicsScene->itemsBoundingRect().united(toolbarRect).adjusted(-CanvasMargin,
                                                                                       -CanvasMargin,
                                                                                       CanvasMargin,
                                                                                       CanvasMargin);
    if (bounds.width() < 420.0)
        bounds.setWidth(420.0);
    if (bounds.height() < 260.0)
        bounds.setHeight(260.0);
    m_graphicsScene->setSceneRect(bounds);
}

void SceneTreeGraphicsWidget::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->fillRect(rect, CanvasBackground);
    drawCanvasGrid(painter, rect, 24.0, MinorGridColor, 1);
    drawCanvasGrid(painter, rect, 96.0, MajorGridColor, 1);
}

void SceneTreeGraphicsWidget::keyPressEvent(QKeyEvent *event)
{
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

void SceneTreeGraphicsWidget::wheelEvent(QWheelEvent *event)
{
    const qreal factor = event->angleDelta().y() > 0 ? 1.12 : 1.0 / 1.12;
    scale(factor, factor);
    event->accept();
}

QRectF SceneTreeGraphicsWidget::drawToolbar()
{
    const QStringList tools = {
        "cube",
        "sphere",
        "cylinder",
        "union",
        "difference",
        "intersection",
        "module"
    };

    const QRectF toolbarRect(ToolbarX - 6.0,
                             ToolbarY - 6.0,
                             tools.size() * ToolSize + (tools.size() - 1) * ToolGap + 12.0,
                             ToolSize + 12.0);
    addSoftShadow(m_graphicsScene, toolbarRect, -4.0);
    addRoundedPanel(m_graphicsScene, toolbarRect, CornerRadius, QPen(QColor(166, 174, 186)), QBrush(QColor(232, 235, 239)), -3.0);

    for (int i = 0; i < tools.size(); ++i) {
        auto *tool = createPaletteToolItem(
            tools[i],
            fillForTool(tools[i]),
            [this](const QPointF &position, const QSizeF &previewSize, const QString &previewTool) {
                showDropPreview(position, previewSize, previewTool);
            },
            [this]() {
                clearDropPreview();
            },
            [this](const QString &toolName, const QPointF &position) {
                handleToolDrop(toolName, position);
            });
        tool->setPos(ToolbarX + i * (ToolSize + ToolGap), ToolbarY);
        m_graphicsScene->addItem(tool);
    }

    return toolbarRect;
}

QRectF SceneTreeGraphicsWidget::drawNode(const SceneDocument::TreeNode &node, const QPointF &topLeft, int depth)
{
    if (node.type == SceneDocument::TreeNode::Primitive)
        return drawPrimitive(node, topLeft);

    return drawGroup(node, topLeft, depth);
}

QRectF SceneTreeGraphicsWidget::drawPrimitive(const SceneDocument::TreeNode &node, const QPointF &topLeft)
{
    const QRectF rect(topLeft, QSizeF(PrimitiveWidth, PrimitiveHeight));
    const QString label = labelForPrimitive(node.shapeId);
    const bool selected = node.id == m_selectedTreeNodeId;
    const QRectF iconRect(rect.left() + 20.0,
                          rect.top() + (PrimitiveHeight - PrimitiveIconSize) * 0.5,
                          PrimitiveIconSize,
                          PrimitiveIconSize);
    if (selected)
        addPrimitiveSelectionHalo(m_graphicsScene, iconRect);
    addPrimitiveIcon(m_graphicsScene, typeForPrimitive(node.shapeId), iconRect);
    addPrimitiveNumberBadge(m_graphicsScene, primitiveNumberText(label, node.shapeId), rect);
    auto *handle = createTreeNodeDragHandleItem(
        node.id,
        label,
        rect,
        rect,
        [this](int nodeId) {
            handleTreeNodeSelected(nodeId);
        },
        rect.size(),
        [this, nodeId = node.id](const QPointF &position, const QSizeF &previewSize, const QString &previewTool) {
            showDropPreview(position, previewSize, previewTool, nodeId);
        },
        [this]() {
            clearDropPreview();
        },
        [this](int nodeId, const QPointF &position) {
            handleTreeNodeDrop(nodeId, position);
        });
    handle->setToolTip(label);
    m_graphicsScene->addItem(handle);
    return rect;
}

QRectF SceneTreeGraphicsWidget::drawGroup(const SceneDocument::TreeNode &node, const QPointF &topLeft, int depth)
{
    QVector<QRectF> childRects;
    QVector<QString> childPreviewTools;
    QVector<int> childNodeIds;
    QPointF childTopLeft(topLeft.x() + GroupPadding, topLeft.y() + GroupHeaderHeight + GroupPadding);
    qreal maxChildWidth = 0.0;

    for (const SceneDocument::TreeNode &child : node.children) {
        const QRectF childRect = drawNode(child, childTopLeft, depth + 1);
        childRects.append(childRect);
        childPreviewTools.append(previewToolForNode(child));
        childNodeIds.append(child.id);
        maxChildWidth = qMax(maxChildWidth, childRect.width());
        childTopLeft.ry() += childRect.height() + ChildGap;
    }

    qreal childrenHeight = childRects.isEmpty()
                               ? PrimitiveHeight
                               : childTopLeft.y() - topLeft.y() - GroupHeaderHeight - GroupPadding - ChildGap;
    if (node.operation == SceneDocument::TreeNode::Difference)
        childrenHeight = qMax(childrenHeight, DifferenceMinContentHeight);
    const QSizeF size(qMax(minimumWidthForOperation(node.operation), maxChildWidth + GroupPadding * 2.0),
                      GroupHeaderHeight + GroupPadding * 2.0 + childrenHeight);
    const QRectF rect(topLeft, size);
    const QColor fill = colorForGroup(node.operation);
    const bool selected = node.id == m_selectedTreeNodeId;
    qreal cutSeparatorY = 0.0;
    if (node.operation == SceneDocument::TreeNode::Difference) {
        cutSeparatorY = rect.top() + GroupHeaderHeight + GroupPadding + PrimitiveHeight + ChildGap * 0.5;
        if (!childRects.isEmpty())
            cutSeparatorY = childRects.first().bottom() + ChildGap * 0.5;
    }
    m_groupHitAreas.append({rect, node.id, depth, node.operation, cutSeparatorY, childRects, childPreviewTools, childNodeIds});

    addSoftShadow(m_graphicsScene, rect, depth * 10.0 - 101.0);
    auto *groupItem = addRoundedPanel(m_graphicsScene,
                                      rect,
                                      CornerRadius,
                                      QPen(selected ? QColor(255, 203, 87) : fill.darker(145), selected ? 3 : 2),
                                      QBrush(fill),
                                      depth * 10.0 - 100.0);
    groupItem->setZValue(depth * 10.0 - 100.0);

    const QRectF headerRect(rect.left() + 1.5, rect.top() + 1.5, rect.width() - 3.0, GroupHeaderHeight - 2.0);
    auto *header = addRoundedPanel(m_graphicsScene,
                                   headerRect,
                                   CornerRadius - 1.0,
                                   Qt::NoPen,
                                   QBrush(fill.lighter(112)),
                                   depth * 10.0 - 95.0);
    header->setZValue(depth * 10.0 - 95.0);
    m_graphicsScene->addItem(createTreeNodeSelectionItem(
        node.id,
        rect,
        depth * 10.0 - 80.0,
        [this](int nodeId) {
            handleTreeNodeSelected(nodeId);
        }));

    const QRectF iconRect(rect.left() + 8.0, rect.top() + 6.0, 18.0, 18.0);
    addOperationIcon(m_graphicsScene, node.operation, iconRect, fill.darker(125));
    const QString groupLabel = labelForGroup(node.operation);
    addLabel(m_graphicsScene, groupLabel, rect.topLeft() + QPointF(32.0, 7.0), QColor(24, 34, 44));
    m_graphicsScene->addItem(createTreeNodeDragHandleItem(
        node.id,
        groupLabel,
        QRectF(rect.topLeft(), QSizeF(rect.width(), GroupHeaderHeight)),
        rect,
        [this](int nodeId) {
            handleTreeNodeSelected(nodeId);
        },
        rect.size(),
        [this, nodeId = node.id](const QPointF &position, const QSizeF &previewSize, const QString &previewTool) {
            showDropPreview(position, previewSize, previewTool, nodeId);
        },
        [this]() {
            clearDropPreview();
        },
        [this](int nodeId, const QPointF &position) {
            handleTreeNodeDrop(nodeId, position);
        }));

    if (node.children.isEmpty())
        addLabel(m_graphicsScene, "empty", QPointF(rect.left() + GroupPadding, rect.top() + GroupHeaderHeight + GroupPadding + 10.0), QColor(95, 98, 105));

    if (node.operation == SceneDocument::TreeNode::Difference) {
        auto *separator = m_graphicsScene->addLine(rect.left() + GroupPadding,
                                                   cutSeparatorY,
                                                   rect.right() - GroupPadding,
                                                   cutSeparatorY,
                                                   QPen(QColor(130, 92, 70), 1, Qt::DashLine));
        separator->setZValue(depth * 10.0 - 70.0);

        addPillLabel(m_graphicsScene, "base", QPointF(rect.right() - 61.0, rect.top() + GroupHeaderHeight + 7.0), QColor(128, 99, 73));
        addPillLabel(m_graphicsScene, "cut", QPointF(rect.right() - 51.0, cutSeparatorY + 5.0), QColor(153, 85, 56));
    }

    return rect;
}

void SceneTreeGraphicsWidget::handleToolDrop(const QString &toolName, const QPointF &scenePosition)
{
    if (m_toolDroppedCallback) {
        const DropTarget target = dropTargetAt(scenePosition);
        m_toolDroppedCallback(toolName, target.parentGroupId, target.insertIndex);
    }
}

QString SceneTreeGraphicsWidget::previewToolForNode(const SceneDocument::TreeNode &node) const
{
    if (node.type != SceneDocument::TreeNode::Primitive)
        return labelForOperation(node.operation);

    const ShapeNode *shape = m_scene ? m_scene->shapeById(node.shapeId) : nullptr;
    const ShapeNode::Type type = shape ? shape->type : ShapeNode::Cube;
    if (type == ShapeNode::Sphere)
        return "sphere";
    if (type == ShapeNode::Cylinder)
        return "cylinder";
    return "cube";
}

void SceneTreeGraphicsWidget::handleTreeNodeDrop(int nodeId, const QPointF &scenePosition)
{
    if (m_treeNodeDroppedCallback) {
        const DropTarget target = dropTargetAt(scenePosition);
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

void SceneTreeGraphicsWidget::showDropPreview(const QPointF &scenePosition, const QSizeF &previewSize, const QString &previewTool, int movingNodeId)
{
    clearDropPreview();

    const QSizeF effectivePreviewSize = previewSize.isValid() ? previewSize : defaultPreviewSize();
    DropTarget target = dropTargetAt(scenePosition, effectivePreviewSize, movingNodeId);
    if (!target.zoneRect.isValid()) {
        target.zoneRect = QRectF(scenePosition - QPointF(effectivePreviewSize.width() * 0.5, effectivePreviewSize.height() * 0.5),
                                 effectivePreviewSize);
        if (movingNodeId <= 0) {
            target.placeholderRect = target.zoneRect;
            target.hasTarget = true;
        }
    }

    const bool hasTreePreview = target.sourceGroupRect.isValid()
                                || target.previewGroupRect.isValid()
                                || !target.expandedGroupRects.isEmpty();
    setTreeItemsVisible(!hasTreePreview);

    addExpandedGroupPreviews(target);
    addSourceGroupPreview(target, movingNodeId);
    addTargetGroupPreview(target, previewTool);
}

void SceneTreeGraphicsWidget::addExpandedGroupPreviews(const DropTarget &target)
{
    for (int i = 0; i < target.expandedGroupRects.size(); ++i) {
        const QRectF expandedRect = target.expandedGroupRects[i];
        if ((target.previewGroupRect.isValid() && expandedRect == target.previewGroupRect)
            || (target.sourceGroupRect.isValid() && expandedRect == target.sourceGroupRect)) {
            continue;
        }

        const SceneDocument::TreeNode::Operation operation = i < target.expandedGroupOperations.size()
                                                                 ? target.expandedGroupOperations[i]
                                                                 : SceneDocument::TreeNode::Union;
        addPreviewGroupFrame(m_graphicsScene,
                             &m_dropPreviewItems,
                             expandedRect,
                             operation,
                             0.0,
                             colorForGroup(operation));
        const QVector<QRectF> childRects = i < target.expandedGroupChildRects.size()
                                               ? target.expandedGroupChildRects[i]
                                               : QVector<QRectF>();
        const QVector<QString> childTools = i < target.expandedGroupChildTools.size()
                                                ? target.expandedGroupChildTools[i]
                                                : QVector<QString>();
        const QVector<int> childNodeIds = i < target.expandedGroupChildNodeIds.size()
                                              ? target.expandedGroupChildNodeIds[i]
                                              : QVector<int>();
        addPreviewChildren(childRects, childTools, childNodeIds, target.previewGroupRect);
    }
}

void SceneTreeGraphicsWidget::addSourceGroupPreview(const DropTarget &target, int movingNodeId)
{
    const bool sourceGroupCoveredByTarget = target.sourceGroupRect.isValid()
                                            && target.previewGroupRect.isValid()
                                            && target.previewGroupRect.contains(target.sourceGroupRect.center());
    if (target.sourceGroupRect.isValid() && !sourceGroupCoveredByTarget) {
        addPreviewGroupFrame(m_graphicsScene,
                             &m_dropPreviewItems,
                             target.sourceGroupRect,
                             target.sourceGroupOperation,
                             target.sourceCutSeparatorY,
                             colorForGroup(target.sourceGroupOperation));
        if (movingNodeId > 0 && target.sourceRect.isValid()) {
            addSourceRemovalMask(m_graphicsScene,
                                 &m_dropPreviewItems,
                                 target.sourceRect,
                                 colorForGroup(target.sourceGroupOperation));
        }
        addPreviewChildren(target.sourceChildRects, target.sourceChildTools, {});
    }
}

void SceneTreeGraphicsWidget::addTargetGroupPreview(const DropTarget &target, const QString &previewTool)
{
    if (target.previewGroupRect.isValid()) {
        addPreviewGroupFrame(m_graphicsScene,
                             &m_dropPreviewItems,
                             target.previewGroupRect,
                             target.previewGroupOperation,
                             target.previewCutSeparatorY,
                             colorForGroup(target.previewGroupOperation));
    }

    addPreviewChildren(target.previewChildRects, target.previewChildTools, target.previewChildNodeIds);

    if (target.hasTarget) {
        addPreviewBlock(m_graphicsScene,
                        &m_dropPreviewItems,
                        previewTool,
                        target.placeholderRect,
                        fillForTool(previewTool));
        addDragFocusOutline(m_graphicsScene,
                            &m_dropPreviewItems,
                            previewTool,
                            target.placeholderRect,
                            90.0);
    }
}

void SceneTreeGraphicsWidget::clearDropPreview()
{
    for (QGraphicsItem *item : m_dropPreviewItems)
        delete item;
    m_dropPreviewItems.clear();
    setTreeItemsVisible(true);
}

void SceneTreeGraphicsWidget::setTreeItemsVisible(bool visible)
{
    for (QGraphicsItem *item : m_treeItems) {
        if (item)
            item->setOpacity(visible ? 1.0 : 0.0);
    }
}

void SceneTreeGraphicsWidget::addPreviewExistingNode(int nodeId, const QRectF &rect)
{
    if (!m_scene || nodeId <= 0)
        return;

    const SceneDocument::TreeNode *node = m_scene->treeNodeById(nodeId);
    if (!node)
        return;

    const QString tool = previewToolForNode(*node);
    if (node->type == SceneDocument::TreeNode::Primitive) {
        addPreviewBlock(m_graphicsScene,
                        &m_dropPreviewItems,
                        tool,
                        rect,
                        fillForTool(tool));
        return;
    }

    qreal cutSeparatorY = 0.0;
    const GroupHitArea *area = nullptr;
    for (const GroupHitArea &candidate : m_groupHitAreas) {
        if (candidate.groupId == nodeId) {
            area = &candidate;
            break;
        }
    }

    if (area)
        cutSeparatorY = area->cutSeparatorY + (rect.top() - area->rect.top());

    addPreviewGroupFrame(m_graphicsScene,
                         &m_dropPreviewItems,
                         rect,
                         node->operation,
                         cutSeparatorY,
                         colorForGroup(node->operation));

    if (!area)
        return;

    const QPointF offset = rect.topLeft() - area->rect.topLeft();
    for (int i = 0; i < area->childRects.size(); ++i) {
        const int childNodeId = i < area->childNodeIds.size() ? area->childNodeIds[i] : 0;
        addPreviewExistingNode(childNodeId, area->childRects[i].translated(offset));
    }
}

void SceneTreeGraphicsWidget::addPreviewTreeItem(const QString &tool, int nodeId, const QRectF &rect)
{
    if (nodeId > 0) {
        addPreviewExistingNode(nodeId, rect);
        return;
    }

    addPreviewBlock(m_graphicsScene,
                    &m_dropPreviewItems,
                    tool,
                    rect,
                    fillForTool(tool));
}

void SceneTreeGraphicsWidget::addPreviewChildren(const QVector<QRectF> &rects,
                                                 const QVector<QString> &tools,
                                                 const QVector<int> &nodeIds,
                                                 const QRectF &excludedRect)
{
    for (int i = 0; i < rects.size(); ++i) {
        const QRectF childRect = rects[i];
        if (excludedRect.isValid() && childRect.contains(excludedRect.center()))
            continue;

        const QString childTool = i < tools.size()
                                      ? tools[i]
                                      : QStringLiteral("cube");
        const int childNodeId = i < nodeIds.size() ? nodeIds[i] : 0;
        addPreviewTreeItem(childTool, childNodeId, childRect);
    }
}

SceneTreeGraphicsWidget::DropTarget SceneTreeGraphicsWidget::dropTargetAt(const QPointF &scenePosition, const QSizeF &previewSize, int movingNodeId) const
{
    DropTarget target;
    const QSizeF effectivePreviewSize = previewSize.isValid() ? previewSize : defaultPreviewSize();
    const GroupHitArea *sourceArea = nullptr;
    int sourceChildIndex = -1;
    qreal sourceRemovalShift = 0.0;
    if (movingNodeId > 0) {
        for (const GroupHitArea &area : m_groupHitAreas) {
            for (int i = 0; i < area.childNodeIds.size() && i < area.childRects.size(); ++i) {
                if (area.childNodeIds[i] == movingNodeId) {
                    target.sourceRect = area.childRects[i];
                    sourceArea = &area;
                    sourceChildIndex = i;
                    sourceRemovalShift = target.sourceRect.height() + ChildGap;
                    break;
                }
            }
            if (target.sourceRect.isValid())
                break;
        }
    }

    int bestDepth = -1;
    const GroupHitArea *bestArea = nullptr;

    for (const GroupHitArea &area : m_groupHitAreas) {
        if (area.depth <= bestDepth || !area.rect.contains(scenePosition))
            continue;
        if (movingNodeId > 0 && area.groupId == movingNodeId)
            continue;
        if (movingNodeId > 0 && sourceArea && target.sourceRect.isValid()
            && area.depth > sourceArea->depth
            && target.sourceRect.contains(area.rect.center())) {
            continue;
        }

        bestArea = &area;
        bestDepth = area.depth;
    }

    auto buildSourcePreview = [&target, sourceArea, movingNodeId, sourceChildIndex, sourceRemovalShift]() {
        if (!sourceArea)
            return;

        target.sourceGroupRect = sourceArea->rect;
        target.sourceGroupOperation = sourceArea->operation;
        target.sourceCutSeparatorY = sourceArea->cutSeparatorY;
        target.sourceChildRects.clear();
        target.sourceChildTools.clear();
        QRectF futureContent;
        bool hasFutureContent = false;
        for (int i = 0; i < sourceArea->childRects.size(); ++i) {
            const int childNodeId = i < sourceArea->childNodeIds.size() ? sourceArea->childNodeIds[i] : 0;
            if (childNodeId == movingNodeId)
                continue;

            QRectF childRect = sourceArea->childRects[i];
            if (sourceChildIndex >= 0 && i > sourceChildIndex)
                childRect.translate(0.0, -sourceRemovalShift);

            target.sourceChildRects.append(childRect);
            target.sourceChildTools.append(i < sourceArea->childPreviewTools.size()
                                               ? sourceArea->childPreviewTools[i]
                                               : QStringLiteral("cube"));
            futureContent = hasFutureContent ? futureContent.united(childRect) : childRect;
            hasFutureContent = true;
        }

        qreal minContentHeight = PrimitiveHeight;
        if (sourceArea->operation == SceneDocument::TreeNode::Difference)
            minContentHeight = DifferenceMinContentHeight;

        const qreal minBottom = sourceArea->rect.top() + GroupHeaderHeight + GroupPadding * 2.0 + minContentHeight;
        const qreal contentBottom = hasFutureContent ? futureContent.bottom() + GroupPadding : minBottom;
        target.sourceGroupRect.setBottom(qMax(minBottom, contentBottom));
        if (sourceArea->operation == SceneDocument::TreeNode::Difference && target.sourceCutSeparatorY > 0.0) {
            target.sourceCutSeparatorY = qMin(target.sourceCutSeparatorY, target.sourceGroupRect.bottom() - GroupPadding - PrimitiveHeight * 0.5);
        }
    };

    auto cancelTargetPreview = [&target]() {
        target.hasTarget = false;
        target.parentGroupId = 0;
        target.insertIndex = -1;
        target.zoneRect = QRectF();
        target.placeholderRect = QRectF();
        target.previewGroupRect = QRectF();
        target.previewChildRects.clear();
        target.previewChildTools.clear();
        target.previewChildNodeIds.clear();
        target.expandedGroupRects.clear();
        target.expandedGroupChildRects.clear();
        target.expandedGroupChildTools.clear();
        target.expandedGroupChildNodeIds.clear();
        target.expandedGroupOperations.clear();
    };

    if (sourceArea && target.sourceRect.contains(scenePosition)) {
        buildSourcePreview();
        return target;
    }

    if (sourceArea) {
        buildSourcePreview();
    }

    if (!bestArea)
        return target;

    qreal targetPreviewShift = 0.0;
    if (sourceArea && sourceArea != bestArea && sourceChildIndex >= 0) {
        for (int i = sourceChildIndex + 1; i < sourceArea->childRects.size(); ++i) {
            if (sourceArea->childRects[i].contains(bestArea->rect.center())) {
                targetPreviewShift = -sourceRemovalShift;
                break;
            }
        }
    }

    target.hasTarget = true;
    target.parentGroupId = bestArea->groupId;
    target.previewGroupOperation = bestArea->operation;
    target.previewCutSeparatorY = bestArea->cutSeparatorY;
    const QRectF contentRect = bestArea->rect.adjusted(GroupPadding,
                                                       GroupHeaderHeight + GroupPadding,
                                                       -GroupPadding,
                                                       -GroupPadding);
    QVector<QRectF> candidateChildRects;
    QVector<QString> candidateChildTools;
    QVector<int> candidateChildNodeIds;
    for (int i = 0; i < bestArea->childRects.size(); ++i) {
        const int childNodeId = i < bestArea->childNodeIds.size() ? bestArea->childNodeIds[i] : 0;
        if (movingNodeId > 0 && childNodeId == movingNodeId)
            continue;

        candidateChildRects.append(bestArea->childRects[i]);
        candidateChildTools.append(i < bestArea->childPreviewTools.size()
                                       ? bestArea->childPreviewTools[i]
                                       : QStringLiteral("cube"));
        candidateChildNodeIds.append(childNodeId);
    }

    auto setPreviewChildren = [&target, &candidateChildRects, &candidateChildTools, &candidateChildNodeIds](qreal shift) {
        target.previewChildRects.clear();
        target.previewChildTools.clear();
        target.previewChildNodeIds.clear();
        const int startIndex = qBound(0, target.insertIndex, candidateChildRects.size());
        for (int i = 0; i < candidateChildRects.size(); ++i) {
            target.previewChildRects.append(i >= startIndex ? candidateChildRects[i].translated(0.0, shift)
                                                            : candidateChildRects[i]);
            target.previewChildTools.append(i < candidateChildTools.size()
                                                ? candidateChildTools[i]
                                                : QStringLiteral("cube"));
            target.previewChildNodeIds.append(i < candidateChildNodeIds.size() ? candidateChildNodeIds[i] : 0);
        }
    };
    target.zoneRect = contentRect;
    target.insertIndex = insertionIndexForY(candidateChildRects, scenePosition.y());
    target.placeholderRect = placeholderRectForInsertIndex(contentRect, candidateChildRects, target.insertIndex, effectivePreviewSize);
    setPreviewChildren(effectivePreviewSize.height() + ChildGap);

    if (bestArea->operation == SceneDocument::TreeNode::Difference && bestArea->cutSeparatorY > 0.0) {
        const bool baseZone = scenePosition.y() < bestArea->cutSeparatorY;
        target.insertIndex = baseZone
                                 ? 0
                                 : insertionIndexForY(candidateChildRects, scenePosition.y(), 1);
        target.zoneRect = baseZone
                              ? QRectF(contentRect.left(),
                                       contentRect.top(),
                                       contentRect.width(),
                                       qMax<qreal>(PrimitiveHeight, bestArea->cutSeparatorY - contentRect.top()))
                              : QRectF(contentRect.left(),
                                       bestArea->cutSeparatorY,
                                       contentRect.width(),
                                       qMax<qreal>(PrimitiveHeight, contentRect.bottom() - bestArea->cutSeparatorY));
        target.placeholderRect = placeholderRectForInsertIndex(contentRect, candidateChildRects, target.insertIndex, effectivePreviewSize);
        if (!baseZone && target.placeholderRect.top() < bestArea->cutSeparatorY)
            target.placeholderRect.moveTop(bestArea->cutSeparatorY + ChildGap * 0.5);
        setPreviewChildren(effectivePreviewSize.height() + ChildGap);
        if (baseZone)
            target.previewCutSeparatorY = target.placeholderRect.bottom() + ChildGap * 0.5;
    }

    if (sourceArea == bestArea && sourceChildIndex >= 0 && target.insertIndex == sourceChildIndex) {
        cancelTargetPreview();
        buildSourcePreview();
        return target;
    }

    QRectF changedOldRect = bestArea->rect;
    QRectF changedNewRect = expandedGroupRectForPreview(bestArea->rect,
                                                        target.placeholderRect,
                                                        candidateChildRects,
                                                        target.insertIndex,
                                                        effectivePreviewSize);
    target.previewGroupRect = changedNewRect;

    QVector<const GroupHitArea *> containingAreas;
    for (const GroupHitArea &area : m_groupHitAreas) {
        if (area.depth <= bestDepth && area.rect.contains(bestArea->rect.center()))
            containingAreas.append(&area);
    }

    for (int i = containingAreas.size() - 1; i >= 0; --i) {
        const GroupHitArea *area = containingAreas[i];
        QRectF expandedRect;
        QVector<QRectF> expandedChildRects = area->childRects;
        QVector<QString> expandedChildTools = area->childPreviewTools;
        QVector<int> expandedChildNodeIds = area->childNodeIds;
        if (area == bestArea) {
            expandedRect = changedNewRect;
        } else {
            QRectF oldChildRect;
            int changedChildIndex = -1;
            for (int childIndex = 0; childIndex < area->childRects.size(); ++childIndex) {
                const QRectF childRect = area->childRects[childIndex];
                if (childRect.contains(changedOldRect.center())) {
                    oldChildRect = childRect;
                    changedChildIndex = childIndex;
                    break;
                }
            }

            if (!oldChildRect.isValid())
                continue;

            expandedRect = expandedGroupRectForChangedChild(area->rect, area->childRects, oldChildRect, changedNewRect);
            if (changedChildIndex >= 0 && changedChildIndex < expandedChildRects.size()) {
                const qreal childShift = changedNewRect.height() - oldChildRect.height();
                expandedChildRects[changedChildIndex] = changedNewRect;
                if (!qFuzzyIsNull(childShift)) {
                    for (int childIndex = changedChildIndex + 1; childIndex < expandedChildRects.size(); ++childIndex)
                        expandedChildRects[childIndex].translate(0.0, childShift);
                }
            }
        }

        if (expandedRect != area->rect) {
            target.expandedGroupRects.prepend(expandedRect);
            target.expandedGroupChildRects.prepend(expandedChildRects);
            target.expandedGroupChildTools.prepend(expandedChildTools);
            target.expandedGroupChildNodeIds.prepend(expandedChildNodeIds);
            target.expandedGroupOperations.prepend(area->operation);
        }

        changedOldRect = area->rect;
        changedNewRect = expandedRect;
    }

    if (targetPreviewShift != 0.0) {
        target.previewGroupRect.translate(0.0, targetPreviewShift);
        target.placeholderRect.translate(0.0, targetPreviewShift);
        for (QRectF &childRect : target.previewChildRects)
            childRect.translate(0.0, targetPreviewShift);
        for (QRectF &expandedRect : target.expandedGroupRects)
            expandedRect.translate(0.0, targetPreviewShift);
        for (QVector<QRectF> &childRects : target.expandedGroupChildRects) {
            for (QRectF &childRect : childRects)
                childRect.translate(0.0, targetPreviewShift);
        }
    }

    return target;
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

    if (shape->type == ShapeNode::Sphere)
        return "sphere";
    if (shape->type == ShapeNode::Cylinder)
        return "cylinder";
    return "cube";
}

ShapeNode::Type SceneTreeGraphicsWidget::typeForPrimitive(int shapeId) const
{
    if (!m_scene)
        return ShapeNode::Cube;

    const ShapeNode *shape = m_scene->shapeById(shapeId);
    return shape ? shape->type : ShapeNode::Cube;
}

QString SceneTreeGraphicsWidget::labelForGroup(SceneDocument::TreeNode::Operation operation) const
{
    return labelForOperation(operation);
}

QColor SceneTreeGraphicsWidget::colorForGroup(SceneDocument::TreeNode::Operation operation) const
{
    if (operation == SceneDocument::TreeNode::Module)
        return QColor(230, 232, 236);
    if (operation == SceneDocument::TreeNode::Difference)
        return QColor(247, 224, 204);
    if (operation == SceneDocument::TreeNode::Intersection)
        return QColor(226, 220, 247);
    return QColor(216, 237, 226);
}
