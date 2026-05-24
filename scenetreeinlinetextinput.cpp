#include "scenetreeinlinetextinput.h"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QtGlobal>

SceneTreeInlineTextInput::SceneTreeInlineTextInput(QWidget *parent)
    : QWidget(parent)
    , m_baseFont(font())
{
    hide();
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void SceneTreeInlineTextInput::startEditing(const QRect &viewRect,
                                            const QString &initialText,
                                            std::function<void(const QString &)> onCommit,
                                            std::function<void()> onCancel)
{
    m_onCommit = onCommit;
    m_onCancel = onCancel;
    m_text = initialText;
    m_cursor = m_text.size();
    m_editing = true;
    m_committing = false;
    selectAllText();

    reposition(viewRect);
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
    update();
}

void SceneTreeInlineTextInput::reposition(const QRect &viewRect)
{
    QFont scaledFont = m_baseFont;
    const int pixelSize = qBound(8, qRound(viewRect.height() * 0.64), 48);
    scaledFont.setPixelSize(pixelSize);
    setFont(scaledFont);

    setGeometry(viewRect.adjusted(-3, -2, 3, 2));
    updateTextViewport();
    update();
}

void SceneTreeInlineTextInput::stopEditing(bool commit)
{
    if (!m_editing)
        return;

    m_editing = false;
    hide();

    if (commit) {
        if (m_onCommit)
            m_onCommit(m_text.trimmed());
    } else {
        if (m_onCancel)
            m_onCancel();
    }

    m_onCommit = nullptr;
    m_onCancel = nullptr;
    m_text.clear();
    m_cursor = 0;
    m_selectionAnchor = -1;
    m_textScale = 1.0;
    m_textScroll = 0.0;
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
    if (event->matches(QKeySequence::SelectAll)) {
        selectAllText();
        update();
        event->accept();
        return;
    }

    const bool shift = event->modifiers() & Qt::ShiftModifier;
    auto moveCursor = [&](int pos) {
        pos = qBound(0, pos, m_text.size());
        if (shift) {
            if (m_selectionAnchor < 0)
                m_selectionAnchor = m_cursor;
        } else {
            m_selectionAnchor = -1;
        }
        m_cursor = pos;
        updateTextViewport();
        update();
    };

    switch (event->key()) {
    case Qt::Key_Left:
        moveCursor(m_cursor - 1);
        event->accept();
        return;
    case Qt::Key_Right:
        moveCursor(m_cursor + 1);
        event->accept();
        return;
    case Qt::Key_Home:
        moveCursor(0);
        event->accept();
        return;
    case Qt::Key_End:
        moveCursor(m_text.size());
        event->accept();
        return;
    case Qt::Key_Backspace:
        if (hasSelection()) {
            replaceSelection(QString());
        } else if (m_cursor > 0) {
            m_text.remove(m_cursor - 1, 1);
            --m_cursor;
            updateTextViewport();
        }
        update();
        event->accept();
        return;
    case Qt::Key_Delete:
        if (hasSelection()) {
            replaceSelection(QString());
        } else if (m_cursor < m_text.size()) {
            m_text.remove(m_cursor, 1);
            updateTextViewport();
        }
        update();
        event->accept();
        return;
    default:
        break;
    }

    if (!event->text().isEmpty() && event->text().at(0).category() != QChar::Other_Control) {
        replaceSelection(event->text());
        update();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void SceneTreeInlineTextInput::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    if (m_editing && !m_committing)
        stopEditing(true);
}

void SceneTreeInlineTextInput::mousePressEvent(QMouseEvent *event)
{
    setCursorFromX(event->pos().x());
    clearSelection();
    update();
    event->accept();
}

void SceneTreeInlineTextInput::paintEvent(QPaintEvent *)
{
    updateTextViewport();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setFont(font());

    const QRectF panel = rect().adjusted(1, 1, -1, -1);
    const qreal radius = qMax<qreal>(3.0, height() * 0.16);
    QLinearGradient panelGradient(panel.topLeft(), panel.bottomLeft());
    panelGradient.setColorAt(0.0, QColor(232, 242, 255, 238));
    panelGradient.setColorAt(1.0, QColor(190, 214, 238, 224));
    painter.setPen(QPen(QColor(67, 117, 158, 220), qMax<qreal>(1.0, height() * 0.045)));
    painter.setBrush(panelGradient);
    painter.drawRoundedRect(panel, radius, radius);

    painter.setPen(QPen(QColor(255, 255, 255, 155), 1.0));
    painter.drawRoundedRect(panel.adjusted(1.0, 1.0, -1.0, -1.0), radius - 1.0, radius - 1.0);

    const QRect tr = textRect();
    const QFontMetrics metrics(font());
    const int baseline = tr.top() + (tr.height() - metrics.height()) / 2 + metrics.ascent();

    if (hasSelection()) {
        const int left = qMin(m_cursor, m_selectionAnchor);
        const int right = qMax(m_cursor, m_selectionAnchor);
        const int sx = qRound(tr.left() + metrics.horizontalAdvance(m_text.left(left)) * m_textScale - m_textScroll);
        const int sw = qRound(metrics.horizontalAdvance(m_text.mid(left, right - left)) * m_textScale);
        painter.fillRect(QRect(sx, tr.top() + 1, qMax(1, sw), tr.height() - 2), QColor(95, 143, 185, 150));
    }

    painter.save();
    painter.setClipRect(tr);
    painter.translate(tr.left() - m_textScroll, baseline);
    painter.scale(m_textScale, 1.0);
    painter.setPen(QColor(20, 50, 78));
    painter.drawText(QPointF(0.0, 0.0), m_text);
    painter.restore();

    if (hasFocus()) {
        const int cursorX = qRound(tr.left() + metrics.horizontalAdvance(m_text.left(m_cursor)) * m_textScale - m_textScroll);
        painter.setPen(QPen(QColor(44, 104, 152), qMax<qreal>(1.0, height() * 0.045)));
        painter.drawLine(QPointF(cursorX, tr.top() + 3), QPointF(cursorX, tr.bottom() - 3));
    }
}

bool SceneTreeInlineTextInput::hasSelection() const
{
    return m_selectionAnchor >= 0 && m_selectionAnchor != m_cursor;
}

void SceneTreeInlineTextInput::clearSelection()
{
    m_selectionAnchor = -1;
}

void SceneTreeInlineTextInput::replaceSelection(const QString &text)
{
    if (hasSelection()) {
        const int left = qMin(m_cursor, m_selectionAnchor);
        const int right = qMax(m_cursor, m_selectionAnchor);
        m_text.replace(left, right - left, text);
        m_cursor = left + text.size();
    } else {
        m_text.insert(m_cursor, text);
        m_cursor += text.size();
    }
    clearSelection();
    updateTextViewport();
}

void SceneTreeInlineTextInput::selectAllText()
{
    m_selectionAnchor = 0;
    m_cursor = m_text.size();
    updateTextViewport();
}

void SceneTreeInlineTextInput::setCursorFromX(int x)
{
    const QFontMetrics metrics(font());
    const qreal localX = (x - textRect().left() + m_textScroll) / qMax<qreal>(0.01, m_textScale);
    int best = 0;
    qreal bestDistance = qAbs(localX);
    for (int i = 1; i <= m_text.size(); ++i) {
        const qreal distance = qAbs(localX - metrics.horizontalAdvance(m_text.left(i)));
        if (distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    m_cursor = best;
    updateTextViewport();
}

void SceneTreeInlineTextInput::updateTextViewport()
{
    const QRect tr = textRect();
    const qreal availableWidth = qMax<qreal>(1.0, tr.width());
    const QFontMetrics metrics(font());
    const qreal textWidth = qMax<qreal>(1.0, metrics.horizontalAdvance(m_text));
    constexpr qreal MinTextScale = 0.68;

    if (textWidth <= availableWidth) {
        m_textScale = 1.0;
        m_textScroll = 0.0;
        return;
    }

    const qreal fitScale = availableWidth / textWidth;
    if (fitScale >= MinTextScale) {
        m_textScale = fitScale;
        m_textScroll = 0.0;
        return;
    }

    m_textScale = MinTextScale;
    const qreal scaledTextWidth = textWidth * m_textScale;
    const qreal cursorX = metrics.horizontalAdvance(m_text.left(m_cursor)) * m_textScale;
    const qreal margin = qMax<qreal>(3.0, height() * 0.14);

    if (cursorX - m_textScroll > availableWidth - margin)
        m_textScroll = cursorX - (availableWidth - margin);
    if (cursorX - m_textScroll < margin)
        m_textScroll = cursorX - margin;

    m_textScroll = qBound<qreal>(0.0, m_textScroll, qMax<qreal>(0.0, scaledTextWidth - availableWidth));
}

QRect SceneTreeInlineTextInput::textRect() const
{
    const int pad = qMax(3, qRound(height() * 0.18));
    return rect().adjusted(pad, 1, -pad, -1);
}
