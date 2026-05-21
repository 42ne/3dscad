#include "scenetreenoderenderer.h"
#include "scenetreegraphicshelpers.h"

#include <QBrush>
#include <QFontMetricsF>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QImage>
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
    if (!rect.isValid() || rect.height() < 18.0)
        return;

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
    painter->drawText(QRectF(position, QSizeF(220.0, 16.0)), Qt::AlignLeft | Qt::AlignVCenter, text);
}

QColor translucent(const QColor &color, int alpha)
{
    return QColor(color.red(), color.green(), color.blue(), alpha);
}

QRectF boundedVerticalLabelRect(qreal left, qreal top, qreal bottom, qreal width, qreal preferredHeight)
{
    if (bottom <= top)
        return QRectF();

    const qreal height = qMin(preferredHeight, bottom - top);
    if (height < 18.0)
        return QRectF();

    return QRectF(left, top + (bottom - top - height) * 0.5, width, height);
}

void paintPrimitiveBadge(QPainter *painter, const QString &number, const QRectF &iconRect)
{
    const QRectF badgeRect(iconRect.right() - 3.0, iconRect.top() + 1.0, 16.0, 16.0);
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
                      int activeParamIndex,
                      int activeNumberStart,
                      qreal opacity,
                      qreal zValue,
                      const QImage &thumbnail = QImage())
        : m_rect(rect)
        , m_shape(shape ? *shape : ShapeNode())
        , m_number(number)
        , m_selected(selected)
        , m_activeParamIndex(activeParamIndex)
        , m_activeNumberStart(activeNumberStart)
        , m_opacity(opacity)
        , m_thumbnail(thumbnail)
    {
        setZValue(zValue);
    }

    QRectF boundingRect() const override { return m_rect.adjusted(-6.0, -6.0, 6.0, 6.0); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setOpacity(m_opacity);
        painter->setFont(sceneTreeGraphicsFont());

        // Card border — covers only the icon area (left PrimitiveCardWidth pixels).
        const QRectF cardRect(m_rect.left(), m_rect.top(), PrimitiveCardWidth, m_rect.height());
        paintRoundedPanel(painter, cardRect, CornerRadius,
                          QPen(QColor(86, 117, 150), 1),
                          QBrush(QColor(219, 231, 246)));

        const qreal iconSize = PrimitiveIconSize - 2.0;
        const QRectF iconRect(m_rect.left() + 10.0,
                              m_rect.top() + (PrimitiveHeight - iconSize) * 0.5,
                              iconSize,
                              iconSize);
        if (m_selected) {
            painter->setPen(QPen(QColor(255, 203, 87), 2, Qt::DashLine));
            painter->setBrush(QColor(255, 203, 87, 32));
            painter->drawEllipse(iconRect.adjusted(-5.0, -5.0, 5.0, 5.0));
        }

        if (!m_thumbnail.isNull())
            painter->drawImage(iconRect, m_thumbnail);
        else
            paintPrimitiveIcon(painter, m_shape.type, iconRect);
        if (!m_number.isEmpty())
            paintPrimitiveBadge(painter, m_number, iconRect);

        // Parameters outside the card border.
        const QFontMetricsF metrics(painter->font());
        const QVector<ShapeParameterControl> controls = shapeParameterControls(m_shape);
        for (int i = 0; i < controls.size(); ++i) {
            const ShapeParameterControl &control = controls[i];
            const QRectF rowRect = shapeParameterControlRect(m_rect, i, controls.size());

            // Label "X =" in muted color.
            painter->setPen(QColor(58, 89, 125));
            painter->drawText(QRectF(rowRect.left(), rowRect.top(), 10.0, rowRect.height()),
                              Qt::AlignLeft | Qt::AlignVCenter, control.label);
            painter->setPen(QColor(104, 122, 148));
            painter->drawText(QRectF(rowRect.left() + 13.0, rowRect.top(), 10.0, rowRect.height()),
                              Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("="));

            // Expression spans with number highlights.
            const QRectF textRect(rowRect.left() + PrimitiveParamLabelArea,
                                  rowRect.top(),
                                  rowRect.right() - rowRect.left() - PrimitiveParamLabelArea,
                                  rowRect.height());
            const QVector<ExpressionTextSpan> spans = expressionSpansInTextRect(textRect, control.expression, metrics);

            for (const ExpressionTextSpan &span : spans) {
                if (!span.number)
                    continue;
                const bool active = (i == m_activeParamIndex) && (span.start == m_activeNumberStart);
                paintRoundedPanel(painter,
                                  span.rect,
                                  3.0,
                                  QPen(active ? QColor(220, 156, 26) : QColor(86, 117, 150), active ? 2 : 1),
                                  QBrush(active ? QColor(255, 220, 108, 205) : QColor(244, 248, 252, 190)));
            }

            painter->setPen(QColor(24, 34, 44));
            for (const ExpressionTextSpan &span : spans)
                painter->drawText(span.rect, Qt::AlignCenter, span.text);

            // Non-number text (variable names, operators).
            painter->setPen(QColor(58, 89, 125));
            for (const ExpressionTextSpan &span : spans) {
                if (!span.number)
                    painter->drawText(span.rect, Qt::AlignCenter, span.text);
            }
        }
    }

