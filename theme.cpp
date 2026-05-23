#include "theme.h"

#include <QApplication>
#include <QPalette>

// ── availableThemes() ─────────────────────────────────────────────────────────

QVector<ThemeSpec> availableThemes()
{
    return {
        { QStringLiteral("dark-nord"),   QStringLiteral("Dark Nord"),
          QColor("#2e3440"), QColor("#252a34"), QColor("#3b4252"), QColor("#434c5e"),
          QColor("#eceff4"), QColor("#7f8898"), QColor("#4c566a"), QColor("#5e81ac"),
          QColor("#81a1c1"), QColor("#bf616a"), QColor("#111827"), QColor("#3b4252") },
        { QStringLiteral("olive-light"), QStringLiteral("Olive Light"),
          QColor("#eef0e6"), QColor("#f8f9f1"), QColor("#dde3cf"), QColor("#cfd8bd"),
          QColor("#23281c"), QColor("#65705a"), QColor("#a7b18f"), QColor("#6f8741"),
          QColor("#8fa85b"), QColor("#9f4f45"), QColor("#3b442e"), QColor("#586b3a") },
        { QStringLiteral("graphite"),    QStringLiteral("Graphite"),
          QColor("#202124"), QColor("#17191c"), QColor("#2b2f34"), QColor("#353b42"),
          QColor("#eef1f4"), QColor("#8f98a3"), QColor("#4b5560"), QColor("#6ea8fe"),
          QColor("#8bbcff"), QColor("#e06c75"), QColor("#111315"), QColor("#303844") },
        { QStringLiteral("blueprint"),   QStringLiteral("Blueprint"),
          QColor("#17202a"), QColor("#111820"), QColor("#223044"), QColor("#2f405a"),
          QColor("#e9f2ff"), QColor("#94a6bc"), QColor("#40556d"), QColor("#4ea1d3"),
          QColor("#78bee8"), QColor("#d66a6a"), QColor("#0b1320"), QColor("#243b55") },
        { QStringLiteral("warm-light"),  QStringLiteral("Warm Light"),
          QColor("#f4f1ea"), QColor("#fffdf7"), QColor("#e8dfd1"), QColor("#d8cbbb"),
          QColor("#27221c"), QColor("#776a5d"), QColor("#b6a897"), QColor("#9b6b3f"),
          QColor("#bd8452"), QColor("#a85048"), QColor("#443326"), QColor("#6b4b35") },
    };
}

ThemeSpec defaultTheme()
{
    return availableThemes().first();
}

// ── colorName() ───────────────────────────────────────────────────────────────

QString colorName(const QColor &c)
{
    return c.name(QColor::HexRgb);
}

// ── applyTheme() ──────────────────────────────────────────────────────────────

