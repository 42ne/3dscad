#include "scenetreenoderenderer.h"
#include "scenetreegraphicshelpers.h"

#include <QBrush>
#include <QFontMetricsF>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <utility>

using namespace SceneTreeGraphics;

namespace {

void paintRoundedPanel(QPainter *painter, const QRectF &rect, qreal radius, const QPen &pen, const QBrush &brush)
{
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    painter->setPen(pen);
    painter->setBrush(brush);
    painter->drawPath(path);
}

void paintLabel(QPainter *painter, const QString &text, const QPointF &position, const QColor &color)
{
    painter->setPen(color);
    painter->drawText(QRectF(position, QSizeF(220.0, 24.0)), Qt::AlignLeft | Qt::AlignTop, text);
}

void paintPillLabel(QPainter *painter, const QString &text, const QPointF &position, const QColor &accent)
{
    const QFontMetricsF metrics(painter->font());
    const QRectF pillRect(position, QSizeF(metrics.horizontalAdvance(text) + 12.0, metrics.height() + 4.0));
    paintRoundedPanel(painter, pillRect, 6.0, QPen(accent, 1), QBrush(QColor(255, 255, 255, 115)));
    paintLabel(painter, text, QPointF(pillRect.left() + 6.0, pillRect.top() + 1.0), accent.darker(135));
}

void paintPrimitiveIcon(QPainter *painter, ShapeNode::Type type, const QRectF &rect)
{
    const QColor outline(59, 95, 134);
    const QColor face(178, 207, 238);
    const QColor faceLight(221, 235, 248);
    const QColor faceDark(139, 176, 214);

    if (type == ShapeNode::Sphere) {
        painter->setPen(QPen(outline, 1));
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

void paintPrimitiveBadge(QPainter *painter, const QString &number, const QRectF &rect)
{
    const QRectF badgeRect(rect.center().x() + 12.0, rect.top() + 5.0, 18.0, 18.0);
    painter->setPen(QPen(QColor(82, 111, 146), 1));
    painter->setBrush(QColor(244, 248, 252));
    painter->drawEllipse(badgeRect);
    painter->setPen(QColor(30, 58, 90));
    painter->drawText(badgeRect, Qt::AlignCenter, number);
}

void paintOperationIcon(QPainter *painter,
                        SceneDocument::TreeNode::Operation operation,
                        const QRectF &rect,
                        const QColor &accent)
{
    paintRoundedPanel(painter, rect, 3.0, QPen(accent.darker(135), 1), QBrush(QColor(255, 255, 255, 135)));

    const QPointF center = rect.center();
    const QRectF symbolRect = rect.adjusted(4.0, 4.0, -4.0, -4.0);
    QPen pen(accent.darker(160), 2);
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    if (operation == SceneDocument::TreeNode::Union) {
        painter->drawLine(symbolRect.left(), center.y(), symbolRect.right(), center.y());
        painter->drawLine(center.x(), symbolRect.top(), center.x(), symbolRect.bottom());
        return;
    }

    if (operation == SceneDocument::TreeNode::Difference) {
        painter->drawLine(symbolRect.left(), center.y(), symbolRect.right(), center.y());
        return;
    }

    if (operation == SceneDocument::TreeNode::Intersection) {
        painter->setPen(QPen(accent.darker(150), 1));
        painter->setBrush(QColor(255, 255, 255, 60));
        painter->drawEllipse(QRectF(symbolRect.left(), symbolRect.top() + 1.0, symbolRect.width() * 0.62, symbolRect.height() - 2.0));
        painter->drawEllipse(QRectF(symbolRect.center().x() - symbolRect.width() * 0.31,
                                    symbolRect.top() + 1.0,
                                    symbolRect.width() * 0.62,
                                    symbolRect.height() - 2.0));
        return;
    }

    painter->setPen(accent.darker(160));
    painter->drawText(rect, Qt::AlignCenter, QStringLiteral("M"));
}

class PrimitiveNodeItem final : public QGraphicsItem
{
public:
    PrimitiveNodeItem(const QRectF &rect, ShapeNode::Type type, const QString &number, bool selected)
        : m_rect(rect)
        , m_type(type)
        , m_number(number)
        , m_selected(selected)
    {
        setZValue(5.0);
    }

    QRectF boundingRect() const override { return m_rect.adjusted(-6.0, -6.0, 6.0, 6.0); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QRectF iconRect(m_rect.left() + 20.0,
                              m_rect.top() + (PrimitiveHeight - PrimitiveIconSize) * 0.5,
                              PrimitiveIconSize,
                              PrimitiveIconSize);
        if (m_selected) {
            painter->setPen(QPen(QColor(255, 203, 87), 2, Qt::DashLine));
            painter->setBrush(QColor(255, 203, 87, 32));
            painter->drawEllipse(iconRect.adjusted(-5.0, -5.0, 5.0, 5.0));
        }
        paintPrimitiveIcon(painter, m_type, iconRect);
        paintPrimitiveBadge(painter, m_number, m_rect);
    }

private:
    QRectF m_rect;
    ShapeNode::Type m_type = ShapeNode::Cube;
    QString m_number;
    bool m_selected = false;
};

class GroupNodeItem final : public QGraphicsItem
{
public:
    GroupNodeItem(const SceneDocument::TreeNode &node, const QRectF &rect, int depth, qreal cutSeparatorY, bool selected)
        : m_rect(rect)
        , m_depth(depth)
        , m_cutSeparatorY(cutSeparatorY)
        , m_operation(node.operation)
        , m_empty(node.children.isEmpty())
        , m_selected(selected)
    {
        setZValue(zForDepth(-101.0));
    }

    QRectF boundingRect() const override { return m_rect.adjusted(-1.0, -1.0, 4.0, 4.0); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        const OperationVisual visual = operationVisual(m_operation);
        const QColor fill = visual.fill;

        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 38));
        painter->drawRect(m_rect.translated(2.0, 2.0));

        paintRoundedPanel(painter,
                          m_rect,
                          CornerRadius,
                          QPen(m_selected ? QColor(255, 203, 87) : fill.darker(145), m_selected ? 3 : 2),
                          QBrush(fill));

        const QRectF headerRect(m_rect.left() + 1.5,
                                m_rect.top() + 1.5,
                                m_rect.width() - 3.0,
                                GroupHeaderHeight - 2.0);
        paintRoundedPanel(painter, headerRect, CornerRadius - 1.0, Qt::NoPen, QBrush(fill.lighter(112)));

        const QRectF iconRect(m_rect.left() + 8.0, m_rect.top() + 6.0, 18.0, 18.0);
        paintOperationIcon(painter, m_operation, iconRect, fill.darker(125));
        paintLabel(painter, labelForOperation(m_operation), m_rect.topLeft() + QPointF(32.0, 7.0), QColor(24, 34, 44));

        if (m_empty) {
            paintLabel(painter,
                       QStringLiteral("empty"),
                       QPointF(m_rect.left() + GroupPadding, m_rect.top() + GroupHeaderHeight + GroupPadding + 10.0),
                       QColor(95, 98, 105));
        }

        if (m_operation != SceneDocument::TreeNode::Difference)
            return;

        painter->setPen(QPen(QColor(130, 92, 70), 1, Qt::DashLine));
        painter->drawLine(QPointF(m_rect.left() + GroupPadding, m_cutSeparatorY),
                          QPointF(m_rect.right() - GroupPadding, m_cutSeparatorY));
        paintPillLabel(painter, QStringLiteral("base"), QPointF(m_rect.right() - 61.0, m_rect.top() + GroupHeaderHeight + 7.0), QColor(128, 99, 73));
        paintPillLabel(painter, QStringLiteral("cut"), QPointF(m_rect.right() - 51.0, m_cutSeparatorY + 5.0), QColor(153, 85, 56));
    }

private:
    qreal zForDepth(qreal offset) const { return m_depth * 10.0 + offset; }

private:
    QRectF m_rect;
    int m_depth = 0;
    qreal m_cutSeparatorY = 0.0;
    SceneDocument::TreeNode::Operation m_operation = SceneDocument::TreeNode::Union;
    bool m_empty = false;
    bool m_selected = false;
};


QColor translucent(const QColor &color, int alpha)
{
    return QColor(color.red(), color.green(), color.blue(), alpha);
}

class PreviewPrimitiveItem final : public QGraphicsItem
{
public:
    PreviewPrimitiveItem(const QRectF &rect, ShapeNode::Type type)
        : m_rect(rect)
        , m_type(type)
    {
        setZValue(58.0);
    }

