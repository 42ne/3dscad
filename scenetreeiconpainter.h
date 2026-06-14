#ifndef SCENETREEICONPAINTER_H
#define SCENETREEICONPAINTER_H

#include "scenedocument.h"
#include "scenetreegraphicsconstants.h"
#include "shapenode.h"

#include <QColor>
#include <QRectF>
#include <QString>

class QPainter;

namespace SceneTreeGraphics {

void paintPrimitiveIcon(QPainter *painter, ShapeNode::Type type, const QRectF &rect);

// Draws the glyph for an upcoming tool that does not yet have an Operation or
// primitive mapping (text, offset, projection, import). Returns true if
// `toolName` was recognized and a glyph was drawn into `rect`. Designed so a
// tool name can be added to the palette and render correctly before the rest of
// the feature is wired up.
bool paintFutureToolIcon(QPainter *painter, const QString &toolName, const QRectF &rect);
QRectF paintToolbarIconFrame(QPainter *painter, const QRectF &rect, const QColor &accent, bool selected = false);
void paintToolbarPrimitiveIcon(QPainter *painter, ShapeNode::Type type, const QRectF &rect, bool selected = false);
void paintOperationIcon(QPainter *painter, SceneDocument::TreeNode::Operation operation, const QRectF &rect, const QColor &accent, qreal symbolInset = 7.0);

} // namespace SceneTreeGraphics

#endif
