#include "scenetreenoderenderer.h"
#include "scenetreegraphicshelpers.h"

#include <QBrush>
#include <QFontMetricsF>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QVector3D>
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

void paintVerticalPillLabel(QPainter *painter, const QString &text, const QRectF &rect, const QColor &accent)
{
    painter->save();
    paintRoundedPanel(painter, rect, 6.0, QPen(accent, 1), QBrush(QColor(255, 255, 255, 115)));
    painter->translate(rect.center());
    painter->rotate(-90.0);
    painter->setPen(accent.darker(135));
    painter->drawText(QRectF(-rect.height() * 0.5, -rect.width() * 0.5, rect.height(), rect.width()),
                      Qt::AlignCenter,
                      text);
    painter->restore();
}

void paintLabel(QPainter *painter, const QString &text, const QPointF &position, const QColor &color)
{
    painter->setPen(color);
    painter->drawText(QRectF(position, QSizeF(220.0, 24.0)), Qt::AlignLeft | Qt::AlignTop, text);
}

QColor translucent(const QColor &color, int alpha)
{
    return QColor(color.red(), color.green(), color.blue(), alpha);
}

void paintPrimitiveBadge(QPainter *painter, const QString &number, const QRectF &iconRect)
{
    const QRectF badgeRect(iconRect.right() - 4.0, iconRect.top() + 1.0, 18.0, 18.0);
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
                      const ShapeNode *shape,
                      const QString &number,
                      bool selected,
                      int activeParameter,
                      qreal opacity,
                      qreal zValue)
        : m_rect(rect)
        , m_shape(shape ? *shape : ShapeNode())
        , m_number(number)
        , m_selected(selected)
        , m_activeParameter(activeParameter)
        , m_opacity(opacity)
    {
        setZValue(zValue);
    }

    QRectF boundingRect() const override { return m_rect.adjusted(-6.0, -6.0, 6.0, 6.0); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setOpacity(m_opacity);

        const QRectF iconRect(m_rect.left() + 10.0,
                              m_rect.top() + (PrimitiveHeight - PrimitiveIconSize) * 0.5,
                              PrimitiveIconSize,
                              PrimitiveIconSize);
        if (m_selected) {
            painter->setPen(QPen(QColor(255, 203, 87), 2, Qt::DashLine));
            painter->setBrush(QColor(255, 203, 87, 32));
            painter->drawEllipse(iconRect.adjusted(-5.0, -5.0, 5.0, 5.0));
        }

        paintPrimitiveIcon(painter, m_shape.type, iconRect);
        if (!m_number.isEmpty())
            paintPrimitiveBadge(painter, m_number, iconRect);

        const QVector<ShapeParameterControl> controls = shapeParameterControls(m_shape);
        for (int i = 0; i < controls.size(); ++i) {
            const ShapeParameterControl &control = controls[i];
            const QRectF controlRect = shapeParameterControlRect(m_rect, i, controls.size());
            const bool active = i == m_activeParameter;
            paintRoundedPanel(painter,
                              controlRect,
                              3.0,
                              QPen(active ? QColor(220, 156, 26) : QColor(86, 117, 150), active ? 2 : 1),
                              QBrush(active ? QColor(255, 220, 108, 205) : QColor(244, 248, 252, 190)));
            painter->setPen(QColor(58, 89, 125));
            painter->drawText(QRectF(controlRect.left() + 4.0, controlRect.top(), 14.0, controlRect.height()),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              control.label);
            painter->setPen(QColor(24, 34, 44));
            painter->drawText(QRectF(controlRect.left() + 20.0, controlRect.top(), controlRect.width() - 24.0, controlRect.height()),
                              Qt::AlignRight | Qt::AlignVCenter,
                              QString::number(control.value, 'f', 0));
        }
    }