    QRectF boundingRect() const override { return m_rect.adjusted(-1.0, -1.0, 1.0, 1.0); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setOpacity(0.78);
        const QRectF iconRect(m_rect.left() + 20.0,
                              m_rect.top() + (PrimitiveHeight - PrimitiveIconSize) * 0.5,
                              PrimitiveIconSize,
                              PrimitiveIconSize);
        paintPrimitiveIcon(painter, m_type, iconRect);
    }

private:
    QRectF m_rect;
    ShapeNode::Type m_type = ShapeNode::Cube;
};

class PreviewGroupItem final : public QGraphicsItem
{
public:
    PreviewGroupItem(const QRectF &rect, SceneDocument::TreeNode::Operation operation, qreal cutSeparatorY, bool insertedPreview)
        : m_rect(rect)
        , m_operation(operation)
        , m_cutSeparatorY(cutSeparatorY)
        , m_insertedPreview(insertedPreview)
    {
        setZValue(insertedPreview ? 56.0 : 52.0);
    }

    QRectF boundingRect() const override { return m_rect.adjusted(-1.0, -1.0, 2.0, 2.0); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        const OperationVisual visual = operationVisual(m_operation);
        const QColor fill = visual.fill;

        paintRoundedPanel(painter,
                          m_rect,
                          CornerRadius,
                          QPen(fill.darker(145), 2),
                          QBrush(m_insertedPreview ? translucent(fill, 205) : fill));

        const QColor headerFill = m_insertedPreview ? translucent(fill.lighter(112), 210) : fill.lighter(112);
        const QRectF headerRect(m_rect.left() + 1.5,
                                m_rect.top() + 1.5,
                                m_rect.width() - 3.0,
                                GroupHeaderHeight - 2.0);
        paintRoundedPanel(painter, headerRect, CornerRadius - 1.0, Qt::NoPen, QBrush(headerFill));

        const QRectF iconRect(m_rect.left() + 8.0, m_rect.top() + 6.0, 18.0, 18.0);
        paintOperationIcon(painter, m_operation, iconRect, fill.darker(125));
        paintLabel(painter, labelForOperation(m_operation), m_rect.topLeft() + QPointF(32.0, 7.0), QColor(24, 34, 44));

        if (m_operation == SceneDocument::TreeNode::Difference && m_cutSeparatorY > 0.0) {
            painter->setPen(QPen(QColor(130, 92, 70), 1, Qt::DashLine));
            painter->drawLine(QPointF(m_rect.left() + GroupPadding, m_cutSeparatorY),
                              QPointF(m_rect.right() - GroupPadding, m_cutSeparatorY));
        }
    }

private:
    QRectF m_rect;
    SceneDocument::TreeNode::Operation m_operation = SceneDocument::TreeNode::Union;
    qreal m_cutSeparatorY = 0.0;
    bool m_insertedPreview = false;
};

} // namespace

