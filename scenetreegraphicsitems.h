#ifndef SCENETREEGRAPHICSITEMS_H
#define SCENETREEGRAPHICSITEMS_H

#include "scenetreegraphicsconstants.h"

#include <QColor>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>
#include <functional>

class QGraphicsItem;
class QGraphicsPathItem;
class QGraphicsScene;
class QPointF;

namespace SceneTreeGraphics {

constexpr int DragPreviewPulseDataRole = 0x3d5;

void appendPreviewItem(QVector<QGraphicsItem *> *items, QGraphicsItem *item);
QGraphicsPathItem *addDropSlotMarker(QGraphicsScene *scene, QVector<QGraphicsItem *> *items, const QRectF &rect, qreal zValue);

QGraphicsItem *createTreeNodeDragHandleItem(int nodeId, const QString &label, const QRectF &rect, const QRectF &sourceRect, std::function<void(int)> onSelected, const QSizeF &previewSize, std::function<void(const QPointF &, const QSizeF &, const QString &)> onPreviewMoved, std::function<void()> onPreviewFinished, std::function<void(int, const QPointF &)> onDropped);
QGraphicsItem *createTreeNodeSelectionItem(int nodeId, const QRectF &rect, qreal zValue, std::function<void(int)> onSelected);
QGraphicsItem *createPaletteToolItem(const QString &label, const QColor &fill, int theme, std::function<void(const QPointF &, const QSizeF &, const QString &)> onPreviewMoved, std::function<void()> onPreviewFinished, std::function<void(const QString &, const QPointF &)> onDropped, std::function<void(const QString &, bool)> onHoverChanged = {});

} // namespace SceneTreeGraphics

#endif
