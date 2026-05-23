#include "scenetreenoderenderer.h"
#include "scenetreegraphicshelpers.h"
#include "scenetreepalette.h"

#include <QBrush>
#include <QFontMetricsF>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QImage>
#include <QLinearGradient>
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
    const QRectF badgeRect(iconRect.right() - 2.0, iconRect.top(), 15.0, 15.0);
    painter->setPen(QPen(QColor(82, 111, 146), 1));
    painter->setBrush(QColor(244, 248, 252));
    painter->drawEllipse(badgeRect);
    painter->setPen(QColor(30, 58, 90));
    painter->drawText(badgeRect, Qt::AlignCenter, number);
}

void paintHeaderGripDots(QPainter *painter, const QRectF &rect, const QColor &color)
{
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    const qreal dotR = 1.35;
    const qreal gap = 5.0;
    const QPointF center = rect.center();
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 2; ++col) {
            const QPointF dot(center.x() - gap * 0.5 + col * gap,
                              center.y() - gap + row * gap);
            painter->drawEllipse(dot, dotR, dotR);
        }
    }
}

void paintHeaderChevron(QPainter *painter, const QRectF &headerRect, const QColor &color)
{
    const QPointF center(headerRect.right() - 18.0, headerRect.center().y() + 1.0);
    QPainterPath path;
    path.moveTo(center.x() - 4.5, center.y() - 2.0);
    path.lineTo(center.x(), center.y() + 2.5);
    path.lineTo(center.x() + 4.5, center.y() - 2.0);
    painter->setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);
}

