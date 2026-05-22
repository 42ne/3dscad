#include "scenetreepalette.h"

namespace SceneTreePalette {

namespace {

using Op = SceneDocument::TreeNode::Operation;

// ---------------------------------------------------------------------------
// Per-operation base fill colours, one column per theme.
//
// Sakura  α=185  — semi-transparent cherry-blossom pastels; grid shows through.
// Glass   α=145  — semi-transparent dark glass; grid quite visible.
// Embers  α=178  — warm semi-opaque amber/terracotta.
// Nordic  α=158  — rich saturated darks; nord-inspired.
// Ocean   α=152  — deep oceanic blues/teals, dark.
// Harvest α=188  — warm autumn earth tones, light.
// ---------------------------------------------------------------------------
struct OperationColor {
    Op     operation;
    QColor sakura;
    QColor glass;
    QColor embers;
    QColor nordic;
    QColor ocean;
    QColor harvest;
};

static const OperationColor OperationColors[] = {
    //              sakura                        glass                        embers                       nordic                        ocean                         harvest
    { Op::Union,        QColor(238,218,226,185), QColor( 28, 48, 72,145), QColor(255,225,185,178), QColor( 45,135,100,158), QColor( 25,105,118,152), QColor(192,218,168,188) },
    { Op::Difference,   QColor(248,212,198,185), QColor( 72, 30, 26,145), QColor(255,180,130,178), QColor(165, 72, 55,158), QColor(105, 48, 55,152), QColor(232,168,118,188) },
    { Op::Intersection, QColor(228,215,248,185), QColor( 50, 26, 72,145), QColor(240,195,155,178), QColor( 95, 68,155,158), QColor( 62, 45,125,152), QColor(205,175,225,188) },
    { Op::Module,       QColor(235,225,232,185), QColor( 30, 40, 58,145), QColor(242,210,178,178), QColor( 58, 82,125,158), QColor( 30, 62,105,152), QColor(238,225,192,188) },
    { Op::Translate,    QColor(215,225,248,185), QColor( 18, 55, 72,145), QColor(215,235,185,178), QColor( 42,112,145,158), QColor( 25, 75,118,152), QColor(195,215,232,188) },
    { Op::Rotate,       QColor(238,215,248,185), QColor( 58, 26, 72,145), QColor(255,195,155,178), QColor(112, 58,142,158), QColor( 75, 42,118,152), QColor(215,185,225,188) },
    { Op::Scale,        QColor(215,238,220,185), QColor( 22, 58, 32,145), QColor(195,230,185,178), QColor( 52,115, 72,158), QColor( 28, 98, 75,152), QColor(178,218,178,188) },
    { Op::For,          QColor(248,232,205,185), QColor( 68, 55, 18,145), QColor(255,220,140,178), QColor(135,112, 42,158), QColor( 95, 88, 38,152), QColor(242,225,155,188) },
    { Op::Scene,        QColor(225,218,228,185), QColor( 35, 45, 62,145), QColor(228,215,192,178), QColor( 52, 65, 92,158), QColor( 28, 45, 75,152), QColor(228,218,205,188) },
};

const QColor *baseColorFor(Op operation, Theme theme)
{
    for (const OperationColor &oc : OperationColors) {
        if (oc.operation == operation) {
            switch (theme) {
            case Theme::Sakura:  return &oc.sakura;
            case Theme::Glass:   return &oc.glass;
            case Theme::Embers:  return &oc.embers;
            case Theme::Nordic:  return &oc.nordic;
            case Theme::Ocean:   return &oc.ocean;
            case Theme::Harvest: return &oc.harvest;
            }
        }
    }
    return &OperationColors[0].sakura; // fallback
}

// Rotate the hue of a QColor by (depth * 18)° in HSV space.
// Achromatic colours (hue == -1) are returned unchanged.
QColor applyDepthHueShift(const QColor &base, int depth)
{
    if (depth <= 0)
        return base;

    const QColor hsv = base.toHsv();
    const int h = hsv.hsvHue();
    if (h < 0)
        return base; // achromatic — nothing to shift

    const int newH = (h + depth * 18) % 360;
    return QColor::fromHsv(newH, hsv.hsvSaturation(), hsv.value(), base.alpha());
}

} // anonymous namespace

// ---------------------------------------------------------------------------

QString themeName(Theme theme)
{
    switch (theme) {
    case Theme::Sakura:  return QStringLiteral("Sakura");
    case Theme::Glass:   return QStringLiteral("Glass");
    case Theme::Embers:  return QStringLiteral("Embers");
    case Theme::Nordic:  return QStringLiteral("Nordic");
    case Theme::Ocean:   return QStringLiteral("Ocean");
    case Theme::Harvest: return QStringLiteral("Harvest");
    }
    return QStringLiteral("Sakura");
}

Theme nextTheme(Theme current)
{
    return static_cast<Theme>((static_cast<int>(current) + 1) % ThemeCount);
}

QColor groupFill(SceneDocument::TreeNode::Operation operation, int depth, Theme theme)
{
    const QColor *base = baseColorFor(operation, theme);
    if (!base)
        return QColor(200, 210, 220, 185);
    return applyDepthHueShift(*base, depth);
}

QColor variableFill(bool isParameter, Theme theme)
{
    switch (theme) {
    case Theme::Sakura:
        return isParameter ? QColor(222, 225, 252, 185) : QColor(252, 238, 222, 185);
    case Theme::Glass:
        return isParameter ? QColor( 24,  36,  72, 145) : QColor( 52,  42,  18, 145);
    case Theme::Embers:
        return isParameter ? QColor(220, 225, 255, 178) : QColor(255, 238, 200, 178);
    case Theme::Nordic:
        return isParameter ? QColor( 45,  72, 135, 158) : QColor(105,  78,  38, 158);
    case Theme::Ocean:
        return isParameter ? QColor( 32,  68, 118, 152) : QColor( 88,  68,  28, 152);
    case Theme::Harvest:
        return isParameter ? QColor(205, 215, 245, 188) : QColor(245, 228, 198, 188);
    }
    return QColor(248, 242, 228, 185);
}

bool isDarkTheme(Theme theme)
{
    return theme == Theme::Glass
        || theme == Theme::Nordic
        || theme == Theme::Ocean;
}

QColor textPrimary(Theme theme)
{
    return isDarkTheme(theme) ? QColor(218, 228, 245) : QColor(24, 34, 44);
}

QColor textMuted(Theme theme)
{
    return isDarkTheme(theme) ? QColor(148, 165, 192) : QColor(80, 92, 108);
}

QColor pillBorder(const QColor &fill, Theme theme)
{
    return isDarkTheme(theme) ? fill.lighter(155) : fill.darker(130);
}

QColor pillFill(Theme theme)
{
    return isDarkTheme(theme) ? QColor(255, 255, 255, 32) : QColor(255, 255, 255, 125);
}

QColor pillBorderActive()
{
    return QColor(220, 156, 26);
}

QColor pillFillActive()
{
    return QColor(255, 220, 108, 205);
}

QColor swatchColor(Theme theme)
{
    switch (theme) {
    case Theme::Sakura:  return QColor(225, 175, 200); // cherry rose
    case Theme::Glass:   return QColor( 28,  48,  72); // dark navy
    case Theme::Embers:  return QColor(215, 140,  75); // ember orange
    case Theme::Nordic:  return QColor( 45, 135, 100); // viridian
    case Theme::Ocean:   return QColor( 25, 105, 118); // deep teal
    case Theme::Harvest: return QColor(185, 142,  68); // harvest gold
    }
    return QColor(200, 210, 220);
}

} // namespace SceneTreePalette
