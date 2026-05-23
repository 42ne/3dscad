#ifndef EXAMPLEBROWSERMENU_H
#define EXAMPLEBROWSERMENU_H

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QString>

class QMenu;
class QTimer;
class ExamplePreviewPopup;

// Populates an "Open Example" QMenu with .scad files found next to the binary,
// shows an async thumbnail preview on hover, and emits exampleSelected()
// when the user clicks an entry.
class ExampleBrowserMenu : public QObject
{
    Q_OBJECT

public:
    // Populates `menu` immediately; `parent` is used for lifetime management.
    explicit ExampleBrowserMenu(QMenu *menu, QObject *parent = nullptr);

signals:
    void exampleSelected(const QString &filePath);

private slots:
    void onHoverTimeout();
    void onThumbnailReady();

private:
    void hidePreview();
    static QString examplesPath();

    ExamplePreviewPopup    *m_preview          = nullptr;
    QTimer                 *m_hoverTimer       = nullptr;
    QFutureWatcher<QImage> *m_watcher          = nullptr;

    QString m_pendingFile;
    QString m_pendingName;
    int     m_pendingMenuRight = 0;
};

#endif // EXAMPLEBROWSERMENU_H