void paintGlossBadge(QPainter *painter,
                     const QRectF &rect,
                     const QString &text,
                     const QColor &top,
                     const QColor &bottom,
                     const QColor &textColor,
                     qreal radius = 4.0)
{
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 36));
    painter->drawRoundedRect(rect.translated(1.0, 1.0), radius, radius);

    QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
    gradient.setColorAt(0.0, top);
    gradient.setColorAt(1.0, bottom);
    paintRoundedPanel(painter, rect, radius, QPen(top.lighter(120), 1.0), QBrush(gradient));

    painter->save();
    QFont badgeFont = painter->font();
    badgeFont.setBold(true);
    badgeFont.setPointSizeF(qMax<qreal>(6.5, badgeFont.pointSizeF() - 1.5));
    painter->setFont(badgeFont);
    painter->setPen(textColor);
    painter->drawText(rect, Qt::AlignCenter, text);
    painter->restore();
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

        const qreal iconSize = PrimitiveIconSize;
        const QRectF iconRect(m_rect.left() + 8.0,
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
                     bool isParameter = false,
                     int depth = 0,
                     int theme = 0)
        : m_rect(rect)
        , m_name(name)
        , m_expression(expression)
        , m_selected(selected)
        , m_activeNumberStart(activeNumberStart)
        , m_opacity(opacity)
        , m_isParameter(isParameter)
        , m_depth(depth)
        , m_theme(theme)
    {
        Q_UNUSED(m_depth);  // reserved for future depth-tinted variable rows
        setZValue(zValue);
    }

    QRectF boundingRect() const override { return m_rect.adjusted(-6.0, -6.0, 6.0, 6.0); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setOpacity(m_opacity);
        painter->setFont(sceneTreeGraphicsFont());

        const auto pt = static_cast<SceneTreePalette::Theme>(m_theme);
        const bool dark = SceneTreePalette::isDarkTheme(pt);

        // Theme-aware accent / text colours.
        const QColor nameColor = dark
            ? (m_isParameter ? QColor(200, 215, 245) : QColor(235, 210, 160))
            : (m_isParameter ? QColor( 24,  36,  72) : QColor( 43,  37,  28));
        const QColor eqColor = dark
            ? (m_isParameter ? QColor(160, 185, 230) : QColor(200, 170, 100))
            : (m_isParameter ? QColor( 58,  80, 140) : QColor(104,  83,  48));
        const QColor numBorder = dark
            ? (m_isParameter ? QColor(120, 160, 230) : QColor(200, 160,  70))
            : (m_isParameter ? QColor( 60, 100, 190) : QColor(158, 126,  51));
        const QColor numBorderA = SceneTreePalette::pillBorderActive();
        const QColor numFillA   = SceneTreePalette::pillFillActive();
        const QString badgeLabel = m_isParameter ? QStringLiteral("PAR") : QStringLiteral("VAR");

        // Row background — rounded card so it looks polished inside groups
        // and as a standalone drag-ghost.
        {
            const QColor rowBg = SceneTreePalette::variableFill(m_isParameter, pt);
            const QColor rowBorder = dark ? rowBg.lighter(148) : rowBg.darker(122);
            paintRoundedPanel(painter, m_rect, 5.0,
                              QPen(rowBorder, 1.0), QBrush(rowBg));
        }

        // Badge pill — vertically centred in VariableHeight.
        const qreal badgeH = 13.0;
        const QRectF badgeRect(m_rect.left() + 6.0,
                               m_rect.top() + (VariableHeight - badgeH) * 0.5,
                               28.0,
                               badgeH);
        const QColor badgeTop = m_isParameter
                                    ? QColor(165, 205, 255)
                                    : QColor(255, 235, 170);
        const QColor badgeBottom = m_isParameter
                                       ? QColor(70, 118, 195)
                                       : QColor(185, 135, 42);
        const QColor badgeText = m_isParameter ? QColor(20, 45, 86) : QColor(62, 46, 20);
        paintGlossBadge(painter, badgeRect, badgeLabel, badgeTop, badgeBottom, badgeText);

        // Name, = and expression.
        const QFontMetricsF metrics(painter->font());
        const qreal nameW = metrics.horizontalAdvance(m_name);
        const QRectF textLineRect(m_rect.left() + 38.0,
                                  m_rect.top() + (VariableHeight - 16.0) * 0.5,
                                  nameW,
                                  16.0);

        painter->setPen(nameColor);
        painter->drawText(textLineRect, Qt::AlignLeft | Qt::AlignVCenter, m_name);

        painter->setPen(eqColor);
        painter->drawText(QRectF(textLineRect.right() + 4.0, textLineRect.top(), 12.0, textLineRect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("="));

        const QColor numFillInactive = dark ? QColor(255, 255, 255, 32) : QColor(255, 255, 255, 110);
        const QVector<ExpressionTextSpan> spans = expressionTextSpans(m_rect, m_expression, metrics, nameW);
        for (const ExpressionTextSpan &span : spans) {
            if (!span.number)
                continue;
            const bool active = span.start == m_activeNumberStart;
            paintRoundedPanel(painter,
                              span.rect,
                              4.0,
                              QPen(active ? numBorderA : numBorder, active ? 2 : 1),
                              QBrush(active ? numFillA : numFillInactive));
        }

        painter->setPen(eqColor);
        for (const ExpressionTextSpan &span : spans)
            painter->drawText(span.rect, Qt::AlignCenter, span.text);
    }

private:
    QRectF  m_rect;
    QString m_name;
    QString m_expression;
    bool    m_selected = false;
    int     m_activeNumberStart = -1;
    qreal   m_opacity = 1.0;
    bool    m_isParameter = false;
    int     m_depth = 0;
    int     m_theme = 0;
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
                  int activeForLoopNumberStart = -1,
                  int depth = 0,
                  int theme = 0)
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
        , m_depth(depth)
        , m_theme(theme)
    {
        setZValue(zValue);
    }

    QRectF boundingRect() const override { return m_rect.adjusted(-2.0, -2.0, 6.0, 7.0); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setFont(sceneTreeGraphicsFont());

        // ----- Palette-derived colours -----
        const auto pt   = static_cast<SceneTreePalette::Theme>(m_theme);
        const bool dark = SceneTreePalette::isDarkTheme(pt);
        const QColor fill = SceneTreePalette::groupFill(m_operation, m_depth, pt);

        // Body border: selected = golden; otherwise lighter above fill on dark themes.
        const QPen bodyBorderPen = m_selected
            ? QPen(QColor(255, 203, 87), 3)
            : QPen(dark ? fill.lighter(165) : fill.darker(145), 1.5);

        const QColor cTextPrimary = SceneTreePalette::textPrimary(pt);
        const QColor cTextMuted   = SceneTreePalette::textMuted(pt);
        const QColor cPillBorder  = SceneTreePalette::pillBorder(fill, pt);
        const QColor cPillFill    = SceneTreePalette::pillFill(pt);

        // Soft multi-layer shadow — rounded so it matches the card shape.
        if (m_showShadow) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0, 0, 0, dark ? 16 : 11));
            painter->drawRoundedRect(m_rect.adjusted(-1, 1, 4, 5), CornerRadius + 2.0, CornerRadius + 2.0);
            painter->setBrush(QColor(0, 0, 0, dark ? 24 : 16));
            painter->drawRoundedRect(m_rect.adjusted( 0, 1, 3, 4), CornerRadius + 1.0, CornerRadius + 1.0);
        }

        const QColor bodyFill = m_insertedPreview ? translucent(fill, qMin(fill.alpha() + 55, 230)) : fill;
        paintRoundedPanel(painter, m_rect, CornerRadius, bodyBorderPen, QBrush(bodyFill));

        if (isTransformOperation(m_operation)) {
            paintVerticalHeader(painter, fill, dark, cTextPrimary, cTextMuted, cPillBorder, cPillFill);
        } else {
            paintHorizontalHeader(painter, fill, dark, cTextPrimary, cTextMuted, cPillBorder, cPillFill);
        }

        if (m_empty) {
            const QPointF emptyPosition = isTransformOperation(m_operation)
                ? QPointF(m_rect.left() + TransformHeaderWidth + GroupPadding + PrimitiveIconSize + 8.0,
                          m_rect.top() + GroupPadding + 10.0)
                : QPointF(m_rect.left() + GroupPadding + PrimitiveIconSize + 8.0,
                          m_rect.top() + GroupHeaderHeight + GroupPadding + 10.0);
            paintLabel(painter, QStringLiteral("empty"), emptyPosition, cTextMuted);
        }

        if (m_operation != SceneDocument::TreeNode::Difference || m_cutSeparatorY <= 0.0)
            return;

        const QColor diffSepColor = dark ? QColor(190, 140, 110) : QColor(130, 92, 70);
        painter->setPen(QPen(diffSepColor, 1, Qt::DashLine));
        painter->drawLine(QPointF(m_rect.left() + GroupPadding, m_cutSeparatorY),
                          QPointF(m_rect.right() - GroupPadding, m_cutSeparatorY));

        if (m_showDifferenceLabels) {
            const qreal labelWidth  = 20.0;
            const qreal labelHeight = 42.0;
            const qreal labelLeft   = m_rect.left() + GroupPadding * 0.5;
            const qreal baseTop     = m_rect.top() + GroupHeaderHeight + GroupPadding;
            const qreal baseBottom  = m_cutSeparatorY - 4.0;
            const qreal cutTop      = m_cutSeparatorY + 4.0;
            const qreal cutBottom   = m_rect.bottom() - GroupPadding;
            const QColor diffAccent = dark ? QColor(200, 160, 120) : QColor(128, 99, 73);
            const QColor cutAccent  = dark ? QColor(210, 130, 100) : QColor(153, 85, 56);
            paintVerticalPillLabel(painter, QStringLiteral("base"),
                                   boundedVerticalLabelRect(labelLeft, baseTop, baseBottom, labelWidth, labelHeight),
                                   diffAccent);
            paintVerticalPillLabel(painter, QStringLiteral("cut"),
                                   boundedVerticalLabelRect(labelLeft, cutTop, cutBottom, labelWidth, labelHeight),
                                   cutAccent);
        }
    }

