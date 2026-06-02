#ifndef SCENETREEOVERLAYCONTROLLER_H
#define SCENETREEOVERLAYCONTROLLER_H

#include "scenetreeoverlaygraphicsitems.h"

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QVector>

class QGraphicsItem;
class SceneTreeGraphicsWidget;

// Manages all viewport overlay elements:
//   • Tool palette toolbar (left / right / top docking with drag)
//   • Canvas-background switcher swatches
//   • Tree-theme switcher swatches + color-edit mode toggle
//
// Uses the established friend-class pattern: holds a pointer to the widget
// and accesses its members directly.  The widget keeps thin wrapper methods
// for the public interface so that call sites in the rest of the widget don't
// need to be updated.
class SceneTreeOverlayController
{
    friend class SceneTreeGraphicsWidget;
    friend class SceneTreeHoverManager;
    friend class SceneTreeColorEditMode;

public:
    explicit SceneTreeOverlayController(SceneTreeGraphicsWidget *widget);

    // ── Public interface (called by widget and friends) ────────────────────
    void updateToolbarOverlay();
    void repositionToolbarItems();
    void repositionToolbarItemsSync();

    bool isOnToolbarBackground(QPointF vpPos) const;
    int  toolbarSnapSideForVpPos(QPoint vpPos) const;

private:
    // ── Drawing ───────────────────────────────────────────────────────────
    QRectF drawToolbar();
    void   drawThemeSwitcher();
    void   drawCanvasBackgroundSwitcher();
    void   clearToolbar();

    // ── Click handlers ────────────────────────────────────────────────────
    void handleThemeSwitcherClick(int themeIndex);
    void handleCanvasBackgroundSwitcherClick(int backgroundIndex);

    // ── Internals ────────────────────────────────────────────────────────
    SceneTreeGraphicsWidget *m_widget;

    // All overlay scene items (toolbar + switchers + hint panel).
    // Kept in sync by updateToolbarOverlay / clearToolbar.
    QVector<QGraphicsItem *> m_items;
    QVector<QPointF>         m_itemOffsets; // VP-pixel offsets for cheap reposition

    // Viewport-pixel rects for drag-zone hit testing (rebuilt in drawToolbar).
    QRectF          m_panelVpRect;
    QVector<QRectF> m_toolVpRects;

    // Toolbar docking side: 0 = top (horizontal), 1 = left (vertical), 2 = right (vertical)
    int m_side = 0;

    // Toolbar drag state (press-threshold: ≥ 8 px before drag activates)
    bool   m_dragPending = false;
    bool   m_dragActive  = false;
    QPoint m_dragPressVp;

    // Guard against queuing multiple deferred repositions in the same frame
    bool m_repositionPending = false;

    // Pointer to the color-edit toggle item so the widget can detect hover priority
    QGraphicsItem *m_colorEditToggleItem = nullptr;
    QGraphicsItem *m_zoomCacheToggleItem = nullptr;
};

#endif // SCENETREEOVERLAYCONTROLLER_H
