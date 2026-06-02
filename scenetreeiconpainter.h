#ifndef SCENETREEICONPAINTER_H
#define SCENETREEICONPAINTER_H

#include "scenedocument.h"
#include "scenetreegraphicsconstants.h"
#include "shapenode.h"

#include <QColor>
#include <QRectF>

class QPainter;

namespace SceneTreeGraphics {

void paintPrimitiveIcon(QPainter *painter, ShapeNode::Type type, const QRectF &rect);
QRectF paintToolbarIconFrame(QPainter *painter, const QRectF &rect, const QColor &accent, bool selected = false);
void paintToolbarPrimitiveIcon(QPainter *painter, ShapeNode::Type type, const QRectF &rect, bool selected = false);
void paintOperationIcon(QPainter *painter, SceneDocument::TreeNode::Operation operation, const QRectF &rect, const QColor &accent, qreal symbolInset = 7.0);

} // namespace SceneTreeGraphics

#endif
