#include "codeeditorpanel.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QList>
#include <QMessageBox>
#include <QPushButton>
#include <QRect>
#include <QSaveFile>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

// ── CodeEditorPanel ───────────────────────────────────────────────────────────

CodeEditorPanel::CodeEditorPanel(QWidget *parent)
    : QWidget(parent)
{
    m_codeEditor = new QTextEdit;
    m_codeEditor->setReadOnly(false);
    m_codeEditor->setMinimumHeight(180);
    m_codeEditor->setFontFamily("Consolas");

    m_applyCodeButton       = new QPushButton(QStringLiteral("Apply code"));
    m_sendToOpenScadButton  = new QPushButton(QStringLiteral("Send to OpenSCAD"));

    m_openScadPreviewLabel = new QLabel;
    m_openScadPreviewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_openScadPreviewLabel->setWordWrap(true);
    m_openScadPreviewLabel->setText(
        QString("Preview file: %1").arg(QDir::toNativeSeparators(previewScadPath())));

    m_parseErrorLabel = new QLabel;
    m_parseErrorLabel->setWordWrap(true);
    m_parseErrorLabel->setContentsMargins(4, 2, 4, 2);
    m_parseErrorLabel->hide();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_codeEditor);
    layout->addWidget(m_applyCodeButton);
    layout->addWidget(m_parseErrorLabel);
    layout->addWidget(m_sendToOpenScadButton);
    layout->addWidget(m_openScadPreviewLabel);

    connect(m_applyCodeButton,      &QPushButton::clicked,
            this, &CodeEditorPanel::applyRequested);
    connect(m_sendToOpenScadButton, &QPushButton::clicked,
            this, &CodeEditorPanel::sendToOpenScadRequested);
}

void CodeEditorPanel::setCodeAndRanges(const QString &code,
                                        const QVector<OpenScadGenerator::SourceRange> &ranges)
{
    m_sourceRanges = ranges;
    const int savedScroll = m_codeEditor->verticalScrollBar()->value();
    m_codeEditor->setPlainText(code);
    m_codeEditor->verticalScrollBar()->setValue(savedScroll);
}

void CodeEditorPanel::setCode(const QString &code)
{
    m_codeEditor->setPlainText(code);
}

QString CodeEditorPanel::code() const
{
    return m_codeEditor->toPlainText();
}

// ── Highlight helpers ─────────────────────────────────────────────────────────

void CodeEditorPanel::scrollToShowCursor(const QTextCursor &cursor)
{
    if (cursor.isNull()) return;
    const QRect r   = m_codeEditor->cursorRect(cursor);
    const int   vpH = m_codeEditor->viewport()->height();
    if (r.top() < 0 || r.bottom() > vpH) {
        QScrollBar *sb = m_codeEditor->verticalScrollBar();
        sb->setValue(sb->value() + r.top() - vpH / 3);
    }
}

void CodeEditorPanel::applySelectionHighlight(int selectedNodeId)
{
    QTextEdit::ExtraSelection sel;
    bool found = false;

    for (const OpenScadGenerator::SourceRange &range : m_sourceRanges) {
        if (range.treeNodeId != selectedNodeId || range.length <= 0) continue;
        QTextCursor cursor(m_codeEditor->document());
        cursor.setPosition(range.start);
        cursor.setPosition(range.start + range.length, QTextCursor::KeepAnchor);
        QTextCharFormat fmt;
        fmt.setBackground(QColor(255, 203, 87, 95));
        sel.cursor = cursor;
        sel.format = fmt;
        found = true;
        break;
    }

    m_codeEditor->setExtraSelections(
        found ? QList<QTextEdit::ExtraSelection>{sel}
              : QList<QTextEdit::ExtraSelection>{});
    if (found)
        scrollToShowCursor(sel.cursor);
}

void CodeEditorPanel::applyCtrlParamHighlight(const SceneController::CtrlParamHighlight &h)
{
    const QString fullCode = m_codeEditor->toPlainText();

    for (const OpenScadGenerator::SourceRange &range : m_sourceRanges) {
        if (range.treeNodeId != h.nodeId || range.length <= 0) continue;
        const int     searchCap = qMin(range.length, 300);
        const QString needle    = h.contextPrefix + h.expression;
        const int     hitPos    = fullCode.indexOf(needle, range.start);
        if (hitPos < 0 || hitPos >= range.start + searchCap) break;

        const int exprPos  = hitPos + h.contextPrefix.size();
        const int numStart = exprPos + h.numberStart;
        const int numLen   = h.numberLength;
        if (numStart + numLen > fullCode.size()) break;

        QTextCursor cursor(m_codeEditor->document());
        cursor.setPosition(numStart);
        cursor.setPosition(numStart + numLen, QTextCursor::KeepAnchor);

        QTextCharFormat fmt;
        fmt.setBackground(QColor(80, 180, 255, 140));
        fmt.setFontUnderline(true);

        QTextEdit::ExtraSelection sel;
        sel.cursor = cursor;
        sel.format = fmt;
        m_codeEditor->setExtraSelections({sel});
        scrollToShowCursor(cursor);
        return;
    }
}

void CodeEditorPanel::clearHighlight()
{
    m_codeEditor->setExtraSelections({});
}

// ── Parse-error banner ────────────────────────────────────────────────────────

void CodeEditorPanel::showParseError(const QString &msg, int errorLine)
{
    m_parseErrorLabel->setText(
        QStringLiteral("<span style='color:#d04040;'>%1</span>")
            .arg(msg.toHtmlEscaped()));
    m_parseErrorLabel->show();

    if (errorLine > 0) {
        QTextCursor cursor(m_codeEditor->document());
        cursor.movePosition(QTextCursor::Start);
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, errorLine - 1);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        m_codeEditor->setTextCursor(cursor);
        m_codeEditor->ensureCursorVisible();
    }
}

void CodeEditorPanel::clearParseError()
{
    m_parseErrorLabel->hide();
}

// ── Preview file I/O ──────────────────────────────────────────────────────────

QString CodeEditorPanel::previewScadPath() const
{
    return QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("openscad_preview.scad"));
}

bool CodeEditorPanel::writeOpenScadPreview(bool notify)
{
    const QString path = previewScadPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (notify)
            QMessageBox::warning(this, QStringLiteral("OpenSCAD preview error"),
                                 QString("Cannot write preview file:\n%1").arg(path));
        return false;
    }
    const QByteArray data = m_codeEditor->toPlainText().toUtf8();
    if (file.write(data) != data.size() || !file.commit()) {
        if (notify)
            QMessageBox::warning(this, QStringLiteral("OpenSCAD preview error"),
                                 QString("Cannot finish writing preview file:\n%1").arg(path));
        return false;
    }
    m_openScadPreviewLabel->setText(
        QString("Preview file: %1").arg(QDir::toNativeSeparators(path)));
    return true;
}
