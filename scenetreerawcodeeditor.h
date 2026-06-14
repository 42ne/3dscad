#ifndef SCENETREERAWCODEEDITOR_H
#define SCENETREERAWCODEEDITOR_H

#include <QObject>
#include <QRectF>

class QPlainTextEdit;
class SceneTreeGraphicsWidget;

class SceneTreeRawCodeEditor : public QObject
{
    Q_OBJECT

public:
    explicit SceneTreeRawCodeEditor(SceneTreeGraphicsWidget *widget);

    void startEditing(int nodeId, const QRectF &sceneRect, const QString &code);
    void updateGeometry();
    void stopEditing(bool commit);

private:
    SceneTreeGraphicsWidget *m_widget = nullptr;
    QPlainTextEdit *m_editor = nullptr;
    QRectF m_sceneRect;
    int m_nodeId = 0;
    bool m_active = false;
};

#endif // SCENETREERAWCODEEDITOR_H