private:
    void paintHorizontalHeader(QPainter *painter, const QColor &fill,
                                bool dark,
                                const QColor &cTextPrimary, const QColor &cTextMuted,
                                const QColor &cPillBorder, const QColor &cPillFill)
    {
        const QRectF headerRect(m_rect.left(), m_rect.top(),
                                m_rect.width(), GroupHeaderHeight);
        // Header fill clipped to the card outline: naturally follows the rounded top corners
        // and presents a flat bottom edge (no bubble-inside-card effect).
        const QColor rawHeaderFill = dark ? fill.lighter(180) : fill.lighter(112);
        const QColor headerFill = m_insertedPreview
            ? translucent(rawHeaderFill, qMin(rawHeaderFill.alpha() + 55, 230))
            : rawHeaderFill;
        {
            painter->save();
            QPainterPath cardClip;
            cardClip.addRoundedRect(m_rect, CornerRadius, CornerRadius);
            painter->setClipPath(cardClip);
            painter->setPen(Qt::NoPen);
            painter->setBrush(headerFill);
            painter->drawRect(headerRect);
            painter->restore();
        }

        const QRectF gripRect(m_rect.left() + 10.0,
                              m_rect.top() + 6.0,
                              16.0,
                              GroupHeaderHeight - 12.0);
        paintHeaderGripDots(painter,
                            gripRect,
                            dark ? QColor(205, 214, 228, 205) : QColor(86, 96, 112, 205));

        const qreal iconLeft = m_rect.left() + 30.0;
        const qreal headerIconSize = m_operation == SceneDocument::TreeNode::Module
                                         ? PrimitiveIconSize - 6.0
                                         : PrimitiveIconSize - 4.0;
        const QRectF iconRect(iconLeft,
                              m_rect.top() + (GroupHeaderHeight - headerIconSize) * 0.5,
                              headerIconSize,
                              headerIconSize);
        const QColor iconAccent = dark ? fill.lighter(210) : fill.darker(125);
        paintOperationIcon(painter, m_operation, iconRect, iconAccent);

        if (m_operation == SceneDocument::TreeNode::For) {
            const QString variableName    = m_loopVariable.trimmed().isEmpty()      ? QStringLiteral("i")         : m_loopVariable.trimmed();
            const QString rangeExpression = m_loopRangeExpression.trimmed().isEmpty() ? QStringLiteral("[0 : 1 : 3]") : m_loopRangeExpression.trimmed();
            const QString prefix = QStringLiteral("for (%1 = ").arg(variableName);
            // Use the canonical scene font directly — not painter->font() which may be
            // device-resolved to a slightly different pixel size — so that the pill rects
            // produced here match the rects produced by the hit-testing code in the widget.
            const QFontMetricsF metrics(sceneTreeGraphicsFont());
            painter->setPen(cTextPrimary);
            const qreal textLeft = iconRect.right() + 10.0;
            painter->drawText(QRectF(textLeft, m_rect.top() + 7.0,
                                     metrics.horizontalAdvance(prefix), 16.0),
                              Qt::AlignLeft | Qt::AlignVCenter, prefix);

            const QVector<ExpressionTextSpan> spans = forLoopRangeTextSpans(m_rect, variableName, rangeExpression, metrics);
            for (const ExpressionTextSpan &span : spans) {
                if (span.number) {
                    const bool active = span.start == m_activeForLoopNumberStart;
                    paintRoundedPanel(painter, span.rect, 3.0,
                                      QPen(active ? SceneTreePalette::pillBorderActive() : cPillBorder, active ? 2 : 1),
                                      QBrush(active ? SceneTreePalette::pillFillActive() : cPillFill));
                }
                painter->setPen(span.number ? cTextPrimary : cTextMuted);
                // Numbers use HCenter: span.rect has 4 px padding on each side so the digit
                // sits centred inside the pill.  Non-numbers are exactly their text width.
                const Qt::Alignment align = span.number
                    ? (Qt::AlignHCenter | Qt::AlignVCenter)
                    : (Qt::AlignLeft | Qt::AlignVCenter);
                painter->drawText(span.rect, align, span.text);
            }
            const qreal suffixLeft = forLoopRangeTextRect(m_rect, variableName, metrics).left()
                                     + metrics.horizontalAdvance(rangeExpression);
            painter->setPen(cTextPrimary);
            painter->drawText(QRectF(suffixLeft, m_rect.top() + 7.0, 10.0, 16.0),
                              Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral(")"));
        } else {
            paintLabel(painter, labelForOperation(m_operation),
                       QPointF(iconRect.right() + 10.0, m_rect.top() + 7.0), cTextPrimary);
        }

        if (!isTransformOperation(m_operation))
            paintHeaderChevron(painter, headerRect, dark ? QColor(220, 228, 238, 210) : QColor(54, 64, 76, 210));

        // Thin separator between header and body.
        const QColor sepColor = dark ? fill.lighter(148) : fill.darker(120);
        painter->setPen(QPen(sepColor, 1.0));
        const qreal sepY = m_rect.top() + GroupHeaderHeight - 0.5;
        painter->drawLine(QPointF(m_rect.left() + CornerRadius * 0.5, sepY),
                          QPointF(m_rect.right() - CornerRadius * 0.5, sepY));
    }

    void paintVerticalHeader(QPainter *painter, const QColor &fill,
                              bool dark,
                              const QColor &cTextPrimary, const QColor & /*cTextMuted*/,
                              const QColor &cPillBorder, const QColor &cPillFill)
    {
        // Left icon stripe — clipped to card shape so it follows the rounded corners
        // on the left side and has a flat right edge (no bubble-inside-card effect).
        const QRectF iconBorderRect(m_rect.left(), m_rect.top(),
                                    TransformIconWidth, m_rect.height());
        const QColor rawHeaderFill = dark ? fill.lighter(180) : fill.lighter(112);
        const QColor headerFill = m_insertedPreview
            ? translucent(rawHeaderFill, qMin(rawHeaderFill.alpha() + 55, 230))
            : rawHeaderFill;
        {
            painter->save();
            QPainterPath cardClip;
            cardClip.addRoundedRect(m_rect, CornerRadius, CornerRadius);
            painter->setClipPath(cardClip);
            painter->setPen(Qt::NoPen);
            painter->setBrush(headerFill);
            painter->drawRect(iconBorderRect);
            painter->restore();
        }

        // Thin vertical separator between icon stripe and value area.
        const QColor stripeSep = dark ? fill.lighter(148) : fill.darker(120);
        painter->setPen(QPen(stripeSep, 1.0));
        const qreal sepX = m_rect.left() + TransformIconWidth - 0.5;
        painter->drawLine(QPointF(sepX, m_rect.top() + CornerRadius * 0.5),
                          QPointF(sepX, m_rect.bottom() - CornerRadius * 0.5));

        const QRectF iconRect(m_rect.left() + 6.0, m_rect.top() + 7.0,
                               PrimitiveIconSize - 6.0, PrimitiveIconSize - 6.0);
        const QColor iconAccent = dark ? fill.lighter(210) : fill.darker(125);
        paintOperationIcon(painter, m_operation, iconRect, iconAccent, 6.0);

        const QString opLabel = m_operation == SceneDocument::TreeNode::Translate ? QStringLiteral("T")
                              : m_operation == SceneDocument::TreeNode::Rotate   ? QStringLiteral("R")
                              : m_operation == SceneDocument::TreeNode::Mirror   ? QStringLiteral("M")
                                                                                 : QStringLiteral("S");
        const QRectF opBadgeRect(iconRect.left() + 1.0,
                                 iconRect.bottom() - 1.0,
                                 iconRect.width() - 2.0,
                                 14.0);
        QColor badgeTop;
        QColor badgeBottom;
        if (m_operation == SceneDocument::TreeNode::Translate) {
            badgeTop = QColor(205, 224, 255);
            badgeBottom = QColor(100, 139, 210);
        } else if (m_operation == SceneDocument::TreeNode::Rotate) {
            badgeTop = QColor(226, 205, 255);
            badgeBottom = QColor(145, 98, 195);
        } else if (m_operation == SceneDocument::TreeNode::Mirror) {
            badgeTop = QColor(252, 215, 238);
            badgeBottom = QColor(175, 85, 138);
        } else {
            badgeTop = QColor(205, 242, 218);
            badgeBottom = QColor(84, 158, 105);
        }
        paintGlossBadge(painter, opBadgeRect, opLabel, badgeTop, badgeBottom, QColor(24, 34, 44), 3.0);

        // X/Y/Z rows outside icon border.
        // Use the canonical scene font for metrics so pill rects are pixel-identical
        // to those produced by the widget's hit-testing code.
        const QFontMetricsF metrics(sceneTreeGraphicsFont());
        QFont valueFont = painter->font();
        valueFont.setPointSizeF(qMax<qreal>(7.0, valueFont.pointSizeF() - 2.0));

        // Axis label colour: on dark themes, derive from the fill so it reads
        // against the dark background; on light themes, use the traditional darker.
        const QColor axisLabelColor = dark ? fill.lighter(220) : fill.darker(160);

        static const QString axisLabels[3] = {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
        for (int axis = 0; axis < 3; ++axis) {
            const QRectF rowRect   = transformParameterControlRect(m_rect, axis, m_transformHeaderWidth);
            const bool   rowActive = (axis == m_activeTransformAxis);

            // Expression for this axis.
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

            painter->setFont(valueFont);
            painter->setPen(axisLabelColor);
            painter->drawText(QRectF(rowRect.left(), rowRect.top(), TransformParamLabelArea - 2.0, rowRect.height()),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              axisLabels[axis] + QStringLiteral(" ="));

            const QRectF textRect(rowRect.left() + TransformParamLabelArea,
                                  rowRect.top(),
                                  rowRect.width() - TransformParamLabelArea,
                                  rowRect.height());
            const QVector<ExpressionTextSpan> spans = expressionSpansInTextRect(textRect, expr, metrics);
            for (const ExpressionTextSpan &span : spans) {
                const bool numActive = rowActive && span.number && (span.start == m_activeTransformNumberStart);
                if (span.number) {
                    paintRoundedPanel(painter, span.rect, 3.0,
                                      QPen(numActive ? SceneTreePalette::pillBorderActive() : cPillBorder,
                                           numActive ? 2 : 1),
                                      QBrush(numActive ? SceneTreePalette::pillFillActive() : cPillFill));
                }
                // Numbers: span.rect is 4 px wider on each side → centre digit inside pill.
                painter->setPen(span.number ? cTextPrimary
                                            : (dark ? QColor(140, 175, 220) : QColor(80, 110, 160)));
                const Qt::Alignment align = span.number
                    ? (Qt::AlignHCenter | Qt::AlignVCenter)
                    : (Qt::AlignLeft | Qt::AlignVCenter);
                painter->drawText(span.rect, align, span.text);
            }
        }
    }

    QRectF      m_rect;
    qreal       m_cutSeparatorY = 0.0;
    QVector3D   m_transformValues;
    QStringList m_transformExpressions;
    qreal       m_transformHeaderWidth = TransformHeaderWidth;
    QString     m_loopVariable;
    QString     m_loopRangeExpression;
    int         m_activeForLoopNumberStart   = -1;
    int         m_activeTransformAxis        = -1;
    int         m_activeTransformNumberStart = -1;
    SceneDocument::TreeNode::Operation m_operation = SceneDocument::TreeNode::Union;
    bool        m_selected            = false;
    bool        m_empty               = false;
    bool        m_showShadow          = false;
    bool        m_showDifferenceLabels = false;
    bool        m_insertedPreview      = false;
    int         m_depth = 0;
    int         m_theme = 0;
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
        const qreal badgeH = 16.0;
        const QRectF badgeRect(m_rect.left() + 6.0,
                               m_rect.top() + (VariableHeight - badgeH) * 0.5,
                               34.0,
                               badgeH);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 38));
        painter->drawRoundedRect(badgeRect.translated(1.0, 1.0), 4.0, 4.0);

        QLinearGradient badgeGradient(badgeRect.topLeft(), badgeRect.bottomLeft());
        badgeGradient.setColorAt(0.0, QColor(82, 145, 205));
        badgeGradient.setColorAt(1.0, QColor(24, 82, 135));
        paintRoundedPanel(painter, badgeRect, 4.0, QPen(QColor(170, 215, 255, 150), 1.0), QBrush(badgeGradient));
        {
            painter->save();
            QFont badgeFont = painter->font();
            badgeFont.setBold(true);
            badgeFont.setPointSizeF(qMax<qreal>(6.5, badgeFont.pointSizeF() - 1.5));
            painter->setFont(badgeFont);
            painter->setPen(QColor(238, 248, 255));
            painter->drawText(badgeRect, Qt::AlignCenter, QStringLiteral("CALL"));
            painter->restore();
        }

        const QFontMetricsF metrics(painter->font());
        const QColor nameColor = m_selected ? QColor(30, 90, 155) : QColor(32, 80, 118);
        const QColor punctColor(60, 60, 80);
        const QColor paramNameColor(70, 80, 60);

        qreal x = m_rect.left() + 46.0;
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
    m_scene->addItem(new VariableCardItem(rect, node.variableName, node.variableExpression,
                                          node.id == m_selectedNodeId, activeNumberStart,
                                          1.0, 5.0, node.isParameter,
                                          0,        // depth (variables: no depth-hue shift)
                                          m_theme));
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
    const QVector3D transformValues = node.operation == SceneDocument::TreeNode::Translate ? node.position
                                    : node.operation == SceneDocument::TreeNode::Rotate    ? node.rotation
                                    : node.operation == SceneDocument::TreeNode::Mirror    ? node.position
                                    : node.operation == SceneDocument::TreeNode::Scale     ? node.scale
                                                                                           : QVector3D();
    const int activeAxis = node.id == m_activeTransformNodeId ? m_activeTransformAxis : -1;
    const int activeNumberStart = node.id == m_activeTransformNodeId ? m_activeTransformNumberStart : -1;
    const int activeForLoopStart = node.id == m_activeForLoopNodeId ? m_activeForLoopNumberStart : -1;
    const qreal transformHeaderWidth = transformHeaderWidthForNode(node);
    const bool showEmptyText = node.operation != SceneDocument::TreeNode::Module
                               && node.operation != SceneDocument::TreeNode::Scene
                               && node.children.isEmpty();
    m_scene->addItem(new GroupCardItem(rect, node.operation, cutSeparatorY, zForDepth(depth, -101.0),
                                       node.id == m_selectedNodeId, showEmptyText, true, true, false,
                                       transformValues, activeAxis, activeNumberStart,
                                       transformHeaderWidth, node.transformExpressions,
                                       node.loopVariable, node.loopRangeExpression, activeForLoopStart,
                                       depth, m_theme));
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
                                              const QRectF &rect,
                                              int theme)
{
    SceneDocument::TreeNode::Operation operation;
    QGraphicsItem *item = nullptr;
    if (tool == QStringLiteral("par")) {
        item = new VariableCardItem(rect, QStringLiteral("par"), QStringLiteral("0"), false, -1, 0.78, 58.0, true, 0, theme);
    } else if (isVariableToolName(tool)) {
        item = new VariableCardItem(rect, QStringLiteral("var"), QStringLiteral("0"), false, -1, 0.78, 58.0, false, 0, theme);
    } else if (tool == QStringLiteral("call")) {
        item = new ModuleCallCardItem(rect, QStringLiteral("call"), {}, false, 0, -1, 0.78, 58.0);
    } else if (operationForToolName(tool, &operation)) {
        item = new GroupCardItem(rect, operation, 0.0, 56.0, false, false, false, false, true,
                                 QVector3D(), -1, -1, TransformHeaderWidth, QStringList(),
                                 QString(), QString(), -1,
                                 0, theme);
    } else {
        ShapeNode shape;
        shape.type = primitiveTypeForTool(tool);
        item = new PrimitiveCardItem(rect, &shape, QString(), false, -1, -1, 0.78, 58.0);
    }

    scene->addItem(item);
    appendPreviewItem(items, item);
}

