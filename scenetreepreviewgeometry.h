#ifndef SCENETREEPREVIEWGEOMETRY_H
#define SCENETREEPREVIEWGEOMETRY_H

#include "scenetreeexpressionlayout.h"
#include "scenetreegraphicsconstants.h"
#include "shapenode.h"

#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

class QFontMetricsF;

namespace SceneTreeGraphics {

struct ModuleCallParam {
    int varNodeId;
    QString name;
    QString expression;
};

struct ModuleCallParamControl {
    int paramVarNodeId;
    int numberStart;
    int numberLength;
    QRectF rect;
};

QString primitiveNumberText(const QString &label, int fallbackId);
int insertionIndexForY(const QVector<QRectF> &childRects, qreal y, int minimumIndex = 0);
QSizeF defaultPreviewSize();
QSizeF variablePreviewSize(const QString &name = QString(), const QString &expression = QString());
QSizeF groupPreviewSize();
QSizeF differencePreviewSize();
QSizeF previewSizeForTool(const QString &tool);
QSizeF primitivePreviewSize(const ShapeNode &shape);
QSizeF moduleCallPreviewSize(const QString &moduleName, const QVector<ModuleCallParam> &params);
QVector<ModuleCallParamControl> moduleCallParamControls(const QRectF &cardRect, const QString &moduleName, const QVector<ModuleCallParam> &params, const QFontMetricsF &metrics);
QRectF placeholderRectForInsertIndex(const QRectF &contentRect, const QVector<QRectF> &childRects, int insertIndex, const QSizeF &previewSize);
QRectF slotMarkerRectForInsertIndex(const QRectF &contentRect, const QVector<QRectF> &childRects, int insertIndex);
QRectF expandedGroupRectForPreview(const QRectF &groupRect, const QRectF &placeholderRect, const QVector<QRectF> &childRects, int insertIndex, const QSizeF &previewSize);
QRectF expandedGroupRectForChangedChild(const QRectF &groupRect, const QVector<QRectF> &childRects, const QRectF &oldChildRect, const QRectF &newChildRect);

} // namespace SceneTreeGraphics

#endif
