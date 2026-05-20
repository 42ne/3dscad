#include "scenetreeinlinetextinput.h"

#include <QFocusEvent>
#include <QKeyEvent>

SceneTreeInlineTextInput::SceneTreeInlineTextInput(QWidget *parent)
    : QLineEdit(parent)
{
    hide();
    setFrame(false);
    setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // Slightly styled so it sits visually above the node card.
    setStyleSheet(
        QStringLiteral("QLineEdit {"
                       "  background: rgba(255,255,235,230);"
                       "  color: #1a1a1a;"
                       "  border: 1.5px solid rgba(145,108,215,180);"
                       "  border-radius: 4px;"
                       "  padding: 1px 4px;"
                       "}"));
}

void SceneTreeInlineTextInput::startEditing(const QRect &viewRect,
                                            const QString &initialText,
                                            std::function<void(const QString &)> onCommit,
                                            std::function<void()> onCancel)
{
    m_onCommit    = onCommit;
    m_onCancel    = onCancel;
    m_editing     = true;
    m_committing  = false;

    // Inflate slightly so the border doesn't clip the text.
    setGeometry(viewRect.adjusted(-3, -2, 3, 2));
    setText(initialText);
    selectAll();
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
}

void SceneTreeInlineTextInput::stopEditing(bool commit)
{
    if (!m_editing)
        return;

    m_editing = false;
    hide();

    if (commit) {
        if (m_onCommit)
            m_onCommit(text().trimmed());
    } else {
        if (m_onCancel)
            m_onCancel();
    }

    m_onCommit = nullptr;
    m_onCancel = nullptr;
}

void SceneTreeInlineTextInput::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        m_committing = true;
        stopEditing(true);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        stopEditing(false);
        event->accept();
        return;
    }
    QLineEdit::keyPressEvent(event);
}

void SceneTreeInlineTextInput::focusOutEvent(QFocusEvent *event)
{
    QLineEdit::focusOutEvent(event);
    // Commit on focus-out unless we are already committing via Enter.
    if (m_editing && !m_committing)
        stopEditing(true);
}