void SceneTreeNodeRenderer::renderPreviewPrimitive(QGraphicsScene *scene,
                                                   QVector<QGraphicsItem *> *items,
                                                   const ShapeNode *shape,
                                                   int shapeId,
                                                   const QRectF &rect)
{
    auto *item = new PrimitiveCardItem(rect,
                                      shape,
                                      primitiveNumberText(QString(), shapeId),
                                      false,
                                      -1,
                                      -1,
                                      0.78,
                                      58.0);
    scene->addItem(item);
    appendPreviewItem(items, item);
}

void SceneTreeNodeRenderer::renderPreviewVariable(QGraphicsScene *scene,
                                                  QVector<QGraphicsItem *> *items,
                                                  const QString &name,
                                                  const QString &expression,
                                                  bool isParameter,
                                                  const QRectF &rect,
                                                  int theme)
{
    auto *item = new VariableCardItem(rect, name, expression, false, -1, 0.78, 58.0, isParameter, 0, theme);
    scene->addItem(item);
    appendPreviewItem(items, item);
}

void SceneTreeNodeRenderer::renderPreviewGroup(QGraphicsScene *scene,
                                               QVector<QGraphicsItem *> *items,
                                               SceneDocument::TreeNode::Operation operation,
                                               const QRectF &rect,
                                               qreal cutSeparatorY,
                                               int theme,
                                               int depth)
{
    auto *item = new GroupCardItem(rect, operation, cutSeparatorY, 52.0, false, false, false, false, false,
                                   QVector3D(), -1, -1, TransformHeaderWidth, QStringList(),
                                   QString(), QString(), -1,
                                   depth, theme);
    scene->addItem(item);
    appendPreviewItem(items, item);
}
