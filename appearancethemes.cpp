#include "appearancethemes.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace {

QString safeFileStem(QString name)
{
    name = name.trimmed();
    for (int i = 0; i < name.size(); ++i) {
        const QChar c = name.at(i);
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_'))
            name[i] = QLatin1Char('_');
    }
    return name.isEmpty() ? QStringLiteral("theme") : name;
}

QString scopeDirectory(const QString &scope)
{
    const QString path = QDir(AppearanceThemes::themesDirectory()).filePath(scope);
    QDir().mkpath(path);
    return path;
}

QString themePath(const QString &scope, const QString &name)
{
    return QDir(scopeDirectory(scope)).filePath(safeFileStem(name) + QStringLiteral(".ini"));
}

QStringList savedNames(const QString &scope)
{
    QDir dir(scopeDirectory(scope));
    QStringList names;
    for (const QFileInfo &info : dir.entryInfoList({QStringLiteral("*.ini")}, QDir::Files, QDir::Name)) {
        QSettings settings(info.filePath(), QSettings::IniFormat);
        if (settings.value(QStringLiteral("meta/builtIn"), false).toBool())
            continue;
        names.append(settings.value(QStringLiteral("meta/name"), info.completeBaseName()).toString());
    }
    return names;
}

QColor readColor(QSettings &settings, const QString &key, const QColor &fallback)
{
    const QColor color(settings.value(QStringLiteral("colors/") + key, fallback.name(QColor::HexArgb)).toString());
    return color.isValid() ? color : fallback;
}

void writeColor(QSettings &settings, const QString &key, const QColor &color)
{
    settings.setValue(QStringLiteral("colors/") + key, color.name(QColor::HexArgb));
}

void writeWindowFile(const QString &path, const ThemeSpec &theme, bool builtIn)
{
    QSettings out(path, QSettings::IniFormat);
    out.setValue(QStringLiteral("meta/type"), QStringLiteral("window"));
    out.setValue(QStringLiteral("meta/name"), theme.label);
    out.setValue(QStringLiteral("meta/builtIn"), builtIn);
    writeColor(out, QStringLiteral("window"), theme.window);
    writeColor(out, QStringLiteral("surface"), theme.surface);
    writeColor(out, QStringLiteral("panel"), theme.panel);
    writeColor(out, QStringLiteral("panelAlt"), theme.panelAlt);
    writeColor(out, QStringLiteral("text"), theme.text);
    writeColor(out, QStringLiteral("mutedText"), theme.mutedText);
    writeColor(out, QStringLiteral("border"), theme.border);
    writeColor(out, QStringLiteral("accent"), theme.accent);
    writeColor(out, QStringLiteral("accentHover"), theme.accentHover);
    writeColor(out, QStringLiteral("danger"), theme.danger);
    writeColor(out, QStringLiteral("titleBase"), theme.titleBase);
    writeColor(out, QStringLiteral("titlePulse"), theme.titlePulse);
    out.sync();
}

} // namespace

namespace AppearanceThemes {

QString themesDirectory()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("themes"));
}

TreeAppearanceTheme defaultTreeTheme()
{
    TreeAppearanceTheme t;
    t.name = QStringLiteral("Tree Glass");
    t.canvas     = QColor(31, 41, 55);
    t.minorGrid  = QColor(96, 106, 121);
    t.majorGrid  = QColor(139, 150, 166);
    t.card       = QColor(28, 48, 72, 185);
    t.header     = QColor(48, 72, 98, 210);
    t.input      = QColor(255, 255, 255, 32);
    t.text       = QColor(218, 228, 245);
    t.mutedText  = QColor(148, 165, 192);
    t.glassTop   = QColor(24, 34, 50, 218);
    t.glassBottom= QColor(8, 13, 22, 196);
    t.glassBorder= QColor(142, 178, 215, 120);
    t.accent     = QColor(255, 203, 87);
    t.numBorder  = QColor(96, 118, 148);
    t.numFill    = QColor(255, 255, 255, 32);
    t.numBorderActive = QColor(220, 156, 26);
    t.numFillActive   = QColor(255, 220, 108, 205);
    t.numText    = QColor(218, 228, 245);
    t.numLabelText    = QColor(140, 175, 220);
    return t;
}

