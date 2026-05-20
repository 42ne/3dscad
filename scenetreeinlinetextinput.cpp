#include "scenetreeinlinetextinput.h"

#include <QFocusEvent>
#include <QKeyEvent>

SceneTreeInlineTextInput::SceneTreeInlineTextInput(QWidget *parent)
    : QLineEdit(parent)
{
    hide();
    setFrame(false);
    setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // Dark theme: black bg, amber/yellow text, thin amber border.
    // Zero vertical padding so the widget hugs the font height.
    setStyleSheet(
        QStringLiteral("QLineEdit {"
                       "  background: #1a1a1a;"
                       "  color: #ffc832;"
                       "  border: 1px solid rgba(255, 185, 40, 180);"
                       "  border-radius: 3px;"
                       "  padding: 0px 4px;"
                       "}"));

    // Fix the height to the font's cap-height + border so it is compact
    // and independent of the platform's default QLineEdit size hints.
    const int compactH = fontMetrics().height() + 4; // text + 1px top/bottom border+gap
    setFixedHeight(compactH);
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

    // Keep the fixed compact height; expand width by a few px so the
    // border doesn't clip ascenders/descenders; centre vertically on the zone.
    const int cy = viewRect.center().y();
    const int h  = height(); // already fixed in constructor
    setGeometry(QRect(viewRect.left() - 2,
                      cy - h / 2,
                      viewRect.width() + 4,
                      h));
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
