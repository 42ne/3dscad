#ifndef SCENETREEEXPRESSIONLAYOUT_H
#define SCENETREEEXPRESSIONLAYOUT_H

#include "scenedocument.h"
#include "scenetreegraphicsconstants.h"
#include "shapenode.h"

#include <QRectF>
#include <QString>
#include <QVector>

class QFontMetricsF;
class QPointF;

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

class ExpressionPillLayout {
public:
    ExpressionPillLayout() = default;
    ExpressionPillLayout(const QRectF &fieldRect,
                         const QString &expression,
                         const QFontMetricsF &metrics);

    const QRectF &fieldRect() const { return m_fieldRect; }
    const QString &expression() const { return m_expression; }
    const QVector<ExpressionTextSpan> &spans() const { return m_spans; }

    bool isSimpleNumber() const;
    bool firstNumberSpan(ExpressionTextSpan *span) const;
    bool spanAt(const QPointF &scenePosition,
                ExpressionTextSpan *span,
                bool matchSimpleNumberField = false) const;

    QRectF pillRectFor(const ExpressionTextSpan &span) const;
    QRectF visiblePillRectFor(const ExpressionTextSpan &span) const;
    QRectF visualRectFor(const ExpressionTextSpan &span) const;
    QRectF contentRect() const;
    QRectF expressionEditRect() const;
    QRectF scrollZoneRect(int numberStart) const;

private:
    QRectF m_fieldRect;
    QString m_expression;
    QVector<ExpressionTextSpan> m_spans;
};

struct LinearExtrudeParamInfo {
    int paramIndex = 0;    // 0=height, 1=center, 2=twist, 3=slices, 4=scale
    QString label;          // e.g. "height=", ", center=", ", twist=", etc.
    QString expression;     // e.g. "15", "true", "0", "1", "0.3"
    QRectF textRect;        // interactive pill area (in group-relative coords)
    bool isNumber = true;   // false for center (boolean toggle)
};

struct RotateExtrudeParamInfo {
    int paramIndex = 0;    // always 0 (angle)
    QString expression;    // e.g. "360"
    QRectF textRect;       // interactive pill area
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
QString linearExtrudeParam(const SceneDocument::TreeNode &node, int index, const QString &fallback);
QString linearExtrudeExtraParams(const SceneDocument::TreeNode &node);
QRectF linearExtrudeHeightTextRect(const QRectF &groupRect, const QFontMetricsF &metrics);
qreal linearExtrudeHeaderMinWidth(const QString &heightExpression,
                                   const QFontMetricsF &metrics,
                                   const QString &centerExpression,
                                   const QString &twistExpression,
                                   const QString &slicesExpression,
                                   const QString &scaleExpression);
QVector<ExpressionTextSpan> linearExtrudeHeightTextSpans(const QRectF &groupRect, const QString &heightExpression, const QFontMetricsF &metrics);
QVector<LinearExtrudeParamInfo> linearExtrudeParamInfos(
    const QRectF &groupRect,
    const QString &heightExpression,
    const QString &centerExpression,
    const QString &twistExpression,
    const QString &slicesExpression,
    const QString &scaleExpression,
    const QFontMetricsF &metrics);
QString rotateExtrudeAngleExpression(const SceneDocument::TreeNode &node);
qreal rotateExtrudeHeaderMinWidth(const QString &angleExpression,
                                   const QFontMetricsF &metrics);
QVector<RotateExtrudeParamInfo> rotateExtrudeParamInfos(
    const QRectF &groupRect,
    const QString &angleExpression,
    const QFontMetricsF &metrics);

QVector<ShapeParameterControl> shapeParameterControls(const ShapeNode &shape);
QRectF shapeParameterControlRect(const QRectF &primitiveRect, int index, int count);
QRectF cubeShapeParameterFieldRect(const QRectF &rowRect);
QVector<ExpressionNumberControl> shapeParameterNumberControls(const QRectF &primitiveRect, int paramIndex, int paramCount, const QString &expression, const QFontMetricsF &metrics);
QVector<ExpressionTextSpan> expressionSpansInTextRect(const QRectF &textRect, const QString &expression, const QFontMetricsF &metrics);
QVector<ExpressionTextSpan> expressionTextSpans(const QRectF &variableRect, const QString &expression, const QFontMetricsF &metrics, qreal nameTextWidth);
QVector<ExpressionNumberControl> expressionNumberControls(const QRectF &variableRect, const QString &expression, const QFontMetricsF &metrics, qreal nameTextWidth);
QRectF variableExpressionTextRect(const QRectF &variableRect, qreal nameTextWidth);

} // namespace SceneTreeGraphics

#endif
