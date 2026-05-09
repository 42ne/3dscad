#ifndef VIEWPORTWIDGET_H
#define VIEWPORTWIDGET_H

#include "shapenode.h"

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
    const QVector<ShapeNode> *m_shapes = nullptr;
    int m_selectedIndex = -1;
    float m_cameraYaw = -35.0f;
    float m_cameraPitch = 28.0f;
    float m_cameraDistance = 220.0f;
    QPoint m_lastMousePosition;
    QPoint m_dragStartMousePosition;
    bool m_draggingShape = false;
    int m_dragShapeIndex = -1;
    QVector<int> m_pickBuffer;
    QSize m_pickBufferSize;
};

#endif
