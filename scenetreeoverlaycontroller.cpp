#include "scenetreeoverlaycontroller.h"
#include "scenetreegraphicswidget.h"
#include "scenetreehovermanager.h"
#include "scenetreegraphicshelpers.h"
#include "scenetreepalette.h"
#include "scenetreetoolbarrenderer.h"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QPainterPath>
#include <QPen>
#include <QTimer>
#include <QtGlobal>
#include <cmath>

using namespace SceneTreeGraphics;

// ── Local helpers (mirror of the anonymous-namespace helpers in widget .cpp) ──
// Consolidated in a single place once audit #1 (constants extraction) is done.
namespace {

constexpr int CanvasBackgroundThemeCount = 6;

struct CanvasBackgroundTheme {
    QColor background;
    QColor minorGrid;
    QColor majorGrid;
};

CanvasBackgroundTheme canvasBackgroundTheme(int index)
{
    static const CanvasBackgroundTheme themes[CanvasBackgroundThemeCount] = {
        { QColor(31, 41, 55),    QColor(96, 106, 121),   QColor(139, 150, 166) },
        { QColor(16, 22, 32),    QColor(57, 69, 84),     QColor(92, 108, 128)  },
        { QColor(50, 51, 56),    QColor(82, 84, 91),     QColor(119, 122, 131) },
        { QColor(231, 235, 241), QColor(197, 204, 214),  QColor(160, 171, 185) },
        { QColor(246, 239, 226), QColor(217, 207, 190),  QColor(183, 171, 151) },
        { QColor(220, 235, 241), QColor(185, 207, 216),  QColor(145, 177, 190) },
    };
    return themes[qBound(0, index, CanvasBackgroundThemeCount - 1)];
}

CanvasBackgroundTheme activeCanvasBackgroundTheme(int index)
{
    if (SceneTreePalette::hasCustomTheme()) {
        const TreeAppearanceTheme theme = SceneTreePalette::customTheme();
        return {theme.canvas, theme.minorGrid, theme.majorGrid};
    }
    return canvasBackgroundTheme(index);
}

bool usesDarkOverlayGlass(int backgroundTheme)
{
    return activeCanvasBackgroundTheme(backgroundTheme).background.lightness() < 128;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

SceneTreeOverlayController::SceneTreeOverlayController(SceneTreeGraphicsWidget *widget)
    : m_widget(widget)
{
}

// ── Public API ────────────────────────────────────────────────────────────────

void SceneTreeOverlayController::updateToolbarOverlay()
{
    if (!m_widget->m_graphicsScene)
        return;

    clearToolbar();
    drawToolbar();
    drawCanvasBackgroundSwitcher();
    drawThemeSwitcher();
    m_widget->m_hoverManager->drawHintOverlay();

    // Compute VP-pixel offsets for cheap repositioning without full rebuild.
    const QPointF vtl = m_widget->mapToScene(QPoint(0, 0));
    const qreal s = qMax(0.001, std::abs(m_widget->transform().m11()));
    m_itemOffsets.resize(m_items.size());
    for (int i = 0; i < m_items.size(); ++i) {
        const QPointF delta = m_items[i]->pos() - vtl;
        m_itemOffsets[i] = QPointF(delta.x() * s, delta.y() * s);
    }
}

void SceneTreeOverlayController::repositionToolbarItems()
{
    if (m_items.isEmpty())
        return;
    if (m_items.size() != m_itemOffsets.size()) {
        updateToolbarOverlay();
        return;
    }
    repositionToolbarItemsSync();
}

void SceneTreeOverlayController::repositionToolbarItemsSync()
{
    if (m_items.isEmpty() || m_items.size() != m_itemOffsets.size())
        return;
    const QPointF vtl = m_widget->mapToScene(QPoint(0, 0));
    const qreal s = qMax(0.001, std::abs(m_widget->transform().m11()));
    for (int i = 0; i < m_items.size(); ++i)
        m_items[i]->setPos(vtl + QPointF(m_itemOffsets[i].x() / s,
                                          m_itemOffsets[i].y() / s));
}

bool SceneTreeOverlayController::isOnToolbarBackground(QPointF vpPos) const
{
    if (!m_panelVpRect.contains(vpPos))
        return false;
    for (const QRectF &r : m_toolVpRects)
        if (r.contains(vpPos))
            return false;
    return true;
}

int SceneTreeOverlayController::toolbarSnapSideForVpPos(QPoint vpPos) const
{
    const QWidget *vp = m_widget->viewport();
    const qreal vw = vp ? vp->width()  : 640.0;
    const qreal vh = vp ? vp->height() : 480.0;
    if (vpPos.y() < vh * 0.25)
        return 0;
    return (vpPos.x() < vw * 0.5) ? 1 : 2;
}

// ── Private: drawing ──────────────────────────────────────────────────────────

QRectF SceneTreeOverlayController::drawToolbar()
{
    const QPointF viewportTopLeft = m_widget->mapToScene(QPoint(0, 0));
    const QWidget *vp = m_widget->viewport();
    const qreal viewportWidth  = vp ? vp->width()  : 640.0;
    const qreal viewportHeight = vp ? vp->height() : 480.0;
    const qreal viewportScale  = m_widget->transform().m11();

    m_toolVpRects.clear();
    return SceneTreeToolbarRenderer(m_widget->m_graphicsScene,
                                    &m_items,
                                    m_widget->m_treeTheme,
                                    usesDarkOverlayGlass(m_widget->m_canvasBackgroundTheme))
        .render(
            [this](const QPointF &pos, const QSizeF &sz, const QString &tool) {
                m_widget->showDropPreview(pos, sz, tool);
            },
            [this]() { m_widget->finishDropPreview(); },
            [this](const QString &tool, const QPointF &pos) {
                m_widget->handleToolDrop(tool, pos);
            },
            [this](const QString &tool, bool hovered) {
                if (hovered) {
                    m_widget->m_hoverManager->updateHoverHint(
                        QStringLiteral("toolbar:%1").arg(tool),
                        toolbarToolTip(tool));
                } else if (m_widget->m_hoverManager->hoverHintKey()
                           == QStringLiteral("toolbar:%1").arg(tool)) {
                    m_widget->m_hoverManager->updateTooltip(
                        m_widget->mapToGlobal(m_widget->m_lastMousePosition),
                        m_widget->mapToScene(m_widget->m_lastMousePosition),
                        QApplication::keyboardModifiers() & Qt::ControlModifier);
                }
            },
            viewportTopLeft,
            viewportWidth,
            viewportHeight,
            viewportScale,
            m_side,
            &m_panelVpRect,
            &m_toolVpRects);
}

void SceneTreeOverlayController::clearToolbar()
{
    m_colorEditToggleItem = nullptr;
    const QVector<QGraphicsItem *> toDelete = m_items;
    for (QGraphicsItem *item : toDelete) {
        if (!item) continue;
        m_widget->m_graphicsScene->removeItem(item);
    }
    m_items.clear();
    m_itemOffsets.clear();
    // Defer deletion: removeItem() prevents new events, but QGraphicsScene's
    // hover-tracking may still hold the pointer until the event loop tick ends.
    QTimer::singleShot(0, m_widget, [toDelete]() { qDeleteAll(toDelete); });
}

void SceneTreeOverlayController::drawCanvasBackgroundSwitcher()
{
    if (!m_widget->m_graphicsScene || !m_widget->viewport())
        return;

    const QPointF viewportTopLeft = m_widget->mapToScene(QPoint(0, 0));
    const qreal viewportHeight = m_widget->viewport()->height();
    const qreal safeScale = qMax<qreal>(0.001, std::abs(m_widget->transform().m11()));
    const auto scenePoint = [&](qreal x, qreal y) {
        return viewportTopLeft + QPointF(x / safeScale, y / safeScale);
    };

    constexpr qreal SwatchR = 7.0;
    constexpr qreal SwatchGap = 5.0;
    constexpr qreal PadH = 7.0;
    constexpr qreal PadV = 6.0;
    constexpr qreal BottomGap = 12.0;
    constexpr qreal RowGap = 8.0;
    constexpr qreal LocalOverlayZ = 10000.0;

    const qreal panelW = CanvasBackgroundThemeCount * (SwatchR * 2.0)
                         + (CanvasBackgroundThemeCount - 1) * SwatchGap + PadH * 2.0;
    const qreal panelH = SwatchR * 2.0 + PadV * 2.0;
    const QRectF panelLocal(0.0, 0.0, panelW, panelH);
    const QPointF panelTopLeft = scenePoint(12.0, viewportHeight - BottomGap - panelH * 2.0 - RowGap);

    const bool darkGlass  = usesDarkOverlayGlass(m_widget->m_canvasBackgroundTheme);
    const bool customGlass = SceneTreePalette::hasCustomTheme();
    const TreeAppearanceTheme customTheme = SceneTreePalette::customTheme();

    addFlatGlassPanel(m_widget->m_graphicsScene, &m_items, panelLocal, panelTopLeft,
                      LocalOverlayZ, darkGlass, customGlass, customTheme, switcherGlassStyle());

    const QColor ringColor = darkGlass ? QColor(255, 255, 255, 210) : QColor(40, 50, 65, 200);

    for (int i = 0; i < CanvasBackgroundThemeCount; ++i) {
        const bool active = (i == m_widget->m_canvasBackgroundTheme);
        const QColor swatch = canvasBackgroundTheme(i).background;
        const QPointF center(PadH + i * (SwatchR * 2.0 + SwatchGap) + SwatchR, PadV + SwatchR);

        if (active) {
            auto *ring = new ThemeSwitcherSwatchItem(
                center, SwatchR + 2.8,
                QPen(ringColor, 1.6), Qt::NoBrush,
                i, [this](int idx) { handleCanvasBackgroundSwitcherClick(idx); });
            ring->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
            ring->setPos(panelTopLeft);
            ring->setZValue(LocalOverlayZ);
            ring->setData(0, QStringLiteral("swatch"));
            m_widget->m_graphicsScene->addItem(ring);
            m_items.append(ring);
        }

        const bool swatchDark  = swatch.lightness() < 128;
        const QColor borderCol = swatchDark ? swatch.lighter(145) : swatch.darker(118);
        const QColor hoverCol  = swatchDark ? swatch.lighter(170) : swatch.darker(135);
        const QPen normalPen = active ? QPen(Qt::NoPen) : QPen(borderCol, 1.0);
        const QPen hoverPen  = active ? QPen(Qt::NoPen) : QPen(hoverCol,  2.0);
        auto *circle = new ThemeSwitcherSwatchItem(
            center, SwatchR, normalPen, QBrush(swatch),
            i, [this](int idx) { handleCanvasBackgroundSwitcherClick(idx); }, hoverPen);
        circle->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        circle->setPos(panelTopLeft);
        circle->setZValue(LocalOverlayZ + 0.5);
        circle->setData(0, QStringLiteral("swatch"));
        m_widget->m_graphicsScene->addItem(circle);
        m_items.append(circle);
    }
}

void SceneTreeOverlayController::drawThemeSwitcher()
{
    if (!m_widget->m_graphicsScene || !m_widget->viewport())
        return;

    const QPointF viewportTopLeft = m_widget->mapToScene(QPoint(0, 0));
    const qreal viewportHeight = m_widget->viewport()->height();
    const qreal safeScale = qMax<qreal>(0.001, std::abs(m_widget->transform().m11()));
    const auto scenePoint = [&](qreal x, qreal y) {
        return viewportTopLeft + QPointF(x / safeScale, y / safeScale);
    };

    constexpr qreal SwatchR   =  7.0;
    constexpr qreal SwatchGap =  5.0;
    constexpr qreal PadH      =  7.0;
    constexpr qreal PadV      =  6.0;
    constexpr qreal BottomGap = 12.0;
    constexpr qreal LocalOverlayZ = 10000.0;

    const int n = SceneTreePalette::ThemeCount;
    const qreal panelW = n * (SwatchR * 2.0) + (n - 1) * SwatchGap + PadH * 2.0;
    const qreal panelH = SwatchR * 2.0 + PadV * 2.0;

    const QRectF panelLocal(0.0, 0.0, panelW, panelH);
    const QPointF panelTopLeft = scenePoint(12.0, viewportHeight - BottomGap - panelH);

    const bool darkGlass  = usesDarkOverlayGlass(m_widget->m_canvasBackgroundTheme);
    const bool customGlass = SceneTreePalette::hasCustomTheme();
    const TreeAppearanceTheme customTheme = SceneTreePalette::customTheme();

    addFlatGlassPanel(m_widget->m_graphicsScene, &m_items, panelLocal, panelTopLeft,
                      LocalOverlayZ, darkGlass, customGlass, customTheme, switcherGlassStyle());

    const QColor ringColor = darkGlass ? QColor(255, 255, 255, 210) : QColor(40, 50, 65, 200);

    for (int i = 0; i < n; ++i) {
        const auto  th     = static_cast<SceneTreePalette::Theme>(i);
        const bool  active = (i == m_widget->m_treeTheme);
        const QColor swatch = SceneTreePalette::swatchColor(th);
        const qreal cx = PadH + i * (SwatchR * 2.0 + SwatchGap) + SwatchR;
        const qreal cy = PadV + SwatchR;

        if (active) {
            auto *ring = new ThemeSwitcherSwatchItem(
                QPointF(cx, cy), SwatchR + 2.8,
                QPen(ringColor, 1.6), Qt::NoBrush,
                i, [this](int idx) { handleThemeSwitcherClick(idx); });
            ring->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
            ring->setPos(panelTopLeft);
            ring->setZValue(LocalOverlayZ);
            ring->setData(0, QStringLiteral("swatch"));
            m_widget->m_graphicsScene->addItem(ring);
            m_items.append(ring);
        }

        const bool swatchDark  = swatch.lightness() < 128;
        const QColor borderCol = swatchDark ? swatch.lighter(145) : swatch.darker(118);
        const QColor hoverCol  = swatchDark ? swatch.lighter(170) : swatch.darker(135);
        const QPen normalPen = active ? QPen(Qt::NoPen) : QPen(borderCol, 1.0);
        const QPen hoverPen  = active ? QPen(Qt::NoPen) : QPen(hoverCol,  2.0);
        auto *circle = new ThemeSwitcherSwatchItem(
            QPointF(cx, cy), SwatchR, normalPen, QBrush(swatch),
            i, [this](int idx) { handleThemeSwitcherClick(idx); }, hoverPen);
        circle->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        circle->setPos(panelTopLeft);
        circle->setZValue(LocalOverlayZ + 0.5);
        circle->setData(0, QStringLiteral("swatch"));
        m_widget->m_graphicsScene->addItem(circle);
        m_items.append(circle);
    }

    // ── Color-edit mode toggle ────────────────────────────────────────────
    {
        constexpr qreal ToggleGap = 7.0;
        constexpr qreal TW = ColorEditToggleItem::kW;
        constexpr qreal TH = ColorEditToggleItem::kH;

        const qreal toggleVpX = 12.0 + panelW + ToggleGap;
        const qreal toggleVpY = viewportHeight - BottomGap - panelH * 0.5 - TH * 0.5;
        const QPointF toggleTopLeft = scenePoint(toggleVpX, toggleVpY);

        QPainterPath shadowPath;
        shadowPath.addRoundedRect(QRectF(1.5, 2.5, TW, TH), TW * 0.5, TW * 0.5);
        auto *toggleShadow = m_widget->m_graphicsScene->addPath(shadowPath,
            Qt::NoPen, QBrush(QColor(0, 0, 0, darkGlass ? 85 : 28)));
        toggleShadow->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        toggleShadow->setAcceptedMouseButtons(Qt::NoButton);
        toggleShadow->setPos(toggleTopLeft);
        toggleShadow->setZValue(LocalOverlayZ - 2.0);
        toggleShadow->setOpacity(darkGlass ? 0.62 : 0.38);
        toggleShadow->setData(0, QStringLiteral("shadow"));
        m_items.append(toggleShadow);

        auto *tog = new ColorEditToggleItem(
            m_widget->colorEditMode(),
            [this] { m_widget->setColorEditMode(!m_widget->colorEditMode()); },
            [this](bool enter) {
                if (enter) {
                    const QString stateStr = m_widget->colorEditMode()
                        ? QStringLiteral("ON  — click to exit")
                        : QStringLiteral("OFF — click to enter");
                    m_widget->m_hoverManager->updateHoverHint(
                        QStringLiteral("colorEdit:toggle"),
                        QStringLiteral("✏ Color edit  ") + stateStr
                        + QStringLiteral("\nHover blocks to inspect and edit theme colors."));
                } else {
                    if (m_widget->colorEditMode())
                        m_widget->m_hoverManager->updateHoverHint(
                            QStringLiteral("colorEdit:mode"),
                            QStringLiteral("✏ Color edit\n"
                                           "Hover a card, then scroll ↕ to cycle properties.\n"
                                           "Click to pick a color.  Esc to exit."));
                    else
                        m_widget->m_hoverManager->updateHoverHint(QString(), QString());
                }
            });
        tog->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        tog->setPos(toggleTopLeft);
        tog->setZValue(LocalOverlayZ + 0.5);
        tog->setData(0, QStringLiteral("toggle"));
        m_widget->m_graphicsScene->addItem(tog);
        m_items.append(tog);
        m_colorEditToggleItem = tog;
    }
}

// ── Private: click handlers ───────────────────────────────────────────────────

void SceneTreeOverlayController::handleThemeSwitcherClick(int themeIndex)
{
    const int clamped = qBound(0, themeIndex, SceneTreePalette::ThemeCount - 1);
    const bool customWasActive = SceneTreePalette::hasCustomTheme();
    if (customWasActive) {
        m_widget->clearCustomAppearanceTheme();
        emit m_widget->builtInAppearanceSelected();
    }
    if (m_widget->m_treeTheme == clamped && !customWasActive)
        return;
    m_widget->setTreeTheme(clamped);
    emit m_widget->treeThemeChanged(m_widget->m_treeTheme);
}

void SceneTreeOverlayController::handleCanvasBackgroundSwitcherClick(int backgroundIndex)
{
    const int clamped = qBound(0, backgroundIndex, CanvasBackgroundThemeCount - 1);
    const bool customWasActive = SceneTreePalette::hasCustomTheme();
    if (customWasActive) {
        m_widget->clearCustomAppearanceTheme();
        emit m_widget->builtInAppearanceSelected();
    }
    if (m_widget->m_canvasBackgroundTheme == clamped && !customWasActive)
        return;
    m_widget->setCanvasBackgroundTheme(clamped);
    emit m_widget->canvasBackgroundThemeChanged(m_widget->m_canvasBackgroundTheme);
}
