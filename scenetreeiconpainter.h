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
// When `withGrid` is true a faint isometric floor grid is drawn inside the glass
// panel, behind where the glyph will be painted (used for the larger tree-node
// primitive icons). Returns the centred glyph rect to paint the symbol into.
QRectF paintToolbarIconFrame(QPainter *painter, const QRectF &rect, const QColor &accent, bool selected = false, bool withGrid = false);
void paintToolbarPrimitiveIcon(QPainter *painter, ShapeNode::Type type, const QRectF &rect, bool selected = false);
// When `onGlass` is true the icon is drawn directly onto the dark glass toolbar
// panel: the opaque white backing plate is skipped and the symbol is stroked in
// a bright accent so it reads on the dark frame (matching the primitive icons).
// When false (default) the white backing plate is drawn — used for the operation
// icons inside light tree-node group headers.
void paintOperationIcon(QPainter *painter, SceneDocument::TreeNode::Operation operation, const QRectF &rect, const QColor &accent, qreal symbolInset = 7.0, bool onGlass = false);

} // namespace SceneTreeGraphics

#endif
