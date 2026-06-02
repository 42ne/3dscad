#ifndef SCENETREECANVASCONTROLLER_H
#define SCENETREECANVASCONTROLLER_H

#include <QObject>
#include <QPoint>
#include <QPointF>

class QTimer;
class QWheelEvent;
class SceneTreeGraphicsWidget;

class SceneTreeCanvasController : public QObject
{
    Q_OBJECT
public:
    explicit SceneTreeCanvasController(SceneTreeGraphicsWidget *widget);

    void snapZoom();
    void stopPanInertia();

    bool    isPanning()     const { return m_panning; }
    bool    isZoomAnimating() const;
    QPointF panVelocity()   const { return m_panVelocity; }
    qreal   zoomLevel()     const { return m_zoomLevel; }
    void    setZoomLevel(qreal level) { m_zoomLevel = level; }

    void startPan(QPoint pos);
    bool updatePan(QPoint pos);   // returns true when actually panning
    void stopPan();

    void handleWheelZoom(QWheelEvent *event);

private:
    void tickZoom();
    void tickPan();

    SceneTreeGraphicsWidget *m_widget;

    // Pan
    bool    m_panning         = false;
    QPoint  m_lastPanPoint;
    QPointF m_panVelocity;
    QTimer *m_panInertiaTimer = nullptr;

    // Zoom
    qreal   m_zoomAccel       = 0.0;
    qreal   m_zoomVelocity    = 0.0;
    qreal   m_zoomLevel       = 1.0;
    bool    m_zoomIdle        = true;
    QTimer *m_zoomAnimTimer   = nullptr;
    QTimer *m_zoomIdleTimer   = nullptr;
    QPointF m_zoomAnchorScene;
};

#endif
