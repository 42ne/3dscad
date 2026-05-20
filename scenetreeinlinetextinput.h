#ifndef SCENETREEINLINETEXTINPUT_H
#define SCENETREEINLINETEXTINPUT_H

#include <QFont>
#include <QWidget>
#include <functional>

class QKeyEvent;
class QFocusEvent;
class QMouseEvent;
class QPaintEvent;

/**
 * @brief Lightweight floating text editor used for inline rename editing.
 */
class SceneTreeInlineTextInput : public QWidget
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

    void reposition(const QRect &viewRect);

    /** Programmatically end editing.  @p commit == true  → fire onCommit. */
    void stopEditing(bool commit);

    bool isEditing() const { return m_editing; }

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    bool hasSelection() const;
    void clearSelection();
    void replaceSelection(const QString &text);
    void selectAllText();
    void setCursorFromX(int x);
    void updateTextViewport();
    QRect textRect() const;

    std::function<void(const QString &)> m_onCommit;
    std::function<void()>               m_onCancel;
    QFont m_baseFont;
    QString m_text;
    int m_cursor = 0;
    int m_selectionAnchor = -1;
    qreal m_textScale = 1.0;
    qreal m_textScroll = 0.0;
    bool m_editing    = false;
    bool m_committing = false; // guard against double-fire on Enter + focusOut
};

#endif // SCENETREEINLINETEXTINPUT_H
