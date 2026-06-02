#ifndef SCENETREEEXPRESSIONLAYOUT_H
#define SCENETREEEXPRESSIONLAYOUT_H

#include "scenedocument.h"
#include "scenetreegraphicsconstants.h"
#include "shapenode.h"

#include <QRectF>
#include <QString>
#include <QVector>

class QFontMetricsF;

namespace SceneTreeGraphics {

struct ShapeParameterControl {
    QString label;
    qreal value = 0.0;
    QString expression;
};

struct ExpressionNumberControl {
    QString text;
    int start = 0;
    int length = 0;
    QRectF rect;
};

struct ExpressionTextSpan {
    QString text;
    int start = 0;
    int length = 0;
    QRectF rect;
    bool number = false;
};

QString transformAxisExpression(const SceneDocument::TreeNode &node, int axis);
qreal transformHeaderWidthForNode(const SceneDocument::TreeNode &node);
QRectF transformParameterControlRect(const QRectF &groupRect, int axis, qreal headerWidth = TransformHeaderWidth);
QVector<ExpressionNumberControl> transformParameterNumberControls(const QRectF &groupRect, int axis, const QString &expression, const QFontMetricsF &metrics, qreal headerWidth = TransformHeaderWidth);
QString forLoopVariableName(const SceneDocument::TreeNode &node);
QString forLoopRangeExpression(const SceneDocument::TreeNode &node);
QRectF forLoopRangeTextRect(const QRectF &groupRect, const QString &variableName, const QFontMetricsF &metrics);
qreal forLoopHeaderMinWidth(const QString &variableName, const QString &rangeExpression, const QFontMetricsF &metrics);
QVector<ExpressionTextSpan> forLoopRangeTextSpans(const QRectF &groupRect, const QString &variableName, const QString &rangeExpression, const QFontMetricsF &metrics);
QVector<ExpressionNumberControl> forLoopRangeNumberControls(const QRectF &groupRect, const QString &variableName, const QString &rangeExpression, const QFontMetricsF &metrics);
QString linearExtrudeHeightExpression(const SceneDocument::TreeNode &node);
QRectF linearExtrudeHeightTextRect(const QRectF &groupRect, const QFontMetricsF &metrics);
qreal linearExtrudeHeaderMinWidth(const QString &heightExpression, const QFontMetricsF &metrics);
QVector<ExpressionTextSpan> linearExtrudeHeightTextSpans(const QRectF &groupRect, const QString &heightExpression, const QFontMetricsF &metrics);
QVector<ShapeParameterControl> shapeParameterControls(const ShapeNode &shape);
QRectF shapeParameterControlRect(const QRectF &primitiveRect, int index, int count);
QVector<ExpressionNumberControl> shapeParameterNumberControls(const QRectF &primitiveRect, int paramIndex, int paramCount, const QString &expression, const QFontMetricsF &metrics);
QVector<ExpressionTextSpan> expressionSpansInTextRect(const QRectF &textRect, const QString &expression, const QFontMetricsF &metrics);
QVector<ExpressionTextSpan> expressionTextSpans(const QRectF &variableRect, const QString &expression, const QFontMetricsF &metrics, qreal nameTextWidth);
QVector<ExpressionNumberControl> expressionNumberControls(const QRectF &variableRect, const QString &expression, const QFontMetricsF &metrics, qreal nameTextWidth);
QRectF variableExpressionTextRect(const QRectF &variableRect, qreal nameTextWidth);

} // namespace SceneTreeGraphics

#endif
