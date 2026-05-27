#ifndef SCENETREEPALETTE_H
#define SCENETREEPALETTE_H

#include "scenedocument.h"
#include "appearancethemes.h"
#include <QColor>
#include <QString>

namespace SceneTreePalette {

enum class Theme {
    Aurora  = 0,  // Northern-lights pastels — teal, mint, soft violet, light
    Glass   = 1,  // Dark glass, like the toolbar panel
    Embers  = 2,  // Warm amber / terracotta
    Nordic  = 3,  // Saturated north-European darks — viridian, rust, slate
    Ocean   = 4,  // Deep oceanic blues and teals, dark
    Meadow  = 5,  // Spring meadow — sage, cornflower, buttercup, light
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

// Number-pill colours — active (Ctrl + hover) state.
QColor pillBorderActive();
QColor pillFillActive();

// Number-pill text colours.
QColor numText(Theme theme);
QColor numLabelText(Theme theme);

// Colour shown as the circular swatch in the theme switcher overlay.
QColor swatchColor(Theme theme);

void setCustomTheme(const TreeAppearanceTheme &theme);
void clearCustomTheme();
bool hasCustomTheme();
TreeAppearanceTheme customTheme();
QColor headerFill(const QColor &cardFill, Theme theme);

// ── Per-operation overrides ──────────────────────────────────────────────────
// These fall back to the global/built-in palette when no override is set for
// the given operation. They're layered on top of the existing groupFill /
// headerFill / textPrimary functions so existing call sites that do NOT need
// per-op colours are unaffected.

// Header fill for a specific operation type. Falls back to headerFill().
QColor groupHeaderColor(SceneDocument::TreeNode::Operation op,
                        const QColor &cardFill, Theme theme);

// Primary text for a specific operation's card. Falls back to textPrimary().
QColor cardTextPrimary(SceneDocument::TreeNode::Operation op, Theme theme);

// Card border for a specific operation. Falls back to derived-from-fill border.
QColor cardBorder(SceneDocument::TreeNode::Operation op,
                  const QColor &fill, Theme theme);

// Active-pill highlight colours per operation. Fall back to the global yellow.
QColor cardPillBorderActive(SceneDocument::TreeNode::Operation op);
QColor cardPillFillActive(SceneDocument::TreeNode::Operation op);

// Access / mutate the per-operation palette stored inside the custom theme.
void setOperationCardPalette(SceneDocument::TreeNode::Operation op,
                             const OperationCardPalette &pal);
OperationCardPalette operationCardPalette(SceneDocument::TreeNode::Operation op);
void clearOperationCardPalettes();

} // namespace SceneTreePalette

#endif // SCENETREEPALETTE_H
