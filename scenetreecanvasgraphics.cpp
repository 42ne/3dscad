#include "scenetreecanvasgraphics.h"
#include "scenetreepalette.h"

#include <QApplication>
#include <QBrush>
#include <QGraphicsScene>
#include <QPainter>
#include <QPen>
#include <QVarLengthArray>
#include <cmath>

namespace SceneTreeGraphics {

const QColor CanvasBackground(31, 41, 55);
const QColor MinorGridColor(96, 106, 121);
const QColor MajorGridColor(139, 150, 166);

CanvasBackgroundTheme canvasBackgroundTheme(int index)
{
    static const CanvasBackgroundTheme themes[CanvasBackgroundThemeCount] = {
        { QColor(31, 41, 55),    QColor(96, 106, 121),  QColor(139, 150, 166) },
        { QColor(16, 22, 32),    QColor(57, 69, 84),    QColor(92, 108, 128)  },
        { QColor(50, 51, 56),    QColor(82, 84, 91),    QColor(119, 122, 131) },
        { QColor(231, 235, 241), QColor(197, 204, 214), QColor(160, 171, 185) },
        { QColor(246, 239, 226), QColor(217, 207, 190), QColor(183, 171, 151) },
        { QColor(220, 235, 241), QColor(185, 207, 216), QColor(145, 177, 190) },
    };
    return themes[qBound(0, index, CanvasBackgroundThemeCount - 1)];
}

CanvasBackgroundTheme activeCanvasBackgroundTheme(int index)
{
    if (SceneTreePalette::hasCustomTheme()) {
        const TreeAppearanceTheme theme = SceneTreePalette::customTheme();
        return {theme.canvas, theme.minorGrid, theme.majorGrid};
    }
    return canvasBackgroundTheme(index);
}

bool usesDarkOverlayGlass(int backgroundTheme)
{
    return activeCanvasBackgroundTheme(backgroundTheme).background.lightness() < 128;
}

QFont sceneTreeGraphicsFont()
{
    QFont font = QApplication::font();
    font.setFamily(QStringLiteral("Segoe UI"));
    font.setPointSizeF(7.25);
    return font;
}

void drawCanvasGrid(QPainter *painter, const QRectF &rect, qreal gridSize, const QColor &color, int width)
{
    QVarLengthArray<QLineF, 128> lines;

    const qreal left = std::floor(rect.left() / gridSize) * gridSize;
    const qreal top = std::floor(rect.top() / gridSize) * gridSize;

    for (qreal x = left; x < rect.right(); x += gridSize)
        lines.append(QLineF(x, rect.top(), x, rect.bottom()));

    for (qreal y = top; y < rect.bottom(); y += gridSize)
        lines.append(QLineF(rect.left(), y, rect.right(), y));

    QPen gridPen(color);
    gridPen.setWidth(width);
    gridPen.setCosmetic(true);
    painter->setPen(gridPen);
    painter->drawLines(lines.constData(), lines.size());
}

class TreeGraphicsScene : public QGraphicsScene
{
public:
    explicit TreeGraphicsScene(QObject *parent = nullptr)
        : QGraphicsScene(parent)
    {
        setBackgroundBrush(CanvasBackground);
    }

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override
    {
        QGraphicsScene::drawBackground(painter, rect);
        drawCanvasGrid(painter, rect, 24.0, MinorGridColor, 1);
        drawCanvasGrid(painter, rect, 120.0, MajorGridColor, 1);
    }
};

void addLabel(QGraphicsScene *scene, const QString &text, const QPointF &position, const QColor &color)
{
    auto *label = scene->addSimpleText(text);
    label->setBrush(color);
    label->setPos(position);
}

QGraphicsRectItem *addSoftShadow(QGraphicsScene *scene, const QRectF &rect, qreal zValue)
{
    auto *shadow = scene->addRect(rect.translated(2.0, 2.0), Qt::NoPen, QBrush(QColor(0, 0, 0, 38)));
    shadow->setZValue(zValue);
    return shadow;
}

QGraphicsPathItem *addRoundedPanel(QGraphicsScene *scene,
                                   const QRectF &rect,
                                   qreal radius,
                                   const QPen &pen,
                                   const QBrush &brush,
                                   qreal zValue)
{
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    auto *panel = scene->addPath(path, pen, brush);
    panel->setZValue(zValue);
    return panel;
}

QGraphicsScene *createTreeGraphicsScene(QObject *parent)
{
    return new TreeGraphicsScene(parent);
}

} // namespace SceneTreeGraphics
