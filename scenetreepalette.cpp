#include "scenetreepalette.h"

namespace SceneTreePalette {

namespace {

using Op = SceneDocument::TreeNode::Operation;

// ---------------------------------------------------------------------------
// Per-operation, per-theme base fill colours.
//
// Alpha values:
//   Frost  185 — semi-transparent pastels; grid shows through lightly.
//   Glass  145 — semi-transparent dark glass; grid quite visible.
//   Embers 178 — warm semi-opaque.
//   Deep   172 — richer semi-opaque.
// ---------------------------------------------------------------------------
struct OperationColor {
    Op     operation;
    QColor frost;
    QColor glass;
    QColor embers;
    QColor deep;
};

static const OperationColor OperationColors[] = {
    // ---- booleans ----
    { Op::Union,        QColor(216,237,226,185), QColor( 28, 48, 72,145), QColor(255,225,185,178), QColor(108,188,138,172) },
    { Op::Difference,   QColor(247,224,204,185), QColor( 72, 30, 26,145), QColor(255,180,130,178), QColor(210,130, 88,172) },
    { Op::Intersection, QColor(226,220,247,185), QColor( 50, 26, 72,145), QColor(240,195,155,178), QColor(148,118,208,172) },
    // ---- module ----
    { Op::Module,       QColor(230,232,236,185), QColor( 30, 40, 58,145), QColor(242,210,178,178), QColor(138,155,182,172) },
    // ---- transforms ----
    { Op::Translate,    QColor(218,238,246,185), QColor( 18, 55, 72,145), QColor(215,235,185,178), QColor( 88,168,208,172) },
    { Op::Rotate,       QColor(239,229,247,185), QColor( 58, 26, 72,145), QColor(255,195,155,178), QColor(178,118,208,172) },
    { Op::Scale,        QColor(229,241,218,185), QColor( 22, 58, 32,145), QColor(195,230,185,178), QColor(108,198, 98,172) },
    // ---- loop ----
    { Op::For,          QColor(236,232,205,185), QColor( 68, 55, 18,145), QColor(255,220,140,178), QColor(208,188, 78,172) },
    // ---- scene root ----
    { Op::Scene,        QColor(210,215,225,185), QColor( 35, 45, 62,145), QColor(228,215,192,178), QColor(118,138,172,172) },
};

const QColor *baseColorFor(Op operation, Theme theme)
{
    for (const OperationColor &oc : OperationColors) {
        if (oc.operation == operation) {
            switch (theme) {
            case Theme::Frost:  return &oc.frost;
            case Theme::Glass:  return &oc.glass;
            case Theme::Embers: return &oc.embers;
            case Theme::Deep:   return &oc.deep;
            }
        }
    }
    return &OperationColors[0].frost; // fallback
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
    case Theme::Frost:  return QStringLiteral("Frost");
    case Theme::Glass:  return QStringLiteral("Glass");
    case Theme::Embers: return QStringLiteral("Embers");
    case Theme::Deep:   return QStringLiteral("Deep");
    }
    return QStringLiteral("Frost");
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
    case Theme::Frost:
        return isParameter ? QColor(232, 236, 252, 185) : QColor(248, 242, 228, 185);
    case Theme::Glass:
        return isParameter ? QColor(24,  36, 72, 145)   : QColor(52,  42, 18, 145);
    case Theme::Embers:
        return isParameter ? QColor(220, 225, 255, 178)  : QColor(255, 238, 200, 178);
    case Theme::Deep:
        return isParameter ? QColor(100, 120, 200, 172)  : QColor(200, 160,  70, 172);
    }
    return QColor(248, 242, 228, 185);
}

bool isDarkTheme(Theme theme)
{
    return theme == Theme::Glass;
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
    case Theme::Frost:  return QColor(216, 237, 226); // mint green
    case Theme::Glass:  return QColor( 28,  48,  72); // dark navy
    case Theme::Embers: return QColor(255, 220, 150); // warm gold
    case Theme::Deep:   return QColor(108, 188, 138); // deep emerald
    }
    return QColor(200, 210, 220);
}

} // namespace SceneTreePalette
