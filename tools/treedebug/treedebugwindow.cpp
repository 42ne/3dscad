#include "treedebugwindow.h"
#include "../../scenetreegraphicswidget.h"
#include "../../scenetreegraphicshelpers.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QPoint>
#include <QPainter>
#include <QResizeEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

using namespace SceneTreeGraphics;

// ─── Theme infrastructure (copied from mainwindow.cpp) ───────────────────────

struct ThemeSpec
{
    QString id;
    QString label;
    QColor  window, surface, panel, panelAlt;
    QColor  text, mutedText, border, accent, accentHover, danger;
    QColor  titleBase, titlePulse;
};

static QVector<ThemeSpec> availableThemes()
{
    return {
        { QStringLiteral("dark-nord"),    QStringLiteral("Dark Nord"),
          QColor("#2e3440"), QColor("#252a34"), QColor("#3b4252"), QColor("#434c5e"),
          QColor("#eceff4"), QColor("#7f8898"), QColor("#4c566a"), QColor("#5e81ac"),
          QColor("#81a1c1"), QColor("#bf616a"), QColor("#111827"), QColor("#3b4252") },
        { QStringLiteral("graphite"),     QStringLiteral("Graphite"),
          QColor("#202124"), QColor("#17191c"), QColor("#2b2f34"), QColor("#353b42"),
          QColor("#eef1f4"), QColor("#8f98a3"), QColor("#4b5560"), QColor("#6ea8fe"),
          QColor("#8bbcff"), QColor("#e06c75"), QColor("#111315"), QColor("#303844") },
        { QStringLiteral("blueprint"),    QStringLiteral("Blueprint"),
          QColor("#17202a"), QColor("#111820"), QColor("#223044"), QColor("#2f405a"),
          QColor("#e9f2ff"), QColor("#94a6bc"), QColor("#40556d"), QColor("#4ea1d3"),
          QColor("#78bee8"), QColor("#d66a6a"), QColor("#0b1320"), QColor("#243b55") },
        { QStringLiteral("olive-light"),  QStringLiteral("Olive Light"),
          QColor("#eef0e6"), QColor("#f8f9f1"), QColor("#dde3cf"), QColor("#cfd8bd"),
          QColor("#23281c"), QColor("#65705a"), QColor("#a7b18f"), QColor("#6f8741"),
          QColor("#8fa85b"), QColor("#9f4f45"), QColor("#3b442e"), QColor("#586b3a") },
        { QStringLiteral("warm-light"),   QStringLiteral("Warm Light"),
          QColor("#f4f1ea"), QColor("#fffdf7"), QColor("#e8dfd1"), QColor("#d8cbbb"),
          QColor("#27221c"), QColor("#776a5d"), QColor("#b6a897"), QColor("#9b6b3f"),
          QColor("#bd8452"), QColor("#a85048"), QColor("#443326"), QColor("#6b4b35") },
    };
}

static ThemeSpec defaultTheme() { return availableThemes().first(); }

static QString colorName(const QColor &c) { return c.name(QColor::HexRgb); }

