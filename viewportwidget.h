#ifndef VIEWPORTWIDGET_H
#define VIEWPORTWIDGET_H

#include "csgevaluator.h"
#include "shapenode.h"

#include <QImage>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector>

class QMouseEvent;
class QWheelEvent;

class ViewportWidget : public QOpenGLWidget
{
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget *parent = nullptr);
    void setShapes(const QVector<ShapeNode> *shapes);
    void setSelectedIndex(int index);
    void invalidateCsgPreview();

signals:
    void shapeClicked(int index);
    void shapeDragStarted(int index);
    void shapeDragged(int index, const QVector3D &delta);
    void shapeDragFinished(int index);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    enum DragMode {
        NoDrag,
        PlaneDrag,
        AxisXDrag,
        AxisYDrag,
        AxisZDrag
    };

    const QVector<ShapeNode> *m_shapes = nullptr;
    int m_selectedIndex = -1;
    float m_cameraYaw = -35.0f;
    float m_cameraPitch = 28.0f;
    float m_cameraDistance = 220.0f;
    QPoint m_lastMousePosition;
    QPoint m_dragStartMousePosition;
    QVector3D m_lastDragDelta;
    bool m_draggingShape = false;
    DragMode m_dragMode = NoDrag;
    int m_dragShapeIndex = -1;
    CsgPreview m_cachedCsgPreview;
    uint m_cachedCsgFingerprint = 0;
    bool m_csgPreviewDirty = true;
    QVector<int> m_pickBuffer;
    QVector<float> m_depthBuffer;
    QImage m_renderImage;
    QSize m_pickBufferSize;
};

#endif
