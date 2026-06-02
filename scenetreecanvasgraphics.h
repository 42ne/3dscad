#ifndef SCENETREECANVASGRAPHICS_H
#define SCENETREECANVASGRAPHICS_H

#include "appearancethemes.h"
#include "scenetreegraphicsconstants.h"

#include <QColor>
#include <QFont>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QRectF>
#include <QString>

class QObject;
class QGraphicsScene;
class QPainter;
class QPointF;
class QBrush;
class QPen;

namespace SceneTreeGraphics {

extern const QColor CanvasBackground;
extern const QColor MinorGridColor;
extern const QColor MajorGridColor;

constexpr int CanvasBackgroundThemeCount = 6;

struct CanvasBackgroundTheme {
    QColor background;
    QColor minorGrid;
    QColor majorGrid;
};

CanvasBackgroundTheme canvasBackgroundTheme(int index);
CanvasBackgroundTheme activeCanvasBackgroundTheme(int index);
bool usesDarkOverlayGlass(int backgroundTheme);

QFont sceneTreeGraphicsFont();
void drawCanvasGrid(QPainter *painter, const QRectF &rect, qreal gridSize, const QColor &color, int width);
QGraphicsScene *createTreeGraphicsScene(QObject *parent = nullptr);

void addLabel(QGraphicsScene *scene, const QString &text, const QPointF &position, const QColor &color = QColor(35, 35, 35));
QGraphicsRectItem *addSoftShadow(QGraphicsScene *scene, const QRectF &rect, qreal zValue);
QGraphicsPathItem *addRoundedPanel(QGraphicsScene *scene, const QRectF &rect, qreal radius, const QPen &pen, const QBrush &brush, qreal zValue);

} // namespace SceneTreeGraphics

#endif
