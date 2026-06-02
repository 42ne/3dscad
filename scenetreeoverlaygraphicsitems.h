#ifndef SCENETREEOVERLAYGRAPHICSITEMS_H
#define SCENETREEOVERLAYGRAPHICSITEMS_H

#include <QGraphicsEllipseItem>
#include <QGraphicsItem>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <functional>

// ── ColorEditToggleItem ───────────────────────────────────────────────────────
// Standalone vertical toggle switch for color-edit mode.
// Thumb slides to top (ON) or bottom (OFF); track darkens when active.
class ColorEditToggleItem : public QGraphicsItem
{
public:
    static constexpr qreal kW      = 16.0;
    static constexpr qreal kH      = 28.0;
    static constexpr qreal kThumbD = 12.0;
    static constexpr qreal kThumbM = (kW - kThumbD) * 0.5;

    ColorEditToggleItem(bool on,
                        std::function<void()> onClick,
                        std::function<void(bool)> onHover = {})
        : m_on(on), m_onClick(std::move(onClick)), m_onHover(std::move(onHover))
    {
        setAcceptedMouseButtons(Qt::LeftButton);
        setAcceptHoverEvents(true);
    }

    QRectF boundingRect() const override
    { return QRectF(-2.0, -2.0, kW + 4.0, kH + 4.0); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QColor track = m_on ? QColor(22, 22, 22, 225) : QColor(110, 115, 125, 135);
        QPainterPath trackPath;
        trackPath.addRoundedRect(QRectF(0, 0, kW, kH), kW * 0.5, kW * 0.5);
        painter->setPen(Qt::NoPen);
        painter->setBrush(track);
        painter->drawPath(trackPath);
        const qreal thumbY = m_on ? kThumbM : kH - kThumbM - kThumbD;
        const QRectF thumbRect(kThumbM, thumbY, kThumbD, kThumbD);
        painter->setBrush(QColor(0, 0, 0, 45));
        painter->drawEllipse(thumbRect.translated(0.5, 1.2));
        painter->setPen(QPen(QColor(140, 140, 140, 55), 0.5));
        painter->setBrush(Qt::white);
        painter->drawEllipse(thumbRect);
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override
    { if (e->button() == Qt::LeftButton) { m_onClick(); e->accept(); } else e->ignore(); }
    void hoverEnterEvent(QGraphicsSceneHoverEvent *) override
    { if (m_onHover) m_onHover(true); }
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *) override
    { if (m_onHover) m_onHover(false); }

private:
    bool m_on;
    std::function<void()> m_onClick;
    std::function<void(bool)> m_onHover;
};

// ── ThemeSwitcherSwatchItem ───────────────────────────────────────────────────
// Clickable swatch circle in the theme/background switcher overlay.
class ThemeSwitcherSwatchItem : public QGraphicsEllipseItem
{
public:
    ThemeSwitcherSwatchItem(const QPointF &center,
                             qreal radius,
                             const QPen &pen,
                             const QBrush &brush,
                             int themeIndex,
                             std::function<void(int)> onClick,
                             const QPen &hoverPen = QPen(Qt::NoPen))
        : QGraphicsEllipseItem(center.x() - radius, center.y() - radius,
                                radius * 2.0, radius * 2.0)
        , m_themeIndex(themeIndex)
        , m_onClick(std::move(onClick))
        , m_normalPen(pen)
        , m_hoverPen(hoverPen)
    {
        setPen(pen);
        setBrush(brush);
        setAcceptedMouseButtons(Qt::LeftButton);
        if (m_hoverPen.style() != Qt::NoPen)
            setAcceptHoverEvents(true);
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (m_onClick) m_onClick(m_themeIndex);
            event->accept();
        } else {
            event->ignore();
        }
    }
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override
    { setPen(m_hoverPen); update(); event->accept(); }
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override
    { setPen(m_normalPen); update(); event->accept(); }

private:
    int m_themeIndex = 0;
    std::function<void(int)> m_onClick;
    QPen m_normalPen;
    QPen m_hoverPen;
};

#endif // SCENETREEOVERLAYGRAPHICSITEMS_H
