#ifndef SCENETREEDROPPREVIEWCONTROLLER_H
#define SCENETREEDROPPREVIEWCONTROLLER_H

#include "scenetreelayout.h"

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <functional>

class QGraphicsItem;
class QTimer;
class SceneTreeGraphicsWidget;

// Manages drag-drop preview animation for the scene-tree canvas.
//
// Owns the transient preview scene items and the 16 ms animation timer.
// dropTargetForToolAt() and the interpolation helpers remain in the widget's
// translation unit (anonymous-namespace scope); this controller accesses them
// via the friend-class pointer.
class SceneTreeDropPreviewController
{
    friend class SceneTreeGraphicsWidget;

public:
    using DropTarget = SceneTreeLayout::DropTarget;

    explicit SceneTreeDropPreviewController(SceneTreeGraphicsWidget *widget);

    // Called by the widget's thin public wrappers (showDropPreview, etc.)
    void show(const QPointF &scenePosition, const QSizeF &previewSize,
              const QString &previewTool, int movingNodeId);
    void finish();
    void clear();
    void scheduleCommit(std::function<void()> action);

    bool isActive()    const { return m_active; }
    bool isFinishing() const { return m_finishing; }

private:
    void startAnimation(const DropTarget &target, const QString &previewTool,
                        int movingNodeId, qreal durationMs);
    void advance();
    void renderFrame(const DropTarget &target);

    SceneTreeGraphicsWidget *m_widget;
    QTimer                  *m_animTimer = nullptr;

    QVector<QGraphicsItem *> m_items;

    DropTarget m_startTarget;
    DropTarget m_target;
    DropTarget m_currentTarget;
    QString    m_tool;
    int        m_movingNodeId  = 0;
    qreal      m_progress      = 0.0;
    qreal      m_durationMs    = 180.0;
    bool       m_active        = false;
    bool       m_finishing     = false;

    // Pending canvas-insertion position: set during a free-floating tool drop,
    // consumed by drawTreeOrPlaceholder() on the next refresh.
    bool    m_hasPendingInsertPos         = false;
    QPointF m_pendingInsertCanvasPosition;
};

#endif // SCENETREEDROPPREVIEWCONTROLLER_H