private:
    QRectF m_rect;
    ShapeNode m_shape;
    QString m_number;
    bool m_selected = false;
    int m_activeParamIndex = -1;
    int m_activeNumberStart = -1;
    qreal m_opacity = 1.0;
    QImage m_thumbnail;
};

class VariableCardItem final : public QGraphicsItem
{
public:
    VariableCardItem(const QRectF &rect,
                     const QString &name,
                     const QString &expression,
                     bool selected,
                     int activeNumberStart,
                     qreal opacity,
                     qreal zValue,
                     bool isParameter = false)
        : m_rect(rect)
        , m_name(name)
        , m_expression(expression)
        , m_selected(selected)
        , m_activeNumberStart(activeNumberStart)
        , m_opacity(opacity)
        , m_isParameter(isParameter)
    {
        setZValue(zValue);
    }

    QRectF boundingRect() const override { return m_rect.adjusted(-6.0, -6.0, 6.0, 6.0); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setOpacity(m_opacity);
        painter->setFont(sceneTreeGraphicsFont());

        // PAR uses indigo-blue palette; VAR uses amber palette.
        const QColor accent     = m_isParameter ? QColor(52, 88, 172)   : QColor(150, 116, 42);
        const QColor nameColor  = m_isParameter ? QColor(24, 36, 72)    : QColor(43, 37, 28);
        const QColor eqColor    = m_isParameter ? QColor(58, 80, 140)   : QColor(104, 83, 48);
        const QColor numBorder  = m_isParameter ? QColor(60, 100, 190)  : QColor(158, 126, 51);
        const QColor numBorderA = m_isParameter ? QColor(40, 120, 220)  : QColor(220, 156, 26);
        const QColor numFillA   = m_isParameter ? QColor(140, 190, 255, 205) : QColor(255, 220, 108, 205);
        const QString badgeLabel = m_isParameter ? QStringLiteral("PAR") : QStringLiteral("VAR");

        // Badge pill — vertically centered in VariableHeight
        const qreal badgeH = 13.0;
        const QRectF badgeRect(m_rect.left() + 6.0,
                               m_rect.top() + (VariableHeight - badgeH) * 0.5,
                               28.0,
                               badgeH);
        paintRoundedPanel(painter, badgeRect, 3.0, QPen(accent, 1.0), QBrush(QColor(255, 255, 255, 150)));
        {
            painter->save();
            QFont badgeFont = painter->font();
            badgeFont.setBold(true);
            badgeFont.setPointSizeF(qMax<qreal>(6.0, badgeFont.pointSizeF() - 2.0));
            painter->setFont(badgeFont);
            painter->setPen(accent.darker(130));
            painter->drawText(badgeRect, Qt::AlignCenter, badgeLabel);
            painter->restore();
        }

        // Name, = and expression
        const QFontMetricsF metrics(painter->font());
        const qreal nameW = metrics.horizontalAdvance(m_name);
        const QRectF textLineRect(m_rect.left() + 38.0,
                                  m_rect.top() + (VariableHeight - 16.0) * 0.5,
                                  nameW,
                                  16.0);

        painter->setPen(nameColor);
        painter->drawText(textLineRect,
                          Qt::AlignLeft | Qt::AlignVCenter,
                          m_name);

        painter->setPen(eqColor);
        painter->drawText(QRectF(textLineRect.right() + 4.0, textLineRect.top(), 12.0, textLineRect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("="));

        const QVector<ExpressionTextSpan> spans = expressionTextSpans(m_rect, m_expression, metrics, nameW);
        for (const ExpressionTextSpan &span : spans) {
            if (!span.number)
                continue;

            const bool active = span.start == m_activeNumberStart;
            paintRoundedPanel(painter,
                              span.rect,
                              4.0,
                              QPen(active ? numBorderA : numBorder, active ? 2 : 1),
                              QBrush(active ? numFillA : QColor(255, 255, 255, 110)));
        }

        painter->setPen(eqColor);
        for (const ExpressionTextSpan &span : spans)
            painter->drawText(span.rect, Qt::AlignCenter, span.text);
    }

private:
    QRectF m_rect;
    QString m_name;
    QString m_expression;
    bool m_selected = false;
    int m_activeNumberStart = -1;
    qreal m_opacity = 1.0;
    bool m_isParameter = false;
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
                  int activeTransformAxis = -1,
                  int activeTransformNumberStart = -1,
                  qreal transformHeaderWidth = TransformHeaderWidth,
                  const QStringList &transformExpressions = QStringList(),
                  const QString &loopVariable = QString(),
                  const QString &loopRangeExpression = QString(),
                  int activeForLoopNumberStart = -1)
        : m_rect(rect)
        , m_cutSeparatorY(cutSeparatorY)
        , m_transformValues(transformValues)
        , m_transformExpressions(transformExpressions)
        , m_transformHeaderWidth(transformHeaderWidth)
        , m_loopVariable(loopVariable)
        , m_loopRangeExpression(loopRangeExpression)
        , m_activeForLoopNumberStart(activeForLoopNumberStart)
        , m_activeTransformAxis(activeTransformAxis)
        , m_activeTransformNumberStart(activeTransformNumberStart)
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
        painter->setFont(sceneTreeGraphicsFont());
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
            const qreal baseBottom = m_cutSeparatorY - 4.0;
            const qreal cutTop = m_cutSeparatorY + 4.0;
            const qreal cutBottom = m_rect.bottom() - GroupPadding;
            paintVerticalPillLabel(painter,
                                   QStringLiteral("base"),
                                   boundedVerticalLabelRect(labelLeft, baseTop, baseBottom, labelWidth, labelHeight),
                                   QColor(128, 99, 73));
            paintVerticalPillLabel(painter,
                                   QStringLiteral("cut"),
                                   boundedVerticalLabelRect(labelLeft, cutTop, cutBottom, labelWidth, labelHeight),
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

        if (m_operation == SceneDocument::TreeNode::For) {
            const QString variableName = m_loopVariable.trimmed().isEmpty() ? QStringLiteral("i") : m_loopVariable.trimmed();
            const QString rangeExpression = m_loopRangeExpression.trimmed().isEmpty() ? QStringLiteral("[0 : 1 : 3]") : m_loopRangeExpression.trimmed();
            const QString prefix = QStringLiteral("for (%1 = ").arg(variableName);
            const QFontMetricsF metrics(painter->font());
            painter->setPen(QColor(24, 34, 44));
            painter->drawText(QRectF(m_rect.left() + 52.0, m_rect.top() + 7.0, metrics.horizontalAdvance(prefix), 16.0),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              prefix);

            const QVector<ExpressionTextSpan> spans = forLoopRangeTextSpans(m_rect, variableName, rangeExpression, metrics);
            for (const ExpressionTextSpan &span : spans) {
                if (span.number) {
                    const bool active = span.start == m_activeForLoopNumberStart;
                    paintRoundedPanel(painter,
                                      span.rect,
                                      3.0,
                                      QPen(active ? QColor(220, 156, 26) : fill.darker(125), active ? 2 : 1),
                                      QBrush(active ? QColor(255, 220, 108, 205) : QColor(255, 255, 255, 125)));
                }
                painter->setPen(span.number ? QColor(24, 34, 44) : QColor(80, 82, 64));
                painter->drawText(span.rect, Qt::AlignLeft | Qt::AlignVCenter, span.text);
            }
            const qreal suffixLeft = forLoopRangeTextRect(m_rect, variableName, metrics).left()
                                     + metrics.horizontalAdvance(rangeExpression);
            painter->setPen(QColor(24, 34, 44));
            painter->drawText(QRectF(suffixLeft, m_rect.top() + 7.0, 10.0, 16.0), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral(")"));
        } else {
            paintLabel(painter, labelForOperation(m_operation), m_rect.topLeft() + QPointF(52.0, 7.0), QColor(24, 34, 44));
        }
    }