SceneTreeNodeRenderer::SceneTreeNodeRenderer(QGraphicsScene *scene,
                                             int selectedNodeId,
                                             NodeSelectedCallback onSelected)
    : m_scene(scene)
    , m_selectedNodeId(selectedNodeId)
    , m_onSelected(std::move(onSelected))
{
}

void SceneTreeNodeRenderer::renderPrimitive(const SceneDocument::TreeNode &node,
                                            const QRectF &rect,
                                            const QString &label,
                                            ShapeNode::Type type)
{
    m_scene->addItem(new PrimitiveNodeItem(rect, type, primitiveNumberText(label, node.shapeId), node.id == m_selectedNodeId));
}

void SceneTreeNodeRenderer::renderGroup(const SceneDocument::TreeNode &node,
                                        const QRectF &rect,
                                        int depth,
                                        qreal cutSeparatorY)
{
    m_scene->addItem(new GroupNodeItem(node, rect, depth, cutSeparatorY, node.id == m_selectedNodeId));
    m_scene->addItem(createTreeNodeSelectionItem(node.id,
                                                 rect,
                                                 zForDepth(depth, -80.0),
                                                 m_onSelected));
}

qreal SceneTreeNodeRenderer::zForDepth(int depth, qreal offset) const
{
    return depth * 10.0 + offset;
}


void SceneTreeNodeRenderer::renderPreviewTool(QGraphicsScene *scene,
                                              QVector<QGraphicsItem *> *items,
                                              const QString &tool,
                                              const QRectF &rect)
{
    SceneDocument::TreeNode::Operation operation;
    QGraphicsItem *item = nullptr;
    if (operationForToolName(tool, &operation))
        item = new PreviewGroupItem(rect, operation, 0.0, true);
    else
        item = new PreviewPrimitiveItem(rect, primitiveTypeForTool(tool));

    scene->addItem(item);
    appendPreviewItem(items, item);
}

void SceneTreeNodeRenderer::renderPreviewGroup(QGraphicsScene *scene,
                                               QVector<QGraphicsItem *> *items,
                                               SceneDocument::TreeNode::Operation operation,
                                               const QRectF &rect,
                                               qreal cutSeparatorY)
{
    auto *item = new PreviewGroupItem(rect, operation, cutSeparatorY, false);
    scene->addItem(item);
    appendPreviewItem(items, item);
}
