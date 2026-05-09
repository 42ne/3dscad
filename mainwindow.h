#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "scenedocument.h"

#include <QMainWindow>

class QTextEdit;
class QListWidget;
class QDoubleSpinBox;
class QComboBox;
class ViewportWidget;

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

private:
    SceneDocument m_scene;

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
