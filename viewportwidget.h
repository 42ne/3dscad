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

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    const QVector<ShapeNode> *m_shapes = nullptr;
    int m_selectedIndex = -1;
    float m_cameraYaw = -35.0f;
    float m_cameraPitch = 28.0f;
    float m_cameraDistance = 220.0f;
    QPoint m_lastMousePosition;
};

#endif
