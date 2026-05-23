#include "mainwindow.h"
#include "csgevaluator.h"
#include "openscadgenerator.h"
#include "openscadparser.h"
#include "scenetreegraphicshelpers.h"
#include "scenetreegraphicswidget.h"
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
#include <QPalette>
#include <QPainter>
#include <QPoint>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QScrollBar>
#include <QTextEdit>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QScreen>
#include <QUndoStack>
#include <QVariantAnimation>
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
// Theme support (UI-only, stays in MainWindow)
// ────────────────────────────────────────────────────────────────────────────

struct ThemeSpec
{
    QString id;
    QString label;
    QColor window, surface, panel, panelAlt, text, mutedText, border,
           accent, accentHover, danger, titleBase, titlePulse;
};

static QVector<ThemeSpec> availableThemes()
{
    return {
        { QStringLiteral("dark-nord"),   QStringLiteral("Dark Nord"),
          QColor("#2e3440"), QColor("#252a34"), QColor("#3b4252"), QColor("#434c5e"),
          QColor("#eceff4"), QColor("#7f8898"), QColor("#4c566a"), QColor("#5e81ac"),
          QColor("#81a1c1"), QColor("#bf616a"), QColor("#111827"), QColor("#3b4252") },
        { QStringLiteral("olive-light"), QStringLiteral("Olive Light"),
          QColor("#eef0e6"), QColor("#f8f9f1"), QColor("#dde3cf"), QColor("#cfd8bd"),
          QColor("#23281c"), QColor("#65705a"), QColor("#a7b18f"), QColor("#6f8741"),
          QColor("#8fa85b"), QColor("#9f4f45"), QColor("#3b442e"), QColor("#586b3a") },
        { QStringLiteral("graphite"),    QStringLiteral("Graphite"),
          QColor("#202124"), QColor("#17191c"), QColor("#2b2f34"), QColor("#353b42"),
          QColor("#eef1f4"), QColor("#8f98a3"), QColor("#4b5560"), QColor("#6ea8fe"),
          QColor("#8bbcff"), QColor("#e06c75"), QColor("#111315"), QColor("#303844") },
        { QStringLiteral("blueprint"),   QStringLiteral("Blueprint"),
          QColor("#17202a"), QColor("#111820"), QColor("#223044"), QColor("#2f405a"),
          QColor("#e9f2ff"), QColor("#94a6bc"), QColor("#40556d"), QColor("#4ea1d3"),
          QColor("#78bee8"), QColor("#d66a6a"), QColor("#0b1320"), QColor("#243b55") },
        { QStringLiteral("warm-light"),  QStringLiteral("Warm Light"),
          QColor("#f4f1ea"), QColor("#fffdf7"), QColor("#e8dfd1"), QColor("#d8cbbb"),
          QColor("#27221c"), QColor("#776a5d"), QColor("#b6a897"), QColor("#9b6b3f"),
          QColor("#bd8452"), QColor("#a85048"), QColor("#443326"), QColor("#6b4b35") },
    };
}

static ThemeSpec defaultTheme()
{
    return availableThemes().first();
}

static QString colorName(const QColor &c) { return c.name(QColor::HexRgb); }

