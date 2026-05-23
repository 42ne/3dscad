#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "examplepreviewpopup.h"
#include "scenecontroller.h"

#include <QFutureWatcher>
#include <QImage>
#include <QMainWindow>

class QByteArray;
class QLabel;
class QMenu;
class QTimer;
class CodeEditorPanel;
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
    void onExampleHoverTimeout();
    void onExampleThumbnailReady();
    void addCube();
    void addSphere();
    void addCylinder();
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
    QString examplesPath() const;
    void populateExamplesMenu(QMenu *menu);
    void hideExamplePreview();

private:
    SceneController *m_controller = nullptr;

    // Example hover preview
    ExamplePreviewPopup    *m_examplePreview     = nullptr;
    QTimer                 *m_exampleHoverTimer  = nullptr;
    QFutureWatcher<QImage> *m_thumbnailWatcher   = nullptr;
    QString                 m_pendingPreviewFile;
    QString                 m_pendingPreviewName;
    int                     m_pendingMenuRight    = 0;
    int                     m_pendingCursorY      = 0;

    ViewportWidget          *m_viewport           = nullptr;
    SceneTreeGraphicsWidget *m_sceneTreeGraphics  = nullptr;
    CodeEditorPanel         *m_codeEditorPanel    = nullptr;
    QLabel                  *m_csgStatusLabel     = nullptr;
};

#endif
