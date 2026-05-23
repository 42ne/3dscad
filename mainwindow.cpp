#include "mainwindow.h"
#include "animatedtitlebar.h"
#include "csgevaluator.h"
#include "openscadgenerator.h"
#include "openscadparser.h"
#include "scenetreegraphicshelpers.h"
#include "scenetreegraphicswidget.h"
#include "theme.h"
#include "viewportwidget.h"

#include <QtConcurrent>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QCursor>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QScrollBar>
#include <QTextEdit>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QWindow>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

// ────────────────────────────────────────────────────────────────────────────
// MainWindow
// ────────────────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_controller = new SceneController(this);

    // Wire controller signals to UI refresh slots.
    connect(m_controller, &SceneController::sceneChanged,
            this, &MainWindow::refreshSceneViews);
    connect(m_controller, &SceneController::selectionChanged,
            this, &MainWindow::onSelectionChanged);
    connect(m_controller, &SceneController::ctrlHighlightChanged,
            this, &MainWindow::highlightOpenScadSelection);
    // liveViewportUpdate is connected after m_viewport is created in buildUi().

    applyTheme(defaultTheme());
    buildUi();
    refreshOpenScadCode();
    refreshCsgStatus();
    refreshProperties();
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
#ifdef Q_OS_WIN
    Q_UNUSED(eventType);
    MSG *msg = static_cast<MSG *>(message);
    if (!msg || msg->message != WM_NCHITTEST || isMaximized())
        return QMainWindow::nativeEvent(eventType, message, result);

    const QPoint pos(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
    const QPoint local = mapFromGlobal(pos);
    constexpr int B = 8;

    if      (local.x() < B && local.y() < B)                         *result = HTTOPLEFT;
    else if (local.x() > width()-B && local.y() < B)                 *result = HTTOPRIGHT;
    else if (local.x() < B && local.y() > height()-B)                *result = HTBOTTOMLEFT;
    else if (local.x() > width()-B && local.y() > height()-B)        *result = HTBOTTOMRIGHT;
    else if (local.y() < B)                                           *result = HTTOP;
    else if (local.y() > height()-B)                                  *result = HTBOTTOM;
    else if (local.x() < B)                                           *result = HTLEFT;
    else if (local.x() > width()-B)                                   *result = HTRIGHT;
    else return QMainWindow::nativeEvent(eventType, message, result);
    return true;
#else
    return QMainWindow::nativeEvent(eventType, message, result);
#endif
}

// ── UI construction ───────────────────────────────────────────────────────────

