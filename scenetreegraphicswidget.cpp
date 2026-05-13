#include "scenetreegraphicswidget.h"

#include <QBrush>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QScrollBar>
#include <QVarLengthArray>
#include <QWheelEvent>
#include <functional>
#include <cmath>

namespace {
constexpr qreal ToolbarX = 12.0;
constexpr qreal ToolbarY = 12.0;
constexpr qreal ToolSize = 54.0;
constexpr qreal ToolGap = 8.0;
constexpr qreal TreeX = 12.0;
constexpr qreal TreeY = 92.0;
constexpr qreal PrimitiveWidth = 88.0;
constexpr qreal PrimitiveHeight = 42.0;
constexpr qreal GroupMinWidth = 180.0;
constexpr qreal GroupHeaderHeight = 28.0;
constexpr qreal GroupPadding = 12.0;
constexpr qreal ChildGap = 10.0;
constexpr qreal CanvasMargin = 2000.0;
const QColor CanvasBackground(31, 41, 55);
const QColor MinorGridColor(96, 106, 121);
const QColor MajorGridColor(139, 150, 166);

void drawCanvasGrid(QPainter *painter, const QRectF &rect, qreal gridSize, const QColor &color, int width)
{
    QVarLengthArray<QLineF, 128> lines;

    const qreal left = std::floor(rect.left() / gridSize) * gridSize;
    const qreal top = std::floor(rect.top() / gridSize) * gridSize;

    for (qreal x = left; x < rect.right(); x += gridSize)
        lines.append(QLineF(x, rect.top(), x, rect.bottom()));

    for (qreal y = top; y < rect.bottom(); y += gridSize)
        lines.append(QLineF(rect.left(), y, rect.right(), y));

    QPen gridPen(color);
    gridPen.setWidth(width);
    gridPen.setCosmetic(true);
    painter->setPen(gridPen);
    painter->drawLines(lines.constData(), lines.size());
}

class TreeGraphicsScene : public QGraphicsScene
{
public:
    explicit TreeGraphicsScene(QObject *parent = nullptr)
        : QGraphicsScene(parent)
    {
        setBackgroundBrush(CanvasBackground);
    }

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override
    {
        QGraphicsScene::drawBackground(painter, rect);
        drawCanvasGrid(painter, rect, 24.0, MinorGridColor, 1);
        drawCanvasGrid(painter, rect, 96.0, MajorGridColor, 1);
    }
};

void addLabel(QGraphicsScene *scene, const QString &text, const QPointF &position, const QColor &color = QColor(35, 35, 35))
{
    auto *label = scene->addSimpleText(text);
    label->setBrush(color);
    label->setPos(position);
}

void addPrimitiveIcon(QGraphicsScene *scene, ShapeNode::Type type, const QRectF &rect)
{
    const QColor outline(59, 95, 134);
    const QColor face(178, 207, 238);
    const QColor faceLight(221, 235, 248);
    const QColor faceDark(139, 176, 214);

    if (type == ShapeNode::Sphere) {
        auto *sphere = scene->addEllipse(rect, QPen(outline, 1), QBrush(face));
        auto *latitude = scene->addEllipse(rect.adjusted(3.0, 9.0, -3.0, -9.0), QPen(QColor(93, 127, 166), 1), Qt::NoBrush);
        auto *highlight = scene->addEllipse(QRectF(rect.left() + rect.width() * 0.25,
                                                   rect.top() + rect.height() * 0.18,
                                                   rect.width() * 0.22,
                                                   rect.height() * 0.16),
                                            Qt::NoPen,
                                            QBrush(QColor(255, 255, 255, 165)));
        sphere->setZValue(5.0);
        latitude->setZValue(6.0);
        highlight->setZValue(7.0);
        return;
    }

    if (type == ShapeNode::Cylinder) {
        const QRectF top(rect.left() + 3.0, rect.top() + 3.0, rect.width() - 6.0, rect.height() * 0.34);
        const QRectF bottom(top.left(), rect.bottom() - top.height() - 3.0, top.width(), top.height());
        auto *body = scene->addRect(QRectF(top.left(), top.center().y(), top.width(), bottom.center().y() - top.center().y()),
                                    Qt::NoPen,
                                    QBrush(face));
        auto *left = scene->addLine(top.left(), top.center().y(), bottom.left(), bottom.center().y(), QPen(outline, 1));
        auto *right = scene->addLine(top.right(), top.center().y(), bottom.right(), bottom.center().y(), QPen(outline, 1));
        auto *bottomEllipse = scene->addEllipse(bottom, QPen(outline, 1), QBrush(faceDark));
        auto *topEllipse = scene->addEllipse(top, QPen(outline, 1), QBrush(faceLight));
        body->setZValue(5.0);
        left->setZValue(6.0);
        right->setZValue(6.0);
        bottomEllipse->setZValue(7.0);
        topEllipse->setZValue(8.0);
        return;
    }

    QPolygonF topFace;
    topFace << QPointF(rect.left() + rect.width() * 0.22, rect.top() + rect.height() * 0.34)
            << QPointF(rect.left() + rect.width() * 0.48, rect.top() + rect.height() * 0.12)
            << QPointF(rect.left() + rect.width() * 0.82, rect.top() + rect.height() * 0.28)
            << QPointF(rect.left() + rect.width() * 0.56, rect.top() + rect.height() * 0.50);

    QPolygonF leftFace;
    leftFace << topFace[0]
             << topFace[3]
             << QPointF(rect.left() + rect.width() * 0.56, rect.top() + rect.height() * 0.86)
             << QPointF(rect.left() + rect.width() * 0.22, rect.top() + rect.height() * 0.70);

    QPolygonF rightFace;
    rightFace << topFace[3]
              << topFace[2]
              << QPointF(rect.left() + rect.width() * 0.82, rect.top() + rect.height() * 0.64)
              << QPointF(rect.left() + rect.width() * 0.56, rect.top() + rect.height() * 0.86);

    auto *leftItem = scene->addPolygon(leftFace, QPen(outline, 1), QBrush(face));
    auto *rightItem = scene->addPolygon(rightFace, QPen(outline, 1), QBrush(faceDark));
    auto *topItem = scene->addPolygon(topFace, QPen(outline, 1), QBrush(faceLight));
    leftItem->setZValue(5.0);
    rightItem->setZValue(6.0);
    topItem->setZValue(7.0);
}

QString primitiveNumberText(const QString &label, int fallbackId)
{
    int end = label.size() - 1;
    while (end >= 0 && label[end].isSpace())
        --end;

    int start = end;
    while (start >= 0 && label[start].isDigit())
        --start;

    if (start < end)
        return label.mid(start + 1, end - start);

    return fallbackId > 0 ? QString::number(fallbackId) : QStringLiteral("?");
}

void addPrimitiveNumberBadge(QGraphicsScene *scene, const QString &number, const QRectF &rect)
{
    const QRectF badgeRect(rect.right() - 25.0, rect.top() + 9.0, 18.0, 18.0);
    auto *badge = scene->addEllipse(badgeRect, QPen(QColor(82, 111, 146), 1), QBrush(QColor(244, 248, 252)));
    badge->setZValue(9.0);

    auto *text = scene->addSimpleText(number);
    text->setBrush(QColor(30, 58, 90));
    const QRectF textRect = text->boundingRect();
    text->setPos(badgeRect.center() - QPointF(textRect.width() * 0.5, textRect.height() * 0.5 + 1.0));
    text->setZValue(10.0);
}

class ToolInstanceItem : public QGraphicsRectItem
{
public:
    explicit ToolInstanceItem(const QString &label, const QColor &fill)
        : QGraphicsRectItem(QRectF(0.0, 0.0, 98.0, 46.0))
    {
        setPen(QPen(fill.darker(145), 2));
        setBrush(fill);
        setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
        setZValue(80.0);

        auto *text = new QGraphicsSimpleTextItem(label, this);
        text->setBrush(QColor(30, 42, 54));
        text->setPos(8.0, 13.0);
    }
};

class TreeNodeDragHandleItem : public QGraphicsRectItem
{
public:
    TreeNodeDragHandleItem(int nodeId,
                           const QString &label,
                           const QRectF &rect,
                           const QColor &fill,
                           std::function<void(int)> onSelected,
                           std::function<void(int, const QPointF &)> onDropped)
        : QGraphicsRectItem(rect)
        , m_nodeId(nodeId)
        , m_label(label)
        , m_fill(fill)
        , m_onSelected(onSelected)
        , m_onDropped(onDropped)
    {
        setPen(Qt::NoPen);
        setBrush(QColor(0, 0, 0, 1));
        setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
        setZValue(70.0);
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (event->button() == Qt::RightButton) {
            if (m_onSelected)
                m_onSelected(m_nodeId);
            event->accept();
            return;
        }

        if (event->button() != Qt::LeftButton) {
            event->ignore();
            return;
        }

        if (!scene())
            return;

        m_dragItem = new ToolInstanceItem(QString("move %1").arg(m_label), m_fill);
        scene()->addItem(m_dragItem);
        moveDragItem(event->scenePos());
        event->accept();
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override
    {
        moveDragItem(event->scenePos());
        event->accept();
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override
    {
        const QPointF dropPosition = event->scenePos();
        moveDragItem(dropPosition);
        delete m_dragItem;
        m_dragItem = nullptr;
        if (m_onDropped)
            m_onDropped(m_nodeId, dropPosition);
        event->accept();
    }

private:
    void moveDragItem(const QPointF &scenePosition)
    {
        if (!m_dragItem)
            return;

        const QRectF rect = m_dragItem->rect();
        m_dragItem->setPos(scenePosition - QPointF(rect.width() * 0.5, rect.height() * 0.5));
    }

private:
    int m_nodeId = 0;
    QString m_label;
    QColor m_fill;
    std::function<void(int)> m_onSelected;
    std::function<void(int, const QPointF &)> m_onDropped;
    ToolInstanceItem *m_dragItem = nullptr;
};

class TreeNodeSelectionItem : public QGraphicsRectItem
{
public:
    TreeNodeSelectionItem(int nodeId, const QRectF &rect, qreal zValue, std::function<void(int)> onSelected)
        : QGraphicsRectItem(rect)
        , m_nodeId(nodeId)
        , m_onSelected(onSelected)
    {
        setPen(Qt::NoPen);
        setBrush(QColor(0, 0, 0, 1));
        setAcceptedMouseButtons(Qt::RightButton);
        setZValue(zValue);
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (event->button() != Qt::RightButton) {
            event->ignore();
            return;
        }

        if (m_onSelected)
            m_onSelected(m_nodeId);
        event->accept();
    }

private:
    int m_nodeId = 0;
    std::function<void(int)> m_onSelected;
};

class PaletteToolItem : public QGraphicsRectItem
{
public:
    PaletteToolItem(const QString &label, const QColor &fill, std::function<void(const QString &, const QPointF &)> onDropped)
        : QGraphicsRectItem(QRectF(0.0, 0.0, ToolSize, ToolSize))
        , m_label(label)
        , m_fill(fill)
        , m_onDropped(onDropped)
    {
        setPen(QPen(QColor(120, 126, 136)));
        setBrush(QColor(255, 255, 255));
        setAcceptedMouseButtons(Qt::LeftButton);
        setZValue(100.0);

        auto *text = new QGraphicsSimpleTextItem(label, this);
        text->setBrush(QColor(45, 50, 58));
        text->setPos(6.0, 18.0);
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (!scene())
            return;

        m_dragItem = new ToolInstanceItem(m_label, m_fill);
        scene()->addItem(m_dragItem);
        moveDragItem(event->scenePos());
        event->accept();
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override
    {
        moveDragItem(event->scenePos());
        event->accept();
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override
    {
        const QPointF dropPosition = event->scenePos();
        moveDragItem(dropPosition);
        delete m_dragItem;
        m_dragItem = nullptr;
        if (m_onDropped)
            m_onDropped(m_label, dropPosition);
        event->accept();
    }

private:
    void moveDragItem(const QPointF &scenePosition)
    {
        if (!m_dragItem)
            return;

        const QRectF rect = m_dragItem->rect();
        m_dragItem->setPos(scenePosition - QPointF(rect.width() * 0.5, rect.height() * 0.5));
    }

private:
    QString m_label;
    QColor m_fill;
    std::function<void(const QString &, const QPointF &)> m_onDropped;
    ToolInstanceItem *m_dragItem = nullptr;
};

QColor fillForTool(const QString &tool)
{
    if (tool == "difference")
        return QColor(247, 224, 204);
    if (tool == "intersection")
        return QColor(226, 220, 247);
    if (tool == "union")
        return QColor(216, 237, 226);
    if (tool == "module")
        return QColor(230, 232, 236);
    return QColor(219, 231, 246);
}
}

SceneTreeGraphicsWidget::SceneTreeGraphicsWidget(QWidget *parent)
    : QGraphicsView(parent)
    , m_graphicsScene(new TreeGraphicsScene(this))
{
    setScene(m_graphicsScene);
    setMinimumHeight(280);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(CanvasBackground);
    setCacheMode(QGraphicsView::CacheNone);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setCursor(Qt::OpenHandCursor);
}

void SceneTreeGraphicsWidget::setSceneDocument(const SceneDocument *scene)
{
    m_scene = scene;
    refresh();
}

void SceneTreeGraphicsWidget::setToolDroppedCallback(std::function<void(const QString &, int)> callback)
{
    m_toolDroppedCallback = callback;
}

void SceneTreeGraphicsWidget::setTreeNodeDroppedCallback(std::function<void(int, int)> callback)
{
    m_treeNodeDroppedCallback = callback;
}

void SceneTreeGraphicsWidget::setTreeNodeSelectedCallback(std::function<void(int)> callback)
{
    m_treeNodeSelectedCallback = callback;
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
    m_graphicsScene->clear();
    m_groupHitAreas.clear();

    const QRectF toolbarRect = drawToolbar();

    if (m_scene && !m_scene->treeRoot().children.isEmpty()) {
        drawNode(m_scene->treeRoot(), QPointF(TreeX, TreeY), 0);
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

void SceneTreeGraphicsWidget::mousePressEvent(QMouseEvent *event)
{
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
    m_graphicsScene->addRect(toolbarRect, QPen(QColor(180, 185, 192)), QBrush(QColor(232, 235, 239)));

    for (int i = 0; i < tools.size(); ++i) {
        auto *tool = new PaletteToolItem(tools[i], fillForTool(tools[i]), [this](const QString &toolName, const QPointF &position) {
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
    m_graphicsScene->addRect(rect,
                             QPen(selected ? QColor(255, 203, 87) : QColor(92, 116, 150), selected ? 3 : 1),
                             QBrush(QColor(219, 231, 246)));
    addPrimitiveIcon(m_graphicsScene, typeForPrimitive(node.shapeId), QRectF(rect.left() + 18.0, rect.top() + 7.0, 32.0, 28.0));
    addPrimitiveNumberBadge(m_graphicsScene, primitiveNumberText(label, node.shapeId), rect);
    auto *handle = new TreeNodeDragHandleItem(
        node.id,
        label,
        rect,
        QColor(219, 231, 246),
        [this](int nodeId) {
            handleTreeNodeSelected(nodeId);
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
    QPointF childTopLeft(topLeft.x() + GroupPadding, topLeft.y() + GroupHeaderHeight + GroupPadding);
    qreal maxChildWidth = 0.0;

    for (const SceneDocument::TreeNode &child : node.children) {
        const QRectF childRect = drawNode(child, childTopLeft, depth + 1);
        childRects.append(childRect);
        maxChildWidth = qMax(maxChildWidth, childRect.width());
        childTopLeft.ry() += childRect.height() + ChildGap;
    }

    const qreal childrenHeight = childRects.isEmpty()
                                     ? PrimitiveHeight
                                     : childTopLeft.y() - topLeft.y() - GroupHeaderHeight - GroupPadding - ChildGap;
    const QSizeF size(qMax(GroupMinWidth, maxChildWidth + GroupPadding * 2.0),
                      GroupHeaderHeight + GroupPadding * 2.0 + childrenHeight);
    const QRectF rect(topLeft, size);
    const QColor fill = colorForGroup(node.operation);
    const bool selected = node.id == m_selectedTreeNodeId;
    m_groupHitAreas.append({rect, node.id, depth});

    auto *groupItem = m_graphicsScene->addRect(rect,
                                               QPen(selected ? QColor(255, 203, 87) : fill.darker(145), selected ? 3 : 2),
                                               QBrush(fill));
    groupItem->setZValue(depth * 10.0 - 100.0);
    m_graphicsScene->addItem(new TreeNodeSelectionItem(
        node.id,
        rect,
        depth * 10.0 - 80.0,
        [this](int nodeId) {
            handleTreeNodeSelected(nodeId);
        }));

    const QRectF iconRect(rect.left() + 8.0, rect.top() + 6.0, 18.0, 18.0);
    m_graphicsScene->addRect(iconRect, QPen(fill.darker(165)), QBrush(fill.lighter(125)));
    const QString groupLabel = labelForGroup(node.operation);
    addLabel(m_graphicsScene, groupLabel, rect.topLeft() + QPointF(32.0, 7.0), QColor(28, 36, 44));
    m_graphicsScene->addItem(new TreeNodeDragHandleItem(
        node.id,
        groupLabel,
        QRectF(rect.topLeft(), QSizeF(rect.width(), GroupHeaderHeight)),
        fill,
        [this](int nodeId) {
            handleTreeNodeSelected(nodeId);
        },
        [this](int nodeId, const QPointF &position) {
            handleTreeNodeDrop(nodeId, position);
        }));

    if (node.children.isEmpty())
        addLabel(m_graphicsScene, "empty", QPointF(rect.left() + GroupPadding, rect.top() + GroupHeaderHeight + GroupPadding + 10.0), QColor(95, 98, 105));

    if (node.operation == SceneDocument::TreeNode::Difference) {
        qreal separatorY = rect.top() + GroupHeaderHeight + GroupPadding + PrimitiveHeight + ChildGap * 0.5;
        if (!childRects.isEmpty())
            separatorY = childRects.first().bottom() + ChildGap * 0.5;

        auto *separator = m_graphicsScene->addLine(rect.left() + GroupPadding,
                                                   separatorY,
                                                   rect.right() - GroupPadding,
                                                   separatorY,
                                                   QPen(QColor(130, 92, 70), 1, Qt::DashLine));
        separator->setZValue(depth * 10.0 - 70.0);

        addLabel(m_graphicsScene, "base", QPointF(rect.right() - 54.0, rect.top() + GroupHeaderHeight + 8.0), QColor(92, 72, 58));
        addLabel(m_graphicsScene, "cut", QPointF(rect.right() - 44.0, separatorY + 6.0), QColor(128, 74, 48));
    }

    return rect;
}

void SceneTreeGraphicsWidget::handleToolDrop(const QString &toolName, const QPointF &scenePosition)
{
    if (m_toolDroppedCallback)
        m_toolDroppedCallback(toolName, groupIdAt(scenePosition));
}

void SceneTreeGraphicsWidget::handleTreeNodeDrop(int nodeId, const QPointF &scenePosition)
{
    if (m_treeNodeDroppedCallback)
        m_treeNodeDroppedCallback(nodeId, groupIdAt(scenePosition));
}

void SceneTreeGraphicsWidget::handleTreeNodeSelected(int nodeId)
{
    m_selectedTreeNodeId = nodeId;
    refresh();
    if (m_treeNodeSelectedCallback)
        m_treeNodeSelectedCallback(nodeId);
}

int SceneTreeGraphicsWidget::groupIdAt(const QPointF &scenePosition) const
{
    int groupId = 0;
    int bestDepth = -1;

    for (const GroupHitArea &area : m_groupHitAreas) {
        if (area.depth <= bestDepth || !area.rect.contains(scenePosition))
            continue;

        groupId = area.groupId;
        bestDepth = area.depth;
    }

    return groupId;
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
    if (operation == SceneDocument::TreeNode::Module)
        return "module";
    if (operation == SceneDocument::TreeNode::Difference)
        return "difference";
    if (operation == SceneDocument::TreeNode::Intersection)
        return "intersection";
    return "union";
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