private:
    QRectF m_rect;
    ShapeNode m_shape;
    QString m_number;
    bool m_selected = false;
    int m_activeParameter = -1;
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
                  bool insertedPreview,
                  const QVector3D &transformValues = QVector3D(),
                  int activeTransformAxis = -1)
        : m_rect(rect)
        , m_cutSeparatorY(cutSeparatorY)
        , m_transformValues(transformValues)
        , m_activeTransformAxis(activeTransformAxis)
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

        if (isTransformOperation(m_operation)) {
            paintVerticalHeader(painter, fill);
        } else {
            paintHorizontalHeader(painter, fill);
        }

        if (m_empty) {
            const QPointF emptyPosition = isTransformOperation(m_operation)
                                              ? QPointF(m_rect.left() + TransformHeaderWidth + GroupPadding + PrimitiveIconSize + 8.0,
                                                        m_rect.top() + GroupPadding + 10.0)
                                              : QPointF(m_rect.left() + GroupPadding + PrimitiveIconSize + 8.0,
                                                        m_rect.top() + GroupHeaderHeight + GroupPadding + 10.0);
            paintLabel(painter, QStringLiteral("empty"), emptyPosition, QColor(95, 98, 105));
        }

        if (m_operation != SceneDocument::TreeNode::Difference || m_cutSeparatorY <= 0.0)
            return;

        painter->setPen(QPen(QColor(130, 92, 70), 1, Qt::DashLine));
        painter->drawLine(QPointF(m_rect.left() + GroupPadding, m_cutSeparatorY),
                          QPointF(m_rect.right() - GroupPadding, m_cutSeparatorY));

        if (m_showDifferenceLabels) {
            const qreal labelWidth = 20.0;
            const qreal labelHeight = 42.0;
            const qreal labelLeft = m_rect.left() + GroupPadding * 0.5;
            const qreal baseTop = m_rect.top() + GroupHeaderHeight + GroupPadding;
            const qreal baseHeight = qMax<qreal>(labelHeight, m_cutSeparatorY - baseTop - 4.0);
            const qreal cutTop = m_cutSeparatorY + 4.0;
            const qreal cutHeight = qMax<qreal>(labelHeight, m_rect.bottom() - GroupPadding - cutTop);
            paintVerticalPillLabel(painter,
                                   QStringLiteral("base"),
                                   QRectF(labelLeft, baseTop, labelWidth, qMin(labelHeight, baseHeight)),
                                   QColor(128, 99, 73));
            paintVerticalPillLabel(painter,
                                   QStringLiteral("cut"),
                                   QRectF(labelLeft, cutTop, labelWidth, qMin(labelHeight, cutHeight)),
                                   QColor(153, 85, 56));
        }
    }