    void paintVerticalHeader(QPainter *painter, const QColor &fill)
    {
        // Narrow icon-only border (like PrimitiveCardWidth for shapes)
        const QRectF iconBorderRect(m_rect.left() + 1.5,
                                    m_rect.top() + 1.5,
                                    TransformIconWidth - 2.0,
                                    m_rect.height() - 3.0);
        const QColor headerFill = m_insertedPreview ? translucent(fill.lighter(112), 210) : fill.lighter(112);
        paintRoundedPanel(painter, iconBorderRect, CornerRadius - 1.0, Qt::NoPen, QBrush(headerFill));

        const QRectF iconRect(m_rect.left() + 6.0, m_rect.top() + 7.0, PrimitiveIconSize - 6.0, PrimitiveIconSize - 6.0);
        paintOperationIcon(painter, m_operation, iconRect, fill.darker(125), 6.0);

        painter->setPen(QColor(24, 34, 44));
        painter->drawText(QRectF(iconRect.left(), iconRect.bottom() - 1.0, iconRect.width(), 14.0),
                          Qt::AlignCenter,
                          m_operation == SceneDocument::TreeNode::Translate
                              ? QStringLiteral("T")
                              : m_operation == SceneDocument::TreeNode::Rotate
                                    ? QStringLiteral("R")
                                    : QStringLiteral("S"));

        // X/Y/Z rows outside icon border
        const QFontMetricsF metrics(painter->font());
        QFont valueFont = painter->font();
        valueFont.setPointSizeF(qMax<qreal>(7.0, valueFont.pointSizeF() - 2.0));

        static const QString axisLabels[3] = {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
        for (int axis = 0; axis < 3; ++axis) {
            const QRectF rowRect = transformParameterControlRect(m_rect, axis, m_transformHeaderWidth);
            const bool rowActive = axis == m_activeTransformAxis;

            // Expression for this axis
            QString expr;
            if (axis < m_transformExpressions.size() && !m_transformExpressions[axis].isEmpty())
                expr = m_transformExpressions[axis];
            else {
                const float val = axis == 0 ? m_transformValues.x()
                                : axis == 1 ? m_transformValues.y()
                                            : m_transformValues.z();
                const int precision = m_operation == SceneDocument::TreeNode::Scale ? 1 : 0;
                expr = QString::number(val, 'f', precision);
            }

            // Label "X =" in muted color
            painter->setFont(valueFont);
            painter->setPen(fill.darker(160));
            painter->drawText(QRectF(rowRect.left(), rowRect.top(), TransformParamLabelArea - 2.0, rowRect.height()),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              axisLabels[axis] + QStringLiteral(" ="));

            // Expression text spans
            const QRectF textRect(rowRect.left() + TransformParamLabelArea,
                                  rowRect.top(),
                                  rowRect.width() - TransformParamLabelArea,
                                  rowRect.height());
            const QVector<ExpressionTextSpan> spans = expressionSpansInTextRect(textRect, expr, metrics);
            for (const ExpressionTextSpan &span : spans) {
                const bool numActive = rowActive && span.number && span.start == m_activeTransformNumberStart;
                if (span.number) {
                    paintRoundedPanel(painter,
                                      span.rect,
                                      3.0,
                                      QPen(numActive ? QColor(220, 156, 26) : fill.darker(125), numActive ? 2 : 1),
                                      QBrush(numActive ? QColor(255, 220, 108, 205) : QColor(255, 255, 255, 125)));
                }
                painter->setPen(span.number ? QColor(24, 34, 44) : QColor(80, 110, 160));
                painter->drawText(span.rect, Qt::AlignLeft | Qt::AlignVCenter, span.text);
            }
        }
    }

    QRectF m_rect;
    qreal m_cutSeparatorY = 0.0;
    QVector3D m_transformValues;
    QStringList m_transformExpressions;
    qreal m_transformHeaderWidth = TransformHeaderWidth;
    QString m_loopVariable;
    QString m_loopRangeExpression;
    int m_activeForLoopNumberStart = -1;
    int m_activeTransformAxis = -1;
    int m_activeTransformNumberStart = -1;
    SceneDocument::TreeNode::Operation m_operation = SceneDocument::TreeNode::Union;
    bool m_selected = false;
    bool m_empty = false;
    bool m_showShadow = false;
    bool m_showDifferenceLabels = false;
    bool m_insertedPreview = false;
};


class ModuleCallCardItem final : public QGraphicsItem
{
public:
    ModuleCallCardItem(const QRectF &rect,
                       const QString &moduleName,
                       const QVector<ModuleCallParam> &params,
                       bool selected,
                       int activeParamVarNodeId,
                       int activeNumberStart,
                       qreal opacity,
                       qreal zValue)
        : m_rect(rect)
        , m_moduleName(moduleName)
        , m_params(params)
        , m_selected(selected)
        , m_activeParamVarNodeId(activeParamVarNodeId)
        , m_activeNumberStart(activeNumberStart)
        , m_opacity(opacity)
    {
        setZValue(zValue);
    }