ViewportAppearanceTheme defaultViewportTheme()
{
    return {
        QStringLiteral("Viewport Dark"),
        QColor(30, 32, 36), QColor(70, 74, 82), QColor(91, 190, 151),
        QColor(118, 214, 168), QColor(24, 34, 50, 218), QColor(8, 13, 22, 196),
        QColor(142, 178, 215, 120), QColor(232, 242, 255), QColor(184, 205, 228),
        QColor(255, 203, 87)
    };
}

void ensureDefaultFiles()
{
    QDir().mkpath(themesDirectory());
    for (const ThemeSpec &theme : availableThemes()) {
        const QString path = themePath(QStringLiteral("window"), theme.label);
        if (!QFileInfo::exists(path))
            writeWindowFile(path, theme, true);
    }

    const QString treeExample = themePath(QStringLiteral("tree"), QStringLiteral("_default_tree_reference"));
    if (!QFileInfo::exists(treeExample)) {
        QSettings out(treeExample, QSettings::IniFormat);
        out.setValue(QStringLiteral("meta/type"), QStringLiteral("tree"));
        out.setValue(QStringLiteral("meta/name"), QStringLiteral("Tree Glass"));
        out.setValue(QStringLiteral("meta/builtIn"), true);
        const TreeAppearanceTheme t = defaultTreeTheme();
        writeColor(out, QStringLiteral("canvas"), t.canvas);
        writeColor(out, QStringLiteral("minorGrid"), t.minorGrid);
        writeColor(out, QStringLiteral("majorGrid"), t.majorGrid);
        writeColor(out, QStringLiteral("card"), t.card);
        writeColor(out, QStringLiteral("header"), t.header);
        writeColor(out, QStringLiteral("input"), t.input);
        writeColor(out, QStringLiteral("text"), t.text);
        writeColor(out, QStringLiteral("mutedText"), t.mutedText);
        writeColor(out, QStringLiteral("glassTop"), t.glassTop);
        writeColor(out, QStringLiteral("glassBottom"), t.glassBottom);
        writeColor(out, QStringLiteral("glassBorder"), t.glassBorder);
        writeColor(out, QStringLiteral("accent"), t.accent);
        writeColor(out, QStringLiteral("numBorder"), t.numBorder);
        writeColor(out, QStringLiteral("numFill"), t.numFill);
        writeColor(out, QStringLiteral("numBorderActive"), t.numBorderActive);
        writeColor(out, QStringLiteral("numFillActive"), t.numFillActive);
        writeColor(out, QStringLiteral("numText"), t.numText);
        writeColor(out, QStringLiteral("numLabelText"), t.numLabelText);
        out.sync();
    }

    const QString viewportExample = themePath(QStringLiteral("viewport"), QStringLiteral("_default_viewport_reference"));
    if (!QFileInfo::exists(viewportExample)) {
        const ViewportAppearanceTheme t = defaultViewportTheme();
        QSettings out(viewportExample, QSettings::IniFormat);
        out.setValue(QStringLiteral("meta/type"), QStringLiteral("viewport"));
        out.setValue(QStringLiteral("meta/name"), t.name);
        out.setValue(QStringLiteral("meta/builtIn"), true);
        writeColor(out, QStringLiteral("background"), t.background);
        writeColor(out, QStringLiteral("grid"), t.grid);
        writeColor(out, QStringLiteral("solid"), t.solid);
        writeColor(out, QStringLiteral("computedSolid"), t.computedSolid);
        writeColor(out, QStringLiteral("glassTop"), t.glassTop);
        writeColor(out, QStringLiteral("glassBottom"), t.glassBottom);
        writeColor(out, QStringLiteral("glassBorder"), t.glassBorder);
        writeColor(out, QStringLiteral("text"), t.text);
        writeColor(out, QStringLiteral("mutedText"), t.mutedText);
        writeColor(out, QStringLiteral("accent"), t.accent);
        out.sync();
    }
}

