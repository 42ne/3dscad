#ifndef SCENETREEGLASSPANELHELPERS_H
#define SCENETREEGLASSPANELHELPERS_H

#include "appearancethemes.h"
#include "scenetreegraphicsconstants.h"

#include <QBrush>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

class QGraphicsItem;
class QGraphicsScene;

namespace SceneTreeGraphics {

struct GlassPanelStyle {
    QPointF shadowOffset       = {2.0, 3.0};
    int     shadowAlphaDark    = 90;
    int     shadowAlphaLight   = 32;
    qreal   shadowOpacityDark  = 0.65;
    qreal   shadowOpacityLight = 0.40;
    QString shadowTag;
    QString panelTag;
    qreal   cornerRadius   = CornerRadius;
    bool    gradientGlass  = false;
};

inline GlassPanelStyle switcherGlassStyle(const QString &panelTag = QStringLiteral("glass_toolbar"))
{ return {{2.0, 3.0}, 90, 32, 0.65, 0.40, QStringLiteral("shadow"), panelTag}; }

inline GlassPanelStyle toolbarPaletteGlassStyle()
{ return {{3.0, 4.0}, 96, 38, 0.70, 0.42, {}, {}}; }

inline GlassPanelStyle hintGlassStyle()
{
    GlassPanelStyle s;
    s.shadowOffset      = {3.0, 4.0};
    s.shadowAlphaDark   = 115;
    s.shadowAlphaLight  = 38;
    s.shadowOpacityDark = 0.72;
    s.shadowOpacityLight = 0.42;
    s.shadowTag         = QStringLiteral("shadow");
    s.panelTag          = QStringLiteral("glass_hint");
    s.cornerRadius      = 8.0;
    s.gradientGlass     = true;
    return s;
}

QPen standardGlassPen(bool darkGlass, bool hasCustom, const TreeAppearanceTheme &theme);
QBrush standardGlassBrush(bool darkGlass, bool hasCustom, const TreeAppearanceTheme &theme);
void addFlatGlassPanel(QGraphicsScene *scene,
                       QVector<QGraphicsItem *> *items,
                       const QRectF &localRect,
                       const QPointF &topLeft,
                       qreal baseZ,
                       bool darkGlass,
                       bool hasCustom,
                       const TreeAppearanceTheme &customTheme,
                       const GlassPanelStyle &style = {});

} // namespace SceneTreeGraphics

#endif
