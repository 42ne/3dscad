#ifndef APPEARANCETHEMES_H
#define APPEARANCETHEMES_H

#include "theme.h"

#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>

// ---------------------------------------------------------------------------
// Per-operation color overrides.
// Any invalid QColor() means "use the global theme default".
// Keyed by (int)SceneDocument::TreeNode::Operation in TreeAppearanceTheme.
// ---------------------------------------------------------------------------
struct OperationCardPalette {
    QColor card;            // card body fill
    QColor header;          // header stripe fill
    QColor text;            // primary text colour
    QColor input;           // variable / parameter row fill (future use)
    QColor border;          // card border colour
    QColor numBorderActive; // active-pill border (the "yellow" highlight outline)
    QColor numFillActive;   // active-pill background
};

struct TreeAppearanceTheme
{
    QString name;
    QColor canvas;
    QColor minorGrid;
    QColor majorGrid;
    QColor card;
    QColor header;
    QColor input;
    QColor text;
    QColor mutedText;
    QColor glassTop;
    QColor glassBottom;
    QColor glassBorder;
    QColor accent;
    QColor numBorder;
    QColor numFill;
    QColor numBorderActive;
    QColor numFillActive;
    QColor numText;
    QColor numLabelText;
    // Per-operation overrides; keyed by (int)SceneDocument::TreeNode::Operation.
    QHash<int, OperationCardPalette> operationCards;
};

struct ViewportAppearanceTheme
{
    QString name;
    QColor background;
    QColor grid;
    QColor solid;
    QColor computedSolid;
    QColor glassTop;
    QColor glassBottom;
    QColor glassBorder;
    QColor text;
    QColor mutedText;
    QColor accent;
};

namespace AppearanceThemes {

QString themesDirectory();
void ensureDefaultFiles();

QStringList customWindowThemeNames();
QStringList customTreeThemeNames();
QStringList customViewportThemeNames();

bool loadWindowTheme(const QString &name, ThemeSpec *theme);
bool loadTreeTheme(const QString &name, TreeAppearanceTheme *theme);
bool loadViewportTheme(const QString &name, ViewportAppearanceTheme *theme);

bool saveWindowTheme(const QString &name, const ThemeSpec &theme);
bool saveTreeTheme(const QString &name, const TreeAppearanceTheme &theme);
bool saveViewportTheme(const QString &name, const ViewportAppearanceTheme &theme);

TreeAppearanceTheme defaultTreeTheme();
ViewportAppearanceTheme defaultViewportTheme();

} // namespace AppearanceThemes

#endif // APPEARANCETHEMES_H
