#ifndef SCENECANVASDRAGHANDLER_H
#define SCENECANVASDRAGHANDLER_H

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QVector>

class QGraphicsPathItem;
class QGraphicsItem;
class QMouseEvent;
class SceneTreeGraphicsWidget;

class SceneCanvasDragHandler : public QObject
{
    Q_OBJECT
    friend class SceneTreeGraphicsWidget;

public:
    explicit SceneCanvasDragHandler(SceneTreeGraphicsWidget *widget);

    bool handleMousePress(QMouseEvent *event);
    bool handleMouseMove(QMouseEvent *event);
    bool handleMouseRelease(QMouseEvent *event);
    bool isActive() const { return m_canvasDragActive || m_canvasDragPending; }
    void clearAfterSceneClear();
    bool groupCollapseControlAt(const QPointF &scenePosition, int *groupId) const;
    bool applyMagneticSnap(const QPointF &candidate, const QSizeF &size,
                           int excludeId, QPointF *snapped,
                           const QVector<int> &additionalExcludeIds = QVector<int>()) const;
    QPointF nonOverlappingCanvasPosition(const QPointF &candidate,
                                         const QSizeF &size,
                                         const QVector<QRectF> &placedBlocks) const;

private:
    void showDragPlaceholder(const QPointF &pos, const QSizeF &size);
    void clearDragGhost();
    QVector<int> findConnectedCluster(int startNodeId) const;
    QVector<QGraphicsItem *> itemsInBlockRect(const QRectF &blockRect) const;
    void toggleGroupCollapsed(int groupId);

    SceneTreeGraphicsWidget *m_widget;

    bool               m_canvasDragPending   = false;
    bool               m_canvasDragActive    = false;
    int                m_canvasDragNodeId    = 0;
    QPointF            m_canvasDragPressScene;
    QPointF            m_canvasDragOrigPos;
    QSizeF             m_canvasDragBlockSize;
    QPointF            m_canvasDragCurrentPos;
    bool               m_canvasDragSnapped   = false;
    QGraphicsPathItem *m_canvasDragGhost     = nullptr;

    QVector<int>                         m_canvasDragCluster;
    QHash<int, QPointF>                  m_canvasDragClusterOrigPos;
    bool                                 m_canvasDragDetached = false;
    QPointF                              m_canvasDragPrevEventScene;
    QVector<QGraphicsItem *>             m_canvasDragItems;
    QHash<int, QVector<QGraphicsItem *>> m_clusterDragItems;

    bool    m_dbgPrevSnapped  = false;
    QPointF m_dbgLastLoggedPos;

    static constexpr qreal kMagnetRadius             = 80.0;
    static constexpr qreal kClusterVelocityThreshold = 16.0;
};

#endif
