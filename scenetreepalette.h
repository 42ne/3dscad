#ifndef SCENETREEPALETTE_H
#define SCENETREEPALETTE_H

#include "scenedocument.h"
#include <QColor>
#include <QString>

namespace SceneTreePalette {

enum class Theme {
    Sakura  = 0,  // Cherry-blossom pastels — soft rose/lavender, light
    Glass   = 1,  // Dark glass, like the toolbar panel
    Embers  = 2,  // Warm amber / terracotta
    Nordic  = 3,  // Saturated north-European darks — viridian, rust, slate
    Ocean   = 4,  // Deep oceanic blues and teals, dark
    Harvest = 5,  // Warm autumn earth tones, light
};

constexpr int ThemeCount = 6;

// Human-readable label for the theme switcher tooltip.
QString themeName(Theme theme);

// Cycle to the next theme.
Theme nextTheme(Theme current);

// Body fill for a group card.
// depth=0 is top-level; each additional nesting level shifts the hue by 18°
// so deeply-nested modules are visually distinct from their parents.
QColor groupFill(SceneDocument::TreeNode::Operation operation, int depth, Theme theme);

// Background fill for a variable / parameter card row.
QColor variableFill(bool isParameter, Theme theme);

// True when the theme uses dark backgrounds (light text required).
bool isDarkTheme(Theme theme);

// Primary text on card bodies (operation labels, numbers, etc.).
QColor textPrimary(Theme theme);

// Secondary / muted text (axis labels, "empty" placeholder, etc.).
QColor textMuted(Theme theme);

// Number-pill colours — normal state.
QColor pillBorder(const QColor &groupFill, Theme theme);
QColor pillFill(Theme theme);

// Number-pill colours — active (Ctrl + hover) state.  Same for all themes.
QColor pillBorderActive();
QColor pillFillActive();

// Colour shown as the circular swatch in the theme switcher overlay.
QColor swatchColor(Theme theme);

} // namespace SceneTreePalette

#endif // SCENETREEPALETTE_H