static QString themeStyleSheet(const ThemeSpec &t)
{
    QString s = QStringLiteral(R"(
        QMainWindow, QWidget {
            background-color: __WINDOW__; color: __TEXT__;
            font-family: "Segoe UI"; font-size: 10pt; }
        QMenuBar { background-color: __SURFACE__; color: __TEXT__;
            border-bottom: 1px solid __BORDER__; padding: 2px; }
        QMenuBar::item { background: transparent; padding: 4px 10px; }
        QMenuBar::item:selected, QMenuBar::item:pressed {
            background-color: __PANEL__; border-radius: 4px; }
        QMenu { background-color: __WINDOW__; color: __TEXT__;
            border: 1px solid __BORDER__; padding: 4px; }
        QMenu::item { padding: 5px 24px 5px 18px; }
        QMenu::item:selected { background-color: __ACCENT__; color: #ffffff; }
        QMenu::separator { height: 1px; background: __BORDER__; margin: 4px 8px; }
        QLabel { background: transparent; color: __TEXT__; }
        QPushButton, QToolButton {
            background-color: __PANEL__; color: __TEXT__;
            border: 1px solid __BORDER__; border-radius: 5px;
            padding: 5px 10px; min-height: 22px; }
        QPushButton:hover, QToolButton:hover {
            background-color: __PANEL_ALT__; border-color: __ACCENT__; }
        QPushButton:pressed, QToolButton:pressed {
            background-color: __ACCENT__; color: #ffffff; }
        QScrollBar:vertical, QScrollBar:horizontal {
            background: __SURFACE__; border: none; margin: 0; }
        QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
            background: __BORDER__; border-radius: 4px;
            min-height: 24px; min-width: 24px; }
        QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover {
            background: __ACCENT__; }
        QScrollBar::add-line, QScrollBar::sub-line,
        QScrollBar::add-page, QScrollBar::sub-page {
            background: none; border: none; width: 0; height: 0; }
        QToolTip { background-color: __PANEL__; color: __TEXT__;
            border: 1px solid __ACCENT__; padding: 4px; }
    )");
    s.replace(QStringLiteral("__WINDOW__"),      colorName(t.window));
    s.replace(QStringLiteral("__SURFACE__"),     colorName(t.surface));
    s.replace(QStringLiteral("__PANEL__"),       colorName(t.panel));
    s.replace(QStringLiteral("__PANEL_ALT__"),   colorName(t.panelAlt));
    s.replace(QStringLiteral("__TEXT__"),        colorName(t.text));
    s.replace(QStringLiteral("__MUTED__"),       colorName(t.mutedText));
    s.replace(QStringLiteral("__BORDER__"),      colorName(t.border));
    s.replace(QStringLiteral("__ACCENT__"),      colorName(t.accent));
    s.replace(QStringLiteral("__ACCENT_HOVER__"),colorName(t.accentHover));
    s.replace(QStringLiteral("__DANGER__"),      colorName(t.danger));
    return s;
}

static void applyTheme(const ThemeSpec &t)
{
    QApplication::setStyle(QStringLiteral("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window,          t.window);
    p.setColor(QPalette::WindowText,      t.text);
    p.setColor(QPalette::Base,            t.surface);
    p.setColor(QPalette::AlternateBase,   t.panel);
    p.setColor(QPalette::ToolTipBase,     t.panel);
    p.setColor(QPalette::ToolTipText,     t.text);
    p.setColor(QPalette::Text,            t.text);
    p.setColor(QPalette::Button,          t.panel);
    p.setColor(QPalette::ButtonText,      t.text);
    p.setColor(QPalette::BrightText,      t.danger);
    p.setColor(QPalette::Highlight,       t.accent);
    p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    p.setColor(QPalette::Disabled, QPalette::Text,       t.mutedText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, t.mutedText);
    p.setColor(QPalette::Disabled, QPalette::WindowText, t.mutedText);
    qApp->setPalette(p);
    qApp->setStyleSheet(themeStyleSheet(t));
}

// ─── Animated title bar (copied from mainwindow.cpp) ─────────────────────────

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

    void setTitle(const QString &t) { m_titleLabel->setText(t); }

    void setTheme(const ThemeSpec &t)
    {
        m_baseColor    = t.titleBase;
        m_pulseColor   = t.titlePulse;
        m_currentColor = m_baseColor;
        m_textColor    = t.text;
        m_borderColor  = t.border;
        m_accentColor  = t.accent;
        m_dangerColor  = t.danger;

        const QString btnStyle = QStringLiteral(
            "QToolButton { background-color: transparent; color: %1; border: none;"
            "  border-radius: 5px; padding: 0; font-weight: 600; }"
            "QToolButton:hover { background-color: %2; color: #ffffff; }"
            "QToolButton:pressed { background-color: %3; color: #ffffff; }")
            .arg(colorName(m_textColor), colorName(m_accentColor), colorName(t.accentHover));

        const QString closeStyle = QStringLiteral(
            "QToolButton { background-color: transparent; color: %1; border: none;"
            "  border-radius: 5px; padding: 0; font-weight: 700; }"
            "QToolButton:hover { background-color: %2; color: #ffffff; }"
            "QToolButton:pressed { background-color: %3; color: #ffffff; }")
            .arg(colorName(m_textColor), colorName(m_dangerColor), colorName(t.accentHover));

        m_titleLabel->setStyleSheet(
            QStringLiteral("background: transparent; color: %1; font-weight: 600;")
            .arg(colorName(m_textColor)));
        m_minimizeButton->setStyleSheet(btnStyle);
        m_maximizeButton->setStyleSheet(btnStyle);
        m_closeButton->setStyleSheet(closeStyle);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), m_currentColor);
        QColor line = m_borderColor; line.setAlpha(180);
        p.setPen(QPen(line, 1));
        p.drawLine(QPoint(0, height()-1), QPoint(width(), height()-1));
    }
    void enterEvent(QEvent *) override { m_pulse->start(); }
    void leaveEvent(QEvent *) override { m_pulse->stop(); m_currentColor = m_baseColor; update(); }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            m_dragging   = true;
            m_dragOffset = event->globalPos() - window()->frameGeometry().topLeft();
            event->accept(); return;
        }
        QWidget::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (m_dragging && (event->buttons() & Qt::LeftButton)) {
            QWidget *w = window();
            if (w && !w->isMaximized()) w->move(event->globalPos() - m_dragOffset);
            event->accept(); return;
        }
        QWidget::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override { m_dragging = false; QWidget::mouseReleaseEvent(event); }
    void mouseDoubleClickEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) { toggleMaximized(); event->accept(); return; }
        QWidget::mouseDoubleClickEvent(event);
    }