    QRectF boundingRect() const override { return m_rect.adjusted(-6.0, -6.0, 6.0, 6.0); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setOpacity(m_opacity);
        painter->setFont(sceneTreeGraphicsFont());

        const QColor accent(38, 108, 148);
        if (m_selected) {
            paintRoundedPanel(painter,
                              m_rect.adjusted(-3.0, -3.0, 3.0, 3.0),
                              5.0,
                              QPen(QColor(255, 193, 56), 2.2),
                              Qt::NoBrush);
        }

        // CALL badge
        const qreal badgeH = 13.0;
        const QRectF badgeRect(m_rect.left() + 6.0,
                               m_rect.top() + (VariableHeight - badgeH) * 0.5,
                               32.0,
                               badgeH);
        paintRoundedPanel(painter, badgeRect, 3.0, QPen(accent, 1.0), QBrush(QColor(255, 255, 255, 150)));
        {
            painter->save();
            QFont badgeFont = painter->font();
            badgeFont.setBold(true);
            badgeFont.setPointSizeF(qMax<qreal>(6.0, badgeFont.pointSizeF() - 2.0));
            painter->setFont(badgeFont);
            painter->setPen(accent.darker(130));
            painter->drawText(badgeRect, Qt::AlignCenter, QStringLiteral("CALL"));
            painter->restore();
        }

        const QFontMetricsF metrics(painter->font());
        const QColor nameColor = m_selected ? QColor(30, 90, 155) : QColor(32, 80, 118);
        const QColor punctColor(60, 60, 80);
        const QColor paramNameColor(70, 80, 60);

        qreal x = m_rect.left() + 42.0;
        const QString nameOpen = m_moduleName + QStringLiteral("(");
        painter->setPen(nameColor);
        painter->drawText(QRectF(x, m_rect.top(), metrics.horizontalAdvance(nameOpen), VariableHeight),
                          Qt::AlignLeft | Qt::AlignVCenter, nameOpen);
        x += metrics.horizontalAdvance(nameOpen);

        if (m_params.isEmpty()) {
            painter->setPen(punctColor);
            painter->drawText(QRectF(x, m_rect.top(), 10.0, VariableHeight),
                              Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral(")"));
        } else {
            for (int i = 0; i < m_params.size(); ++i) {
                const QString nameEq = m_params[i].name + QStringLiteral(" = ");
                painter->setPen(paramNameColor);
                painter->drawText(QRectF(x, m_rect.top(), metrics.horizontalAdvance(nameEq), VariableHeight),
                                  Qt::AlignLeft | Qt::AlignVCenter, nameEq);
                x += metrics.horizontalAdvance(nameEq);

                const QRectF exprRect(x, m_rect.top(), m_rect.right() - x, VariableHeight);
                const QVector<ExpressionTextSpan> spans =
                    expressionSpansInTextRect(exprRect, m_params[i].expression, metrics);
                for (const ExpressionTextSpan &span : spans) {
                    if (span.number) {
                        const bool active = m_params[i].varNodeId == m_activeParamVarNodeId
                                            && span.start == m_activeNumberStart;
                        paintRoundedPanel(painter, span.rect, 4.0,
                                          QPen(active ? QColor(220, 156, 26) : accent.darker(125), active ? 2 : 1),
                                          QBrush(active ? QColor(255, 220, 108, 205) : QColor(255, 255, 255, 110)));
                    }
                }
                painter->setPen(QColor(24, 60, 95));
                for (const ExpressionTextSpan &span : spans)
                    painter->drawText(span.rect, Qt::AlignCenter, span.text);

                x += metrics.horizontalAdvance(m_params[i].expression);

                if (i < m_params.size() - 1) {
                    const QString sep = QStringLiteral(", ");
                    painter->setPen(punctColor);
                    painter->drawText(QRectF(x, m_rect.top(), metrics.horizontalAdvance(sep), VariableHeight),
                                      Qt::AlignLeft | Qt::AlignVCenter, sep);
                    x += metrics.horizontalAdvance(sep);
                }
            }
            painter->setPen(punctColor);
            painter->drawText(QRectF(x, m_rect.top(), 10.0, VariableHeight),
                              Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral(")"));
        }
    }

private:
    QRectF m_rect;
    QString m_moduleName;
    QVector<ModuleCallParam> m_params;
    bool m_selected = false;
    int m_activeParamVarNodeId = 0;
    int m_activeNumberStart = -1;
    qreal m_opacity = 1.0;
};

} // namespace