void MainWindow::buildUi()
{
    setWindowTitle("OpenSCAD Visual Editor Prototype");
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground, false);

    ThemeSpec activeTheme = defaultTheme();
    auto *titleBar = new AnimatedTitleBar(this);
    titleBar->setTitle(windowTitle());
    titleBar->setTheme(activeTheme);

    QMenuBar *appMenuBar = menuBar();
    auto *fileMenu     = appMenuBar->addMenu("File");
    auto *examplesMenu = fileMenu->addMenu("Open Example");
    populateExamplesMenu(examplesMenu);

    auto *editMenu = appMenuBar->addMenu("Edit");
    editMenu->addAction(m_controller->undoAction());
    editMenu->addAction(m_controller->redoAction());

    auto *settingsMenu = appMenuBar->addMenu("Settings");
    auto *themeMenu    = settingsMenu->addMenu("Theme");
    auto *themeGroup   = new QActionGroup(this);
    themeGroup->setExclusive(true);
    for (const ThemeSpec &theme : availableThemes()) {
        QAction *action = themeMenu->addAction(theme.label);
        action->setCheckable(true);
        action->setData(theme.id);
        themeGroup->addAction(action);
        if (theme.id == activeTheme.id) action->setChecked(true);
        connect(action, &QAction::triggered, this, [theme, titleBar]() {
            applyTheme(theme);
            titleBar->setTheme(theme);
        });
    }

    auto *chrome = new QWidget(this);
    auto *chromeLayout = new QVBoxLayout(chrome);
    chromeLayout->setContentsMargins(0, 0, 0, 0);
    chromeLayout->setSpacing(0);
    chromeLayout->addWidget(titleBar);
    chromeLayout->addWidget(appMenuBar);
    setMenuWidget(chrome);

    // Example hover preview
    m_examplePreview  = new ExamplePreviewPopup;
    m_exampleHoverTimer = new QTimer(this);
    m_exampleHoverTimer->setSingleShot(true);
    m_exampleHoverTimer->setInterval(900);
    connect(m_exampleHoverTimer, &QTimer::timeout, this, &MainWindow::onExampleHoverTimeout);
    m_thumbnailWatcher = new QFutureWatcher<QImage>(this);
    connect(m_thumbnailWatcher, &QFutureWatcher<QImage>::finished,
            this, &MainWindow::onExampleThumbnailReady);

    m_viewport = new ViewportWidget;
    m_viewport->setScene(&m_controller->scene());

    // Now that m_viewport exists, wire live-update signal.
    connect(m_controller, &SceneController::liveViewportUpdate, this, [this]() {
        m_viewport->invalidateCsgPreview();
        m_viewport->update();
    });

    m_codeEditor = new QTextEdit;
    m_codeEditor->setReadOnly(false);
    m_codeEditor->setMinimumHeight(180);
    m_codeEditor->setFontFamily("Consolas");

    m_applyCodeButton       = new QPushButton("Apply code");
    m_sendToOpenScadButton  = new QPushButton("Send to OpenSCAD");
    m_openScadPreviewLabel  = new QLabel;
    m_openScadPreviewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_openScadPreviewLabel->setWordWrap(true);
    m_openScadPreviewLabel->setText(
        QString("Preview file: %1").arg(QDir::toNativeSeparators(previewScadPath())));
    m_parseErrorLabel = new QLabel;
    m_parseErrorLabel->setWordWrap(true);
    m_parseErrorLabel->setContentsMargins(4, 2, 4, 2);
    m_parseErrorLabel->hide();

    auto *codePanel  = new QWidget;
    auto *codeLayout = new QVBoxLayout(codePanel);
    codeLayout->setContentsMargins(0, 0, 0, 0);
    codeLayout->addWidget(m_codeEditor);
    codeLayout->addWidget(m_applyCodeButton);
    codeLayout->addWidget(m_parseErrorLabel);
    codeLayout->addWidget(m_sendToOpenScadButton);
    codeLayout->addWidget(m_openScadPreviewLabel);

    auto *mainSplitter = new QSplitter(Qt::Vertical);
    mainSplitter->addWidget(m_viewport);
    mainSplitter->addWidget(codePanel);
    mainSplitter->setStretchFactor(0, 4);
    mainSplitter->setStretchFactor(1, 1);
    setCentralWidget(mainSplitter);

    // Left dock
    auto *leftDock   = new QDockWidget("Shapes", this);
    auto *leftPanel  = new QWidget;
    auto *leftLayout = new QVBoxLayout(leftPanel);

    m_sceneTreeGraphics = new SceneTreeGraphicsWidget;
    m_sceneTreeGraphics->setSceneDocument(&m_controller->scene());

    // Wire all graphics-tree signals through the controller.
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::toolDropped,
            m_controller, &SceneController::handleToolDrop);
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::moduleCallDropped,
            m_controller, &SceneController::handleModuleCallDrop);
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::treeNodeDropped,
            m_controller, &SceneController::moveTreeNodeToGroup);
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::treeNodeSelected,
            m_controller, &SceneController::handleNodeSelected);
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::treeNodeDeleteRequested,
            m_controller, &SceneController::handleNodeDeleteRequested);
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::transformValueAdjusted,
            m_controller, &SceneController::handleTransformValueAdjusted);
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::transformControlHovered,
            this, [this](int groupId, SceneDocument::TreeNode::Operation op, int axis) {
                if (m_viewport) m_viewport->setTreeTransformControlPreview(groupId, op, axis);
            });
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::shapeParameterAdjusted,
            m_controller, &SceneController::handleShapeParameterAdjusted);
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::shapeParameterHovered,
            this, [this](int shapeId, int parameter) {
                if (m_viewport) m_viewport->setTreeShapeParameterPreview(shapeId, parameter);
            });
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::variableNumberAdjusted,
            m_controller, &SceneController::handleVariableNumberAdjusted);
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::moduleCallArgumentAdjusted,
            m_controller, &SceneController::handleModuleCallArgumentAdjusted);
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::forLoopRangeAdjusted,
            m_controller, &SceneController::handleForLoopRangeAdjusted);
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::ctrlReleased,
            m_controller, &SceneController::handleCtrlReleased);
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::moduleRenameRequested,
            m_controller, &SceneController::handleModuleRenameRequested);
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::variableRenameRequested,
            m_controller, &SceneController::handleVariableRenameRequested);

    leftLayout->addWidget(m_sceneTreeGraphics, 1);
    m_csgStatusLabel = new QLabel;
    m_csgStatusLabel->setWordWrap(true);
    leftLayout->addWidget(m_csgStatusLabel);
    leftDock->setWidget(leftPanel);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);

    // Buttons
    connect(m_applyCodeButton,      &QPushButton::clicked, this, &MainWindow::applyOpenScadCode);
    connect(m_sendToOpenScadButton, &QPushButton::clicked, this, &MainWindow::sendToOpenScad);

    // Viewport signals → controller handlers
    connect(m_viewport, &ViewportWidget::shapeClicked, this, [this](int index) {
        const ShapeNode *shape = m_controller->scene().shapeAt(index);
        m_controller->selectShape(shape ? shape->id : -1);
    });
    connect(m_viewport, &ViewportWidget::emptyClicked,
            this, &MainWindow::clearSelection);
    connect(m_viewport, &ViewportWidget::shapeDragStarted, m_controller,
            &SceneController::handleShapeDragStarted);
    connect(m_viewport, &ViewportWidget::shapeDragged, m_controller,
            &SceneController::handleShapeDragged);
    connect(m_viewport, &ViewportWidget::shapeDragFinished, m_controller,
            &SceneController::handleShapeDragFinished);
    connect(m_viewport, &ViewportWidget::shapeRotationDragStarted, m_controller,
            &SceneController::handleShapeRotationDragStarted);
    connect(m_viewport, &ViewportWidget::shapeRotated, m_controller,
            &SceneController::handleShapeRotated);
    connect(m_viewport, &ViewportWidget::shapeRotationDragFinished, m_controller,
            &SceneController::handleShapeRotationDragFinished);
    connect(m_viewport, &ViewportWidget::groupDragStarted, m_controller,
            &SceneController::handleGroupDragStarted);
    connect(m_viewport, &ViewportWidget::groupDragged, m_controller,
            &SceneController::handleGroupDragged);
    connect(m_viewport, &ViewportWidget::groupDragFinished, m_controller,
            &SceneController::handleGroupDragFinished);
    connect(m_viewport, &ViewportWidget::groupRotationDragStarted, m_controller,
            &SceneController::handleGroupRotationDragStarted);
    connect(m_viewport, &ViewportWidget::groupRotated, m_controller,
            &SceneController::handleGroupRotated);
    connect(m_viewport, &ViewportWidget::groupRotationDragFinished, m_controller,
            &SceneController::handleGroupRotationDragFinished);
    connect(m_viewport, &ViewportWidget::csgPreviewReady,
            this, &MainWindow::refreshCsgStatus);
}