private:
    static QColor mix(const QColor &a, const QColor &b, qreal t) {
        return QColor::fromRgbF(a.redF()+(b.redF()-a.redF())*t, a.greenF()+(b.greenF()-a.greenF())*t,
                                a.blueF()+(b.blueF()-a.blueF())*t);
    }
    void toggleMaximized() {
        if (QWidget *w = window()) w->isMaximized() ? w->showNormal() : w->showMaximized();
    }

    QLabel        *m_titleLabel     = nullptr;
    QToolButton   *m_minimizeButton = nullptr;
    QToolButton   *m_maximizeButton = nullptr;
    QToolButton   *m_closeButton    = nullptr;
    QVariantAnimation *m_pulse      = nullptr;
    QColor m_baseColor    {QStringLiteral("#111827")};
    QColor m_pulseColor   {QStringLiteral("#3b4252")};
    QColor m_currentColor {QStringLiteral("#111827")};
    QColor m_textColor    {QStringLiteral("#eceff4")};
    QColor m_borderColor  {QStringLiteral("#4c566a")};
    QColor m_accentColor  {QStringLiteral("#5e81ac")};
    QColor m_dangerColor  {QStringLiteral("#bf616a")};
    bool   m_dragging     = false;
    QPoint m_dragOffset;
};

// ─── TreeDebugWindow ─────────────────────────────────────────────────────────

TreeDebugWindow::TreeDebugWindow(QWidget *parent)
    : QMainWindow(parent)
{
    applyTheme(defaultTheme());
    buildUi();
    buildTestDocument();
    m_treeWidget->setSceneDocument(&m_scene);
    m_treeWidget->refresh();
}

void TreeDebugWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Scene Tree Debugger"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    resize(420, 800);

    ThemeSpec activeTheme = defaultTheme();
    auto *titleBar = new AnimatedTitleBar(this);
    titleBar->setTitle(windowTitle());
    titleBar->setTheme(activeTheme);

    QMenuBar *appMenuBar = menuBar();
    auto *themeMenu = appMenuBar->addMenu(QStringLiteral("Theme"));
    for (const ThemeSpec &theme : availableThemes()) {
        QAction *action = themeMenu->addAction(theme.label);
        connect(action, &QAction::triggered, this, [theme, titleBar]() {
            applyTheme(theme);
            titleBar->setTheme(theme);
        });
    }

    auto *chrome       = new QWidget(this);
    auto *chromeLayout = new QVBoxLayout(chrome);
    chromeLayout->setContentsMargins(0, 0, 0, 0);
    chromeLayout->setSpacing(0);
    chromeLayout->addWidget(titleBar);
    chromeLayout->addWidget(appMenuBar);
    setMenuWidget(chrome);

    m_treeWidget = new SceneTreeGraphicsWidget(this);

    // Wire up callbacks that just print to qDebug.
    m_treeWidget->setToolDroppedCallback([](const QString &tool, int parent, int idx) {
        qDebug("toolDropped: %s parent=%d idx=%d", qPrintable(tool), parent, idx);
    });
    m_treeWidget->setTreeNodeDroppedCallback([](int nodeId, int parent, int idx) {
        qDebug("nodeDropped: node=%d parent=%d idx=%d", nodeId, parent, idx);
    });
    m_treeWidget->setTreeNodeSelectedCallback([](int nodeId) {
        qDebug("nodeSelected: %d", nodeId);
    });
    m_treeWidget->setTreeNodeDeleteRequestedCallback([](int nodeId) {
        qDebug("deleteRequested: %d", nodeId);
    });
    m_treeWidget->setTransformValueAdjustedCallback([this](int groupId, int axis, int start, int len, qreal delta) {
        qDebug("transformAdjusted: group=%d axis=%d start=%d len=%d delta=%f", groupId, axis, start, len, delta);
        const SceneDocument::TreeNode *node = m_scene.treeNodeById(groupId);
        if (!node) return;
        // Apply a simple in-place update for testing
        QVector3D pos = node->position, rot = node->rotation, scl = node->scale;
        QStringList exprs = node->transformExpressions;
        if (exprs.size() < 3) exprs = QStringList{QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0")};
        bool ok = false;
        const qreal cur = exprs[axis].isEmpty() ? 0.0 : exprs[axis].toDouble(&ok);
        const qreal next = (ok ? cur : 0.0) + delta;
        exprs[axis] = QString::number(next, 'f', 1);
        m_scene.updateGroupTransform(groupId, pos, rot, scl, exprs);
        m_treeWidget->refresh();
    });
    m_treeWidget->setForLoopRangeAdjustedCallback([this](int nodeId, int start, int len, qreal delta) {
        qDebug("forLoopAdjusted: node=%d start=%d len=%d delta=%f", nodeId, start, len, delta);
        const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
        if (!node) return;
        QString expr = node->loopRangeExpression;
        bool ok = false;
        const qreal cur = expr.mid(start, len).toDouble(&ok);
        const qreal next = (ok ? cur : 0.0) + delta;
        const QString replacement = QString::number(next, 'f', 0);
        expr.replace(start, len, replacement);
        m_scene.updateForLoop(nodeId, node->loopVariable, expr);
        m_treeWidget->refresh();
    });
    m_treeWidget->setVariableNumberAdjustedCallback([this](int nodeId, int start, int len, qreal delta) {
        qDebug("varAdjusted: node=%d start=%d len=%d delta=%f", nodeId, start, len, delta);
        const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
        if (!node) return;
        QString expr = node->variableExpression;
        bool ok = false;
        const qreal cur = expr.mid(start, len).toDouble(&ok);
        const qreal next = (ok ? cur : 0.0) + delta;
        expr.replace(start, len, QString::number(next, 'f', 1));
        m_scene.updateVariableExpression(nodeId, expr);
        m_treeWidget->refresh();
    });
    m_treeWidget->setShapeParameterAdjustedCallback([this](int nodeId, int param, int start, int len, qreal delta) {
        qDebug("shapeParamAdjusted: node=%d param=%d start=%d len=%d delta=%f", nodeId, param, start, len, delta);
        const SceneDocument::TreeNode *treeNode = m_scene.treeNodeById(nodeId);
        if (!treeNode) return;
        ShapeNode *shape = m_scene.shapeById(treeNode->shapeId);
        if (!shape) return;
        QString expr = param < shape->parameterExpressions.size() ? shape->parameterExpressions[param] : QString();
        if (expr.isEmpty()) return;
        bool ok = false;
        const qreal cur = expr.mid(start, len).toDouble(&ok);
        const qreal next = (ok ? cur : 0.0) + delta;
        expr.replace(start, len, QString::number(next, 'f', 1));
        if (param < shape->parameterExpressions.size())
            shape->parameterExpressions[param] = expr;
        m_scene.updateShape(*shape);
        m_treeWidget->refresh();
    });
    m_treeWidget->setModuleRenameRequestedCallback([this](int groupId, const QString &newName) {
        qDebug("moduleRename: %d -> %s", groupId, qPrintable(newName));
        m_scene.setModuleName(groupId, newName);
        m_treeWidget->refresh();
    });
    m_treeWidget->setVariableRenameRequestedCallback([this](int varId, const QString &newName) {
        qDebug("varRename: %d -> %s", varId, qPrintable(newName));
        m_scene.renameVariable(varId, newName);
        m_treeWidget->refresh();
    });
    m_treeWidget->setCtrlReleasedCallback([]() {});

    setCentralWidget(m_treeWidget);
}

