#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QOpenGLWidget>
#include <QVector3D>
#include <QVector>

class QTextEdit;
class QListWidget;
class QDoubleSpinBox;
class QComboBox;
class QMouseEvent;
class QWheelEvent;

struct ShapeNode
{
    enum Type {
        Cube,
        Sphere,
        Cylinder
    };

    Type type = Cube;
    QString name;

    QVector3D position = QVector3D(0, 0, 0);
    QVector3D rotation = QVector3D(0, 0, 0);
    QVector3D size = QVector3D(20, 20, 20);

    float radius = 10.0f;
    float height = 20.0f;
};

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

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void addCube();
    void addSphere();
    void addCylinder();

    void onSelectionChanged(int row);
    void onPropertyChanged();

private:
    void buildUi();
    void refreshShapeList();
    void refreshProperties();
    void refreshOpenScadCode();
    QString generateOpenScadCode() const;
    QString shapeToOpenScad(const ShapeNode &shape) const;

private:
    QVector<ShapeNode> m_shapes;
    int m_selectedIndex = -1;

    ViewportWidget *m_viewport = nullptr;
    QListWidget *m_shapeList = nullptr;
    QTextEdit *m_codeEditor = nullptr;

    QDoubleSpinBox *m_posX = nullptr;
    QDoubleSpinBox *m_posY = nullptr;
    QDoubleSpinBox *m_posZ = nullptr;

    QDoubleSpinBox *m_rotX = nullptr;
    QDoubleSpinBox *m_rotY = nullptr;
    QDoubleSpinBox *m_rotZ = nullptr;

    QDoubleSpinBox *m_sizeX = nullptr;
    QDoubleSpinBox *m_sizeY = nullptr;
    QDoubleSpinBox *m_sizeZ = nullptr;

    QDoubleSpinBox *m_radius = nullptr;
    QDoubleSpinBox *m_height = nullptr;
};

#endif
