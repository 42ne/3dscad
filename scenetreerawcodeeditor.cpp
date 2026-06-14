#include "scenetreerawcodeeditor.h"
#include "scenetreegraphicswidget.h"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QtGlobal>

namespace {

class RawCodePlainTextEdit final : public QPlainTextEdit
{
public:
    explicit RawCodePlainTextEdit(SceneTreeRawCodeEditor *owner, QWidget *parent = nullptr)
        : QPlainTextEdit(parent)
        , m_owner(owner)
    {
        setLineWrapMode(QPlainTextEdit::NoWrap);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setTabChangesFocus(false);
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape) {
            m_owner->stopEditing(false);
            event->accept();
            return;
        }

        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            && (event->modifiers() & Qt::ControlModifier)) {
            m_owner->stopEditing(true);
            event->accept();
            return;
        }

        QPlainTextEdit::keyPressEvent(event);
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        QPlainTextEdit::focusOutEvent(event);
        m_owner->stopEditing(true);
    }

private:
    SceneTreeRawCodeEditor *m_owner = nullptr;
};

} // namespace

SceneTreeRawCodeEditor::SceneTreeRawCodeEditor(SceneTreeGraphicsWidget *widget)
    : QObject(widget)
    , m_widget(widget)
{
    m_editor = new RawCodePlainTextEdit(this, widget);
    m_editor->hide();
    m_editor->setStyleSheet(QStringLiteral(
        "QPlainTextEdit {"
        "  background: rgba(30, 34, 40, 235);"
        "  color: #f5f0df;"
        "  border: 1px solid rgba(255,255,255,150);"
        "  border-radius: 6px;"
        "  padding: 6px;"
        "  font-family: Consolas, 'Cascadia Mono', monospace;"
        "  font-size: 12px;"
        "}"));
}

void SceneTreeRawCodeEditor::startEditing(int nodeId, const QRectF &sceneRect, const QString &code)
{
    if (!m_editor || !m_widget)
        return;

    m_nodeId = nodeId;
    m_sceneRect = sceneRect;
    m_active = true;
    m_editor->setPlainText(code);
    updateGeometry();
    m_editor->show();
    m_editor->raise();
    m_editor->setFocus(Qt::MouseFocusReason);
}

void SceneTreeRawCodeEditor::updateGeometry()
{
    if (!m_active || !m_editor || !m_widget || !m_sceneRect.isValid())
        return;

    QRect viewRect = m_widget->mapFromScene(m_sceneRect).boundingRect();
    viewRect = viewRect.adjusted(4, 4, -4, -4);
    viewRect.setWidth(qMax(viewRect.width(), 260));
    viewRect.setHeight(qMax(viewRect.height(), 120));
    m_editor->setGeometry(viewRect);
}

void SceneTreeRawCodeEditor::stopEditing(bool commit)
{
    if (!m_active || !m_editor)
        return;

    const int nodeId = m_nodeId;
    const QString code = m_editor->toPlainText();
    m_active = false;
    m_nodeId = 0;
    m_sceneRect = QRectF();
    m_editor->hide();

    if (commit && m_widget)
        emit m_widget->rawCodeEdited(nodeId, code);
}
