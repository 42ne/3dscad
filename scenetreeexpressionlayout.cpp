#include "scenetreecanvasgraphics.h"
#include "scenetreeexpressionlayout.h"
#include "scenetreepreviewgeometry.h"
#include "scenetreetoolmetadata.h"

#include <QFontMetricsF>
#include <QPointF>
#include <QtGlobal>

namespace SceneTreeGraphics {

QVector<ShapeParameterControl> shapeParameterControls(const ShapeNode &shape)
{
    auto expr = [&](int idx, qreal numericValue) -> QString {
        if (idx < shape.parameterExpressions.size() && !shape.parameterExpressions[idx].isEmpty())
            return shape.parameterExpressions[idx];
        return QString::number(numericValue, 'g');
    };

    if (shape.type == ShapeNode::Sphere || shape.type == ShapeNode::Circle) {
        if (shape.usesDiameter) {
            const qreal d = shape.radius * 2.0;
            return {{QStringLiteral("D"), d, expr(0, d)}};
        }
        return {{QStringLiteral("R"), shape.radius, expr(0, shape.radius)}};
    }

    if (shape.type == ShapeNode::Square)
        return {{QStringLiteral("X"), shape.size.x(), expr(0, shape.size.x())},
                {QStringLiteral("Y"), shape.size.y(), expr(1, shape.size.y())}};

    if (shape.type == ShapeNode::Cylinder) {
        if (shape.usesDiameter) {
            const qreal d = shape.radius * 2.0;
            return {{QStringLiteral("D"), d, expr(0, d)},
                    {QStringLiteral("H"), shape.height, expr(1, shape.height)}};
        }
        return {{QStringLiteral("R"), shape.radius, expr(0, shape.radius)},
                {QStringLiteral("H"), shape.height, expr(1, shape.height)}};
    }

    if (shape.type == ShapeNode::Cone) {
        const QString r1Label = shape.usesD1 ? QStringLiteral("D1") : QStringLiteral("R1");
        const QString r2Label = shape.usesD2 ? QStringLiteral("D2") : QStringLiteral("R2");
        const qreal   r1Val   = shape.usesD1 ? shape.radius  * 2.0 : shape.radius;
        const qreal   r2Val   = shape.usesD2 ? shape.radius2 * 2.0 : shape.radius2;
        return {{r1Label, r1Val, expr(0, r1Val)},
                {r2Label, r2Val, expr(1, r2Val)},
                {QStringLiteral("H"), shape.height, expr(2, shape.height)}};
    }

    if (shape.type == ShapeNode::Polyhedron || shape.type == ShapeNode::Polygon2D)
        return {};

    if (shape.type == ShapeNode::Point3D)
        return {{QStringLiteral("X"), shape.position.x(), expr(0, shape.position.x())},
                {QStringLiteral("Y"), shape.position.y(), expr(1, shape.position.y())},
                {QStringLiteral("Z"), shape.position.z(), expr(2, shape.position.z())}};

    if (shape.type == ShapeNode::Face3D) {
        const int count = shape.polyhedronFaces.isEmpty() ? 0 : shape.polyhedronFaces.first().size();
        QVector<ShapeParameterControl> controls;
        controls.append({QStringLiteral("N"), static_cast<qreal>(count), expr(0, static_cast<qreal>(count))});
        for (int i = 0; i < count; ++i) {
            const QString label = QStringLiteral("V%1").arg(i);
            controls.append({label, static_cast<qreal>(shape.polyhedronFaces.first()[i]),
                            expr(1 + i, static_cast<qreal>(shape.polyhedronFaces.first()[i]))});
        }
        return controls;
    }

    return {{QStringLiteral("X"), shape.size.x(), expr(0, shape.size.x())},
            {QStringLiteral("Y"), shape.size.y(), expr(1, shape.size.y())},
            {QStringLiteral("Z"), shape.size.z(), expr(2, shape.size.z())}};
}

QRectF variableExpressionTextRect(const QRectF &variableRect, qreal nameTextWidth)
{
    // Single-row: badge(6+28+4=38) + nameW + gap(4) + eq(12) + gap(2) = 56+nameW
    const qreal exprLeft = 56.0 + nameTextWidth;
    return QRectF(variableRect.left() + exprLeft,
                  variableRect.top() + (VariableHeight - 16.0) * 0.5,
                  variableRect.width() - exprLeft - 6.0,
                  16.0);
}

QVector<ExpressionTextSpan> expressionSpansInTextRect(const QRectF &textRect, const QString &expression, const QFontMetricsF &metrics)
{
    QVector<ExpressionTextSpan> spans;
    // Extra padding around arithmetic operators so pill borders don't overlap them.
    const qreal operatorGap = 4.0;
    qreal x = textRect.left();

    const QString trimmed = expression.trimmed();
    const bool standaloneSignedNumber = !trimmed.isEmpty()
                                        && (trimmed[0] == QLatin1Char('-') || trimmed[0] == QLatin1Char('+'))
                                        && trimmed.size() > 1;
    if (standaloneSignedNumber) {
        bool ok = false;
        trimmed.toDouble(&ok);
        if (ok) {
            const int start = expression.indexOf(trimmed);
            const qreal width = metrics.horizontalAdvance(trimmed);
            spans.append({trimmed,
                          start,
                          trimmed.size(),
                          QRectF(x - 4.0, textRect.top() + 1.0, width + 8.0, textRect.height() - 2.0),
                          true});
            return spans;
        }
    }

    int index = 0;
    while (index < expression.size()) {
        const QChar ch = expression[index];
        const bool startsWithDigit = ch.isDigit();
        const bool startsWithDecimalPoint = ch == QLatin1Char('.')
                                            && index + 1 < expression.size()
                                            && expression[index + 1].isDigit();

        const bool previousIsIdentifier = index > 0
                                          && (expression[index - 1].isLetterOrNumber()
                                              || expression[index - 1] == QLatin1Char('_'));
        if ((startsWithDigit || startsWithDecimalPoint) && !previousIsIdentifier) {
            const int start = index;
            bool hasDigit = false;
            while (index < expression.size() && expression[index].isDigit()) {
                hasDigit = true;
                ++index;
            }
            if (index < expression.size() && expression[index] == QLatin1Char('.')) {
                ++index;
                while (index < expression.size() && expression[index].isDigit()) {
                    hasDigit = true;
                    ++index;
                }
            }

            const bool nextIsIdentifier = index < expression.size()
                                          && (expression[index].isLetter()
                                              || expression[index] == QLatin1Char('_'));
            if (hasDigit && !nextIsIdentifier) {
                const QString text = expression.mid(start, index - start);
                const qreal width = metrics.horizontalAdvance(text);
                spans.append({text,
                              start,
                              index - start,
                              QRectF(x - 4.0, textRect.top() + 1.0, width + 8.0, textRect.height() - 2.0),
                              true});
                x += width;
                continue;
            }
        }

        if (ch.isLetter() || ch == QLatin1Char('_')) {
            const int start = index++;
            while (index < expression.size() && (expression[index].isLetterOrNumber() || expression[index] == QLatin1Char('_')))
                ++index;

            const QString text = expression.mid(start, index - start);
            const qreal width = metrics.horizontalAdvance(text);
            spans.append({text, start, index - start, QRectF(x, textRect.top(), width, textRect.height()), false, true});
            x += width;
            continue;
        }

        if (ch.isSpace()) {
            x += metrics.horizontalAdvance(ch);
            ++index;
            continue;
        }

        const QString text(ch);
        const bool spacedOperator = ch == QLatin1Char('+')
                                    || ch == QLatin1Char('-')
                                    || ch == QLatin1Char('*')
                                    || ch == QLatin1Char('/');
        if (spacedOperator)
            x += operatorGap;

        const qreal width = metrics.horizontalAdvance(text);
        spans.append({text, index, 1, QRectF(x, textRect.top(), width, textRect.height()), false});
        x += width + (spacedOperator ? operatorGap : 0.0);
        ++index;
    }

    return spans;
}

ExpressionPillLayout::ExpressionPillLayout(const QRectF &fieldRect,
                                           const QString &expression,
                                           const QFontMetricsF &metrics)
    : m_fieldRect(fieldRect)
    , m_expression(expression)
    , m_spans(expressionSpansInTextRect(fieldRect, expression, metrics))
{
}

bool ExpressionPillLayout::isSimpleNumber() const
{
    return m_spans.size() == 1 && m_spans.first().number;
}

bool ExpressionPillLayout::firstNumberSpan(ExpressionTextSpan *span) const
{
    for (const ExpressionTextSpan &candidate : m_spans) {
        if (!candidate.number)
            continue;
        if (span)
            *span = candidate;
        return true;
    }
    return false;
}

bool ExpressionPillLayout::spanAt(const QPointF &scenePosition,
                                  ExpressionTextSpan *span,
                                  bool matchSimpleNumberField) const
{
    if (!m_fieldRect.contains(scenePosition))
        return false;

    if (matchSimpleNumberField && isSimpleNumber())
        return firstNumberSpan(span);

    for (const ExpressionTextSpan &candidate : m_spans) {
        if (!candidate.number)
            continue;
        const QRectF pillRect = visiblePillRectFor(candidate);
        if (pillRect.isEmpty())
            continue;
        if (!pillRect.adjusted(-2.0, -1.0, 2.0, 1.0).contains(scenePosition))
            continue;
        if (span)
            *span = candidate;
        return true;
    }
    return false;
}

QRectF ExpressionPillLayout::pillRectFor(const ExpressionTextSpan &span) const
{
    const qreal textW = span.rect.width() - 8.0;
    const qreal pillW = textW + 4.0;
    const qreal pillLeft = qMax(m_fieldRect.left(), span.rect.left() + 2.0);
    return QRectF(pillLeft, m_fieldRect.top(), pillW, m_fieldRect.height());
}

QRectF ExpressionPillLayout::visiblePillRectFor(const ExpressionTextSpan &span) const
{
    return pillRectFor(span).intersected(m_fieldRect);
}

QRectF ExpressionPillLayout::visualRectFor(const ExpressionTextSpan &span) const
{
    if (span.number)
        return visiblePillRectFor(span);
    return QRectF(span.rect.left(), m_fieldRect.top(),
                  span.rect.width(), m_fieldRect.height()).intersected(m_fieldRect);
}

QRectF ExpressionPillLayout::contentRect() const
{
    QRectF bounds;
    for (const ExpressionTextSpan &span : m_spans) {
        const QRectF rect = visualRectFor(span);
        if (rect.isEmpty())
            continue;
        bounds = bounds.isNull() ? rect : bounds.united(rect);
    }
    return bounds;
}

QRectF ExpressionPillLayout::expressionEditRect() const
{
    const QRectF bounds = contentRect();
    if (bounds.isEmpty())
        return m_fieldRect;
    return bounds.adjusted(-4.0, 0.0, 4.0, 0.0).intersected(m_fieldRect);
}

QRectF ExpressionPillLayout::scrollZoneRect(int numberStart) const
{
    for (const ExpressionTextSpan &span : m_spans) {
        if (!span.number)
            continue;
        if (isSimpleNumber() || span.start == numberStart)
            return visiblePillRectFor(span);
    }
    return m_fieldRect;
}

QString transformAxisExpression(const SceneDocument::TreeNode &node, int axis)
{
    if (axis >= 0 && axis < node.transformExpressions.size() && !node.transformExpressions[axis].isEmpty())
        return node.transformExpressions[axis];
    const QVector3D &v = (node.operation == SceneDocument::TreeNode::Translate
                       || node.operation == SceneDocument::TreeNode::Mirror)  ? node.position
                       : node.operation == SceneDocument::TreeNode::Rotate    ? node.rotation
                                                                              : node.scale;
    const float val = axis == 0 ? v.x() : axis == 1 ? v.y() : v.z();
    const int precision = node.operation == SceneDocument::TreeNode::Scale ? 1 : 0;
    return QString::number(val, 'f', precision);
}

qreal transformHeaderWidthForNode(const SceneDocument::TreeNode &node)
{
    if (!isTransformOperation(node.operation))
        return 0.0;

    const QFontMetricsF metrics(sceneTreeGraphicsFont());
    qreal maxExpressionWidth = 0.0;
    for (int axis = 0; axis < 3; ++axis) {
        const QString expression = transformAxisExpression(node, axis);
        const qreal exprPx = metrics.horizontalAdvance(expression) + 8.0;
        maxExpressionWidth = qMax(maxExpressionWidth, exprPx);
    }

    return qMax<qreal>(TransformHeaderWidth, TransformIconWidth + 4.0 + TransformParamLabelArea + maxExpressionWidth + 8.0);
}

QRectF transformParameterControlRect(const QRectF &groupRect, int axis, qreal headerWidth)
{
    if (axis < 0 || axis > 2)
        return QRectF();
    const qreal left = groupRect.left() + TransformIconWidth + 4.0;
    const qreal width = qMax<qreal>(TransformHeaderWidth, headerWidth) - TransformIconWidth - 8.0;
    const qreal rowHeight = 13.0;
    const qreal rowTop = groupRect.top() + 8.0 + axis * 15.0;
    return QRectF(left, rowTop, width, rowHeight);
}

QVector<ExpressionNumberControl> transformParameterNumberControls(const QRectF &groupRect, int axis, const QString &expression, const QFontMetricsF &metrics, qreal headerWidth)
{
    const QRectF rowRect = transformParameterControlRect(groupRect, axis, headerWidth);
    if (!rowRect.isValid())
        return {};
    const QRectF textRect(rowRect.left() + TransformParamLabelArea,
                          rowRect.top(),
                          rowRect.width() - TransformParamLabelArea,
                          rowRect.height());
    QVector<ExpressionNumberControl> controls;
    for (const ExpressionTextSpan &span : expressionSpansInTextRect(textRect, expression, metrics)) {
        if (span.number)
            controls.append({span.text, span.start, span.length, span.rect});
    }
    return controls;
}

QString forLoopVariableName(const SceneDocument::TreeNode &node)
{
    const QString name = node.loopVariable.trimmed();
    return name.isEmpty() ? QStringLiteral("i") : name;
}

QString forLoopRangeExpression(const SceneDocument::TreeNode &node)
{
    const QString range = node.loopRangeExpression.trimmed();
    return range.isEmpty() ? QStringLiteral("[0 : 1 : 3]") : range;
}

QRectF forLoopRangeTextRect(const QRectF &groupRect, const QString &variableName, const QFontMetricsF &metrics)
{
    const QString prefix = QStringLiteral("for (%1 = ").arg(variableName);
    const qreal left = groupRect.left() + 68.0 + metrics.horizontalAdvance(prefix);
    return QRectF(left,
                  groupRect.top() + 7.0,
                  qMax<qreal>(0.0, groupRect.right() - left - 8.0),
                  16.0);
}

qreal forLoopHeaderMinWidth(const QString &variableName,
                            const QString &rangeExpression,
                            const QFontMetricsF &metrics)
{
    const QString name = variableName.trimmed().isEmpty() ? QStringLiteral("i") : variableName.trimmed();
    const QString range = rangeExpression.trimmed().isEmpty() ? QStringLiteral("[0 : 1 : 3]") : rangeExpression.trimmed();
    const QString prefix = QStringLiteral("for (%1 = ").arg(name);
    const QRectF measureRect(0.0, 0.0, 2048.0, GroupHeaderHeight);

    qreal right = 64.0 + metrics.horizontalAdvance(prefix);
    const QVector<ExpressionTextSpan> spans = forLoopRangeTextSpans(measureRect, name, range, metrics);
    for (const ExpressionTextSpan &span : spans)
        right = qMax(right, span.rect.right());

    // Keep the complete expression clear of the collapse chevron drawn at the
    // right of the horizontal header.
    return right + metrics.horizontalAdvance(QStringLiteral(")")) + 36.0;
}

QVector<ExpressionTextSpan> forLoopRangeTextSpans(const QRectF &groupRect,
                                                  const QString &variableName,
                                                  const QString &rangeExpression,
                                                  const QFontMetricsF &metrics)
{
    QVector<ExpressionTextSpan> spans;
    const QRectF textRect = forLoopRangeTextRect(groupRect, variableName, metrics);
    qreal x = textRect.left();
    int index = 0;

    while (index < rangeExpression.size()) {
        const QChar ch = rangeExpression[index];
        if (ch.isSpace()) {
            x += metrics.horizontalAdvance(ch);
            ++index;
            continue;
        }

        const bool signedNumber = (ch == QLatin1Char('-') || ch == QLatin1Char('+'))
                                  && index + 1 < rangeExpression.size()
                                  && (rangeExpression[index + 1].isDigit() || rangeExpression[index + 1] == QLatin1Char('.'));
        if (ch.isDigit() || ch == QLatin1Char('.') || signedNumber) {
            const int start = index;
            if (signedNumber)
                ++index;

            bool hasDigit = false;
            while (index < rangeExpression.size() && rangeExpression[index].isDigit()) {
                hasDigit = true;
                ++index;
            }

            if (index < rangeExpression.size() && rangeExpression[index] == QLatin1Char('.')) {
                ++index;
                while (index < rangeExpression.size() && rangeExpression[index].isDigit()) {
                    hasDigit = true;
                    ++index;
                }
            }

            if (hasDigit) {
                const QString text = rangeExpression.mid(start, index - start);
                const qreal width = metrics.horizontalAdvance(text);
                spans.append({text,
                              start,
                              index - start,
                              QRectF(x - 4.0, textRect.top() + 1.0, width + 8.0, textRect.height() - 2.0),
                              true});
                x += width;
                continue;
            }

            index = start;
        }

        const QString text(ch);
        const qreal width = metrics.horizontalAdvance(text);
        spans.append({text, index, 1, QRectF(x, textRect.top(), width, textRect.height()), false});
        x += width;
        ++index;
    }

    return spans;
}

QVector<ExpressionNumberControl> forLoopRangeNumberControls(const QRectF &groupRect,
                                                            const QString &variableName,
                                                            const QString &rangeExpression,
                                                            const QFontMetricsF &metrics)
{
    QVector<ExpressionNumberControl> controls;
    for (const ExpressionTextSpan &span : forLoopRangeTextSpans(groupRect, variableName, rangeExpression, metrics)) {
        if (span.number)
            controls.append({span.text, span.start, span.length, span.rect});
    }
    return controls;
}

QString linearExtrudeHeightExpression(const SceneDocument::TreeNode &node)
{
    if (!node.transformExpressions.isEmpty()) {
        const QString expression = node.transformExpressions.first().trimmed();
        if (!expression.isEmpty())
            return expression;
    }
    const qreal height = node.scale.x() > 0.0f ? node.scale.x() : 20.0f;
    return QString::number(height, 'g');
}

QString linearExtrudeParam(const SceneDocument::TreeNode &node, int index, const QString &fallback)
{
    if (node.transformExpressions.size() > index) {
        const QString expr = node.transformExpressions[index].trimmed();
        if (!expr.isEmpty())
            return expr;
    }
    return fallback;
}

QString linearExtrudeFullLabel(const SceneDocument::TreeNode &node)
{
    const QString h = linearExtrudeHeightExpression(node);
    const QString c = node.linearExtrudeCenter ? QStringLiteral("true") : QStringLiteral("false");
    const QString t = linearExtrudeParam(node, 1, QString::number(node.linearExtrudeTwist, 'g'));
    const QString sl = linearExtrudeParam(node, 2, QString::number(node.linearExtrudeSlices));
    const QString sc = linearExtrudeParam(node, 3, QString::number(node.linearExtrudeScaleVal, 'g'));
    return QStringLiteral("linear_extrude(height = %1, center = %2, twist = %3, slices = %4, scale = %5)")
        .arg(h, c, t, sl, sc);
}

// The function-name part drawn as a static label; individual params follow.
static const char *kLinearExtrudePrefix = "linear_extrude(";

// Pixel offset from groupRect.left() where the expression text starts.
// = iconLeft(30) + headerIconSize(24) + gap(10) = 64.
static constexpr qreal kLinearExtrudeTextLeft = 64.0;

QRectF linearExtrudeHeightTextRect(const QRectF &groupRect, const QFontMetricsF &metrics)
{
    // Returns the pill rect for the height parameter (param index 0).
    const qreal labelW = metrics.horizontalAdvance(QStringLiteral("h="));
    const qreal left   = groupRect.left() + kLinearExtrudeTextLeft
                         + metrics.horizontalAdvance(QLatin1String(kLinearExtrudePrefix))
                         + labelW;
    return QRectF(left, groupRect.top() + 7.0, qMax(20.0, 0.0), 14.0);
}

QString linearExtrudeExtraParams(const SceneDocument::TreeNode &node)
{
    QString extra;
    if (node.transformExpressions.size() > 1) {
        const QString twistStr = node.transformExpressions[1].trimmed();
        if (!twistStr.isEmpty() && twistStr != QLatin1String("0"))
            extra += QStringLiteral(", twist=") + twistStr;
        else if (qAbs(node.linearExtrudeTwist) > 0.001f)
            extra += QStringLiteral(", twist=") + QString::number(node.linearExtrudeTwist, 'g');
    }
    if (node.transformExpressions.size() > 2) {
        const QString slicesStr = node.transformExpressions[2].trimmed();
        if (!slicesStr.isEmpty() && slicesStr != QLatin1String("0"))
            extra += QStringLiteral(", slices=") + slicesStr;
        else if (node.linearExtrudeSlices > 0)
            extra += QStringLiteral(", slices=") + QString::number(node.linearExtrudeSlices);
    }
    if (node.transformExpressions.size() > 3) {
        const QString scaleStr = node.transformExpressions[3].trimmed();
        if (!scaleStr.isEmpty() && scaleStr != QLatin1String("1"))
            extra += QStringLiteral(", scale=") + scaleStr;
        else if (qAbs(node.linearExtrudeScaleVal - 1.0f) > 0.001f)
            extra += QStringLiteral(", scale=") + QString::number(node.linearExtrudeScaleVal, 'g');
    }
    return extra;
}

qreal linearExtrudeHeaderMinWidth(const QString &heightExpression,
                                   const QFontMetricsF &metrics,
                                   const QString &centerExpression,
                                   const QString &twistExpression,
                                   const QString &slicesExpression,
                                   const QString &scaleExpression)
{
    // Mirrors linearExtrudeParamInfos() position logic.
    qreal cursorX = kLinearExtrudeTextLeft
                    + metrics.horizontalAdvance(QLatin1String(kLinearExtrudePrefix));

    struct { QString label; QString expr; bool isNumber; } params[] = {
        {QStringLiteral("h="),   heightExpression.trimmed().isEmpty()  ? QStringLiteral("20")    : heightExpression.trimmed(),  true},
        {QStringLiteral(", c="), centerExpression.trimmed().isEmpty()  ? QStringLiteral("false") : centerExpression.trimmed(),  false},
        {QStringLiteral(", t="), twistExpression.trimmed().isEmpty()   ? QStringLiteral("0")     : twistExpression.trimmed(),   true},
        {QStringLiteral(", sl="),slicesExpression.trimmed().isEmpty()  ? QStringLiteral("0")     : slicesExpression.trimmed(),  true},
        {QStringLiteral(", sc="),scaleExpression.trimmed().isEmpty()   ? QStringLiteral("1")     : scaleExpression.trimmed(),   true},
    };
    for (const auto &p : params) {
        const qreal labelW = metrics.horizontalAdvance(p.label);
        const qreal exprW  = metrics.horizontalAdvance(p.expr);
        const qreal pillW  = p.isNumber ? qMax(exprW + 8.0, 20.0) : exprW + 8.0;
        cursorX += labelW + pillW;
    }
    cursorX += metrics.horizontalAdvance(QLatin1String(")"));
    return cursorX + 36.0; // 36 = right margin for chevron
}

QVector<ExpressionTextSpan> linearExtrudeHeightTextSpans(const QRectF &groupRect,
                                                         const QString &heightExpression,
                                                         const QFontMetricsF &metrics)
{
    const QString expression = heightExpression.trimmed().isEmpty()
        ? QStringLiteral("20")
        : heightExpression.trimmed();
    return expressionSpansInTextRect(linearExtrudeHeightTextRect(groupRect, metrics),
                                     expression,
                                     metrics);
}

QVector<LinearExtrudeParamInfo> linearExtrudeParamInfos(
    const QRectF &groupRect,
    const QString &heightExpression,
    const QString &centerExpression,
    const QString &twistExpression,
    const QString &slicesExpression,
    const QString &scaleExpression,
    const QFontMetricsF &metrics)
{
    // textLeft matches the renderer: iconRect.right() + 10 = rect.left() + 30 + 24 + 10 = rect.left() + 64
    const qreal textLeft = groupRect.left() + kLinearExtrudeTextLeft;
    const qreal pillTop  = groupRect.top() + 7.0;
    const qreal pillH    = 14.0;

    // Cursor starts right after the static prefix "linear_extrude("
    qreal cursorX = textLeft + metrics.horizontalAdvance(QLatin1String(kLinearExtrudePrefix));

    struct ParamDef { int index; QString label; QString expression; bool isNumber; };
    const QVector<ParamDef> defs = {
        {0, QStringLiteral("h="),   heightExpression.trimmed().isEmpty()  ? QStringLiteral("20")    : heightExpression.trimmed(),  true},
        {1, QStringLiteral(", c="), centerExpression.trimmed().isEmpty()  ? QStringLiteral("false") : centerExpression.trimmed(),  false},
        {2, QStringLiteral(", t="), twistExpression.trimmed().isEmpty()   ? QStringLiteral("0")     : twistExpression.trimmed(),   true},
        {3, QStringLiteral(", sl="),slicesExpression.trimmed().isEmpty()  ? QStringLiteral("0")     : slicesExpression.trimmed(),  true},
        {4, QStringLiteral(", sc="),scaleExpression.trimmed().isEmpty()   ? QStringLiteral("1")     : scaleExpression.trimmed(),   true},
    };

    QVector<LinearExtrudeParamInfo> infos;
    for (const ParamDef &def : defs) {
        LinearExtrudeParamInfo info;
        info.paramIndex = def.index;
        info.label      = def.label;
        info.expression = def.expression;
        info.isNumber   = def.isNumber;

        const qreal labelW = metrics.horizontalAdvance(def.label);
        const qreal exprW  = metrics.horizontalAdvance(def.expression);
        const qreal pillW  = def.isNumber ? qMax(exprW + 8.0, 20.0) : exprW + 8.0;

        info.textRect = QRectF(cursorX + labelW, pillTop, pillW, pillH);
        cursorX += labelW + pillW; // advance by full pill width, not just text width
        infos.append(info);
    }

    return infos;
}

// ── rotate_extrude layout helpers ──────────────────────────────────────────

static const char *kRotateExtrudePrefix = "rotate_extrude(angle=";
static constexpr qreal kRotateExtrudeTextLeft = 64.0;

QString rotateExtrudeAngleExpression(const SceneDocument::TreeNode &node)
{
    if (!node.transformExpressions.isEmpty()) {
        const QString expr = node.transformExpressions.first().trimmed();
        if (!expr.isEmpty())
            return expr;
    }
    const qreal angle = node.scale.x();
    return QString::number(angle, 'g');
}

qreal rotateExtrudeHeaderMinWidth(const QString &angleExpression,
                                   const QFontMetricsF &metrics)
{
    const QString expr = angleExpression.trimmed().isEmpty()
        ? QStringLiteral("360") : angleExpression.trimmed();
    const qreal prefixW = metrics.horizontalAdvance(QLatin1String(kRotateExtrudePrefix));
    const qreal exprW   = metrics.horizontalAdvance(expr);
    const qreal pillW   = qMax(exprW + 8.0, 20.0);
    const qreal closeW  = metrics.horizontalAdvance(QStringLiteral(")"));
    return kRotateExtrudeTextLeft + prefixW + pillW + closeW + 36.0;
}

QVector<RotateExtrudeParamInfo> rotateExtrudeParamInfos(
    const QRectF &groupRect,
    const QString &angleExpression,
    const QFontMetricsF &metrics)
{
    const QString expr = angleExpression.trimmed().isEmpty()
        ? QStringLiteral("360") : angleExpression.trimmed();

    const qreal textLeft = groupRect.left() + kRotateExtrudeTextLeft;
    const qreal prefixW  = metrics.horizontalAdvance(QLatin1String(kRotateExtrudePrefix));
    const qreal exprW    = metrics.horizontalAdvance(expr);
    const qreal pillW    = qMax(exprW + 8.0, 20.0);
    const qreal pillTop  = groupRect.top() + 7.0;
    const qreal pillH    = 14.0;

    RotateExtrudeParamInfo info;
    info.paramIndex = 0;
    info.expression = expr;
    info.textRect   = QRectF(textLeft + prefixW, pillTop, pillW, pillH);

    return {info};
}

QRectF shapeParameterControlRect(const QRectF &primitiveRect, int index, int count)
{
    if (index < 0 || count <= 0 || index >= count)
        return QRectF();

    // Row is positioned outside the card border (right of PrimitiveCardWidth).
    const qreal left = primitiveRect.left() + PrimitiveCardWidth + 4.0;
    const qreal width = primitiveRect.right() - left - 4.0;
    const qreal gap = 2.0;
    const qreal availableHeight = PrimitiveHeight - 6.0;
    const qreal rowHeight = qMin<qreal>(14.0, (availableHeight - gap * (count - 1)) / count);
    const qreal totalHeight = rowHeight * count + gap * (count - 1);
    const qreal top = primitiveRect.top() + (PrimitiveHeight - totalHeight) * 0.5 + index * (rowHeight + gap);
    return QRectF(left, top, width, rowHeight);
}

QRectF cubeShapeParameterFieldRect(const QRectF &rowRect)
{
    return rowRect.adjusted(16.0, 0.0, -4.0, 0.0);
}

bool shapeUsesExpressionPillLayout(const ShapeNode &shape)
{
    return shape.type != ShapeNode::Polygon2D
        && shape.type != ShapeNode::Polyhedron;
}

QRectF shapeParameterFieldRect(const QRectF &rowRect, const ShapeNode &shape)
{
    if (shape.type == ShapeNode::Cube)
        return cubeShapeParameterFieldRect(rowRect);
    return QRectF(rowRect.left() + PrimitiveParamLabelArea,
                  rowRect.top(),
                  rowRect.width() - PrimitiveParamLabelArea,
                  rowRect.height());
}

QVector<ExpressionNumberControl> shapeParameterNumberControls(const QRectF &primitiveRect, int paramIndex, int paramCount, const QString &expression, const QFontMetricsF &metrics)
{
    const QRectF rowRect = shapeParameterControlRect(primitiveRect, paramIndex, paramCount);
    if (!rowRect.isValid())
        return {};

    const QRectF textRect(rowRect.left() + PrimitiveParamLabelArea,
                          rowRect.top(),
                          rowRect.right() - rowRect.left() - PrimitiveParamLabelArea,
                          rowRect.height());

    QVector<ExpressionNumberControl> controls;
    for (const ExpressionTextSpan &span : expressionSpansInTextRect(textRect, expression, metrics)) {
        if (span.number)
            controls.append({span.text, span.start, span.length, span.rect});
    }
    return controls;
}

QVector<ModuleCallParamControl> moduleCallParamControls(const QRectF &cardRect,
                                                        const QString &moduleName,
                                                        const QVector<ModuleCallParam> &params,
                                                        const QFontMetricsF &metrics)
{
    QVector<ModuleCallParamControl> controls;
    if (params.isEmpty())
        return controls;

    qreal x = cardRect.left() + 46.0 + metrics.horizontalAdvance(moduleName + QStringLiteral("("));
    for (int i = 0; i < params.size(); ++i) {
        x += metrics.horizontalAdvance(params[i].name + QStringLiteral(" = "));
        const QRectF exprRect(x, cardRect.top(), cardRect.right() - x, VariableHeight);
        for (const ExpressionTextSpan &span : expressionSpansInTextRect(exprRect, params[i].expression, metrics)) {
            if (span.number)
                controls.append({params[i].varNodeId, span.start, span.length, span.rect});
        }
        x += metrics.horizontalAdvance(params[i].expression);
        if (i < params.size() - 1)
            x += metrics.horizontalAdvance(QStringLiteral(", "));
    }
    return controls;
}

} // namespace SceneTreeGraphics