QStringList customWindowThemeNames() { return savedNames(QStringLiteral("window")); }
QStringList customTreeThemeNames() { return savedNames(QStringLiteral("tree")); }
QStringList customViewportThemeNames() { return savedNames(QStringLiteral("viewport")); }

bool loadWindowTheme(const QString &name, ThemeSpec *theme)
{
    if (!theme)
        return false;
    QSettings in(themePath(QStringLiteral("window"), name), QSettings::IniFormat);
    if (!QFileInfo::exists(in.fileName()))
        return false;
    ThemeSpec t = defaultTheme();
    t.id = QStringLiteral("custom:") + safeFileStem(name);
    t.label = in.value(QStringLiteral("meta/name"), name).toString();
    t.window = readColor(in, QStringLiteral("window"), t.window);
    t.surface = readColor(in, QStringLiteral("surface"), t.surface);
    t.panel = readColor(in, QStringLiteral("panel"), t.panel);
    t.panelAlt = readColor(in, QStringLiteral("panelAlt"), t.panelAlt);
    t.text = readColor(in, QStringLiteral("text"), t.text);
    t.mutedText = readColor(in, QStringLiteral("mutedText"), t.mutedText);
    t.border = readColor(in, QStringLiteral("border"), t.border);
    t.accent = readColor(in, QStringLiteral("accent"), t.accent);
    t.accentHover = readColor(in, QStringLiteral("accentHover"), t.accentHover);
    t.danger = readColor(in, QStringLiteral("danger"), t.danger);
    t.titleBase = readColor(in, QStringLiteral("titleBase"), t.titleBase);
    t.titlePulse = readColor(in, QStringLiteral("titlePulse"), t.titlePulse);
    *theme = t;
    return true;
}

bool saveWindowTheme(const QString &name, const ThemeSpec &theme)
{
    ThemeSpec saved = theme;
    saved.label = name.trimmed();
    writeWindowFile(themePath(QStringLiteral("window"), name), saved, false);
    return true;
}

bool loadTreeTheme(const QString &name, TreeAppearanceTheme *theme)
{
    if (!theme)
        return false;
    const QString path = themePath(QStringLiteral("tree"), name);
    if (!QFileInfo::exists(path))
        return false;
    QSettings in(path, QSettings::IniFormat);
    TreeAppearanceTheme t = defaultTreeTheme();
    t.name = in.value(QStringLiteral("meta/name"), name).toString();
    t.canvas = readColor(in, QStringLiteral("canvas"), t.canvas);
    t.minorGrid = readColor(in, QStringLiteral("minorGrid"), t.minorGrid);
    t.majorGrid = readColor(in, QStringLiteral("majorGrid"), t.majorGrid);
    t.card = readColor(in, QStringLiteral("card"), t.card);
    t.header = readColor(in, QStringLiteral("header"), t.header);
    t.input = readColor(in, QStringLiteral("input"), t.input);
    t.text = readColor(in, QStringLiteral("text"), t.text);
    t.mutedText = readColor(in, QStringLiteral("mutedText"), t.mutedText);
    t.glassTop = readColor(in, QStringLiteral("glassTop"), t.glassTop);
    t.glassBottom = readColor(in, QStringLiteral("glassBottom"), t.glassBottom);
    t.glassBorder = readColor(in, QStringLiteral("glassBorder"), t.glassBorder);
    t.accent = readColor(in, QStringLiteral("accent"), t.accent);
    t.numBorder = readColor(in, QStringLiteral("numBorder"), t.numBorder);
    t.numFill = readColor(in, QStringLiteral("numFill"), t.numFill);
    t.numBorderActive = readColor(in, QStringLiteral("numBorderActive"), t.numBorderActive);
    t.numFillActive = readColor(in, QStringLiteral("numFillActive"), t.numFillActive);
    t.numText = readColor(in, QStringLiteral("numText"), t.numText);
    t.numLabelText = readColor(in, QStringLiteral("numLabelText"), t.numLabelText);
    *theme = t;
    return true;
}

