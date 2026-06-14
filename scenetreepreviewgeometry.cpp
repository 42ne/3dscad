#include "scenetreecanvasgraphics.h"
#include "scenetreepreviewgeometry.h"
#include "scenetreetoolmetadata.h"

#include <QFontMetricsF>
#include <QtGlobal>
#include <limits>

namespace SceneTreeGraphics {

QSizeF primitivePreviewSize(const ShapeNode &shape)
{
    if (shape.type == ShapeNode::Polygon2D) {
        const int points = qMax(3, shape.polyhedronPoints.size());
        return QSizeF(190.0, PrimitiveHeight + 10.0 + 22.0 + points * 21.0 + 24.0);
    }

    const QFontMetricsF metrics(sceneTreeValueFont());
    const QVector<ShapeParameterControl> controls = shapeParameterControls(shape);
    qreal maxExprWidth = 0.0;
    for (const auto &control : controls) {
        const qreal w = expressionVisualWidth(control.expression, metrics);
        maxExprWidth = qMax(maxExprWidth, w);
    }
    maxExprWidth = qMax(maxExprWidth, expressionVisualWidth(QStringLiteral("0"), metrics));
    // Label area: cube uses kLabelW(14)+gap(2)+right-trim(4)=20; others use PrimitiveParamLabelArea.
    const qreal labelArea = shapeUsesExpressionPillLayout(shape) && shape.type == ShapeNode::Cube
        ? 20.0 : PrimitiveParamLabelArea;
    // +8 right margin: ensures the last pill's 1px border and potential fp rounding fit.
    const qreal width = qMax<qreal>(PrimitiveWidth,
                                    PrimitiveCardWidth + 4.0 + labelArea + maxExprWidth + 8.0);
    return QSizeF(width, PrimitiveHeight);
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

QSizeF variablePreviewSize(const QString &name, const QString &expression)
{
    const QFontMetricsF metrics(sceneTreeGraphicsFont());
    const QString trimmedName = name.trimmed();
    const QString trimmedExpr = expression.trimmed();

    // Guarantee a minimum name width of ~3 chars so tiny names still look OK
    const qreal nameWidth = qMax(metrics.horizontalAdvance(QStringLiteral("xxx")),
                                 metrics.horizontalAdvance(trimmedName));
    const qreal exprWidth = qMax(expressionVisualWidth(QStringLiteral("0"), metrics),
                                 expressionVisualWidth(trimmedExpr, metrics));

    // Single-row: badge(38) + nameW + gap(4) + eq(12) + gap(2) + exprW + right_pad(6)
    return QSizeF(qMax(PrimitiveWidth, 62.0 + nameWidth + exprWidth), VariableHeight);
}

static qreal moduleCallExprAdvance(const QString &expr, const QFontMetricsF &metrics)
{
    return expressionVisualWidth(expr, metrics);
}

QSizeF moduleCallPreviewSize(const QString &moduleName, const QVector<ModuleCallParam> &params)
{
    const QFontMetricsF metrics(sceneTreeGraphicsFont());
    // "moduleName(" + params + ")" — measure each piece with actual font
    qreal textWidth = metrics.horizontalAdvance(moduleName.trimmed() + QStringLiteral("()"));
    for (int i = 0; i < params.size(); ++i) {
        textWidth += metrics.horizontalAdvance(params[i].name + QStringLiteral(" = "));
        textWidth += moduleCallExprAdvance(params[i].expression.trimmed(), metrics);
        if (i < params.size() - 1)
            textWidth += metrics.horizontalAdvance(QStringLiteral(", "));
    }
    textWidth += 8.0; // a little right padding
    return QSizeF(qMax(PrimitiveWidth, 46.0 + textWidth), VariableHeight);
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
    if (isVariableToolName(tool))
        return variablePreviewSize();
    if (tool == "call")
        return moduleCallPreviewSize(QStringLiteral("module"), {});
    if (ShapeNode::isPrimitiveTool(tool))
    {
        ShapeNode preview;
        preview.type = ShapeNode::typeFromToolName(tool);
        if (preview.type == ShapeNode::Polygon2D) {
            preview.polyhedronPoints = {QVector3D(), QVector3D(), QVector3D()};
            return primitivePreviewSize(preview);
        }
        return primitivePreviewSize(preview);
    }
    if (tool == "difference")
        return differencePreviewSize();
    if (tool == "intersection")
        return QSizeF(GroupWideMinWidth, GroupHeaderHeight + GroupPadding * 2.0 + PrimitiveHeight);
    if (tool == "module") {
        // Height matches the actual empty-module render: header + param placeholder + separator
        // + call template + body label + one body-slot (+PrimitiveHeight).
        const qreal bodyContent = 16.0 + VariableHeight + ChildGap    // "Parameters" label + empty param row
                                + 16.0 + ChildGap                     // separator / template-label gap
                                + VariableHeight + ChildGap            // call template row
                                + 16.0 + ChildGap                     // "Body" label gap
                                + PrimitiveHeight;                     // visible body drop zone
        return QSizeF(GroupModuleMinWidth,
                      GroupHeaderHeight + GroupPadding * 2.0 + bodyContent);
    }
    if (tool == "for")
        return QSizeF(qMax(GroupWideMinWidth,
                           forLoopHeaderMinWidth(QStringLiteral("i"),
                                                 QStringLiteral("[0 : 1 : 3]"),
                                                 QFontMetricsF(sceneTreeGraphicsFont()))),
                      GroupHeaderHeight + GroupPadding * 2.0 + PrimitiveHeight);
    if (tool == "if")
        return QSizeF(qMax(GroupWideMinWidth,
                           conditionHeaderMinWidth(QStringLiteral("true"),
                                                   QFontMetricsF(sceneTreeGraphicsFont()))),
                      GroupHeaderHeight + GroupPadding * 2.0 + PrimitiveHeight);
    if (tool == "linear_extrude")
        return QSizeF(qMax(GroupWideMinWidth,
                           linearExtrudeHeaderMinWidth(QStringLiteral("20"),
                                                        QFontMetricsF(sceneTreeGraphicsFont()),
                                                        QStringLiteral("false"),
                                                        QStringLiteral("0"),
                                                        QStringLiteral("0"),
                                                        QStringLiteral("1"))),
                      GroupHeaderHeight + GroupPadding * 2.0 + PrimitiveHeight);
    if (tool == "rotate_extrude")
        return QSizeF(qMax(GroupWideMinWidth,
                           rotateExtrudeHeaderMinWidth(QStringLiteral("360"),
                                                       QFontMetricsF(sceneTreeGraphicsFont()))),
                      GroupHeaderHeight + GroupPadding * 2.0 + PrimitiveHeight);
    if (tool == "color")
        return QSizeF(GroupWideMinWidth, GroupHeaderHeight + GroupPadding * 2.0 + PrimitiveHeight);
    if (tool == "translate" || tool == "rotate" || tool == "scale")
        return transformPreviewSize();

    return groupPreviewSize();
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

} // namespace SceneTreeGraphics
