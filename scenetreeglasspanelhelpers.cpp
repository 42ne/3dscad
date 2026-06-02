#include "scenetreegraphicshelpers.h"

#include <QBrush>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QLinearGradient>
#include <QPen>

namespace SceneTreeGraphics {

QPen standardGlassPen(bool darkGlass, bool hasCustom, const TreeAppearanceTheme &theme)
{
    return QPen(hasCustom ? theme.glassBorder
                           : darkGlass ? QColor(148, 163, 184, 82)
                                       : QColor(118, 136, 156, 58), 1.0);
}

QBrush standardGlassBrush(bool darkGlass, bool hasCustom, const TreeAppearanceTheme &theme)
{
    return QBrush(hasCustom ? theme.glassBottom
                             : darkGlass ? QColor(10, 16, 24, 178)
                                         : QColor(250, 253, 255, 88));
}

void addFlatGlassPanel(QGraphicsScene *scene,
                       QVector<QGraphicsItem *> *items,
                       const QRectF &localRect,
                       const QPointF &topLeft,
                       qreal baseZ,
                       bool darkGlass,
                       bool hasCustom,
                       const TreeAppearanceTheme &customTheme,
                       const GlassPanelStyle &style)
{
    const int shadowAlpha = darkGlass ? style.shadowAlphaDark : style.shadowAlphaLight;
    auto *shadow = scene->addRect(localRect.translated(style.shadowOffset),
                                  Qt::NoPen, QBrush(QColor(0, 0, 0, shadowAlpha)));
    shadow->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    shadow->setAcceptedMouseButtons(Qt::NoButton);
    shadow->setPos(topLeft);
    shadow->setZValue(baseZ - 2.0);
    shadow->setOpacity(darkGlass ? style.shadowOpacityDark : style.shadowOpacityLight);
    if (!style.shadowTag.isEmpty())
        shadow->setData(0, style.shadowTag);
    if (items)
        items->append(shadow);

    QPainterPath panelPath;
    const qreal cr = style.cornerRadius;
    panelPath.addRoundedRect(localRect, cr, cr);

    QBrush glassBrush;
    if (style.gradientGlass) {
        QLinearGradient grad(QPointF(0.0, 0.0), QPointF(0.0, localRect.height()));
        grad.setColorAt(0.0, hasCustom ? customTheme.glassTop
                                       : darkGlass ? QColor(24, 34, 50, 218) : QColor(255, 255, 255, 116));
        grad.setColorAt(1.0, hasCustom ? customTheme.glassBottom
                                       : darkGlass ? QColor(8, 13, 22, 196) : QColor(237, 244, 249, 74));
        glassBrush = QBrush(grad);
    } else {
        glassBrush = standardGlassBrush(darkGlass, hasCustom, customTheme);
    }
    auto *panel = scene->addPath(panelPath,
                                 standardGlassPen(darkGlass, hasCustom, customTheme),
                                 glassBrush);
    panel->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    panel->setAcceptedMouseButtons(Qt::NoButton);
    panel->setPos(topLeft);
    panel->setZValue(baseZ - 1.0);
    if (!style.panelTag.isEmpty())
        panel->setData(0, style.panelTag);
    if (items)
        items->append(panel);
}

} // namespace SceneTreeGraphics
