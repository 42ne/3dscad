#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "examplepreviewpopup.h"
#include "openscadgenerator.h"
#include "scenecontroller.h"

#include <QFutureWatcher>
#include <QImage>
#include <QMainWindow>
#include <QVector>

class QTextCursor;
class QTextEdit;
class QLabel;
class QPushButton;
class QAction;
class QByteArray;
class QTimer;
class ViewportWidget;
class SceneTreeGraphicsWidget;

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
    void applyOpenScadCode();
    void sendToOpenScad();
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
    void applyCtrlParamHighlight();
    void scrollCodeEditorToShowCursor(const QTextCursor &cursor);
    QString previewScadPath() const;
    QString examplesPath() const;
    void populateExamplesMenu(QMenu *menu);
    void hideExamplePreview();
    bool writeOpenScadPreview(bool notify);

private:
    SceneController *m_controller = nullptr;

    // Example hover preview
    ExamplePreviewPopup    *m_examplePreview    = nullptr;
    QTimer                 *m_exampleHoverTimer = nullptr;
    QFutureWatcher<QImage> *m_thumbnailWatcher  = nullptr;
    QString                 m_pendingPreviewFile;
    QString                 m_pendingPreviewName;
    int                     m_pendingMenuRight   = 0;
    int                     m_pendingCursorY     = 0;

    ViewportWidget           *m_viewport         = nullptr;
    SceneTreeGraphicsWidget  *m_sceneTreeGraphics = nullptr;
    QTextEdit                *m_codeEditor        = nullptr;
    QPushButton              *m_applyCodeButton   = nullptr;
    QPushButton              *m_sendToOpenScadButton = nullptr;
    QLabel                   *m_csgStatusLabel    = nullptr;
    QLabel                   *m_openScadPreviewLabel = nullptr;
    QLabel                   *m_parseErrorLabel   = nullptr;

    QVector<OpenScadGenerator::SourceRange> m_openScadSourceRanges;
};

#endif