// ─── Test document ───────────────────────────────────────────────────────────
//
// Scene layout:
//   [scene root]
//   ├─ var radius = 15
//   ├─ var step = 2.5
//   ├─ for (i = [0 : 10 : 100])          ← 3 number pills
//   │   └─ translate [10 0 0]
//   │       └─ cube 20×20×20
//   ├─ for (a = [0 : 2.5 : -30])         ← numbers incl. negative
//   │   └─ sphere r=5
//   ├─ union
//   │   ├─ cube 30×20×10
//   │   └─ sphere r=15
//   ├─ difference
//   │   ├─ cube 40×40×20
//   │   └─ cylinder r=8 h=25
//   ├─ translate [5 10 0]  (plain numbers)
//   │   └─ rotate [0 0 45]
//   │       └─ cube 10×10×10
//   └─ scale [2 1.5 1]
//       └─ cylinder r=5 h=30
//
//   [module "gadget"]
//   ├─ param r = 5
//   ├─ param h = 20
//   └─ cylinder r=[5] h=[20]
//
//   scene also has:
//   └─ module-call "gadget(r=12, h=35)"
//
void TreeDebugWindow::buildTestDocument()
{
    // ── Variables ────────────────────────────────────────────────────────────
    const int varRadiusId = m_scene.addVariable();
    m_scene.renameVariable(varRadiusId, QStringLiteral("radius"));
    m_scene.updateVariableExpression(varRadiusId, QStringLiteral("15"));

    const int varStepId = m_scene.addVariable();
    m_scene.renameVariable(varStepId, QStringLiteral("step"));
    m_scene.updateVariableExpression(varStepId, QStringLiteral("2.5"));

    // ── for (i = [0 : 10 : 100]) ─────────────────────────────────────────────
    const int forId1 = m_scene.addGroup(SceneDocument::TreeNode::For);
    m_scene.updateForLoop(forId1, QStringLiteral("i"), QStringLiteral("[0 : 10 : 100]"));

    const int trans1 = m_scene.addGroup(SceneDocument::TreeNode::Translate, forId1);
    m_scene.updateGroupTransform(trans1, QVector3D(10, 0, 0), {}, QVector3D(1,1,1),
                                 QStringList{QStringLiteral("10"), QStringLiteral("0"), QStringLiteral("0")});
    {
        ShapeNode cube; cube.type = ShapeNode::Cube; cube.size = QVector3D(20, 20, 20);
        cube.parameterExpressions = QStringList{QStringLiteral("20"), QStringLiteral("20"), QStringLiteral("20")};
        m_scene.addShape(cube, trans1);
    }

    // ── for (a = [0 : 2.5 : -30]) ────────────────────────────────────────────
    const int forId2 = m_scene.addGroup(SceneDocument::TreeNode::For);
    m_scene.updateForLoop(forId2, QStringLiteral("a"), QStringLiteral("[0 : 2.5 : -30]"));
    {
        ShapeNode sphere; sphere.type = ShapeNode::Sphere; sphere.radius = 5.0f;
        sphere.parameterExpressions = QStringList{QStringLiteral("5")};
        m_scene.addShape(sphere, forId2);
    }

    // ── union ─────────────────────────────────────────────────────────────────
    const int unionId = m_scene.addGroup(SceneDocument::TreeNode::Union);
    {
        ShapeNode cube; cube.type = ShapeNode::Cube; cube.size = QVector3D(30, 20, 10);
        cube.parameterExpressions = QStringList{QStringLiteral("30"), QStringLiteral("20"), QStringLiteral("10")};
        m_scene.addShape(cube, unionId);
        ShapeNode sphere; sphere.type = ShapeNode::Sphere; sphere.radius = 15.0f;
        sphere.parameterExpressions = QStringList{QStringLiteral("15")};
        m_scene.addShape(sphere, unionId);
    }

    // ── difference ────────────────────────────────────────────────────────────
    const int diffId = m_scene.addGroup(SceneDocument::TreeNode::Difference);
    {
        ShapeNode cube; cube.type = ShapeNode::Cube; cube.size = QVector3D(40, 40, 20);
        cube.parameterExpressions = QStringList{QStringLiteral("40"), QStringLiteral("40"), QStringLiteral("20")};
        m_scene.addShape(cube, diffId);
        ShapeNode cyl; cyl.type = ShapeNode::Cylinder; cyl.radius = 8.0f; cyl.height = 25.0f;
        cyl.parameterExpressions = QStringList{QStringLiteral("8"), QStringLiteral("25")};
        m_scene.addShape(cyl, diffId);
    }

    // ── translate [5 10 0] → rotate [0 0 45] → cube ─────────────────────────
    const int transId = m_scene.addGroup(SceneDocument::TreeNode::Translate);
    m_scene.updateGroupTransform(transId, QVector3D(5, 10, 0), {}, QVector3D(1,1,1),
                                 QStringList{QStringLiteral("5"), QStringLiteral("10"), QStringLiteral("0")});
    {
        const int rotId = m_scene.addGroup(SceneDocument::TreeNode::Rotate, transId);
        m_scene.updateGroupTransform(rotId, {}, QVector3D(0, 0, 45), QVector3D(1,1,1),
                                     QStringList{QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("45")});
        ShapeNode cube; cube.type = ShapeNode::Cube; cube.size = QVector3D(10, 10, 10);
        cube.parameterExpressions = QStringList{QStringLiteral("10"), QStringLiteral("10"), QStringLiteral("10")};
        m_scene.addShape(cube, rotId);
    }

    // ── scale [2 1.5 1] → cylinder ───────────────────────────────────────────
    const int scaleId = m_scene.addGroup(SceneDocument::TreeNode::Scale);
    m_scene.updateGroupTransform(scaleId, {}, {}, QVector3D(2, 1.5f, 1),
                                 QStringList{QStringLiteral("2"), QStringLiteral("1.5"), QStringLiteral("1")});
    {
        ShapeNode cyl; cyl.type = ShapeNode::Cylinder; cyl.radius = 5.0f; cyl.height = 30.0f;
        cyl.parameterExpressions = QStringList{QStringLiteral("5"), QStringLiteral("30")};
        m_scene.addShape(cyl, scaleId);
    }

    // ── module "gadget" ───────────────────────────────────────────────────────
    const int gadgetId = m_scene.addGroup(SceneDocument::TreeNode::Module);
    m_scene.setModuleName(gadgetId, QStringLiteral("gadget"));
    {
        const int paramR = m_scene.addVariableToModule(gadgetId, /*isParameter=*/true);
        m_scene.renameVariable(paramR, QStringLiteral("r"));
        m_scene.updateVariableExpression(paramR, QStringLiteral("5"));

        const int paramH = m_scene.addVariableToModule(gadgetId, /*isParameter=*/true);
        m_scene.renameVariable(paramH, QStringLiteral("h"));
        m_scene.updateVariableExpression(paramH, QStringLiteral("20"));

        ShapeNode cyl; cyl.type = ShapeNode::Cylinder; cyl.radius = 5.0f; cyl.height = 20.0f;
        cyl.parameterExpressions = QStringList{QStringLiteral("r"), QStringLiteral("h")};
        m_scene.addShape(cyl, gadgetId);
    }

    // ── module call: gadget(r=12, h=35) ──────────────────────────────────────
    m_scene.addModuleCall(gadgetId, /*parent=*/0, -1, QStringLiteral("r = 12, h = 35"));
}

