#ifndef ANIMATEDTITLEBAR_H
#define ANIMATEDTITLEBAR_H

#include "theme.h"

#include <QColor>
#include <QPoint>
#include <QWidget>

class QLabel;
class QToolButton;

// Frameless-window title bar with hover feedback, drag-to-move,
// and minimize / maximize / close buttons.
class AnimatedTitleBar : public QWidget
{
public:
    explicit AnimatedTitleBar(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    void setTheme(const ThemeSpec &theme);

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void toggleMaximized();

    QLabel         *m_titleLabel;
    QToolButton    *m_minimizeButton;
    QToolButton    *m_maximizeButton;
    QToolButton    *m_closeButton;

    QColor m_baseColor    {QStringLiteral("#111827")};
    QColor m_pulseColor   {QStringLiteral("#3b4252")};
    QColor m_currentColor {QStringLiteral("#111827")};
    QColor m_textColor    {QStringLiteral("#eceff4")};
    QColor m_borderColor  {QStringLiteral("#4c566a")};
    QColor m_accentColor  {QStringLiteral("#5e81ac")};
    QColor m_dangerColor  {QStringLiteral("#bf616a")};

    bool   m_dragging   = false;
    QPoint m_dragOffset;
};

#endif // ANIMATEDTITLEBAR_H
