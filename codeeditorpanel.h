#ifndef CODEEDITORPANEL_H
#define CODEEDITORPANEL_H

#include "openscadgenerator.h"
#include "scenecontroller.h"

#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QTextCursor;
class QTextEdit;

// Self-contained code editor panel: displays generated OpenSCAD code,
// accepts edits, reports apply/send requests, and writes the live preview file.
class CodeEditorPanel : public QWidget
{
    Q_OBJECT

public:
    explicit CodeEditorPanel(QWidget *parent = nullptr);

    // Replace editor contents and update source-range map.
    // Preserves the vertical scroll position.
    void setCodeAndRanges(const QString &code,
                          const QVector<OpenScadGenerator::SourceRange> &ranges);

    // Set code without touching the source-range map (e.g. before loading an example).
    void setCode(const QString &code);

    // Returns the current editor text.
    QString code() const;

    // Highlight the source range for selectedNodeId in yellow (pass 0 to clear).
    void applySelectionHighlight(int selectedNodeId);

    // Highlight the ctrl+wheel active number in blue.
    void applyCtrlParamHighlight(const SceneController::CtrlParamHighlight &h);

    // Clear any extra selection highlighting.
    void clearHighlight();

    // Parse-error banner.
    void showParseError(const QString &msg, int errorLine);
    void clearParseError();

    // Preview-file helpers.
    QString previewScadPath() const;
    bool    writeOpenScadPreview(bool notify);

signals:
    void applyRequested();
    void sendToOpenScadRequested();

private:
    void scrollToShowCursor(const QTextCursor &cursor);

    QTextEdit   *m_codeEditor           = nullptr;
    QPushButton *m_applyCodeButton      = nullptr;
    QPushButton *m_sendToOpenScadButton  = nullptr;
    QLabel      *m_openScadPreviewLabel  = nullptr;
    QLabel      *m_parseErrorLabel       = nullptr;

    QVector<OpenScadGenerator::SourceRange> m_sourceRanges;
};

#endif // CODEEDITORPANEL_H
