#include "scenetreecanvasgraphics.h"
#include "scenetreeexpressionlayout.h"
#include "scenetreepreviewgeometry.h"
#include "scenetreetoolmetadata.h"

#include <QFontMetricsF>
#include <QtGlobal>

namespace SceneTreeGraphics {

QVector<ShapeParameterControl> shapeParameterControls(const ShapeNode &shape)
{
    auto expr = [&](int idx, qreal numericValue) -> QString {
        if (idx < shape.parameterExpressions.size() && !shape.parameterExpressions[idx].isEmpty())
            return shape.parameterExpressions[idx];
        return QString::number(numericValue, 'g');
    };

    if (shape.type == ShapeNode::Sphere || shape.type == ShapeNode::Circle)
        return {{QStringLiteral("R"), shape.radius, expr(0, shape.radius)}};

    if (shape.type == ShapeNode::Square)
        return {{QStringLiteral("X"), shape.size.x(), expr(0, shape.size.x())},
                {QStringLiteral("Y"), shape.size.y(), expr(1, shape.size.y())}};

    if (shape.type == ShapeNode::Cylinder)
        return {{QStringLiteral("R"), shape.radius, expr(0, shape.radius)},
                {QStringLiteral("H"), shape.height, expr(1, shape.height)}};

    if (shape.type == ShapeNode::Cone)
        return {{QStringLiteral("R1"), shape.radius,  expr(0, shape.radius)},
                {QStringLiteral("R2"), shape.radius2, expr(1, shape.radius2)},
                {QStringLiteral("H"),  shape.height,  expr(2, shape.height)}};

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
    // OpenSCAD expressions already have spaces around operators (e.g. "a + b"),
    // so no extra gap is needed — the natural spaces provide visual separation.
    const qreal operatorGap = 0.0;
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
            spans.append({text, start, index - start, QRectF(x, textRect.top(), width, textRect.height()), false});
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

QVector<ExpressionTextSpan> expressionTextSpans(const QRectF &variableRect, const QString &expression, const QFontMetricsF &metrics, qreal nameTextWidth)
{
    return expressionSpansInTextRect(variableExpressionTextRect(variableRect, nameTextWidth), expression, metrics);
}

QVector<ExpressionNumberControl> expressionNumberControls(const QRectF &variableRect, const QString &expression, const QFontMetricsF &metrics, qreal nameTextWidth)
{
    QVector<ExpressionNumberControl> controls;
    const QVector<ExpressionTextSpan> spans = expressionTextSpans(variableRect, expression, metrics, nameTextWidth);
    for (const ExpressionTextSpan &span : spans) {
        if (span.number)
            controls.append({span.text, span.start, span.length, span.rect});
    }
    return controls;
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

QRectF linearExtrudeHeightTextRect(const QRectF &groupRect, const QFontMetricsF &metrics)
{
    const QString prefix = QStringLiteral("linear_extrude(height = ");
    const qreal left = groupRect.left() + 68.0 + metrics.horizontalAdvance(prefix);
    return QRectF(left,
                  groupRect.top() + 7.0,
                  qMax<qreal>(0.0, groupRect.right() - left - 8.0),
                  16.0);
}

qreal linearExtrudeHeaderMinWidth(const QString &heightExpression, const QFontMetricsF &metrics)
{
    const QString expression = heightExpression.trimmed().isEmpty()
        ? QStringLiteral("20")
        : heightExpression.trimmed();
    const QString prefix = QStringLiteral("linear_extrude(height = ");
    const QRectF measureRect(0.0, 0.0, 2048.0, GroupHeaderHeight);

    qreal right = 64.0 + metrics.horizontalAdvance(prefix);
    const QVector<ExpressionTextSpan> spans = linearExtrudeHeightTextSpans(measureRect, expression, metrics);
    for (const ExpressionTextSpan &span : spans)
        right = qMax(right, span.rect.right());

    return right + metrics.horizontalAdvance(QStringLiteral(")")) + 36.0;
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