SceneTreeNodeRenderer::SceneTreeNodeRenderer(QGraphicsScene *scene,
                                             int selectedNodeId,
                                             NodeSelectedCallback onSelected,
                                             int activeTransformNodeId,
                                             int activeTransformAxis,
                                             int activeTransformNumberStart,
                                             int activeShapeNodeId,
                                             int activeShapeParameter,
                                             int activeShapeParamNumberStart,
                                             int activeVariableNodeId,
                                             int activeVariableNumberStart,
                                             int activeForLoopNodeId,
                                             int activeForLoopNumberStart,
                                             int activeModuleCallNodeId,
                                             int activeModuleCallVarNodeId,
                                             int activeModuleCallNumberStart)
    : m_scene(scene)
    , m_selectedNodeId(selectedNodeId)
    , m_activeTransformNodeId(activeTransformNodeId)
    , m_activeTransformAxis(activeTransformAxis)
    , m_activeTransformNumberStart(activeTransformNumberStart)
    , m_activeShapeNodeId(activeShapeNodeId)
    , m_activeShapeParameter(activeShapeParameter)
    , m_activeShapeParamNumberStart(activeShapeParamNumberStart)
    , m_activeVariableNodeId(activeVariableNodeId)
    , m_activeVariableNumberStart(activeVariableNumberStart)
    , m_activeForLoopNodeId(activeForLoopNodeId)
    , m_activeForLoopNumberStart(activeForLoopNumberStart)
    , m_activeModuleCallNodeId(activeModuleCallNodeId)
    , m_activeModuleCallVarNodeId(activeModuleCallVarNodeId)
    , m_activeModuleCallNumberStart(activeModuleCallNumberStart)
    , m_onSelected(std::move(onSelected))
{
}