// ─── nativeEvent for frameless drag on Windows ───────────────────────────────

bool TreeDebugWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
#ifdef Q_OS_WIN
    Q_UNUSED(eventType);
    MSG *msg = static_cast<MSG *>(message);
    if (!msg || msg->message != WM_NCHITTEST || isMaximized())
        return QMainWindow::nativeEvent(eventType, message, result);

    const QPoint pos = QPoint(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
    const QPoint local = mapFromGlobal(pos);
    constexpr int ResizeBorder = 6;
    if (local.x() < ResizeBorder && local.y() < ResizeBorder)          { *result = HTTOPLEFT;     return true; }
    if (local.x() > width() - ResizeBorder && local.y() < ResizeBorder) { *result = HTTOPRIGHT;    return true; }
    if (local.x() < ResizeBorder && local.y() > height() - ResizeBorder){ *result = HTBOTTOMLEFT;  return true; }
    if (local.x() > width() - ResizeBorder && local.y() > height() - ResizeBorder){ *result = HTBOTTOMRIGHT; return true; }
    if (local.y() < ResizeBorder)  { *result = HTTOP;    return true; }
    if (local.y() > height() - ResizeBorder) { *result = HTBOTTOM; return true; }
    if (local.x() < ResizeBorder)  { *result = HTLEFT;   return true; }
    if (local.x() > width() - ResizeBorder)  { *result = HTRIGHT;  return true; }
    return QMainWindow::nativeEvent(eventType, message, result);
#else
    return QMainWindow::nativeEvent(eventType, message, result);
#endif
}

void TreeDebugWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_treeWidget)
        m_treeWidget->refresh();
}