bool saveTreeTheme(const QString &name, const TreeAppearanceTheme &theme)
{
    QSettings out(themePath(QStringLiteral("tree"), name), QSettings::IniFormat);
    out.setValue(QStringLiteral("meta/type"), QStringLiteral("tree"));
    out.setValue(QStringLiteral("meta/name"), name.trimmed());
    out.setValue(QStringLiteral("meta/builtIn"), false);
    writeColor(out, QStringLiteral("canvas"), theme.canvas);
    writeColor(out, QStringLiteral("minorGrid"), theme.minorGrid);
    writeColor(out, QStringLiteral("majorGrid"), theme.majorGrid);
    writeColor(out, QStringLiteral("card"), theme.card);
    writeColor(out, QStringLiteral("header"), theme.header);
    writeColor(out, QStringLiteral("input"), theme.input);
    writeColor(out, QStringLiteral("text"), theme.text);
    writeColor(out, QStringLiteral("mutedText"), theme.mutedText);
    writeColor(out, QStringLiteral("glassTop"), theme.glassTop);
    writeColor(out, QStringLiteral("glassBottom"), theme.glassBottom);
    writeColor(out, QStringLiteral("glassBorder"), theme.glassBorder);
    writeColor(out, QStringLiteral("accent"), theme.accent);
    writeColor(out, QStringLiteral("numBorder"), theme.numBorder);
    writeColor(out, QStringLiteral("numFill"), theme.numFill);
    writeColor(out, QStringLiteral("numBorderActive"), theme.numBorderActive);
    writeColor(out, QStringLiteral("numFillActive"), theme.numFillActive);
    writeColor(out, QStringLiteral("numText"), theme.numText);
    writeColor(out, QStringLiteral("numLabelText"), theme.numLabelText);
    out.sync();
    return true;
}

bool loadViewportTheme(const QString &name, ViewportAppearanceTheme *theme)
{
    if (!theme)
        return false;
    const QString path = themePath(QStringLiteral("viewport"), name);
    if (!QFileInfo::exists(path))
        return false;
    QSettings in(path, QSettings::IniFormat);
    ViewportAppearanceTheme t = defaultViewportTheme();
    t.name = in.value(QStringLiteral("meta/name"), name).toString();
    t.background = readColor(in, QStringLiteral("background"), t.background);
    t.grid = readColor(in, QStringLiteral("grid"), t.grid);
    t.solid = readColor(in, QStringLiteral("solid"), t.solid);
    t.computedSolid = readColor(in, QStringLiteral("computedSolid"), t.computedSolid);
    t.glassTop = readColor(in, QStringLiteral("glassTop"), t.glassTop);
    t.glassBottom = readColor(in, QStringLiteral("glassBottom"), t.glassBottom);
    t.glassBorder = readColor(in, QStringLiteral("glassBorder"), t.glassBorder);
    t.text = readColor(in, QStringLiteral("text"), t.text);
    t.mutedText = readColor(in, QStringLiteral("mutedText"), t.mutedText);
    t.accent = readColor(in, QStringLiteral("accent"), t.accent);
    *theme = t;
    return true;
}

bool saveViewportTheme(const QString &name, const ViewportAppearanceTheme &theme)
{
    QSettings out(themePath(QStringLiteral("viewport"), name), QSettings::IniFormat);
    out.setValue(QStringLiteral("meta/type"), QStringLiteral("viewport"));
    out.setValue(QStringLiteral("meta/name"), name.trimmed());
    out.setValue(QStringLiteral("meta/builtIn"), false);
    writeColor(out, QStringLiteral("background"), theme.background);
    writeColor(out, QStringLiteral("grid"), theme.grid);
    writeColor(out, QStringLiteral("solid"), theme.solid);
    writeColor(out, QStringLiteral("computedSolid"), theme.computedSolid);
    writeColor(out, QStringLiteral("glassTop"), theme.glassTop);
    writeColor(out, QStringLiteral("glassBottom"), theme.glassBottom);
    writeColor(out, QStringLiteral("glassBorder"), theme.glassBorder);
    writeColor(out, QStringLiteral("text"), theme.text);
    writeColor(out, QStringLiteral("mutedText"), theme.mutedText);
    writeColor(out, QStringLiteral("accent"), theme.accent);
    out.sync();
    return true;
}

} // namespace AppearanceThemes
