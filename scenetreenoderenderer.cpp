#include "scenetreenoderenderer.h"
#include "scenetreegraphicshelpers.h"

#include <QBrush>
#include <QFontMetricsF>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
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

QColor translucent(const QColor &color, int alpha)
{
    return QColor(color.red(), color.green(), color.blue(), alpha);
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

class PrimitiveCardItem final : public QGraphicsItem
{
public:
    PrimitiveCardItem(const QRectF &rect,
                      ShapeNode::Type type,
                      const QString &number,
                      bool selected,
                      qreal opacity,
                      qreal zValue)
        : m_rect(rect)
        , m_type(type)
        , m_number(number)
        , m_selected(selected)
        , m_opacity(opacity)
    {
        setZValue(zValue);
    }

    QRectF boundingRect() const override { return m_rect.adjusted(-6.0, -6.0, 6.0, 6.0); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setOpacity(m_opacity);

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
        if (!m_number.isEmpty())
            paintPrimitiveBadge(painter, m_number, m_rect);
    }

private:
    QRectF m_rect;
    ShapeNode::Type m_type = ShapeNode::Cube;
    QString m_number;
    bool m_selected = false;
    qreal m_opacity = 1.0;
};

class GroupCardItem final : public QGraphicsItem
{
public:
    GroupCardItem(const QRectF &rect,
                  SceneDocument::TreeNode::Operation operation,
                  qreal cutSeparatorY,
                  qreal zValue,
                  bool selected,
                  bool empty,
                  bool showShadow,
                  bool showDifferenceLabels,
                  bool insertedPreview)
        : m_rect(rect)
        , m_cutSeparatorY(cutSeparatorY)
        , m_operation(operation)
        , m_selected(selected)
        , m_empty(empty)
        , m_showShadow(showShadow)
        , m_showDifferenceLabels(showDifferenceLabels)
        , m_insertedPreview(insertedPreview)
    {
        setZValue(zValue);
    }

    QRectF boundingRect() const override { return m_rect.adjusted(-1.0, -1.0, 4.0, 4.0); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QColor fill = operationVisual(m_operation).fill;

        if (m_showShadow) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0, 0, 0, 38));
            painter->drawRect(m_rect.translated(2.0, 2.0));
        }

        const QColor bodyFill = m_insertedPreview ? translucent(fill, 205) : fill;
        paintRoundedPanel(painter,
                          m_rect,
                          CornerRadius,
                          QPen(m_selected ? QColor(255, 203, 87) : fill.darker(145), m_selected ? 3 : 2),
                          QBrush(bodyFill));

        const QRectF headerRect(m_rect.left() + 1.5,
                                m_rect.top() + 1.5,
                                m_rect.width() - 3.0,
                                GroupHeaderHeight - 2.0);
        const QColor headerFill = m_insertedPreview ? translucent(fill.lighter(112), 210) : fill.lighter(112);
        paintRoundedPanel(painter, headerRect, CornerRadius - 1.0, Qt::NoPen, QBrush(headerFill));

        const QRectF iconRect(m_rect.left() + 8.0, m_rect.top() + 6.0, PrimitiveIconSize, PrimitiveIconSize);
        paintOperationIcon(painter, m_operation, iconRect, fill.darker(125));
        paintLabel(painter, labelForOperation(m_operation), m_rect.topLeft() + QPointF(52.0, 7.0), QColor(24, 34, 44));

        if (m_empty) {
            paintLabel(painter,
                       QStringLiteral("empty"),
                       QPointF(m_rect.left() + GroupPadding + PrimitiveIconSize + 8.0, m_rect.top() + GroupHeaderHeight + GroupPadding + 10.0),
                       QColor(95, 98, 105));
        }

        if (m_operation != SceneDocument::TreeNode::Difference || m_cutSeparatorY <= 0.0)
            return;

        painter->setPen(QPen(QColor(130, 92, 70), 1, Qt::DashLine));
        painter->drawLine(QPointF(m_rect.left() + GroupPadding, m_cutSeparatorY),
                          QPointF(m_rect.right() - GroupPadding, m_cutSeparatorY));

        if (m_showDifferenceLabels) {
            paintPillLabel(painter, QStringLiteral("base"), QPointF(m_rect.right() - 61.0, m_rect.top() + GroupHeaderHeight + 7.0), QColor(128, 99, 73));
            paintPillLabel(painter, QStringLiteral("cut"), QPointF(m_rect.right() - 51.0, m_cutSeparatorY + 5.0), QColor(153, 85, 56));
        }
    }

private:
    QRectF m_rect;
    qreal m_cutSeparatorY = 0.0;
    SceneDocument::TreeNode::Operation m_operation = SceneDocument::TreeNode::Union;
    bool m_selected = false;
    bool m_empty = false;
    bool m_showShadow = false;
    bool m_showDifferenceLabels = false;
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
    m_scene->addItem(new PrimitiveCardItem(rect, type, primitiveNumberText(label, node.shapeId), node.id == m_selectedNodeId, 1.0, 5.0));
}

void SceneTreeNodeRenderer::renderGroup(const SceneDocument::TreeNode &node,
                                        const QRectF &rect,
                                        int depth,
                                        qreal cutSeparatorY)
{
    m_scene->addItem(new GroupCardItem(rect, node.operation, cutSeparatorY, zForDepth(depth, -101.0), node.id == m_selectedNodeId, node.children.isEmpty(), true, true, false));
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
        item = new GroupCardItem(rect, operation, 0.0, 56.0, false, false, false, false, true);
    else
        item = new PrimitiveCardItem(rect, primitiveTypeForTool(tool), QString(), false, 0.78, 58.0);

    scene->addItem(item);
    appendPreviewItem(items, item);
}

void SceneTreeNodeRenderer::renderPreviewGroup(QGraphicsScene *scene,
                                               QVector<QGraphicsItem *> *items,
                                               SceneDocument::TreeNode::Operation operation,
                                               const QRectF &rect,
                                               qreal cutSeparatorY)
{
    auto *item = new GroupCardItem(rect, operation, cutSeparatorY, 52.0, false, false, false, false, false);
    scene->addItem(item);
    appendPreviewItem(items, item);
}
