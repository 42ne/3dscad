#include "animatedtitlebar.h"

#include <QApplication>
#include <QColor>
#include <QEasingCurve>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QScreen>
#include <QToolButton>
#include <QVariantAnimation>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// ── helpers ───────────────────────────────────────────────────────────────────

static QColor mixColors(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF()   + (b.redF()   - a.redF())   * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF()  + (b.blueF()  - a.blueF())  * t);
}

// ── AnimatedTitleBar ──────────────────────────────────────────────────────────

AnimatedTitleBar::AnimatedTitleBar(QWidget *parent)
    : QWidget(parent)
    , m_titleLabel    (new QLabel(this))
    , m_minimizeButton(new QToolButton(this))
    , m_maximizeButton(new QToolButton(this))
    , m_closeButton   (new QToolButton(this))
    , m_pulse         (new QVariantAnimation(this))
{
    setFixedHeight(34);
    setAttribute(Qt::WA_Hover, true);

    m_titleLabel->setText(QStringLiteral("OpenSCAD Visual Editor Prototype"));
    m_titleLabel->setContentsMargins(10, 0, 0, 0);

    m_minimizeButton->setText(QStringLiteral("_"));
    m_maximizeButton->setText(QStringLiteral("[]"));
    m_closeButton->setText(QStringLiteral("X"));
    for (QToolButton *b : {m_minimizeButton, m_maximizeButton, m_closeButton}) {
        b->setFixedSize(36, 24);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
    }

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 4, 0);
    layout->setSpacing(4);
    layout->addWidget(m_titleLabel, 1);
    layout->addWidget(m_minimizeButton);
    layout->addWidget(m_maximizeButton);
    layout->addWidget(m_closeButton);

    m_pulse->setDuration(1200);
    m_pulse->setLoopCount(-1);
    m_pulse->setEasingCurve(QEasingCurve::InOutSine);
    m_pulse->setStartValue(0.0);
    m_pulse->setKeyValueAt(0.5, 1.0);
    m_pulse->setEndValue(0.0);
    connect(m_pulse, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_currentColor = mixColors(m_baseColor, m_pulseColor, v.toReal());
        update();
    });

    connect(m_minimizeButton, &QToolButton::clicked, this, [this]() {
        if (QWidget *w = window()) w->showMinimized();
    });
    connect(m_maximizeButton, &QToolButton::clicked, this, [this]() { toggleMaximized(); });
    connect(m_closeButton,    &QToolButton::clicked, this, [this]() {
        if (QWidget *w = window()) w->close();
    });
}

void AnimatedTitleBar::setTitle(const QString &title)
{
    m_titleLabel->setText(title);
}

void AnimatedTitleBar::setTheme(const ThemeSpec &theme)
{
    m_baseColor    = theme.titleBase;
    m_pulseColor   = theme.titlePulse;
    m_currentColor = m_baseColor;
    m_textColor    = theme.text;
    m_borderColor  = theme.border;
    m_accentColor  = theme.accent;
    m_dangerColor  = theme.danger;

    const QString btn = QStringLiteral(
        "QToolButton { background-color: transparent; color: %1; border: none; border-radius: 5px; padding: 0; font-weight: 600; }"
        "QToolButton:hover { background-color: %2; color: #ffffff; }"
        "QToolButton:pressed { background-color: %3; color: #ffffff; }")
        .arg(colorName(m_textColor), colorName(m_accentColor), colorName(theme.accentHover));

    const QString cls = QStringLiteral(
        "QToolButton { background-color: transparent; color: %1; border: none; border-radius: 5px; padding: 0; font-weight: 700; }"
        "QToolButton:hover { background-color: %2; color: #ffffff; }"
        "QToolButton:pressed { background-color: %3; color: #ffffff; }")
        .arg(colorName(m_textColor), colorName(m_dangerColor), colorName(theme.accentHover));

    m_titleLabel->setStyleSheet(
        QStringLiteral("background: transparent; color: %1; font-weight: 600;")
            .arg(colorName(m_textColor)));
    m_minimizeButton->setStyleSheet(btn);
    m_maximizeButton->setStyleSheet(btn);
    m_closeButton->setStyleSheet(cls);
    update();
}

// ── events ────────────────────────────────────────────────────────────────────

void AnimatedTitleBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), m_currentColor);
    QColor line = m_borderColor;
    line.setAlpha(180);
    p.setPen(QPen(line, 1));
    p.drawLine(QPoint(0, height() - 1), QPoint(width(), height() - 1));
}

void AnimatedTitleBar::enterEvent(QEvent *)  { m_pulse->start(); }
void AnimatedTitleBar::leaveEvent(QEvent *)  { m_pulse->stop(); m_currentColor = m_baseColor; update(); }

void AnimatedTitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging   = true;
        m_dragOffset = event->globalPos() - window()->frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void AnimatedTitleBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        if (QWidget *w = window(); w && !w->isMaximized())
            w->move(event->globalPos() - m_dragOffset);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void AnimatedTitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
    QWidget::mouseReleaseEvent(event);
}

void AnimatedTitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) { toggleMaximized(); event->accept(); return; }
    QWidget::mouseDoubleClickEvent(event);
}

void AnimatedTitleBar::toggleMaximized()
{
    QWidget *w = window();
    if (!w) return;
    if (w->isMaximized()) { w->showNormal(); return; }
#ifdef Q_OS_WIN
    // ShowWindow(SW_SHOWMAXIMIZED) picks the target monitor via MonitorFromWindow(),
    // which relies on the window's current HWND position.  For a frameless Qt popup
    // window this can resolve to the primary monitor even when the window is visually
    // on a secondary screen.  Moving to the correct screen's origin first fixes the
    // monitor lookup.
    // SWP_NOREDRAW suppresses any repaint during the position change so the window
    // never appears at the intermediate top-left position — eliminating the blink that
    // would otherwise occur between the move and the maximise.
    if (QScreen *s = QApplication::screenAt(w->frameGeometry().center())) {
        const QPoint tl = s->availableGeometry().topLeft();
        ::SetWindowPos(reinterpret_cast<HWND>(w->winId()),
                       nullptr, tl.x(), tl.y(), 0, 0,
                       SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    }
#else
    if (QScreen *s = QApplication::screenAt(w->frameGeometry().center()))
        w->move(s->availableGeometry().topLeft());
#endif
    w->showMaximized();
}