// ── Toolbar shape/group actions ────────────────────────────────────────────────

void MainWindow::addCube()          { m_controller->addCube();     }
void MainWindow::addSphere()        { m_controller->addSphere();   }
void MainWindow::addCylinder()      { m_controller->addCylinder(); }
void MainWindow::addUnionGroup()        { m_controller->addGroup(SceneDocument::TreeNode::Union);        }
void MainWindow::addDifferenceGroup()   { m_controller->addGroup(SceneDocument::TreeNode::Difference);   }
void MainWindow::addIntersectionGroup() { m_controller->addGroup(SceneDocument::TreeNode::Intersection); }

// ── Code apply / OpenSCAD preview ─────────────────────────────────────────────

void MainWindow::applyOpenScadCode()
{
    QString errorMessage;
    int     errorLine = -1;

    if (!m_controller->applyCode(m_codeEditor->toPlainText(), &errorMessage, &errorLine)) {
        m_parseErrorLabel->setText(
            QString("<span style='color:#d04040;'>%1</span>")
                .arg(errorMessage.toHtmlEscaped()));
        m_parseErrorLabel->show();

        if (errorLine > 0) {
            QTextCursor cursor(m_codeEditor->document());
            cursor.movePosition(QTextCursor::Start);
            cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, errorLine - 1);
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            m_codeEditor->setTextCursor(cursor);
            m_codeEditor->ensureCursorVisible();
        }
        return;
    }
    m_parseErrorLabel->hide();
}

void MainWindow::sendToOpenScad()
{
    if (!writeOpenScadPreview(true))
        return;
    const QString nativePath = QDir::toNativeSeparators(previewScadPath());
    QApplication::clipboard()->setText(nativePath);
    QMessageBox::information(
        this, "OpenSCAD preview file",
        QString("Saved the current model to:\n\n%1\n\n"
                "The path was copied to the clipboard. Open this file in OpenSCAD and enable automatic reload/preview there.")
            .arg(nativePath));
}

// ── Selection ─────────────────────────────────────────────────────────────────

void MainWindow::clearSelection()
{
    m_controller->clearSelection(); // emits selectionChanged(0) → onSelectionChanged(0)
}

