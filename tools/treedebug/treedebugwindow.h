#ifndef TREEDEBUGWINDOW_H
#define TREEDEBUGWINDOW_H

#include "../../scenedocument.h"

#include <QMainWindow>
#include <QPoint>
#include <QPointF>

class SceneTreeGraphicsWidget;
class QTextEdit;
class QLabel;
class QToolButton;
class QVariantAnimation;
class DebugTreeWidget;   // defined in treedebugwindow.cpp

// ─────────────────────────────────────────────────────────────────────────────
// Standalone scene-tree debug window.
// Right panel: structured text log — copy-paste to share with Claude.
// ─────────────────────────────────────────────────────────────────────────────
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

    // Logging helpers
    void log(const QString &line);
    void logSnapshot(const QString &context);
    void logCursor(const QPoint &widget, const QPointF &scene);
    QString formatRect(const QRectF &r) const;
    QString formatRectShort(const QRectF &r) const;

    void appendSnapshotForNode(QString &out, const SceneDocument::TreeNode &node,
                               int depth, const QString &indent) const;

private:
    SceneDocument      m_scene;
    DebugTreeWidget   *m_treeWidget  = nullptr;
    QTextEdit         *m_debugLog    = nullptr;
    int                m_eventSeq    = 0;
    QPointF            m_lastScene;
    QPoint             m_lastWidget;
};

#endif // TREEDEBUGWINDOW_H
