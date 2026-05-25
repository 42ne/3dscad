#include "codeeditorpanel.h"

#include <QCheckBox>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QMessageBox>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPushButton>
#include <QRect>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QVBoxLayout>

class CodeTextEdit;

class LineNumberArea final : public QWidget
{
public:
    explicit LineNumberArea(CodeTextEdit *editor);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    CodeTextEdit *m_editor = nullptr;
};

class CodeTextEdit final : public QTextEdit
{
public:
    explicit CodeTextEdit(QWidget *parent = nullptr)
        : QTextEdit(parent), m_lineNumberArea(new LineNumberArea(this))
    {
        setLineWrapMode(QTextEdit::NoWrap);
        connect(document(), &QTextDocument::blockCountChanged, this, [this]() {
            updateLineNumberAreaWidth();
            m_lineNumberArea->update();
        });
        connect(verticalScrollBar(), &QScrollBar::valueChanged, m_lineNumberArea, qOverload<>(&QWidget::update));
        connect(this, &QTextEdit::textChanged, m_lineNumberArea, qOverload<>(&QWidget::update));
        updateLineNumberAreaWidth();
    }

    int lineNumberAreaWidth() const
    {
        const int digits = qMax(2, QString::number(qMax(1, document()->blockCount())).size());
        return 10 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    }

    void lineNumberAreaPaintEvent(QPaintEvent *event)
    {
        QPainter painter(m_lineNumberArea);
        painter.fillRect(event->rect(), palette().color(QPalette::Base).darker(108));
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawLine(m_lineNumberArea->width() - 1, event->rect().top(),
                         m_lineNumberArea->width() - 1, event->rect().bottom());
        painter.setPen(palette().color(QPalette::Text).darker(125));

        for (QTextBlock block = document()->firstBlock(); block.isValid(); block = block.next()) {
            const int top = cursorRect(QTextCursor(block)).top();
            const int bottom = top + fontMetrics().height();
            if (bottom < event->rect().top())
                continue;
            if (top > event->rect().bottom())
                break;
            painter.drawText(0, top, m_lineNumberArea->width() - 6, fontMetrics().height(),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(block.blockNumber() + 1));
        }
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QTextEdit::resizeEvent(event);
        const QRect contents = contentsRect();
        m_lineNumberArea->setGeometry(contents.left(), contents.top(),
                                      lineNumberAreaWidth(), contents.height());
    }

private:
    void updateLineNumberAreaWidth()
    {
        setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
        m_lineNumberArea->setFixedWidth(lineNumberAreaWidth());
    }

    LineNumberArea *m_lineNumberArea = nullptr;
};

LineNumberArea::LineNumberArea(CodeTextEdit *editor)
    : QWidget(editor), m_editor(editor)
{
}

QSize LineNumberArea::sizeHint() const
{
    return QSize(m_editor ? m_editor->lineNumberAreaWidth() : 0, 0);
}

void LineNumberArea::paintEvent(QPaintEvent *event)
{
    if (m_editor)
        m_editor->lineNumberAreaPaintEvent(event);
}

// ── CodeEditorPanel ───────────────────────────────────────────────────────────

CodeEditorPanel::CodeEditorPanel(QWidget *parent)
    : QWidget(parent)
{
    setMinimumWidth(280);

    m_codeEditor = new CodeTextEdit;
    m_codeEditor->setReadOnly(false);
    m_codeEditor->setFontFamily("Consolas");

    m_applyCodeButton      = new QPushButton(QStringLiteral("Apply code"));
    m_sendToOpenScadButton = new QPushButton(QStringLiteral("Send to OpenSCAD"));

    m_syncCheckBox = new QCheckBox(QStringLiteral("Sync to file"));
    m_syncCheckBox->setChecked(false);
    m_syncCheckBox->setToolTip(
        QStringLiteral("When checked, the OpenSCAD preview file is rewritten\n"
                       "automatically on every scene change."));

    m_openScadPreviewLabel = new QLabel;
    m_openScadPreviewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_openScadPreviewLabel->setWordWrap(true);
    m_openScadPreviewLabel->setText(
        QString("Preview file: %1").arg(QDir::toNativeSeparators(previewScadPath())));
    m_openScadPreviewLabel->hide();   // hidden until sync is on or a manual send succeeds

    m_parseErrorLabel = new QLabel;
    m_parseErrorLabel->setWordWrap(true);
    m_parseErrorLabel->setContentsMargins(4, 2, 4, 2);
    m_parseErrorLabel->hide();

    // ── Control bar: [Apply code]  ·  [☐ Sync to file]  [Send to OpenSCAD] ──
    auto *controlBar = new QHBoxLayout;
    controlBar->setContentsMargins(4, 2, 4, 2);
    controlBar->setSpacing(6);
    controlBar->addWidget(m_applyCodeButton);
    controlBar->addStretch(1);
    controlBar->addWidget(m_syncCheckBox);
    controlBar->addWidget(m_sendToOpenScadButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(m_codeEditor, 1);
    layout->addLayout(controlBar);
    layout->addWidget(m_parseErrorLabel);
    layout->addWidget(m_openScadPreviewLabel);

    connect(m_applyCodeButton,      &QPushButton::clicked,
            this, &CodeEditorPanel::applyRequested);
    connect(m_sendToOpenScadButton, &QPushButton::clicked,
            this, &CodeEditorPanel::sendToOpenScadRequested);
    connect(m_syncCheckBox, &QCheckBox::toggled,
            this, &CodeEditorPanel::onSyncToggled);
}

void CodeEditorPanel::onSyncToggled(bool checked)
{
    m_openScadPreviewLabel->setVisible(checked);
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
    // Automatic (non-user-initiated) writes are suppressed when sync is disabled.
    if (!notify && !m_syncCheckBox->isChecked())
        return true;

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
    // After a successful manual send, briefly show the path even if sync is off.
    if (notify)
        m_openScadPreviewLabel->show();
    return true;
}