private:
    void paintHorizontalHeader(QPainter *painter, const QColor &fill)
    {
        const QRectF headerRect(m_rect.left() + 1.5,
                                m_rect.top() + 1.5,
                                m_rect.width() - 3.0,
                                GroupHeaderHeight - 2.0);
        const QColor headerFill = m_insertedPreview ? translucent(fill.lighter(112), 210) : fill.lighter(112);
        paintRoundedPanel(painter, headerRect, CornerRadius - 1.0, Qt::NoPen, QBrush(headerFill));

        const QRectF iconRect(m_rect.left() + 8.0, m_rect.top() + 6.0, PrimitiveIconSize, PrimitiveIconSize);
        paintOperationIcon(painter, m_operation, iconRect, fill.darker(125));
        paintLabel(painter, labelForOperation(m_operation), m_rect.topLeft() + QPointF(52.0, 7.0), QColor(24, 34, 44));
    }

    void paintVerticalHeader(QPainter *painter, const QColor &fill)
    {
        const QRectF headerRect(m_rect.left() + 1.5,
                                m_rect.top() + 1.5,
                                TransformHeaderWidth - 2.0,
                                m_rect.height() - 3.0);
        const QColor headerFill = m_insertedPreview ? translucent(fill.lighter(112), 210) : fill.lighter(112);
        paintRoundedPanel(painter, headerRect, CornerRadius - 1.0, Qt::NoPen, QBrush(headerFill));

        const QRectF iconRect(m_rect.left() + 6.0, m_rect.top() + 7.0, PrimitiveIconSize - 6.0, PrimitiveIconSize - 6.0);
        paintOperationIcon(painter, m_operation, iconRect, fill.darker(125), 6.0);

        painter->setPen(QColor(24, 34, 44));
        painter->drawText(QRectF(iconRect.left(), iconRect.bottom() - 1.0, iconRect.width(), 14.0),
                          Qt::AlignCenter,
                          m_operation == SceneDocument::TreeNode::Translate ? QStringLiteral("T") : QStringLiteral("R"));

        const QVector<QPair<QString, qreal>> rows = {
            {QStringLiteral("X"), m_transformValues.x()},
            {QStringLiteral("Y"), m_transformValues.y()},
            {QStringLiteral("Z"), m_transformValues.z()}
        };
        QFont valueFont = painter->font();
        valueFont.setPointSizeF(qMax<qreal>(7.0, valueFont.pointSizeF() - 2.0));
        painter->setFont(valueFont);

        qreal rowTop = m_rect.top() + 8.0;
        for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            const auto &row = rows[rowIndex];
            const QRectF rowRect(m_rect.left() + 38.0, rowTop, TransformHeaderWidth - 44.0, 11.0);
            const bool active = rowIndex == m_activeTransformAxis;
            paintRoundedPanel(painter,
                              rowRect,
                              4.0,
                              QPen(active ? QColor(220, 156, 26) : fill.darker(125), active ? 2 : 1),
                              QBrush(active ? QColor(255, 220, 108, 205) : QColor(255, 255, 255, 125)));

            painter->setPen(fill.darker(170));
            painter->drawText(QRectF(rowRect.left() + 3.0, rowRect.top(), 9.0, rowRect.height()),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              row.first);
            painter->setPen(QColor(24, 34, 44));
            painter->drawText(QRectF(rowRect.left() + 12.0, rowRect.top(), rowRect.width() - 15.0, rowRect.height()),
                              Qt::AlignRight | Qt::AlignVCenter,
                              QString::number(row.second, 'f', 0));
            rowTop += 13.0;
        }
    }

    QRectF m_rect;
    qreal m_cutSeparatorY = 0.0;
    QVector3D m_transformValues;
    int m_activeTransformAxis = -1;
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
                                             NodeSelectedCallback onSelected,
                                             int activeTransformNodeId,
                                             int activeTransformAxis,
                                             int activeShapeNodeId,
                                             int activeShapeParameter)
    : m_scene(scene)
    , m_selectedNodeId(selectedNodeId)
    , m_activeTransformNodeId(activeTransformNodeId)
    , m_activeTransformAxis(activeTransformAxis)
    , m_activeShapeNodeId(activeShapeNodeId)
    , m_activeShapeParameter(activeShapeParameter)
    , m_onSelected(std::move(onSelected))
{
}

void SceneTreeNodeRenderer::renderPrimitive(const SceneDocument::TreeNode &node,
                                            const QRectF &rect,
                                            const QString &label,
                                            const ShapeNode *shape)
{
    const int activeParameter = node.id == m_activeShapeNodeId ? m_activeShapeParameter : -1;
    m_scene->addItem(new PrimitiveCardItem(rect, shape, primitiveNumberText(label, node.shapeId), node.id == m_selectedNodeId, activeParameter, 1.0, 5.0));
}

void SceneTreeNodeRenderer::renderGroup(const SceneDocument::TreeNode &node,
                                        const QRectF &rect,
                                        int depth,
                                        qreal cutSeparatorY)
{
    const QVector3D transformValues = node.operation == SceneDocument::TreeNode::Translate
                                          ? node.position
                                          : node.operation == SceneDocument::TreeNode::Rotate
                                                ? node.rotation
                                                : QVector3D();
    const int activeAxis = node.id == m_activeTransformNodeId ? m_activeTransformAxis : -1;
    m_scene->addItem(new GroupCardItem(rect, node.operation, cutSeparatorY, zForDepth(depth, -101.0), node.id == m_selectedNodeId, node.children.isEmpty(), true, true, false, transformValues, activeAxis));
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
    if (operationForToolName(tool, &operation)) {
        item = new GroupCardItem(rect, operation, 0.0, 56.0, false, false, false, false, true);
    } else {
        ShapeNode shape;
        shape.type = primitiveTypeForTool(tool);
        item = new PrimitiveCardItem(rect, &shape, QString(), false, -1, 0.78, 58.0);
    }

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