static QString themeStyleSheet(const ThemeSpec &t)
{
    QString style = QStringLiteral(R"(
        QMainWindow, QWidget { background-color: __WINDOW__; color: __TEXT__; font-family: "Segoe UI"; font-size: 10pt; }
        QMenuBar { background-color: __SURFACE__; color: __TEXT__; border-bottom: 1px solid __BORDER__; padding: 2px; }
        QMenuBar::item { background: transparent; padding: 4px 10px; }
        QMenuBar::item:selected, QMenuBar::item:pressed { background-color: __PANEL__; border-radius: 4px; }
        QMenu { background-color: __WINDOW__; color: __TEXT__; border: 1px solid __BORDER__; padding: 4px; }
        QMenu::item { padding: 5px 24px 5px 18px; }
        QMenu::item:selected { background-color: __ACCENT__; color: #ffffff; }
        QMenu::separator { height: 1px; background: __BORDER__; margin: 4px 8px; }
        QDockWidget { titlebar-close-icon: none; titlebar-normal-icon: none; }
        QDockWidget::title { background-color: __PANEL__; color: __TEXT__; padding: 5px 8px; border: 1px solid __BORDER__; }
        QLabel { background: transparent; color: __TEXT__; }
        QPushButton, QToolButton { background-color: __PANEL__; color: __TEXT__; border: 1px solid __BORDER__; border-radius: 5px; padding: 5px 10px; min-height: 22px; }
        QPushButton:hover, QToolButton:hover { background-color: __PANEL_ALT__; border-color: __ACCENT__; }
        QPushButton:pressed, QToolButton:pressed { background-color: __ACCENT__; color: #ffffff; }
        QPushButton:disabled, QToolButton:disabled { background-color: __PANEL__; color: __MUTED__; border-color: __BORDER__; }
        QTextEdit, QPlainTextEdit, QLineEdit, QAbstractSpinBox, QComboBox, QListView, QTreeView, QTableView { background-color: __SURFACE__; color: __TEXT__; selection-background-color: __ACCENT__; selection-color: #ffffff; border: 1px solid __BORDER__; border-radius: 4px; padding: 3px; }
        QTextEdit { font-family: "Consolas", "Courier New", monospace; }
        QAbstractSpinBox:disabled, QComboBox:disabled, QLineEdit:disabled { background-color: __PANEL__; color: __MUTED__; border-color: __BORDER__; }
        QComboBox::drop-down { width: 20px; border-left: 1px solid __BORDER__; background-color: __PANEL__; }
        QComboBox QAbstractItemView { background-color: __SURFACE__; color: __TEXT__; border: 1px solid __BORDER__; selection-background-color: __ACCENT__; }
        QGroupBox { color: __TEXT__; border: 1px solid __BORDER__; border-radius: 5px; margin-top: 10px; padding-top: 10px; }
        QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; background-color: __WINDOW__; }
        QCheckBox { spacing: 6px; background: transparent; }
        QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid __BORDER__; border-radius: 3px; background-color: __SURFACE__; }
        QCheckBox::indicator:checked { background-color: __ACCENT__; border-color: __ACCENT_HOVER__; }
        QSplitter::handle { background-color: __BORDER__; }
        QSplitter::handle:hover { background-color: __ACCENT__; }
        QScrollBar:vertical, QScrollBar:horizontal { background: __SURFACE__; border: none; margin: 0; }
        QScrollBar::handle:vertical, QScrollBar::handle:horizontal { background: __BORDER__; border-radius: 4px; min-height: 24px; min-width: 24px; }
        QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover { background: __ACCENT__; }
        QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: none; border: none; width: 0; height: 0; }
        QToolTip { background-color: __PANEL__; color: __TEXT__; border: 1px solid __ACCENT__; padding: 4px; }
    )");
    style.replace(QStringLiteral("__WINDOW__"),       colorName(t.window));
    style.replace(QStringLiteral("__SURFACE__"),      colorName(t.surface));
    style.replace(QStringLiteral("__PANEL__"),        colorName(t.panel));
    style.replace(QStringLiteral("__PANEL_ALT__"),    colorName(t.panelAlt));
    style.replace(QStringLiteral("__TEXT__"),         colorName(t.text));
    style.replace(QStringLiteral("__MUTED__"),        colorName(t.mutedText));
    style.replace(QStringLiteral("__BORDER__"),       colorName(t.border));
    style.replace(QStringLiteral("__ACCENT__"),       colorName(t.accent));
    style.replace(QStringLiteral("__ACCENT_HOVER__"), colorName(t.accentHover));
    style.replace(QStringLiteral("__DANGER__"),       colorName(t.danger));
    return style;
}

void applyTheme(const ThemeSpec &theme)
{
    QApplication::setStyle(QStringLiteral("Fusion"));
    QPalette palette;
    palette.setColor(QPalette::Window,          theme.window);
    palette.setColor(QPalette::WindowText,      theme.text);
    palette.setColor(QPalette::Base,            theme.surface);
    palette.setColor(QPalette::AlternateBase,   theme.panel);
    palette.setColor(QPalette::ToolTipBase,     theme.panel);
    palette.setColor(QPalette::ToolTipText,     theme.text);
    palette.setColor(QPalette::Text,            theme.text);
    palette.setColor(QPalette::Button,          theme.panel);
    palette.setColor(QPalette::ButtonText,      theme.text);
    palette.setColor(QPalette::BrightText,      theme.danger);
    palette.setColor(QPalette::Highlight,       theme.accent);
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::Disabled, QPalette::Text,       theme.mutedText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, theme.mutedText);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, theme.mutedText);
    qApp->setPalette(palette);
    qApp->setStyleSheet(themeStyleSheet(theme));
}
