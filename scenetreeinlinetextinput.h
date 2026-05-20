#ifndef SCENETREEINLINETEXTINPUT_H
#define SCENETREEINLINETEXTINPUT_H

#include <QLineEdit>
#include <functional>

class QKeyEvent;
class QFocusEvent;

/**
 * @brief Floating QLineEdit overlay used for inline rename editing in the
 * SceneTreeGraphicsWidget.  The widget parents itself onto the graphics view,
 * positions over a given viewport rect, and fires onCommit / onCancel when
 * the user confirms or discards the edit.
 */
class SceneTreeInlineTextInput : public QLineEdit
{
    Q_OBJECT

public:
    explicit SceneTreeInlineTextInput(QWidget *parent = nullptr);

    /**
     * Show the editor positioned over @p viewRect (in viewport / widget
     * coordinates), pre-filled with @p initialText.
     */
    void startEditing(const QRect &viewRect,
                      const QString &initialText,
                      std::function<void(const QString &)> onCommit,
                      std::function<void()> onCancel);

    /** Programmatically end editing.  @p commit == true  → fire onCommit. */
    void stopEditing(bool commit);

    bool isEditing() const { return m_editing; }

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    std::function<void(const QString &)> m_onCommit;
    std::function<void()>               m_onCancel;
    bool m_editing    = false;
    bool m_committing = false; // guard against double-fire on Enter + focusOut
};

#endif // SCENETREEINLINETEXTINPUT_H
