#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "scenecontroller.h"

#include <QMainWindow>

class QByteArray;
class QLabel;
class QMenu;
class CodeEditorPanel;
class ExampleBrowserMenu;
class SceneTreeGraphicsWidget;
class ViewportWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;

private slots:
    void addCube();
    void addSphere();
    void addCylinder();
    void addCone();
    void addUnionGroup();
    void addDifferenceGroup();
    void addIntersectionGroup();
    void loadExample(const QString &filePath);

private:
    void buildUi();
    void refreshShapeList();
    void refreshProperties();
    void refreshOpenScadCode();
    void refreshCsgStatus();
    void refreshSceneViews();
    void onSelectionChanged(int nodeId);
    void highlightOpenScadSelection();
    void clearSelection();

private:
    SceneController *m_controller = nullptr;

    ExampleBrowserMenu      *m_exampleBrowser    = nullptr;
    ViewportWidget          *m_viewport          = nullptr;
    SceneTreeGraphicsWidget *m_sceneTreeGraphics  = nullptr;
    CodeEditorPanel         *m_codeEditorPanel    = nullptr;
    QLabel                  *m_csgStatusLabel     = nullptr;
};

#endif