// Called when SceneController::selectionChanged(nodeId) fires.
void MainWindow::onSelectionChanged(int nodeId)
{
    if (m_sceneTreeGraphics)
        m_sceneTreeGraphics->setSelectedTreeNodeId(nodeId);

    if (!m_viewport) return;

    if (nodeId == 0) {
        m_viewport->setSelectedIndex(-1);
        m_viewport->setSelectedGroupId(0);
        m_viewport->setTreeTransformControlPreview(0, SceneDocument::TreeNode::Union, -1);
        m_viewport->setTreeShapeParameterPreview(-1, -1);
        m_viewport->update();
        highlightOpenScadSelection();
        refreshProperties();
        return;
    }

    const SceneDocument::TreeNode *node = m_controller->scene().treeNodeById(nodeId);
    if (!node) {
        m_viewport->setSelectedIndex(-1);
        m_viewport->setSelectedGroupId(0);
        m_viewport->update();
        highlightOpenScadSelection();
        refreshProperties();
        return;
    }

    if (node->type == SceneDocument::TreeNode::Primitive) {
        m_viewport->setSelectedIndex(m_controller->scene().selectedIndex());
        m_viewport->setSelectedGroupId(0);
    } else if (node->type == SceneDocument::TreeNode::ModuleCall) {
        m_viewport->setSelectedIndex(-1);
        m_viewport->setSelectedGroupId(nodeId);
    } else if (node->type == SceneDocument::TreeNode::Variable) {
        m_viewport->setSelectedIndex(-1);
        m_viewport->setSelectedGroupId(0);
    } else { // Group
        m_viewport->setSelectedIndex(-1);
        m_viewport->setSelectedGroupId(nodeId);
    }

    m_viewport->update();
    highlightOpenScadSelection();
    refreshProperties();
}

// ── Refresh ────────────────────────────────────────────────────────────────────

void MainWindow::refreshShapeList()
{
    if (m_sceneTreeGraphics)
        m_sceneTreeGraphics->refresh();

    refreshOpenScadCode();
    m_viewport->invalidateCsgPreview();
    m_viewport->update();
    refreshCsgStatus();
}

void MainWindow::refreshSceneViews()
{
    refreshShapeList();
    m_viewport->setSelectedIndex(m_controller->scene().selectedIndex());
    m_viewport->setSelectedGroupId(m_controller->selectedDirectGroupId());
    refreshProperties();
}

void MainWindow::refreshProperties()
{
    // Reserved for future property panel.
}

void MainWindow::refreshOpenScadCode()
{
    const int savedScroll = m_codeEditor->verticalScrollBar()->value();
    const QString code = OpenScadGenerator::generateWithSourceMap(
        m_controller->scene(), &m_openScadSourceRanges);
    m_codeEditor->setPlainText(code);
    m_codeEditor->verticalScrollBar()->setValue(savedScroll);
    highlightOpenScadSelection();
    writeOpenScadPreview(false);
}

void MainWindow::refreshCsgStatus()
{
    if (!m_csgStatusLabel) return;
    if (m_viewport)
        m_csgStatusLabel->setText(m_viewport->csgStatusText());
    else
        m_csgStatusLabel->setText(buildCsgPreview(m_controller->scene()).statusText);
}

// ── Code-editor highlight ──────────────────────────────────────────────────────

void MainWindow::scrollCodeEditorToShowCursor(const QTextCursor &cursor)
{
    if (!m_codeEditor || cursor.isNull()) return;
    const QRect r   = m_codeEditor->cursorRect(cursor);
    const int   vpH = m_codeEditor->viewport()->height();
    if (r.top() < 0 || r.bottom() > vpH) {
        QScrollBar *sb = m_codeEditor->verticalScrollBar();
        sb->setValue(sb->value() + r.top() - vpH / 3);
    }
}

void MainWindow::applyCtrlParamHighlight()
{
    const SceneController::CtrlParamHighlight &h = m_controller->ctrlHighlight();
    const QString code = m_codeEditor->toPlainText();

    for (const OpenScadGenerator::SourceRange &range : m_openScadSourceRanges) {
        if (range.treeNodeId != h.nodeId || range.length <= 0) continue;
        const int    searchCap = qMin(range.length, 300);
        const QString needle   = h.contextPrefix + h.expression;
        const int    hitPos    = code.indexOf(needle, range.start);
        if (hitPos < 0 || hitPos >= range.start + searchCap) break;

        const int exprPos  = hitPos + h.contextPrefix.size();
        const int numStart = exprPos + h.numberStart;
        const int numLen   = h.numberLength;
        if (numStart + numLen > code.size()) break;

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
        scrollCodeEditorToShowCursor(cursor);
        return;
    }
}

