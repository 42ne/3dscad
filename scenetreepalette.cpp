#include "scenetreepalette.h"

namespace SceneTreePalette {

namespace {

using Op = SceneDocument::TreeNode::Operation;
bool CustomThemeActive = false;
TreeAppearanceTheme CustomTheme;

// ---------------------------------------------------------------------------
// Per-operation base fill colours, one column per theme.
//
// Aurora  α=183  — cool northern-lights pastels; teal/mint/violet, light.
// Glass   α=145  — semi-transparent dark glass; grid quite visible.
// Embers  α=178  — warm semi-opaque amber/terracotta.
// Nordic  α=158  — rich saturated darks; nord-inspired.
// Ocean   α=152  — deep oceanic blues/teals, dark.
// Meadow  α=186  — spring meadow; sage/cornflower/buttercup, light.
// ---------------------------------------------------------------------------
struct OperationColor {
    Op     operation;
    QColor aurora;
    QColor glass;
    QColor embers;
    QColor nordic;
    QColor ocean;
    QColor meadow;
};

static const OperationColor OperationColors[] = {
    //              aurora                        glass                        embers                       nordic                        ocean                         meadow
    { Op::Union,        QColor(192,235,225,183), QColor( 28, 48, 72,145), QColor(255,225,185,178), QColor( 45,135,100,158), QColor( 25,105,118,152), QColor(198,232,195,186) },
    { Op::Difference,   QColor(245,210,220,183), QColor( 72, 30, 26,145), QColor(255,180,130,178), QColor(165, 72, 55,158), QColor(105, 48, 55,152), QColor(248,210,215,186) },
    { Op::Intersection, QColor(212,215,250,183), QColor( 50, 26, 72,145), QColor(240,195,155,178), QColor( 95, 68,155,158), QColor( 62, 45,125,152), QColor(218,205,245,186) },
    { Op::Module,       QColor(205,225,242,183), QColor( 30, 40, 58,145), QColor(242,210,178,178), QColor( 58, 82,125,158), QColor( 30, 62,105,152), QColor(215,228,208,186) },
    { Op::Translate,    QColor(188,232,245,183), QColor( 18, 55, 72,145), QColor(215,235,185,178), QColor( 42,112,145,158), QColor( 25, 75,118,152), QColor(198,218,248,186) },
    { Op::Rotate,       QColor(228,210,250,183), QColor( 58, 26, 72,145), QColor(255,195,155,178), QColor(112, 58,142,158), QColor( 75, 42,118,152), QColor(225,208,245,186) },
    { Op::Scale,        QColor(198,242,222,183), QColor( 22, 58, 32,145), QColor(195,230,185,178), QColor( 52,115, 72,158), QColor( 28, 98, 75,152), QColor(188,235,200,186) },
    { Op::Mirror,       QColor(245,215,238,183), QColor( 72, 28, 58,145), QColor(255,180,195,178), QColor(145, 52, 98,158), QColor(105, 35, 78,152), QColor(245,205,230,186) },
    { Op::Hull,         QColor(215,242,218,183), QColor( 25, 62, 32,145), QColor(185,228,172,178), QColor( 48,118, 58,158), QColor( 28, 98, 48,152), QColor(205,238,202,186) },
    { Op::Polyhedron,   QColor(218,238,228,183), QColor( 22, 55, 42,145), QColor(195,228,190,178), QColor( 48,118, 78,158), QColor( 28, 98, 65,152), QColor(205,238,210,186) },
    { Op::For,          QColor(248,232,195,183), QColor( 68, 55, 18,145), QColor(255,220,140,178), QColor(135,112, 42,158), QColor( 95, 88, 38,152), QColor(248,238,188,186) },
    { Op::Scene,        QColor(212,225,238,183), QColor( 35, 45, 62,145), QColor(228,215,192,178), QColor( 52, 65, 92,158), QColor( 28, 45, 75,152), QColor(218,228,215,186) },
};

const QColor *baseColorFor(Op operation, Theme theme)
{
    for (const OperationColor &oc : OperationColors) {
        if (oc.operation == operation) {
            switch (theme) {
            case Theme::Aurora:  return &oc.aurora;
            case Theme::Glass:   return &oc.glass;
            case Theme::Embers:  return &oc.embers;
            case Theme::Nordic:  return &oc.nordic;
            case Theme::Ocean:   return &oc.ocean;
            case Theme::Meadow:  return &oc.meadow;
            }
        }
    }
    return &OperationColors[0].aurora; // fallback
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
    case Theme::Aurora:  return QStringLiteral("Aurora");
    case Theme::Glass:   return QStringLiteral("Glass");
    case Theme::Embers:  return QStringLiteral("Embers");
    case Theme::Nordic:  return QStringLiteral("Nordic");
    case Theme::Ocean:   return QStringLiteral("Ocean");
    case Theme::Meadow:  return QStringLiteral("Meadow");
    }
    return QStringLiteral("Aurora");
}

Theme nextTheme(Theme current)
{
    return static_cast<Theme>((static_cast<int>(current) + 1) % ThemeCount);
}

QColor groupFill(SceneDocument::TreeNode::Operation operation, int depth, Theme theme)
{
    if (CustomThemeActive) {
        // User-chosen colors are returned as-is — no depth hue shift.
        // The shift is a built-in theming effect; it must not alter explicit user picks.
        const auto it = CustomTheme.operationCards.find(static_cast<int>(operation));
        if (it != CustomTheme.operationCards.end() && it.value().card.isValid())
            return it.value().card;
        return CustomTheme.card;
    }
    const QColor *base = baseColorFor(operation, theme);
    if (!base)
        return QColor(200, 215, 225, 183);
    return applyDepthHueShift(*base, depth);
}

QColor variableFill(bool isParameter, Theme theme)
{
    if (CustomThemeActive)
        return CustomTheme.input;
    switch (theme) {
    case Theme::Aurora:
        return isParameter ? QColor(195, 218, 252, 183) : QColor(252, 225, 215, 183);
    case Theme::Glass:
        return isParameter ? QColor( 24,  36,  72, 145) : QColor( 52,  42,  18, 145);
    case Theme::Embers:
        return isParameter ? QColor(220, 225, 255, 178) : QColor(255, 238, 200, 178);
    case Theme::Nordic:
        return isParameter ? QColor( 45,  72, 135, 158) : QColor(105,  78,  38, 158);
    case Theme::Ocean:
        return isParameter ? QColor( 32,  68, 118, 152) : QColor( 88,  68,  28, 152);
    case Theme::Meadow:
        return isParameter ? QColor(210, 218, 250, 186) : QColor(250, 238, 198, 186);
    }
    return QColor(248, 242, 228, 185);
}

bool isDarkTheme(Theme theme)
{
    if (CustomThemeActive)
        return CustomTheme.canvas.lightness() < 128;
    return theme == Theme::Glass
        || theme == Theme::Nordic
        || theme == Theme::Ocean;
}

QColor textPrimary(Theme theme)
{
    if (CustomThemeActive)
        return CustomTheme.text;
    return isDarkTheme(theme) ? QColor(218, 228, 245) : QColor(24, 34, 44);
}

QColor textMuted(Theme theme)
{
    if (CustomThemeActive)
        return CustomTheme.mutedText;
    return isDarkTheme(theme) ? QColor(148, 165, 192) : QColor(80, 92, 108);
}

QColor pillBorder(const QColor &fill, Theme theme)
{
    if (CustomThemeActive)
        return CustomTheme.numBorder;
    return isDarkTheme(theme) ? fill.lighter(155) : fill.darker(130);
}

QColor pillFill(Theme theme)
{
    if (CustomThemeActive)
        return CustomTheme.numFill;
    return isDarkTheme(theme) ? QColor(255, 255, 255, 32) : QColor(255, 255, 255, 125);
}

QColor pillBorderActive()
{
    if (CustomThemeActive)
        return CustomTheme.numBorderActive;
    return QColor(220, 156, 26);
}

QColor pillFillActive()
{
    if (CustomThemeActive)
        return CustomTheme.numFillActive;
    return QColor(255, 220, 108, 205);
}

QColor numText(Theme theme)
{
    if (CustomThemeActive)
        return CustomTheme.numText;
    return textPrimary(theme);
}

QColor numLabelText(Theme theme)
{
    if (CustomThemeActive)
        return CustomTheme.numLabelText;
    return isDarkTheme(theme) ? QColor(140, 175, 220) : QColor(80, 110, 160);
}

QColor leafCardFill()
{
    return CustomThemeActive ? CustomTheme.leafCard : QColor(0, 0, 0, 0);
}

QColor swatchColor(Theme theme)
{
    switch (theme) {
    case Theme::Aurora:  return QColor(105, 195, 188); // aurora teal
    case Theme::Glass:   return QColor( 28,  48,  72); // dark navy
    case Theme::Embers:  return QColor(215, 140,  75); // ember orange
    case Theme::Nordic:  return QColor( 45, 135, 100); // viridian
    case Theme::Ocean:   return QColor( 25, 105, 118); // deep teal
    case Theme::Meadow:  return QColor(122, 192, 122); // meadow green
    }
    return QColor(200, 215, 225);
}

void setCustomTheme(const TreeAppearanceTheme &theme)
{
    CustomTheme = theme;
    CustomThemeActive = true;
}

void clearCustomTheme()
{
    CustomThemeActive = false;
}

bool hasCustomTheme()
{
    return CustomThemeActive;
}

TreeAppearanceTheme customTheme()
{
    return CustomThemeActive ? CustomTheme : AppearanceThemes::defaultTreeTheme();
}

QColor headerFill(const QColor &cardFill, Theme theme)
{
    if (CustomThemeActive)
        return CustomTheme.header;
    return isDarkTheme(theme) ? cardFill.lighter(180) : cardFill.lighter(112);
}

// ---------------------------------------------------------------------------
// Per-operation overrides — fall through to global defaults when not set.
// ---------------------------------------------------------------------------

QColor groupHeaderColor(SceneDocument::TreeNode::Operation op,
                        const QColor &cardFill, Theme theme)
{
    if (CustomThemeActive) {
        const auto it = CustomTheme.operationCards.find(static_cast<int>(op));
        if (it != CustomTheme.operationCards.end() && it.value().header.isValid())
            return it.value().header;
    }
    return headerFill(cardFill, theme);
}

QColor cardTextPrimary(SceneDocument::TreeNode::Operation op, Theme theme)
{
    if (CustomThemeActive) {
        const auto it = CustomTheme.operationCards.find(static_cast<int>(op));
        if (it != CustomTheme.operationCards.end() && it.value().text.isValid())
            return it.value().text;
    }
    return textPrimary(theme);
}

QColor cardBorder(SceneDocument::TreeNode::Operation op,
                  const QColor &fill, Theme theme)
{
    if (CustomThemeActive) {
        const auto it = CustomTheme.operationCards.find(static_cast<int>(op));
        if (it != CustomTheme.operationCards.end() && it.value().border.isValid())
            return it.value().border;
    }
    return isDarkTheme(theme) ? fill.lighter(165) : fill.darker(145);
}

QColor cardPillBorderActive(SceneDocument::TreeNode::Operation op)
{
    if (CustomThemeActive) {
        const auto it = CustomTheme.operationCards.find(static_cast<int>(op));
        if (it != CustomTheme.operationCards.end() && it.value().numBorderActive.isValid())
            return it.value().numBorderActive;
    }
    return pillBorderActive();
}

QColor cardPillFillActive(SceneDocument::TreeNode::Operation op)
{
    if (CustomThemeActive) {
        const auto it = CustomTheme.operationCards.find(static_cast<int>(op));
        if (it != CustomTheme.operationCards.end() && it.value().numFillActive.isValid())
            return it.value().numFillActive;
    }
    return pillFillActive();
}

void setOperationCardPalette(SceneDocument::TreeNode::Operation op,
                             const OperationCardPalette &pal)
{
    // Ensure a custom theme is active (snapshot the built-in defaults if needed).
    if (!CustomThemeActive)
        CustomTheme = AppearanceThemes::defaultTreeTheme();
    CustomThemeActive = true;
    CustomTheme.operationCards[static_cast<int>(op)] = pal;
}

OperationCardPalette operationCardPalette(SceneDocument::TreeNode::Operation op)
{
    if (CustomThemeActive) {
        const auto it = CustomTheme.operationCards.find(static_cast<int>(op));
        if (it != CustomTheme.operationCards.end())
            return it.value();
    }
    return OperationCardPalette{};
}

void clearOperationCardPalettes()
{
    CustomTheme.operationCards.clear();
}

} // namespace SceneTreePalette
