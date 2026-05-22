#ifndef TREEDEBUGWINDOW_H
#define TREEDEBUGWINDOW_H

#include "../../scenedocument.h"

#include <QMainWindow>
#include <QPoint>

class SceneTreeGraphicsWidget;
class QLabel;
class QToolButton;
class QVariantAnimation;

// ─────────────────────────────────────────────
// Minimal standalone window for debugging
// the scene tree widget visual behaviour.
// No viewport, no OpenSCAD, no CSG backend.
// ─────────────────────────────────────────────
class TreeDebugWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit TreeDebugWindow(QWidget *parent = nullptr);

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildUi();
    void buildTestDocument();

private:
    SceneDocument m_scene;
    SceneTreeGraphicsWidget *m_treeWidget = nullptr;
};

#endif // TREEDEBUGWINDOW_H
