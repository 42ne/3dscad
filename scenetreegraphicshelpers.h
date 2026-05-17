#ifndef SCENETREEGRAPHICSHELPERS_H
#define SCENETREEGRAPHICSHELPERS_H

#include "scenedocument.h"
#include "shapenode.h"

#include <QColor>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QPainterPath>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>
#include <functional>

class QObject;
class QGraphicsItem;
class QGraphicsScene;
class QPainter;
class QPointF;
class QBrush;
class QPen;

namespace SceneTreeGraphics {

constexpr qreal ToolbarX = 12.0;
constexpr qreal ToolbarY = 12.0;
constexpr qreal ToolSize = 54.0;
constexpr qreal ToolGap = 8.0;
constexpr qreal TreeX = 12.0;
constexpr qreal TreeY = 92.0;
constexpr qreal PrimitiveWidth = 148.0;
constexpr qreal PrimitiveHeight = 42.0;
constexpr qreal PrimitiveIconSize = 34.0;
constexpr qreal GroupMinWidth = 128.0;
constexpr qreal GroupModuleMinWidth = 136.0;
constexpr qreal GroupWideMinWidth = 164.0;
constexpr qreal GroupHeaderHeight = 28.0;
constexpr qreal TransformHeaderWidth = 78.0;
constexpr qreal GroupPadding = 12.0;
constexpr qreal ChildGap = 10.0;
constexpr qreal CornerRadius = 5.0;
constexpr qreal DifferenceMinContentHeight = PrimitiveHeight * 2.0 + ChildGap;
constexpr qreal DragPreviewStartDistance = 6.0;
constexpr qreal CanvasMargin = 2000.0;
extern const QColor CanvasBackground;
extern const QColor MinorGridColor;
extern const QColor MajorGridColor;

void drawCanvasGrid(QPainter *painter, const QRectF &rect, qreal gridSize, const QColor &color, int width);
QGraphicsScene *createTreeGraphicsScene(QObject *parent = nullptr);

void addLabel(QGraphicsScene *scene, const QString &text, const QPointF &position, const QColor &color = QColor(35, 35, 35));
QGraphicsRectItem *addSoftShadow(QGraphicsScene *scene, const QRectF &rect, qreal zValue);
QGraphicsPathItem *addRoundedPanel(QGraphicsScene *scene, const QRectF &rect, qreal radius, const QPen &pen, const QBrush &brush, qreal zValue);
QString primitiveNumberText(const QString &label, int fallbackId);
void paintPrimitiveIcon(QPainter *painter, ShapeNode::Type type, const QRectF &rect);
void paintOperationIcon(QPainter *painter, SceneDocument::TreeNode::Operation operation, const QRectF &rect, const QColor &accent, qreal symbolInset = 7.0);

int insertionIndexForY(const QVector<QRectF> &childRects, qreal y, int minimumIndex = 0);
QSizeF defaultPreviewSize();
QSizeF groupPreviewSize();
QSizeF differencePreviewSize();
QSizeF previewSizeForTool(const QString &tool);
ShapeNode::Type primitiveTypeForTool(const QString &tool);
QString toolNameForPrimitiveType(ShapeNode::Type type);
bool operationForToolName(const QString &tool, SceneDocument::TreeNode::Operation *operation);
bool isTransformOperation(SceneDocument::TreeNode::Operation operation);
struct OperationVisual {
    SceneDocument::TreeNode::Operation operation;
    const char *toolName;
    QColor fill;
    qreal minWidth;
};

struct ShapeParameterControl {
    QString label;
    qreal value = 0.0;
};

QVector<ShapeParameterControl> shapeParameterControls(const ShapeNode &shape);
QRectF shapeParameterControlRect(const QRectF &primitiveRect, int index, int count);
const OperationVisual &operationVisual(SceneDocument::TreeNode::Operation operation);
qreal minimumWidthForOperation(SceneDocument::TreeNode::Operation operation);
QString labelForOperation(SceneDocument::TreeNode::Operation operation);
QColor fillForTool(const QString &tool);
QColor fillForOperation(SceneDocument::TreeNode::Operation operation);
QRectF placeholderRectForInsertIndex(const QRectF &contentRect, const QVector<QRectF> &childRects, int insertIndex, const QSizeF &previewSize);
QRectF slotMarkerRectForInsertIndex(const QRectF &contentRect, const QVector<QRectF> &childRects, int insertIndex);
QRectF expandedGroupRectForPreview(const QRectF &groupRect, const QRectF &placeholderRect, const QVector<QRectF> &childRects, int insertIndex, const QSizeF &previewSize);
QRectF expandedGroupRectForChangedChild(const QRectF &groupRect, const QVector<QRectF> &childRects, const QRectF &oldChildRect, const QRectF &newChildRect);

void appendPreviewItem(QVector<QGraphicsItem *> *items, QGraphicsItem *item);
QPainterPath dragFocusOutlinePath(const QString &tool, const QRectF &rect);
QGraphicsPathItem *addDragFocusOutline(QGraphicsScene *scene, QVector<QGraphicsItem *> *items, const QString &tool, const QRectF &rect, qreal zValue);
QGraphicsPathItem *addDropSlotMarker(QGraphicsScene *scene, QVector<QGraphicsItem *> *items, const QRectF &rect, qreal zValue);

QGraphicsItem *createTreeNodeDragHandleItem(int nodeId, const QString &label, const QRectF &rect, const QRectF &sourceRect, std::function<void(int)> onSelected, const QSizeF &previewSize, std::function<void(const QPointF &, const QSizeF &, const QString &)> onPreviewMoved, std::function<void()> onPreviewFinished, std::function<void(int, const QPointF &)> onDropped);
QGraphicsItem *createTreeNodeSelectionItem(int nodeId, const QRectF &rect, qreal zValue, std::function<void(int)> onSelected);
QGraphicsItem *createPaletteToolItem(const QString &label, const QColor &fill, std::function<void(const QPointF &, const QSizeF &, const QString &)> onPreviewMoved, std::function<void()> onPreviewFinished, std::function<void(const QString &, const QPointF &)> onDropped);

} // namespace SceneTreeGraphics

#endif