void MainWindow::highlightOpenScadSelection()
{
    if (!m_codeEditor) return;

    if (m_controller->ctrlHighlight().active) {
        applyCtrlParamHighlight();
        return;
    }

    const int selectedNodeId = m_controller->selectedTreeNodeId();
    QTextEdit::ExtraSelection selection;
    bool hasSelection = false;

    for (const OpenScadGenerator::SourceRange &range : m_openScadSourceRanges) {
        if (range.treeNodeId != selectedNodeId || range.length <= 0) continue;
        QTextCursor cursor(m_codeEditor->document());
        cursor.setPosition(range.start);
        cursor.setPosition(range.start + range.length, QTextCursor::KeepAnchor);
        QTextCharFormat format;
        format.setBackground(QColor(255, 203, 87, 95));
        selection.cursor = cursor;
        selection.format = format;
        hasSelection = true;
        break;
    }

    m_codeEditor->setExtraSelections(
        hasSelection ? QList<QTextEdit::ExtraSelection>{selection}
                     : QList<QTextEdit::ExtraSelection>{});
    if (hasSelection)
        scrollCodeEditorToShowCursor(selection.cursor);
}

// ── Utilities / file I/O ──────────────────────────────────────────────────────

QString MainWindow::previewScadPath() const
{
    return QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath("openscad_preview.scad");
}

QString MainWindow::examplesPath() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        QDir candidate(dir.absoluteFilePath("docs/sample_codes"));
        if (candidate.exists()) return candidate.absolutePath();
        if (!dir.cdUp()) break;
    }
    return QString();
}

bool MainWindow::writeOpenScadPreview(bool notify)
{
    const QString path = previewScadPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (notify)
            QMessageBox::warning(this, "OpenSCAD preview error",
                                 QString("Cannot write preview file:\n%1").arg(path));
        return false;
    }
    const QByteArray data = m_codeEditor->toPlainText().toUtf8();
    if (file.write(data) != data.size() || !file.commit()) {
        if (notify)
            QMessageBox::warning(this, "OpenSCAD preview error",
                                 QString("Cannot finish writing preview file:\n%1").arg(path));
        return false;
    }
    if (m_openScadPreviewLabel)
        m_openScadPreviewLabel->setText(
            QString("Preview file: %1").arg(QDir::toNativeSeparators(path)));
    return true;
}

// ── Examples ──────────────────────────────────────────────────────────────────

void MainWindow::populateExamplesMenu(QMenu *menu)
{
    const QString path = examplesPath();
    if (path.isEmpty()) {
        menu->addAction("(no examples found)")->setEnabled(false); return;
    }
    const QStringList files = QDir(path).entryList({"*.scad"}, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        menu->addAction("(no .scad files)")->setEnabled(false); return;
    }
    for (const QString &fileName : files) {
        const QString filePath = QDir(path).absoluteFilePath(fileName);
        const QString name     = QFileInfo(fileName).completeBaseName();
        QAction *action = menu->addAction(name);
        connect(action, &QAction::triggered, this, [this, filePath]() {
            loadExample(filePath);
        });
        connect(action, &QAction::hovered, this, [this, filePath, name, menu]() {
            m_pendingPreviewFile = filePath;
            m_pendingPreviewName = name;
            m_pendingMenuRight   = menu->mapToGlobal(menu->rect().topRight()).x();
            m_pendingCursorY     = QCursor::pos().y();
            m_exampleHoverTimer->start();
        });
    }
    connect(menu, &QMenu::aboutToHide, this, &MainWindow::hideExamplePreview);
}

void MainWindow::hideExamplePreview()
{
    m_exampleHoverTimer->stop();
    if (m_thumbnailWatcher->isRunning()) m_thumbnailWatcher->cancel();
    m_examplePreview->hidePopup();
}

void MainWindow::onExampleHoverTimeout()
{
    m_examplePreview->setLoading(m_pendingPreviewName);
    m_examplePreview->showAt(m_pendingMenuRight, QCursor::pos().y());
    if (m_thumbnailWatcher->isRunning()) return;

    const QString filePath = m_pendingPreviewFile;
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
    m_thumbnailWatcher->setFuture(future);
}

void MainWindow::onExampleThumbnailReady()
{
    if (m_thumbnailWatcher->isCanceled()) return;
    const QImage image = m_thumbnailWatcher->result();
    if (m_examplePreview->isVisible())
        m_examplePreview->setImage(image, m_pendingPreviewName);
}

void MainWindow::loadExample(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Open Example",
                             QString("Cannot open:\n%1").arg(filePath));
        return;
    }
    m_codeEditor->setPlainText(QString::fromUtf8(file.readAll()));
    applyOpenScadCode();
}
