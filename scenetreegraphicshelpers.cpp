#include "scenetreegraphicshelpers.h"

#include <QBrush>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QStyleOptionGraphicsItem>
#include <QtGlobal>
#include <QVarLengthArray>
#include <QWidget>
#include <cmath>

namespace SceneTreeGraphics {

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

void addLabel(QGraphicsScene *scene, const QString &text, const QPointF &position, const QColor &color)
{
    auto *label = scene->addSimpleText(text);
    label->setBrush(color);
    label->setPos(position);
}

QGraphicsRectItem *addSoftShadow(QGraphicsScene *scene, const QRectF &rect, qreal zValue)
{
    auto *shadow = scene->addRect(rect.translated(2.0, 2.0), Qt::NoPen, QBrush(QColor(0, 0, 0, 38)));
    shadow->setZValue(zValue);
    return shadow;
}

QGraphicsPathItem *addRoundedPanel(QGraphicsScene *scene,
                                   const QRectF &rect,
                                   qreal radius,
                                   const QPen &pen,
                                   const QBrush &brush,
                                   qreal zValue)
{
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    auto *panel = scene->addPath(path, pen, brush);
    panel->setZValue(zValue);
    return panel;
}

void paintPrimitiveIcon(QPainter *painter, ShapeNode::Type type, const QRectF &rect)
{
    const QColor outline(59, 95, 134);
    const QColor face(178, 207, 238);
    const QColor faceLight(221, 235, 248);
    const QColor faceDark(139, 176, 214);

    painter->setPen(QPen(outline, 1));
    if (type == ShapeNode::Sphere) {
        painter->setBrush(face);
        painter->drawEllipse(rect);
        painter->setPen(QPen(QColor(93, 127, 166), 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(rect.adjusted(3.0, 9.0, -3.0, -9.0));
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 255, 255, 165));
        painter->drawEllipse(QRectF(rect.left() + rect.width() * 0.25,
                                    rect.top() + rect.height() * 0.18,
                                    rect.width() * 0.22,
                                    rect.height() * 0.16));
        return;
    }

    if (type == ShapeNode::Cylinder) {
        const QRectF top(rect.left() + 3.0, rect.top() + 3.0, rect.width() - 6.0, rect.height() * 0.34);
        const QRectF bottom(top.left(), rect.bottom() - top.height() - 3.0, top.width(), top.height());
        painter->setPen(Qt::NoPen);
        painter->setBrush(face);
        painter->drawRect(QRectF(top.left(), top.center().y(), top.width(), bottom.center().y() - top.center().y()));
        painter->setPen(QPen(outline, 1));
        painter->drawLine(top.left(), top.center().y(), bottom.left(), bottom.center().y());
        painter->drawLine(top.right(), top.center().y(), bottom.right(), bottom.center().y());
        painter->setBrush(faceDark);
        painter->drawEllipse(bottom);
        painter->setBrush(faceLight);
        painter->drawEllipse(top);
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

    painter->setPen(QPen(outline, 1));
    painter->setBrush(face);
    painter->drawPolygon(leftFace);
    painter->setBrush(faceDark);
    painter->drawPolygon(rightFace);
    painter->setBrush(faceLight);
    painter->drawPolygon(topFace);
}

void paintOperationIcon(QPainter *painter,
                        SceneDocument::TreeNode::Operation operation,
                        const QRectF &rect,
                        const QColor &accent,
                        qreal symbolInset)
{
    painter->setPen(QPen(accent.darker(135), 1));
    painter->setBrush(QColor(255, 255, 255, 135));
    painter->drawRoundedRect(rect, 3.0, 3.0);

    const QPointF center = rect.center();
    const QRectF symbolRect = rect.adjusted(symbolInset, symbolInset, -symbolInset, -symbolInset);
    QPen pen(accent.darker(160), 2);
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    if (operation == SceneDocument::TreeNode::Union) {
        painter->drawLine(symbolRect.left(), center.y(), symbolRect.right(), center.y());
        painter->drawLine(center.x(), symbolRect.top(), center.x(), symbolRect.bottom());
    } else if (operation == SceneDocument::TreeNode::Difference) {
        painter->drawLine(symbolRect.left(), center.y(), symbolRect.right(), center.y());
    } else if (operation == SceneDocument::TreeNode::Intersection) {
        painter->setPen(QPen(accent.darker(150), 1));
        painter->setBrush(QColor(255, 255, 255, 60));
        painter->drawEllipse(QRectF(symbolRect.left(), symbolRect.top() + 1.0, symbolRect.width() * 0.62, symbolRect.height() - 2.0));
        painter->drawEllipse(QRectF(symbolRect.center().x() - symbolRect.width() * 0.31,
                                    symbolRect.top() + 1.0,
                                    symbolRect.width() * 0.62,
                                    symbolRect.height() - 2.0));
    } else if (operation == SceneDocument::TreeNode::Translate) {
        painter->drawLine(symbolRect.left(), center.y(), symbolRect.right(), center.y());
        painter->drawLine(symbolRect.right() - 4.0, center.y() - 4.0, symbolRect.right(), center.y());
        painter->drawLine(symbolRect.right() - 4.0, center.y() + 4.0, symbolRect.right(), center.y());
    } else if (operation == SceneDocument::TreeNode::Rotate) {
        painter->drawArc(symbolRect, 35 * 16, 285 * 16);
        painter->drawLine(symbolRect.right() - 2.0, center.y() - 5.0, symbolRect.right(), center.y());
        painter->drawLine(symbolRect.right() - 7.0, center.y() - 1.0, symbolRect.right(), center.y());
    } else if (operation == SceneDocument::TreeNode::Scale) {
        const QRectF small(symbolRect.left(), symbolRect.top() + symbolRect.height() * 0.35,
                           symbolRect.width() * 0.44, symbolRect.height() * 0.44);
        const QRectF large(symbolRect.left() + symbolRect.width() * 0.28,
                           symbolRect.top(),
                           symbolRect.width() * 0.72,
                           symbolRect.height() * 0.72);
        painter->setPen(QPen(accent.darker(160), 1.6));
        painter->drawRect(large);
        painter->drawRect(small);
        painter->drawLine(small.right(), small.top(), large.right(), large.top());
    } else {
        painter->setPen(accent.darker(160));
        painter->drawText(rect, Qt::AlignCenter, QStringLiteral("M"));
    }
}

QVector<ShapeParameterControl> shapeParameterControls(const ShapeNode &shape)
{
    if (shape.type == ShapeNode::Sphere)
        return {{QStringLiteral("R"), shape.radius}};

    if (shape.type == ShapeNode::Cylinder)
        return {{QStringLiteral("R"), shape.radius}, {QStringLiteral("H"), shape.height}};

    return {{QStringLiteral("X"), shape.size.x()},
            {QStringLiteral("Y"), shape.size.y()},
            {QStringLiteral("Z"), shape.size.z()}};
}

QRectF shapeParameterControlRect(const QRectF &primitiveRect, int index, int count)
{
    if (index < 0 || count <= 0 || index >= count)
        return QRectF();

    const qreal left = primitiveRect.left() + 72.0;
    const qreal width = qMin<qreal>(54.0, qMax<qreal>(44.0, primitiveRect.right() - left - 6.0));
    const qreal gap = 2.0;
    const qreal availableHeight = PrimitiveHeight - 6.0;
    const qreal rowHeight = qMin<qreal>(16.0, (availableHeight - gap * (count - 1)) / count);
    const qreal totalHeight = rowHeight * count + gap * (count - 1);
    const qreal top = primitiveRect.top() + (PrimitiveHeight - totalHeight) * 0.5 + index * (rowHeight + gap);
    return QRectF(left, top, width, rowHeight);
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

int insertionIndexForY(const QVector<QRectF> &childRects, qreal y, int minimumIndex)
{
    if (childRects.isEmpty())
        return minimumIndex;

    const int minIndex = qBound(0, minimumIndex, childRects.size());
    const qreal hysteresis = qMax<qreal>(ChildGap * 2.0, 20.0);

    if (minIndex == 0 && y <= childRects.first().top() + hysteresis)
        return 0;

    for (int slot = qMax(1, minIndex); slot < childRects.size(); ++slot) {
        const qreal gapTop = childRects[slot - 1].bottom();
        const qreal gapBottom = childRects[slot].top();
        if (y >= gapTop - hysteresis && y <= gapBottom + hysteresis)
            return slot;
    }

    if (y >= childRects.last().bottom() - hysteresis)
        return childRects.size();

    int bestSlot = minIndex;
    qreal bestDistance = std::numeric_limits<qreal>::max();
    for (int slot = minIndex; slot <= childRects.size(); ++slot) {
        qreal slotY = 0.0;
        if (slot == 0) {
            slotY = childRects.first().top();
        } else if (slot == childRects.size()) {
            slotY = childRects.last().bottom();
        } else {
            slotY = (childRects[slot - 1].bottom() + childRects[slot].top()) * 0.5;
        }

        const qreal distance = qAbs(y - slotY);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestSlot = slot;
        }
    }

    return bestSlot;
}

QSizeF defaultPreviewSize()
{
    return QSizeF(PrimitiveWidth, PrimitiveHeight);
}

QSizeF groupPreviewSize()
{
    return QSizeF(GroupMinWidth, GroupHeaderHeight + GroupPadding * 2.0 + PrimitiveHeight);
}

QSizeF transformPreviewSize()
{
    return QSizeF(TransformHeaderWidth + GroupPadding * 2.0 + PrimitiveWidth,
                  GroupPadding * 2.0 + PrimitiveHeight);
}

QSizeF differencePreviewSize()
{
    return QSizeF(GroupWideMinWidth, GroupHeaderHeight + GroupPadding * 2.0 + DifferenceMinContentHeight);
}

QSizeF previewSizeForTool(const QString &tool)
{
    if (tool == "cube" || tool == "sphere" || tool == "cylinder")
        return defaultPreviewSize();
    if (tool == "difference")
        return differencePreviewSize();
    if (tool == "intersection")
        return QSizeF(GroupWideMinWidth, GroupHeaderHeight + GroupPadding * 2.0 + PrimitiveHeight);
    if (tool == "module")
        return QSizeF(GroupModuleMinWidth, GroupHeaderHeight + GroupPadding * 2.0 + PrimitiveHeight);
    if (tool == "translate" || tool == "rotate" || tool == "scale")
        return transformPreviewSize();

    return groupPreviewSize();
}

ShapeNode::Type primitiveTypeForTool(const QString &tool)
{
    const QString normalized = tool.toLower();
    if (normalized.contains("sphere"))
        return ShapeNode::Sphere;
    if (normalized.contains("cylinder"))
        return ShapeNode::Cylinder;
    return ShapeNode::Cube;
}

QString toolNameForPrimitiveType(ShapeNode::Type type)
{
    if (type == ShapeNode::Sphere)
        return QStringLiteral("sphere");
    if (type == ShapeNode::Cylinder)
        return QStringLiteral("cylinder");
    return QStringLiteral("cube");
}

bool operationForToolName(const QString &tool, SceneDocument::TreeNode::Operation *operation)
{
    if (!operation)
        return false;

    const QString normalized = tool.toLower();
    if (normalized.contains("union")) {
        *operation = SceneDocument::TreeNode::Union;
        return true;
    }
    if (normalized.contains("difference")) {
        *operation = SceneDocument::TreeNode::Difference;
        return true;
    }
    if (normalized.contains("intersection")) {
        *operation = SceneDocument::TreeNode::Intersection;
        return true;
    }
    if (normalized.contains("module")) {
        *operation = SceneDocument::TreeNode::Module;
        return true;
    }
    if (normalized.contains("translate")) {
        *operation = SceneDocument::TreeNode::Translate;
        return true;
    }
    if (normalized.contains("rotate")) {
        *operation = SceneDocument::TreeNode::Rotate;
        return true;
    }
    if (normalized.contains("scale")) {
        *operation = SceneDocument::TreeNode::Scale;
        return true;
    }
    return false;
}

namespace {

const OperationVisual OperationVisuals[] = {
    {SceneDocument::TreeNode::Union, "union", QColor(216, 237, 226), GroupMinWidth},
    {SceneDocument::TreeNode::Difference, "difference", QColor(247, 224, 204), GroupWideMinWidth},
    {SceneDocument::TreeNode::Intersection, "intersection", QColor(226, 220, 247), GroupWideMinWidth},
    {SceneDocument::TreeNode::Module, "module", QColor(230, 232, 236), GroupModuleMinWidth},
    {SceneDocument::TreeNode::Translate, "translate", QColor(218, 238, 246), TransformHeaderWidth + GroupPadding * 2.0 + PrimitiveWidth},
    {SceneDocument::TreeNode::Rotate, "rotate", QColor(239, 229, 247), TransformHeaderWidth + GroupPadding * 2.0 + PrimitiveWidth},
    {SceneDocument::TreeNode::Scale, "scale", QColor(229, 241, 218), TransformHeaderWidth + GroupPadding * 2.0 + PrimitiveWidth},
};

} // namespace

const OperationVisual &operationVisual(SceneDocument::TreeNode::Operation operation)
{
    for (const OperationVisual &visual : OperationVisuals) {
        if (visual.operation == operation)
            return visual;
    }
    return OperationVisuals[0];
}

qreal minimumWidthForOperation(SceneDocument::TreeNode::Operation operation)
{
    return operationVisual(operation).minWidth;
}

QString labelForOperation(SceneDocument::TreeNode::Operation operation)
{
    return QString::fromLatin1(operationVisual(operation).toolName);
}

bool isTransformOperation(SceneDocument::TreeNode::Operation operation)
{
    return operation == SceneDocument::TreeNode::Translate
           || operation == SceneDocument::TreeNode::Rotate
           || operation == SceneDocument::TreeNode::Scale;
}

QRectF placeholderRectForInsertIndex(const QRectF &contentRect, const QVector<QRectF> &childRects, int insertIndex, const QSizeF &previewSize)
{
    if (childRects.isEmpty())
        return QRectF(contentRect.left(), contentRect.top(), previewSize.width(), previewSize.height());

    if (insertIndex <= 0)
        return QRectF(contentRect.left(), contentRect.top(), previewSize.width(), previewSize.height());

    if (insertIndex >= childRects.size())
        return QRectF(contentRect.left(), childRects.last().bottom() + ChildGap, previewSize.width(), previewSize.height());

    return QRectF(contentRect.left(), childRects[insertIndex].top(), previewSize.width(), previewSize.height());
}

QRectF slotMarkerRectForInsertIndex(const QRectF &contentRect, const QVector<QRectF> &childRects, int insertIndex)
{
    const qreal markerWidth = childRects.isEmpty()
                                  ? qMax(contentRect.width(), PrimitiveWidth)
                                  : qMax(contentRect.width(), childRects.first().width());
    qreal y = contentRect.top();

    if (childRects.isEmpty()) {
        y = contentRect.top();
    } else if (insertIndex <= 0) {
        y = childRects.first().top() - ChildGap * 0.5;
    } else if (insertIndex >= childRects.size()) {
        y = childRects.last().bottom() + ChildGap * 0.5;
    } else {
        y = (childRects[insertIndex - 1].bottom() + childRects[insertIndex].top()) * 0.5;
    }

    return QRectF(contentRect.left(), y, markerWidth, 1.0);
}

QRectF expandedGroupRectForPreview(const QRectF &groupRect, const QRectF &placeholderRect, const QVector<QRectF> &childRects, int insertIndex, const QSizeF &previewSize)
{
    QRectF expanded = groupRect;
    QRectF futureContent = placeholderRect;
    const qreal shift = previewSize.height() + ChildGap;

    for (int i = 0; i < childRects.size(); ++i) {
        const QRectF childRect = i >= insertIndex ? childRects[i].translated(0.0, shift) : childRects[i];
        futureContent = futureContent.united(childRect);
    }

    expanded.setRight(qMax(expanded.right(), futureContent.right() + GroupPadding));
    expanded.setBottom(qMax(expanded.bottom(), futureContent.bottom() + GroupPadding));
    return expanded;
}

QRectF expandedGroupRectForChangedChild(const QRectF &groupRect, const QVector<QRectF> &childRects, const QRectF &oldChildRect, const QRectF &newChildRect)
{
    QRectF futureContent = newChildRect;
    const qreal shift = qMax<qreal>(0.0, newChildRect.height() - oldChildRect.height());
    bool passedChangedChild = false;

    for (const QRectF &childRect : childRects) {
        if (childRect == oldChildRect) {
            passedChangedChild = true;
            continue;
        }

        futureContent = futureContent.united(passedChangedChild ? childRect.translated(0.0, shift) : childRect);
    }

    QRectF expanded = groupRect;
    expanded.setRight(qMax(expanded.right(), futureContent.right() + GroupPadding));
    expanded.setBottom(qMax(expanded.bottom(), futureContent.bottom() + GroupPadding));
    return expanded;
}


void appendPreviewItem(QVector<QGraphicsItem *> *items, QGraphicsItem *item)
{
    if (!items || !item)
        return;
    items->append(item);
}

QPainterPath dragFocusOutlinePath(const QString &tool, const QRectF &rect)
{
    QPainterPath path;
    SceneDocument::TreeNode::Operation operation;
    if (operationForToolName(tool, &operation)) {
        path.addRoundedRect(rect.adjusted(-4.0, -4.0, 4.0, 4.0), CornerRadius + 2.0, CornerRadius + 2.0);
        return path;
    }

    const QRectF iconRect(rect.left() + 20.0,
                          rect.top() + (PrimitiveHeight - PrimitiveIconSize) * 0.5,
                          PrimitiveIconSize,
                          PrimitiveIconSize);
    path.addEllipse(iconRect.adjusted(-6.0, -6.0, 6.0, 6.0));
    return path;
}

QGraphicsPathItem *addDragFocusOutline(QGraphicsScene *scene,
                                       QVector<QGraphicsItem *> *items,
                                       const QString &tool,
                                       const QRectF &rect,
                                       qreal zValue)
{
    auto *outline = scene->addPath(dragFocusOutlinePath(tool, rect),
                                   QPen(QColor(74, 190, 116), 2, Qt::DashLine),
                                   Qt::NoBrush);
    outline->setZValue(zValue);
    appendPreviewItem(items, outline);
    return outline;
}

QGraphicsPathItem *addDropSlotMarker(QGraphicsScene *scene,
                                     QVector<QGraphicsItem *> *items,
                                     const QRectF &rect,
                                     qreal zValue)
{
    const qreal y = rect.top();
    const qreal left = rect.left() - 10.0;
    const qreal right = rect.right() + 10.0;
    const qreal arrow = 8.0;

    QPainterPath path;
    path.moveTo(left, y);
    path.lineTo(right - arrow, y);
    path.moveTo(right - arrow, y - arrow * 0.55);
    path.lineTo(right, y);
    path.lineTo(right - arrow, y + arrow * 0.55);
    path.moveTo(left, y - 4.0);
    path.lineTo(left + 8.0, y);
    path.lineTo(left, y + 4.0);

    auto *marker = scene->addPath(path,
                                  QPen(QColor(74, 190, 116, 185), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin),
                                  Qt::NoBrush);
    marker->setZValue(zValue);
    appendPreviewItem(items, marker);

    auto *shadow = scene->addPath(path,
                                  QPen(QColor(0, 0, 0, 55), 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin),
                                  Qt::NoBrush);
    shadow->setZValue(zValue - 0.1);
    appendPreviewItem(items, shadow);
    return marker;
}

class TreeNodeDragHandleItem : public QGraphicsRectItem
{
public:
    TreeNodeDragHandleItem(int nodeId,
                           const QString &label,
                           const QRectF &rect,
                           const QRectF &sourceRect,
                           std::function<void(int)> onSelected,
                           const QSizeF &previewSize,
                           std::function<void(const QPointF &, const QSizeF &, const QString &)> onPreviewMoved,
                           std::function<void()> onPreviewFinished,
                           std::function<void(int, const QPointF &)> onDropped)
        : QGraphicsRectItem(rect)
        , m_nodeId(nodeId)
        , m_label(label)
        , m_sourceRect(sourceRect)
        , m_previewSize(previewSize)
        , m_onSelected(onSelected)
        , m_onPreviewMoved(onPreviewMoved)
        , m_onPreviewFinished(onPreviewFinished)
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

        m_pressScenePos = event->scenePos();
        m_previewActive = false;
        createDragSnapshot(m_pressScenePos);
        event->accept();
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override
    {
        moveDragSnapshot(event->scenePos());
        if (!m_previewActive) {
            const QPointF delta = event->scenePos() - m_pressScenePos;
            if (delta.x() * delta.x() + delta.y() * delta.y() < DragPreviewStartDistance * DragPreviewStartDistance) {
                event->accept();
                return;
            }
            m_previewActive = true;
        }
        if (m_onPreviewMoved)
            m_onPreviewMoved(draggedRectCenter(event->scenePos()), m_previewSize, m_label);
        event->accept();
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override
    {
        const QPointF dropPosition = event->scenePos();
        const QPointF dropCenter = draggedRectCenter(dropPosition);
        const bool wasPreviewActive = m_previewActive;
        const int nodeId = m_nodeId;
        const auto onPreviewFinished = m_onPreviewFinished;
        const auto onDropped = m_onDropped;

        moveDragSnapshot(dropPosition);
        delete m_dragSnapshot;
        m_dragSnapshot = nullptr;
        delete m_dragOutline;
        m_dragOutline = nullptr;
        m_previewActive = false;
        event->accept();

        if (wasPreviewActive && onPreviewFinished)
            onPreviewFinished();
        if (wasPreviewActive && onDropped)
            onDropped(nodeId, dropCenter);
    }

private:
    void createDragSnapshot(const QPointF &scenePosition)
    {
        if (!scene() || !m_sourceRect.isValid())
            return;

        QPixmap pixmap(m_sourceRect.size().toSize());
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        scene()->render(&painter, QRectF(QPointF(0.0, 0.0), m_sourceRect.size()), m_sourceRect);

        m_dragOffset = scenePosition - m_sourceRect.topLeft();
        m_dragSnapshot = scene()->addPixmap(pixmap);
        m_dragSnapshot->setOpacity(0.88);
        m_dragSnapshot->setZValue(120.0);
        m_dragOutline = scene()->addPath(dragFocusOutlinePath(m_label, QRectF(QPointF(0.0, 0.0), m_sourceRect.size())),
                                         QPen(QColor(74, 190, 116), 2, Qt::DashLine),
                                         Qt::NoBrush);
        m_dragOutline->setZValue(121.0);
        moveDragSnapshot(scenePosition);
    }

    void moveDragSnapshot(const QPointF &scenePosition)
    {
        const QPointF dragPos = scenePosition - m_dragOffset;
        if (m_dragSnapshot)
            m_dragSnapshot->setPos(dragPos);
        if (m_dragOutline)
            m_dragOutline->setPos(dragPos);
    }

    QPointF draggedRectCenter(const QPointF &scenePosition) const
    {
        return scenePosition - m_dragOffset + QPointF(m_previewSize.width() * 0.5,
                                                      m_previewSize.height() * 0.5);
    }

    int m_nodeId = 0;
    QString m_label;
    QRectF m_sourceRect;
    QPointF m_pressScenePos;
    QPointF m_dragOffset;
    QSizeF m_previewSize;
    bool m_previewActive = false;
    std::function<void(int)> m_onSelected;
    std::function<void(const QPointF &, const QSizeF &, const QString &)> m_onPreviewMoved;
    std::function<void()> m_onPreviewFinished;
    std::function<void(int, const QPointF &)> m_onDropped;
    QGraphicsPixmapItem *m_dragSnapshot = nullptr;
    QGraphicsPathItem *m_dragOutline = nullptr;
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

class PaletteToolItem : public QGraphicsItem
{
public:
    PaletteToolItem(const QString &label,
                    const QColor &fill,
                    std::function<void(const QPointF &, const QSizeF &, const QString &)> onPreviewMoved,
                    std::function<void()> onPreviewFinished,
                    std::function<void(const QString &, const QPointF &)> onDropped)
        : m_label(label)
        , m_fill(fill)
        , m_onPreviewMoved(onPreviewMoved)
        , m_onPreviewFinished(onPreviewFinished)
        , m_onDropped(onDropped)
    {
        setAcceptedMouseButtons(Qt::LeftButton);
        setZValue(100.0);
    }

    QRectF boundingRect() const override { return QRectF(0.0, 0.0, ToolSize, ToolSize); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(m_fill.darker(145)));
        painter->setBrush(Qt::white);
        painter->drawRect(boundingRect());

        const QRectF glyphRect(10.0, 8.0, 34.0, 34.0);
        SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
        if (operationForToolName(m_label, &operation))
            paintOperationIcon(painter, operation, glyphRect, m_fill.darker(125));
        else
            paintPrimitiveIcon(painter, primitiveTypeForTool(m_label), glyphRect);
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override { updatePreview(event); }
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override { updatePreview(event); }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override
    {
        const QPointF dropPosition = event->scenePos();
        const QString label = m_label;
        const auto onPreviewFinished = m_onPreviewFinished;
        const auto onDropped = m_onDropped;

        event->accept();

        if (onPreviewFinished)
            onPreviewFinished();
        if (onDropped)
            onDropped(label, dropPosition);
    }

private:
    void updatePreview(QGraphicsSceneMouseEvent *event)
    {
        if (m_onPreviewMoved)
            m_onPreviewMoved(event->scenePos(), previewSizeForTool(m_label), m_label);
        event->accept();
    }

    QString m_label;
    QColor m_fill;
    std::function<void(const QPointF &, const QSizeF &, const QString &)> m_onPreviewMoved;
    std::function<void()> m_onPreviewFinished;
    std::function<void(const QString &, const QPointF &)> m_onDropped;
};

QColor fillForTool(const QString &tool)
{
    SceneDocument::TreeNode::Operation operation;
    if (operationForToolName(tool, &operation))
        return operationVisual(operation).fill;
    return QColor(219, 231, 246);
}


QColor fillForOperation(SceneDocument::TreeNode::Operation operation)
{
    return operationVisual(operation).fill;
}

QGraphicsScene *createTreeGraphicsScene(QObject *parent)
{
    return new TreeGraphicsScene(parent);
}

QGraphicsItem *createTreeNodeDragHandleItem(int nodeId, const QString &label, const QRectF &rect, const QRectF &sourceRect, std::function<void(int)> onSelected, const QSizeF &previewSize, std::function<void(const QPointF &, const QSizeF &, const QString &)> onPreviewMoved, std::function<void()> onPreviewFinished, std::function<void(int, const QPointF &)> onDropped)
{
    return new TreeNodeDragHandleItem(nodeId, label, rect, sourceRect, onSelected, previewSize, onPreviewMoved, onPreviewFinished, onDropped);
}

QGraphicsItem *createTreeNodeSelectionItem(int nodeId, const QRectF &rect, qreal zValue, std::function<void(int)> onSelected)
{
    return new TreeNodeSelectionItem(nodeId, rect, zValue, onSelected);
}

QGraphicsItem *createPaletteToolItem(const QString &label, const QColor &fill, std::function<void(const QPointF &, const QSizeF &, const QString &)> onPreviewMoved, std::function<void()> onPreviewFinished, std::function<void(const QString &, const QPointF &)> onDropped)
{
    return new PaletteToolItem(label, fill, onPreviewMoved, onPreviewFinished, onDropped);
}

} // namespace SceneTreeGraphics
