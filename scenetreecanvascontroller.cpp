#include "scenetreecanvascontroller.h"
#include "scenetreegraphicswidget.h"
#include "scenetreeinlineeditor.h"

#include <QGraphicsItem>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>

SceneTreeCanvasController::SceneTreeCanvasController(SceneTreeGraphicsWidget *widget)
    : QObject(widget)
    , m_widget(widget)
{
    m_panInertiaTimer = new QTimer(this);
    m_panInertiaTimer->setInterval(16);
    connect(m_panInertiaTimer, &QTimer::timeout, this, &SceneTreeCanvasController::tickPan);

    m_zoomAnimTimer = new QTimer(this);
    m_zoomAnimTimer->setInterval(24);
    connect(m_zoomAnimTimer, &QTimer::timeout, this, &SceneTreeCanvasController::tickZoom);

    m_zoomIdleTimer = new QTimer(this);
    m_zoomIdleTimer->setSingleShot(true);
    m_zoomIdleTimer->setInterval(80);
    connect(m_zoomIdleTimer, &QTimer::timeout, this, [this]() {
        m_zoomIdle = true;
    });
}

void SceneTreeCanvasController::snapZoom()
{
    m_zoomAnimTimer->stop();
    m_zoomIdleTimer->stop();
    m_zoomAccel    = 0.0;
    m_zoomVelocity = 0.0;
    m_zoomIdle     = true;
}

bool SceneTreeCanvasController::isZoomAnimating() const
{
    return m_zoomAnimTimer && m_zoomAnimTimer->isActive();
}

void SceneTreeCanvasController::stopPanInertia()
{
    m_panInertiaTimer->stop();
    m_panVelocity = QPointF();
}

void SceneTreeCanvasController::startPan(QPoint pos)
{
    m_panning      = true;
    m_lastPanPoint = pos;
    m_widget->setCursor(Qt::ClosedHandCursor);
}

bool SceneTreeCanvasController::updatePan(QPoint pos)
{
    if (!m_panning)
        return false;
    const QPoint delta = pos - m_lastPanPoint;
    m_lastPanPoint = pos;
    m_panVelocity  = QPointF(delta.x(), delta.y());
    m_widget->horizontalScrollBar()->setValue(m_widget->horizontalScrollBar()->value() - delta.x());
    m_widget->verticalScrollBar()->setValue(m_widget->verticalScrollBar()->value() - delta.y());
    return true;
}

void SceneTreeCanvasController::stopPan()
{
    m_panning = false;
    m_widget->setCursor(Qt::OpenHandCursor);
    if (qAbs(m_panVelocity.x()) > 8.0 || qAbs(m_panVelocity.y()) > 8.0)
        m_panInertiaTimer->start();
    else
        m_panVelocity = QPointF();
}

void SceneTreeCanvasController::handleWheelZoom(QWheelEvent *event)
{
    constexpr qreal kAccelPerStep = 0.008;
    constexpr qreal kMaxAccel     = 0.020;

    m_zoomAccel += (event->angleDelta().y() > 0 ? 1 : -1) * kAccelPerStep;
    m_zoomAccel = qBound(-kMaxAccel, m_zoomAccel, kMaxAccel);
    m_zoomAnchorScene = m_widget->mapToScene(event->position().toPoint());

    m_zoomIdle = false;
    m_zoomIdleTimer->start();  // restart — will set idle after 80ms

    if (!m_zoomAnimTimer->isActive()) {
        for (QGraphicsItem *item : m_widget->m_treeItems)
            item->setCacheMode(QGraphicsItem::ItemCoordinateCache);
        m_widget->setRenderHint(QPainter::Antialiasing, false);
        m_widget->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
        m_zoomAnimTimer->start();
    }
    event->accept();
}