static QString themeStyleSheet(const ThemeSpec &t)
{
    QString style = QStringLiteral(R"(
        QMainWindow, QWidget { background-color: __WINDOW__; color: __TEXT__; font-family: "Segoe UI"; font-size: 10pt; }
        QMenuBar { background-color: __SURFACE__; color: __TEXT__; border-bottom: 1px solid __BORDER__; padding: 2px; }
        QMenuBar::item { background: transparent; padding: 4px 10px; }
        QMenuBar::item:selected, QMenuBar::item:pressed { background-color: __PANEL__; border-radius: 4px; }
        QMenu { background-color: __WINDOW__; color: __TEXT__; border: 1px solid __BORDER__; padding: 4px; }
        QMenu::item { padding: 5px 24px 5px 18px; }
        QMenu::item:selected { background-color: __ACCENT__; color: #ffffff; }
        QMenu::separator { height: 1px; background: __BORDER__; margin: 4px 8px; }
        QDockWidget { titlebar-close-icon: none; titlebar-normal-icon: none; }
        QDockWidget::title { background-color: __PANEL__; color: __TEXT__; padding: 5px 8px; border: 1px solid __BORDER__; }
        QLabel { background: transparent; color: __TEXT__; }
        QPushButton, QToolButton { background-color: __PANEL__; color: __TEXT__; border: 1px solid __BORDER__; border-radius: 5px; padding: 5px 10px; min-height: 22px; }
        QPushButton:hover, QToolButton:hover { background-color: __PANEL_ALT__; border-color: __ACCENT__; }
        QPushButton:pressed, QToolButton:pressed { background-color: __ACCENT__; color: #ffffff; }
        QPushButton:disabled, QToolButton:disabled { background-color: __PANEL__; color: __MUTED__; border-color: __BORDER__; }
        QTextEdit, QPlainTextEdit, QLineEdit, QAbstractSpinBox, QComboBox, QListView, QTreeView, QTableView { background-color: __SURFACE__; color: __TEXT__; selection-background-color: __ACCENT__; selection-color: #ffffff; border: 1px solid __BORDER__; border-radius: 4px; padding: 3px; }
        QTextEdit { font-family: "Consolas", "Courier New", monospace; }
        QAbstractSpinBox:disabled, QComboBox:disabled, QLineEdit:disabled { background-color: __PANEL__; color: __MUTED__; border-color: __BORDER__; }
        QComboBox::drop-down { width: 20px; border-left: 1px solid __BORDER__; background-color: __PANEL__; }
        QComboBox QAbstractItemView { background-color: __SURFACE__; color: __TEXT__; border: 1px solid __BORDER__; selection-background-color: __ACCENT__; }
        QGroupBox { color: __TEXT__; border: 1px solid __BORDER__; border-radius: 5px; margin-top: 10px; padding-top: 10px; }
        QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; background-color: __WINDOW__; }
        QCheckBox { spacing: 6px; background: transparent; }
        QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid __BORDER__; border-radius: 3px; background-color: __SURFACE__; }
        QCheckBox::indicator:checked { background-color: __ACCENT__; border-color: __ACCENT_HOVER__; }
        QSplitter::handle { background-color: __BORDER__; }
        QSplitter::handle:hover { background-color: __ACCENT__; }
        QScrollBar:vertical, QScrollBar:horizontal { background: __SURFACE__; border: none; margin: 0; }
        QScrollBar::handle:vertical, QScrollBar::handle:horizontal { background: __BORDER__; border-radius: 4px; min-height: 24px; min-width: 24px; }
        QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover { background: __ACCENT__; }
        QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: none; border: none; width: 0; height: 0; }
        QToolTip { background-color: __PANEL__; color: __TEXT__; border: 1px solid __ACCENT__; padding: 4px; }
    )");
    style.replace(QStringLiteral("__WINDOW__"),      colorName(t.window));
    style.replace(QStringLiteral("__SURFACE__"),     colorName(t.surface));
    style.replace(QStringLiteral("__PANEL__"),       colorName(t.panel));
    style.replace(QStringLiteral("__PANEL_ALT__"),   colorName(t.panelAlt));
    style.replace(QStringLiteral("__TEXT__"),        colorName(t.text));
    style.replace(QStringLiteral("__MUTED__"),       colorName(t.mutedText));
    style.replace(QStringLiteral("__BORDER__"),      colorName(t.border));
    style.replace(QStringLiteral("__ACCENT__"),      colorName(t.accent));
    style.replace(QStringLiteral("__ACCENT_HOVER__"),colorName(t.accentHover));
    style.replace(QStringLiteral("__DANGER__"),      colorName(t.danger));
    return style;
}

static void applyTheme(const ThemeSpec &theme)
{
    QApplication::setStyle(QStringLiteral("Fusion"));
    QPalette palette;
    palette.setColor(QPalette::Window,          theme.window);
    palette.setColor(QPalette::WindowText,      theme.text);
    palette.setColor(QPalette::Base,            theme.surface);
    palette.setColor(QPalette::AlternateBase,   theme.panel);
    palette.setColor(QPalette::ToolTipBase,     theme.panel);
    palette.setColor(QPalette::ToolTipText,     theme.text);
    palette.setColor(QPalette::Text,            theme.text);
    palette.setColor(QPalette::Button,          theme.panel);
    palette.setColor(QPalette::ButtonText,      theme.text);
    palette.setColor(QPalette::BrightText,      theme.danger);
    palette.setColor(QPalette::Highlight,       theme.accent);
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::Disabled, QPalette::Text,       theme.mutedText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, theme.mutedText);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, theme.mutedText);
    qApp->setPalette(palette);
    qApp->setStyleSheet(themeStyleSheet(theme));
}

// ────────────────────────────────────────────────────────────────────────────
// AnimatedTitleBar
// ────────────────────────────────────────────────────────────────────────────

