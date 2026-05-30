#include "mainwindow.h"
#include "animatedtitlebar.h"
#include "appearancethemes.h"
#include "codeeditorpanel.h"
#include "csgevaluator.h"
#include "examplebrowsermenu.h"
#include "openscadgenerator.h"
#include "scenetreegraphicshelpers.h"
#include "scenetreegraphicswidget.h"
#include "theme.h"
#include "themeeditordialog.h"
#include "viewportwidget.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QSettings>
#include <QVBoxLayout>

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

namespace {

ThemeSpec applicationThemeForId(const QString &id)
{
    for (const ThemeSpec &theme : availableThemes()) {
        if (theme.id == id)
            return theme;
    }
    return defaultTheme();
}

QString settingsFilePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("3DScad.ini"));
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent, Qt::FramelessWindowHint | Qt::Window)
{
    // WA_TranslucentBackground must be false (the default) for a solid frameless window.
    // Setting it explicitly here — before any child widgets are created — ensures the
    // native HWND is configured correctly from the start and never needs to be
    // destroyed/recreated.
    setAttribute(Qt::WA_TranslucentBackground, false);

    AppearanceThemes::ensureDefaultFiles();
    m_settings = new QSettings(settingsFilePath(), QSettings::IniFormat, this);
    ThemeSpec initialTheme = applicationThemeForId(
        m_settings->value(QStringLiteral("appearance/window/theme"),
                          defaultTheme().id).toString());
    m_customWindowThemeName = m_settings->value(QStringLiteral("appearance/window/customTheme")).toString();
    if (!m_customWindowThemeName.isEmpty())
        AppearanceThemes::loadWindowTheme(m_customWindowThemeName, &initialTheme);
    m_applicationThemeId = initialTheme.id;
    m_activeWindowTheme = initialTheme;

    m_controller = new SceneController(this);

    // Wire controller signals to UI refresh slots.
    connect(m_controller, &SceneController::sceneChanged,
            this, &MainWindow::refreshSceneViews);
    connect(m_controller, &SceneController::selectionChanged,
            this, &MainWindow::onSelectionChanged);
    connect(m_controller, &SceneController::ctrlHighlightChanged,
            this, &MainWindow::highlightOpenScadSelection);
    // liveViewportUpdate is connected after m_viewport is created in buildUi().

    applyTheme(initialTheme);
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

    if      (local.x() < B && local.y() < B)                  *result = HTTOPLEFT;
    else if (local.x() > width()-B && local.y() < B)          *result = HTTOPRIGHT;
    else if (local.x() < B && local.y() > height()-B)         *result = HTBOTTOMLEFT;
    else if (local.x() > width()-B && local.y() > height()-B) *result = HTBOTTOMRIGHT;
    else if (local.y() < B)                                    *result = HTTOP;
    else if (local.y() > height()-B)                           *result = HTBOTTOM;
    else if (local.x() < B)                                    *result = HTLEFT;
    else if (local.x() > width()-B)                            *result = HTRIGHT;
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

    const ThemeSpec activeTheme = m_activeWindowTheme;
    m_titleBar = new AnimatedTitleBar(this);
    m_titleBar->setTitle(windowTitle());
    m_titleBar->setTheme(activeTheme);

    QMenuBar *appMenuBar = menuBar();
    auto *fileMenu     = appMenuBar->addMenu("File");
    auto *examplesMenu = fileMenu->addMenu("Open Example");
    m_exampleBrowser   = new ExampleBrowserMenu(examplesMenu, this);
    connect(m_exampleBrowser, &ExampleBrowserMenu::exampleSelected,
            this, &MainWindow::loadExample);

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
        connect(action, &QAction::triggered, this, [this, theme]() {
            m_applicationThemeId = theme.id;
            m_activeWindowTheme = theme;
            m_customWindowThemeName.clear();
            applyTheme(theme);
            m_titleBar->setTheme(theme);
            saveAppearanceSettings();
        });
    }
    themeMenu->addSeparator();
    themeMenu->addAction(QStringLiteral("Edit Theme..."), this, &MainWindow::openThemeEditor);
    m_savedWindowThemeMenu = themeMenu->addMenu(QStringLiteral("Saved Window Themes"));
    m_savedTreeThemeMenu = themeMenu->addMenu(QStringLiteral("Saved Tree Themes"));
    m_savedViewportThemeMenu = themeMenu->addMenu(QStringLiteral("Saved Viewport Themes"));

    auto *chrome = new QWidget(this);
    auto *chromeLayout = new QVBoxLayout(chrome);
    chromeLayout->setContentsMargins(0, 0, 0, 0);
    chromeLayout->setSpacing(0);
    chromeLayout->addWidget(m_titleBar);
    chromeLayout->addWidget(appMenuBar);
    setMenuWidget(chrome);

    m_viewport = new ViewportWidget;
    m_viewport->setDarkViewportTheme(
        m_settings->value(QStringLiteral("appearance/viewport/darkTheme"),
                          m_viewport->darkViewportTheme()).toBool());
    m_viewport->setViewportColorVariant(
        m_settings->value(QStringLiteral("appearance/viewport/materialColorVariant"),
                          m_viewport->viewportColorVariant()).toInt());
    m_customViewportThemeName = m_settings->value(QStringLiteral("appearance/viewport/customTheme")).toString();
    if (!m_customViewportThemeName.isEmpty()) {
        ViewportAppearanceTheme theme;
        if (AppearanceThemes::loadViewportTheme(m_customViewportThemeName, &theme))
            m_viewport->setCustomAppearanceTheme(theme);
        else
            m_customViewportThemeName.clear();
    }
    m_viewport->setScene(&m_controller->scene());
    connect(m_viewport, &ViewportWidget::darkViewportThemeChanged,
            this, [this](bool) { saveAppearanceSettings(); });
    connect(m_viewport, &ViewportWidget::viewportColorVariantChanged,
            this, [this](int) { saveAppearanceSettings(); });
    connect(m_viewport, &ViewportWidget::builtInAppearanceSelected, this, [this]() {
        m_customViewportThemeName.clear();
        saveAppearanceSettings();
    });

    // Now that m_viewport exists, wire live-update signal.
    connect(m_controller, &SceneController::liveViewportUpdate, this, [this]() {
        m_viewport->invalidateCsgPreview();
        m_viewport->update();
    });

    m_codeEditorPanel = new CodeEditorPanel;

    setCentralWidget(m_viewport);

    // Code-editor dock — defaults to the right of the viewport; can be
    // undocked or moved to any edge by the user.
    auto *codeDock = new QDockWidget(tr("OpenSCAD Code"), this);
    codeDock->setObjectName(QStringLiteral("CodeEditorDock"));
    codeDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea
                              | Qt::BottomDockWidgetArea);
    codeDock->setWidget(m_codeEditorPanel);
    addDockWidget(Qt::RightDockWidgetArea, codeDock);

    // Left dock
    auto *leftDock   = new QDockWidget("Scene Tree Canvas", this);
    auto *leftPanel  = new QWidget;
    auto *leftLayout = new QVBoxLayout(leftPanel);

    m_sceneTreeGraphics = new SceneTreeGraphicsWidget;
    m_sceneTreeGraphics->setTreeTheme(
        m_settings->value(QStringLiteral("appearance/sceneTree/theme"),
                          m_sceneTreeGraphics->treeTheme()).toInt());
    m_sceneTreeGraphics->setCanvasBackgroundTheme(
        m_settings->value(QStringLiteral("appearance/sceneTree/canvasBackgroundTheme"),
                          m_sceneTreeGraphics->canvasBackgroundThemeIndex()).toInt());
    m_customTreeThemeName = m_settings->value(QStringLiteral("appearance/sceneTree/customTheme")).toString();
    if (!m_customTreeThemeName.isEmpty()) {
        TreeAppearanceTheme theme;
        if (AppearanceThemes::loadTreeTheme(m_customTreeThemeName, &theme))
            m_sceneTreeGraphics->setCustomAppearanceTheme(theme);
        else
            m_customTreeThemeName.clear();
    }
    m_sceneTreeGraphics->setSceneDocument(&m_controller->scene());
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::treeThemeChanged,
            this, [this](int) { saveAppearanceSettings(); });
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::canvasBackgroundThemeChanged,
            this, [this](int) { saveAppearanceSettings(); });
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::builtInAppearanceSelected, this, [this]() {
        m_customTreeThemeName.clear();
        saveAppearanceSettings();
    });
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::inlineThemeEdited, this, [this]() {
        if (m_customTreeThemeName.isEmpty())
            m_customTreeThemeName = QStringLiteral("__autosave__");
        AppearanceThemes::saveTreeTheme(m_customTreeThemeName, SceneTreePalette::customTheme());
        saveAppearanceSettings();
    });

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
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::transformExpressionEdited,
            m_controller, &SceneController::handleTransformExpressionEdited);
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::colorChannelAdjusted,
            m_controller, &SceneController::handleColorChannelAdjusted);
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::transformControlHovered,
            this, [this](int groupId, SceneDocument::TreeNode::Operation op, int axis) {
                if (m_viewport) m_viewport->setTreeTransformControlPreview(groupId, op, axis);
            });
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::shapeParameterAdjusted,
            m_controller, &SceneController::handleShapeParameterAdjusted);
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::shapeParameterExpressionEdited,
            m_controller, &SceneController::handleShapeParameterExpressionEdited);
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::shapeParameterHovered,
            this, [this](int shapeId, int parameter) {
                if (m_viewport) m_viewport->setTreeShapeParameterPreview(shapeId, parameter);
            });
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::variableNumberAdjusted,
            m_controller, &SceneController::handleVariableNumberAdjusted);
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::variableExpressionEdited,
            m_controller, &SceneController::handleVariableExpressionEdited);
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::moduleCallArgumentAdjusted,
            m_controller, &SceneController::handleModuleCallArgumentAdjusted);
    connect(m_sceneTreeGraphics, &SceneTreeGraphicsWidget::moduleCallArgumentExpressionEdited,
            m_controller, &SceneController::handleModuleCallArgumentExpressionEdited);
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

    // Code-editor panel signals
    connect(m_codeEditorPanel, &CodeEditorPanel::applyRequested, this, [this]() {
        QString errorMsg;
        int     errorLine = -1;
        if (!m_controller->applyCode(m_codeEditorPanel->code(), &errorMsg, &errorLine)) {
            m_codeEditorPanel->showParseError(errorMsg, errorLine);
        } else {
            m_codeEditorPanel->clearParseError();
        }
    });
    connect(m_codeEditorPanel, &CodeEditorPanel::sendToOpenScadRequested, this, [this]() {
        if (!m_codeEditorPanel->writeOpenScadPreview(true)) return;
        const QString nativePath = QDir::toNativeSeparators(m_codeEditorPanel->previewScadPath());
        QApplication::clipboard()->setText(nativePath);
        QMessageBox::information(
            this, "OpenSCAD preview file",
            QString("Saved the current model to:\n\n%1\n\n"
                    "The path was copied to the clipboard. Open this file in OpenSCAD and "
                    "enable automatic reload/preview there.").arg(nativePath));
    });

    // Viewport signals → controller handlers
    connect(m_viewport, &ViewportWidget::shapeClicked, this, [this](int index) {
        const ShapeNode *shape = m_controller->scene().shapeAt(index);
        m_controller->selectShape(shape ? shape->id : -1);
        if (shape && m_sceneTreeGraphics)
            m_sceneTreeGraphics->focusSelectedNodeAnimated();
    });
    connect(m_viewport, &ViewportWidget::treeNodeClicked, this, [this](int nodeId) {
        m_controller->handleNodeSelected(nodeId);
        if (m_sceneTreeGraphics)
            m_sceneTreeGraphics->focusSelectedNodeAnimated();
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

    rebuildSavedThemeMenus();
    saveAppearanceSettings();
}

// ── Toolbar shape/group actions ────────────────────────────────────────────────

void MainWindow::addCube()              { m_controller->addCube();     }
void MainWindow::addSphere()            { m_controller->addSphere();   }
void MainWindow::addCylinder()          { m_controller->addCylinder(); }
void MainWindow::addCone()              { m_controller->addCone();     }
void MainWindow::addUnionGroup()        { m_controller->addGroup(SceneDocument::TreeNode::Union);        }
void MainWindow::addDifferenceGroup()   { m_controller->addGroup(SceneDocument::TreeNode::Difference);   }
void MainWindow::addIntersectionGroup() { m_controller->addGroup(SceneDocument::TreeNode::Intersection); }

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

void MainWindow::saveAppearanceSettings()
{
    if (!m_settings)
        return;

    m_settings->setValue(QStringLiteral("appearance/window/theme"), m_applicationThemeId);
    m_settings->setValue(QStringLiteral("appearance/window/customTheme"), m_customWindowThemeName);
    if (m_sceneTreeGraphics) {
        m_settings->setValue(QStringLiteral("appearance/sceneTree/theme"),
                             m_sceneTreeGraphics->treeTheme());
        m_settings->setValue(QStringLiteral("appearance/sceneTree/canvasBackgroundTheme"),
                             m_sceneTreeGraphics->canvasBackgroundThemeIndex());
        m_settings->setValue(QStringLiteral("appearance/sceneTree/customTheme"), m_customTreeThemeName);
    }
    if (m_viewport) {
        m_settings->setValue(QStringLiteral("appearance/viewport/darkTheme"),
                             m_viewport->darkViewportTheme());
        m_settings->setValue(QStringLiteral("appearance/viewport/materialColorVariant"),
                             m_viewport->viewportColorVariant());
        m_settings->setValue(QStringLiteral("appearance/viewport/customTheme"), m_customViewportThemeName);
    }
    m_settings->sync();
}

void MainWindow::rebuildSavedThemeMenus()
{
    if (!m_savedWindowThemeMenu || !m_savedTreeThemeMenu || !m_savedViewportThemeMenu)
        return;

    m_savedWindowThemeMenu->clear();
    for (const QString &name : AppearanceThemes::customWindowThemeNames()) {
        QAction *action = m_savedWindowThemeMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(name == m_customWindowThemeName);
        connect(action, &QAction::triggered, this, [this, name]() { applySavedWindowTheme(name); });
    }
    if (m_savedWindowThemeMenu->isEmpty())
        m_savedWindowThemeMenu->addAction(QStringLiteral("(none)"))->setEnabled(false);

    m_savedTreeThemeMenu->clear();
    for (const QString &name : AppearanceThemes::customTreeThemeNames()) {
        QAction *action = m_savedTreeThemeMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(name == m_customTreeThemeName);
        connect(action, &QAction::triggered, this, [this, name]() { applySavedTreeTheme(name); });
    }
    if (m_savedTreeThemeMenu->isEmpty())
        m_savedTreeThemeMenu->addAction(QStringLiteral("(none)"))->setEnabled(false);

    m_savedViewportThemeMenu->clear();
    for (const QString &name : AppearanceThemes::customViewportThemeNames()) {
        QAction *action = m_savedViewportThemeMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(name == m_customViewportThemeName);
        connect(action, &QAction::triggered, this, [this, name]() { applySavedViewportTheme(name); });
    }
    if (m_savedViewportThemeMenu->isEmpty())
        m_savedViewportThemeMenu->addAction(QStringLiteral("(none)"))->setEnabled(false);
}

void MainWindow::applySavedWindowTheme(const QString &name)
{
    ThemeSpec theme;
    if (!AppearanceThemes::loadWindowTheme(name, &theme))
        return;
    m_customWindowThemeName = name;
    m_applicationThemeId = theme.id;
    m_activeWindowTheme = theme;
    applyTheme(theme);
    if (m_titleBar)
        m_titleBar->setTheme(theme);
    rebuildSavedThemeMenus();
    saveAppearanceSettings();
}

void MainWindow::applySavedTreeTheme(const QString &name)
{
    TreeAppearanceTheme theme;
    if (!m_sceneTreeGraphics || !AppearanceThemes::loadTreeTheme(name, &theme))
        return;
    m_customTreeThemeName = name;
    m_sceneTreeGraphics->setCustomAppearanceTheme(theme);
    rebuildSavedThemeMenus();
    saveAppearanceSettings();
}

void MainWindow::applySavedViewportTheme(const QString &name)
{
    ViewportAppearanceTheme theme;
    if (!m_viewport || !AppearanceThemes::loadViewportTheme(name, &theme))
        return;
    m_customViewportThemeName = name;
    m_viewport->setCustomAppearanceTheme(theme);
    rebuildSavedThemeMenus();
    saveAppearanceSettings();
}

void MainWindow::openThemeEditor()
{
    TreeAppearanceTheme treeTheme = AppearanceThemes::defaultTreeTheme();
    ViewportAppearanceTheme viewportTheme = AppearanceThemes::defaultViewportTheme();
    if (!m_customTreeThemeName.isEmpty())
        AppearanceThemes::loadTreeTheme(m_customTreeThemeName, &treeTheme);
    if (!m_customViewportThemeName.isEmpty())
        AppearanceThemes::loadViewportTheme(m_customViewportThemeName, &viewportTheme);

    // freeze current window as preview background
    const QPixmap snapshot = grab();

    ThemeEditorDialog editor(snapshot, m_activeWindowTheme, treeTheme, viewportTheme, this);
    if (editor.exec() != QDialog::Accepted)
        return;

    const QString name = editor.themeName();
    if (editor.target() == ThemeEditorDialog::WindowTarget) {
        AppearanceThemes::saveWindowTheme(name, editor.windowTheme());
        applySavedWindowTheme(name);
    } else if (editor.target() == ThemeEditorDialog::TreeTarget) {
        AppearanceThemes::saveTreeTheme(name, editor.treeTheme());
        applySavedTreeTheme(name);
    } else {
        AppearanceThemes::saveViewportTheme(name, editor.viewportTheme());
        applySavedViewportTheme(name);
    }
    rebuildSavedThemeMenus();
}

void MainWindow::refreshOpenScadCode()
{
    QVector<OpenScadGenerator::SourceRange> ranges;
    const QString code = OpenScadGenerator::generateWithSourceMap(m_controller->scene(), &ranges);
    m_codeEditorPanel->setCodeAndRanges(code, ranges);
    highlightOpenScadSelection();
    m_codeEditorPanel->writeOpenScadPreview(false);
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

void MainWindow::highlightOpenScadSelection()
{
    if (!m_codeEditorPanel) return;
    const SceneController::CtrlParamHighlight &h = m_controller->ctrlHighlight();
    if (h.active)
        m_codeEditorPanel->applyCtrlParamHighlight(h);
    else
        m_codeEditorPanel->applySelectionHighlight(m_controller->selectedTreeNodeId());
}

void MainWindow::loadExample(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Open Example",
                             QString("Cannot open:\n%1").arg(filePath));
        return;
    }
    m_codeEditorPanel->setCode(QString::fromUtf8(file.readAll()));
    // Apply the loaded code immediately.
    QString errorMsg;
    int     errorLine = -1;
    if (!m_controller->applyCode(m_codeEditorPanel->code(), &errorMsg, &errorLine)) {
        m_codeEditorPanel->showParseError(errorMsg, errorLine);
    } else {
        m_codeEditorPanel->clearParseError();
        if (m_sceneTreeGraphics)
            m_sceneTreeGraphics->compactRootBlocksAndFit();
    }
}