void SceneTreeNodeRenderer::renderPrimitive(const SceneDocument::TreeNode &node,
                                            const QRectF &rect,
                                            const QString &label,
                                            const ShapeNode *shape,
                                            const QImage &thumbnail)
{
    const int activeParamIndex = node.id == m_activeShapeNodeId ? m_activeShapeParameter : -1;
    const int activeNumberStart = node.id == m_activeShapeNodeId ? m_activeShapeParamNumberStart : -1;
    m_scene->addItem(new PrimitiveCardItem(rect, shape, primitiveNumberText(label, node.shapeId), node.id == m_selectedNodeId, activeParamIndex, activeNumberStart, 1.0, 5.0, thumbnail));
}

void SceneTreeNodeRenderer::renderVariable(const SceneDocument::TreeNode &node, const QRectF &rect)
{
    const int activeNumberStart = node.id == m_activeVariableNodeId ? m_activeVariableNumberStart : -1;
    m_scene->addItem(new VariableCardItem(rect, node.variableName, node.variableExpression, node.id == m_selectedNodeId, activeNumberStart, 1.0, 5.0, node.isParameter));
}

void SceneTreeNodeRenderer::renderModuleCall(const SceneDocument::TreeNode &node,
                                             const QRectF &rect,
                                             const QVector<SceneTreeGraphics::ModuleCallParam> &params)
{
    const int activeVarNodeId = node.id == m_activeModuleCallNodeId ? m_activeModuleCallVarNodeId : 0;
    const int activeNumStart = node.id == m_activeModuleCallNodeId ? m_activeModuleCallNumberStart : -1;
    m_scene->addItem(new ModuleCallCardItem(rect, node.moduleName, params, node.id == m_selectedNodeId, activeVarNodeId, activeNumStart, 1.0, 5.0));
    m_scene->addItem(createTreeNodeSelectionItem(node.id, rect, 5.0, m_onSelected));
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
                                                : node.operation == SceneDocument::TreeNode::Scale
                                                      ? node.scale
                                                      : QVector3D();
    const int activeAxis = node.id == m_activeTransformNodeId ? m_activeTransformAxis : -1;
    const int activeNumberStart = node.id == m_activeTransformNodeId ? m_activeTransformNumberStart : -1;
    const int activeForLoopStart = node.id == m_activeForLoopNodeId ? m_activeForLoopNumberStart : -1;
    const qreal transformHeaderWidth = transformHeaderWidthForNode(node);
    const bool showEmptyText = node.operation != SceneDocument::TreeNode::Module
                               && node.operation != SceneDocument::TreeNode::Scene
                               && node.children.isEmpty();
    m_scene->addItem(new GroupCardItem(rect, node.operation, cutSeparatorY, zForDepth(depth, -101.0), node.id == m_selectedNodeId, showEmptyText, true, true, false, transformValues, activeAxis, activeNumberStart, transformHeaderWidth, node.transformExpressions, node.loopVariable, node.loopRangeExpression, activeForLoopStart));
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
    if (tool == QStringLiteral("par")) {
        item = new VariableCardItem(rect, QStringLiteral("par"), QStringLiteral("0"), false, -1, 0.78, 58.0, true);
    } else if (isVariableToolName(tool)) {
        item = new VariableCardItem(rect, QStringLiteral("var"), QStringLiteral("0"), false, -1, 0.78, 58.0);
    } else if (tool == QStringLiteral("call")) {
        item = new ModuleCallCardItem(rect, QStringLiteral("call"), {}, false, 0, -1, 0.78, 58.0);
    } else if (operationForToolName(tool, &operation)) {
        item = new GroupCardItem(rect, operation, 0.0, 56.0, false, false, false, false, true);
    } else {
        ShapeNode shape;
        shape.type = primitiveTypeForTool(tool);
        item = new PrimitiveCardItem(rect, &shape, QString(), false, -1, -1, 0.78, 58.0);
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