class AnimatedTitleBar : public QWidget
{
public:
    explicit AnimatedTitleBar(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_titleLabel(new QLabel(this))
        , m_minimizeButton(new QToolButton(this))
        , m_maximizeButton(new QToolButton(this))
        , m_closeButton(new QToolButton(this))
        , m_pulse(new QVariantAnimation(this))
    {
        setFixedHeight(34);
        setAttribute(Qt::WA_Hover, true);
        m_titleLabel->setText(QStringLiteral("OpenSCAD Visual Editor Prototype"));
        m_titleLabel->setContentsMargins(10, 0, 0, 0);
        m_minimizeButton->setText(QStringLiteral("_"));
        m_maximizeButton->setText(QStringLiteral("[]"));
        m_closeButton->setText(QStringLiteral("X"));
        for (QToolButton *b : {m_minimizeButton, m_maximizeButton, m_closeButton}) {
            b->setFixedSize(36, 24);
            b->setAutoRaise(true);
            b->setFocusPolicy(Qt::NoFocus);
        }
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 4, 0);
        layout->setSpacing(4);
        layout->addWidget(m_titleLabel, 1);
        layout->addWidget(m_minimizeButton);
        layout->addWidget(m_maximizeButton);
        layout->addWidget(m_closeButton);

        m_pulse->setDuration(1200);
        m_pulse->setLoopCount(-1);
        m_pulse->setEasingCurve(QEasingCurve::InOutSine);
        m_pulse->setStartValue(0.0);
        m_pulse->setKeyValueAt(0.5, 1.0);
        m_pulse->setEndValue(0.0);
        connect(m_pulse, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_currentColor = mix(m_baseColor, m_pulseColor, v.toReal());
            update();
        });
        connect(m_minimizeButton, &QToolButton::clicked, this, [this]() {
            if (QWidget *w = window()) w->showMinimized();
        });
        connect(m_maximizeButton, &QToolButton::clicked, this, [this]() { toggleMaximized(); });
        connect(m_closeButton,    &QToolButton::clicked, this, [this]() {
            if (QWidget *w = window()) w->close();
        });
    }

    void setTitle(const QString &title) { m_titleLabel->setText(title); }

    void setTheme(const ThemeSpec &theme)
    {
        m_baseColor   = theme.titleBase;
        m_pulseColor  = theme.titlePulse;
        m_currentColor = m_baseColor;
        m_textColor   = theme.text;
        m_borderColor = theme.border;
        m_accentColor = theme.accent;
        m_dangerColor = theme.danger;

        const QString btn = QStringLiteral(
            "QToolButton { background-color: transparent; color: %1; border: none; border-radius: 5px; padding: 0; font-weight: 600; }"
            "QToolButton:hover { background-color: %2; color: #ffffff; }"
            "QToolButton:pressed { background-color: %3; color: #ffffff; }")
            .arg(colorName(m_textColor), colorName(m_accentColor), colorName(theme.accentHover));
        const QString cls = QStringLiteral(
            "QToolButton { background-color: transparent; color: %1; border: none; border-radius: 5px; padding: 0; font-weight: 700; }"
            "QToolButton:hover { background-color: %2; color: #ffffff; }"
            "QToolButton:pressed { background-color: %3; color: #ffffff; }")
            .arg(colorName(m_textColor), colorName(m_dangerColor), colorName(theme.accentHover));

        m_titleLabel->setStyleSheet(
            QStringLiteral("background: transparent; color: %1; font-weight: 600;")
                .arg(colorName(m_textColor)));
        m_minimizeButton->setStyleSheet(btn);
        m_maximizeButton->setStyleSheet(btn);
        m_closeButton->setStyleSheet(cls);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), m_currentColor);
        QColor line = m_borderColor; line.setAlpha(180);
        p.setPen(QPen(line, 1));
        p.drawLine(QPoint(0, height() - 1), QPoint(width(), height() - 1));
    }
    void enterEvent(QEvent *) override { m_pulse->start(); }
    void leaveEvent(QEvent *) override { m_pulse->stop(); m_currentColor = m_baseColor; update(); }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragOffset = event->globalPos() - window()->frameGeometry().topLeft();
            event->accept(); return;
        }
        QWidget::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_dragging && (event->buttons() & Qt::LeftButton)) {
            QWidget *w = window();
            if (w && !w->isMaximized()) w->move(event->globalPos() - m_dragOffset);
            event->accept(); return;
        }
        QWidget::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        m_dragging = false; QWidget::mouseReleaseEvent(event);
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) { toggleMaximized(); event->accept(); return; }
        QWidget::mouseDoubleClickEvent(event);
    }

private:
    static QColor mix(const QColor &a, const QColor &b, qreal t)
    {
        return QColor::fromRgbF(a.redF() + (b.redF()-a.redF())*t,
                                a.greenF()+(b.greenF()-a.greenF())*t,
                                a.blueF() +(b.blueF()-a.blueF())*t);
    }
    void toggleMaximized()
    {
        QWidget *w = window(); if (!w) return;
        if (w->isMaximized()) { w->showNormal(); return; }
        if (QScreen *s = QApplication::screenAt(w->frameGeometry().center()))
            w->move(s->availableGeometry().topLeft());
        w->showMaximized();
    }
    QLabel *m_titleLabel; QToolButton *m_minimizeButton, *m_maximizeButton, *m_closeButton;
    QVariantAnimation *m_pulse;
    QColor m_baseColor{QStringLiteral("#111827")}, m_pulseColor{QStringLiteral("#3b4252")},
           m_currentColor{QStringLiteral("#111827")}, m_textColor{QStringLiteral("#eceff4")},
           m_borderColor{QStringLiteral("#4c566a")}, m_accentColor{QStringLiteral("#5e81ac")},
           m_dangerColor{QStringLiteral("#bf616a")};
    bool   m_dragging = false;
    QPoint m_dragOffset;
};

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