void SceneTreeCanvasController::tickZoom()
{
    // ── Physics step: accel → velocity → zoom ──────────────────────────
    constexpr qreal kAccelDecay  = 0.70;
    constexpr qreal kVelFriction = 0.94;
    constexpr qreal kMaxVel      = 0.12;
    constexpr qreal kStepMin     = 0.89;
    constexpr qreal kStepMax     = 1.12;

    m_zoomVelocity += m_zoomAccel;
    m_zoomVelocity = qBound(-kMaxVel, m_zoomVelocity, kMaxVel);

    qreal step = 1.0 + m_zoomVelocity;
    step = qBound(kStepMin, step, kStepMax);

    // Active: gentle decay — accel persists, speed builds
    // Idle:   2× decay — fast stop when wheel stops
    if (m_zoomIdle) {
        m_zoomAccel    *= kAccelDecay  * kAccelDecay;   // 0.49
        m_zoomVelocity *= kVelFriction * kVelFriction;  // 0.88
    } else {
        m_zoomAccel    *= kAccelDecay;                   // 0.70
        m_zoomVelocity *= kVelFriction;                  // 0.94
    }

    // ── Apply zoom with stable anchor (clamped to safe range) ──────────
    // kMinZoom guards against scene-scale ~0 which causes GDI font-engine
    // failures (GetGlyphOutline error 1003) when toolbar text items are
    // rebuilt at an extreme inverse transform.
    constexpr qreal kMinZoom = 0.05;
    constexpr qreal kMaxZoom = 8.0;
    const qreal currentScale = m_widget->transform().m11();
    const qreal clampedStep  = qBound(kMinZoom / currentScale, step, kMaxZoom / currentScale);
    if (qAbs(clampedStep - step) > 1e-9) {
        m_zoomVelocity = 0.0;
        m_zoomAccel    = 0.0;
    }
    const QPointF anchorVp      = m_widget->mapFromScene(m_zoomAnchorScene);
    m_widget->scale(clampedStep, clampedStep);
    const QPointF anchorVpAfter = m_widget->mapFromScene(m_zoomAnchorScene);
    const QPointF vpDelta       = anchorVpAfter - anchorVp;
    m_widget->horizontalScrollBar()->setValue(
        m_widget->horizontalScrollBar()->value() + qRound(vpDelta.x()));
    m_widget->verticalScrollBar()->setValue(
        m_widget->verticalScrollBar()->value() + qRound(vpDelta.y()));

    const qreal newLevel = m_widget->transform().m11();

    // ── Stop when both are negligible ───────────────────────────────────
    const bool zoomJustStopped = qAbs(m_zoomVelocity) < 0.002 && qAbs(m_zoomAccel) < 0.0001;
    if (zoomJustStopped) {
        m_zoomAnimTimer->stop();
        m_zoomIdleTimer->stop();
        m_zoomVelocity = 0.0;
        m_zoomAccel    = 0.0;
        for (QGraphicsItem *item : m_widget->m_treeItems)
            item->setCacheMode(QGraphicsItem::NoCache);
        m_widget->setRenderHint(QPainter::Antialiasing, true);
        m_widget->setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
        m_widget->viewport()->update();
    }

    // ── Reposition UI when zoom actually changed noticeably ──────────
    // During animation: cheap reposition only — avoids addText() calls
    // (drawHintOverlay) at extreme scene scales that trigger GDI failures.
    // After stop: full rebuild at a stable, clamped scale.
    if (qAbs(newLevel - m_zoomLevel) > 0.001) {
        m_zoomLevel = newLevel;
        m_widget->m_inlineEditor->updateInlineInputGeometry();
        if (zoomJustStopped)
            m_widget->updateToolbarOverlay();
        else
            m_widget->repositionToolbarItemsSync();
    }
}

void SceneTreeCanvasController::tickPan()
{
    if (qAbs(m_panVelocity.x()) < 2.0 && qAbs(m_panVelocity.y()) < 2.0) {
        m_panInertiaTimer->stop();
        m_panVelocity = QPointF();
        return;
    }
    m_widget->horizontalScrollBar()->setValue(
        m_widget->horizontalScrollBar()->value() - qRound(m_panVelocity.x()));
    m_widget->verticalScrollBar()->setValue(
        m_widget->verticalScrollBar()->value() - qRound(m_panVelocity.y()));
    m_panVelocity *= 0.92;
}
