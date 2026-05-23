#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QString>
#include <QVector>

// ── ThemeSpec ─────────────────────────────────────────────────────────────────
// Plain data describing one application colour theme.

struct ThemeSpec
{
    QString id;
    QString label;
    QColor window, surface, panel, panelAlt, text, mutedText, border,
           accent, accentHover, danger, titleBase, titlePulse;
};

// ── Free functions ────────────────────────────────────────────────────────────

QVector<ThemeSpec> availableThemes();
ThemeSpec          defaultTheme();

// Returns "#rrggbb" hex string (no alpha).
QString colorName(const QColor &c);

// Applies palette + stylesheet to qApp.
void applyTheme(const ThemeSpec &theme);

#endif // THEME_H
