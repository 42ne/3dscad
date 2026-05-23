#include "examplebrowsermenu.h"

#include "examplepreviewpopup.h"
#include "openscadparser.h"
#include "scenedocument.h"
#include "viewportwidget.h"

#include <QtConcurrent>

#include <QAction>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QSize>
#include <QTimer>

// ── ExampleBrowserMenu ────────────────────────────────────────────────────────

ExampleBrowserMenu::ExampleBrowserMenu(QMenu *menu, QObject *parent)
    : QObject(parent)
{
    m_preview    = new ExamplePreviewPopup;   // top-level popup, no Qt parent
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    m_hoverTimer->setInterval(900);
    m_watcher    = new QFutureWatcher<QImage>(this);

    connect(m_hoverTimer, &QTimer::timeout,                         this, &ExampleBrowserMenu::onHoverTimeout);
    connect(m_watcher,    &QFutureWatcher<QImage>::finished,        this, &ExampleBrowserMenu::onThumbnailReady);
    connect(menu,         &QMenu::aboutToHide,                      this, &ExampleBrowserMenu::hidePreview);

    // ── populate menu ─────────────────────────────────────────────────────────
    const QString path = examplesPath();
    if (path.isEmpty()) {
        menu->addAction("(no examples found)")->setEnabled(false);
        return;
    }
    const QStringList files = QDir(path).entryList({"*.scad"}, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        menu->addAction("(no .scad files)")->setEnabled(false);
        return;
    }
    for (const QString &fileName : files) {
        const QString filePath = QDir(path).absoluteFilePath(fileName);
        const QString name     = QFileInfo(fileName).completeBaseName();
        QAction *action = menu->addAction(name);
        connect(action, &QAction::triggered, this, [this, filePath]() {
            emit exampleSelected(filePath);
        });
        connect(action, &QAction::hovered, this, [this, filePath, name, menu]() {
            m_pendingFile      = filePath;
            m_pendingName      = name;
            m_pendingMenuRight = menu->mapToGlobal(menu->rect().topRight()).x();
            m_hoverTimer->start();
        });
    }
}

// ── private slots ─────────────────────────────────────────────────────────────

void ExampleBrowserMenu::onHoverTimeout()
{
    m_preview->setLoading(m_pendingName);
    m_preview->showAt(m_pendingMenuRight, QCursor::pos().y());
    if (m_watcher->isRunning()) return;

    const QString filePath = m_pendingFile;
    QFuture<QImage> future = QtConcurrent::run([filePath]() -> QImage {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QImage();
        const QString code = QString::fromUtf8(file.readAll());
        SceneDocument::Snapshot snapshot;
        if (!OpenScadParser::parseScene(code, &snapshot, nullptr, nullptr)) return QImage();
        SceneDocument scene;
        scene.restoreSnapshot(snapshot);
        return ViewportWidget::renderThumbnail(scene, QSize(280, 210));
    });
    m_watcher->setFuture(future);
}

void ExampleBrowserMenu::onThumbnailReady()
{
    if (m_watcher->isCanceled()) return;
    const QImage image = m_watcher->result();
    if (m_preview->isVisible())
        m_preview->setImage(image, m_pendingName);
}

// ── private helpers ───────────────────────────────────────────────────────────

void ExampleBrowserMenu::hidePreview()
{
    m_hoverTimer->stop();
    if (m_watcher->isRunning()) m_watcher->cancel();
    m_preview->hidePopup();
}

// static
QString ExampleBrowserMenu::examplesPath()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        QDir candidate(dir.absoluteFilePath("docs/sample_codes"));
        if (candidate.exists()) return candidate.absolutePath();
        if (!dir.cdUp()) break;
    }
    return QString();
}
