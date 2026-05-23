#include "treedebugwindow.h"
#include "../../scenetreegraphicswidget.h"
#include "../../scenetreegraphicshelpers.h"
#include "../../scenetreelayout.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPoint>
#include <QPainter>
#include <QResizeEvent>
#include <QSplitter>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QScrollBar>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

using namespace SceneTreeGraphics;

// ─── Theme infrastructure ────────────────────────────────────────────────────

struct ThemeSpec
{
    QString id, label;
    QColor  window, surface, panel, panelAlt;
    QColor  text, mutedText, border, accent, accentHover, danger;
    QColor  titleBase, titlePulse;
};

static QVector<ThemeSpec> availableThemes()
{
    return {
        { QStringLiteral("dark-nord"),   QStringLiteral("Dark Nord"),
          QColor("#2e3440"), QColor("#252a34"), QColor("#3b4252"), QColor("#434c5e"),
          QColor("#eceff4"), QColor("#7f8898"), QColor("#4c566a"), QColor("#5e81ac"),
          QColor("#81a1c1"), QColor("#bf616a"), QColor("#111827"), QColor("#3b4252") },
        { QStringLiteral("graphite"),    QStringLiteral("Graphite"),
          QColor("#202124"), QColor("#17191c"), QColor("#2b2f34"), QColor("#353b42"),
          QColor("#eef1f4"), QColor("#8f98a3"), QColor("#4b5560"), QColor("#6ea8fe"),
          QColor("#8bbcff"), QColor("#e06c75"), QColor("#111315"), QColor("#303844") },
        { QStringLiteral("blueprint"),   QStringLiteral("Blueprint"),
          QColor("#17202a"), QColor("#111820"), QColor("#223044"), QColor("#2f405a"),
          QColor("#e9f2ff"), QColor("#94a6bc"), QColor("#40556d"), QColor("#4ea1d3"),
          QColor("#78bee8"), QColor("#d66a6a"), QColor("#0b1320"), QColor("#243b55") },
        { QStringLiteral("olive-light"), QStringLiteral("Olive Light"),
          QColor("#eef0e6"), QColor("#f8f9f1"), QColor("#dde3cf"), QColor("#cfd8bd"),
          QColor("#23281c"), QColor("#65705a"), QColor("#a7b18f"), QColor("#6f8741"),
          QColor("#8fa85b"), QColor("#9f4f45"), QColor("#3b442e"), QColor("#586b3a") },
        { QStringLiteral("warm-light"),  QStringLiteral("Warm Light"),
          QColor("#f4f1ea"), QColor("#fffdf7"), QColor("#e8dfd1"), QColor("#d8cbbb"),
          QColor("#27221c"), QColor("#776a5d"), QColor("#b6a897"), QColor("#9b6b3f"),
          QColor("#bd8452"), QColor("#a85048"), QColor("#443326"), QColor("#6b4b35") },
    };
}

static ThemeSpec defaultTheme() { return availableThemes().first(); }
static QString   colorName(const QColor &c) { return c.name(QColor::HexRgb); }

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
        QTextEdit {
            background-color: __SURFACE__; color: __TEXT__;
            border: 1px solid __BORDER__; border-radius: 4px;
            font-family: "Consolas", "Courier New", monospace; font-size: 8pt; }
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
        QSplitter::handle { background-color: __BORDER__; }
        QSplitter::handle:hover { background-color: __ACCENT__; }
    )");
    s.replace(QStringLiteral("__WINDOW__"),       colorName(t.window));
    s.replace(QStringLiteral("__SURFACE__"),      colorName(t.surface));
    s.replace(QStringLiteral("__PANEL__"),        colorName(t.panel));
    s.replace(QStringLiteral("__PANEL_ALT__"),    colorName(t.panelAlt));
    s.replace(QStringLiteral("__TEXT__"),         colorName(t.text));
    s.replace(QStringLiteral("__MUTED__"),        colorName(t.mutedText));
    s.replace(QStringLiteral("__BORDER__"),       colorName(t.border));
    s.replace(QStringLiteral("__ACCENT__"),       colorName(t.accent));
    s.replace(QStringLiteral("__ACCENT_HOVER__"), colorName(t.accentHover));
    s.replace(QStringLiteral("__DANGER__"),       colorName(t.danger));
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

// ─── Animated title bar ───────────────────────────────────────────────────────

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
            b->setFixedSize(36, 24); b->setAutoRaise(true); b->setFocusPolicy(Qt::NoFocus);
        }
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 4, 0); layout->setSpacing(4);
        layout->addWidget(m_titleLabel, 1);
        layout->addWidget(m_minimizeButton);
        layout->addWidget(m_maximizeButton);
        layout->addWidget(m_closeButton);

        m_pulse->setDuration(1200); m_pulse->setLoopCount(-1);
        m_pulse->setEasingCurve(QEasingCurve::InOutSine);
        m_pulse->setStartValue(0.0); m_pulse->setKeyValueAt(0.5, 1.0); m_pulse->setEndValue(0.0);
        connect(m_pulse, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_currentColor = mix(m_baseColor, m_pulseColor, v.toReal()); update();
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
        m_baseColor = t.titleBase; m_pulseColor = t.titlePulse;
        m_currentColor = m_baseColor; m_textColor = t.text;
        m_borderColor = t.border; m_accentColor = t.accent; m_dangerColor = t.danger;
        const QString btn = QStringLiteral(
            "QToolButton{background:transparent;color:%1;border:none;border-radius:5px;padding:0;font-weight:600}"
            "QToolButton:hover{background:%2;color:#fff}"
            "QToolButton:pressed{background:%3;color:#fff}")
            .arg(colorName(t.text), colorName(t.accent), colorName(t.accentHover));
        const QString close = QStringLiteral(
            "QToolButton{background:transparent;color:%1;border:none;border-radius:5px;padding:0;font-weight:700}"
            "QToolButton:hover{background:%2;color:#fff}"
            "QToolButton:pressed{background:%3;color:#fff}")
            .arg(colorName(t.text), colorName(t.danger), colorName(t.accentHover));
        m_titleLabel->setStyleSheet(
            QStringLiteral("background:transparent;color:%1;font-weight:600").arg(colorName(t.text)));
        m_minimizeButton->setStyleSheet(btn); m_maximizeButton->setStyleSheet(btn);
        m_closeButton->setStyleSheet(close); update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.fillRect(rect(), m_currentColor);
        QColor line = m_borderColor; line.setAlpha(180);
        p.setPen(QPen(line, 1));
        p.drawLine(QPoint(0, height()-1), QPoint(width(), height()-1));
    }
    void enterEvent(QEvent *) override { m_pulse->start(); }
    void leaveEvent(QEvent *) override { m_pulse->stop(); m_currentColor = m_baseColor; update(); }
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragOffset = e->globalPos() - window()->frameGeometry().topLeft();
            e->accept(); return;
        }
        QWidget::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (m_dragging && (e->buttons() & Qt::LeftButton)) {
            QWidget *w = window();
            if (w && !w->isMaximized()) w->move(e->globalPos() - m_dragOffset);
            e->accept(); return;
        }
        QWidget::mouseMoveEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override { m_dragging = false; QWidget::mouseReleaseEvent(e); }
    void mouseDoubleClickEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) { toggleMaximized(); e->accept(); return; }
        QWidget::mouseDoubleClickEvent(e);
    }

private:
    static QColor mix(const QColor &a, const QColor &b, qreal t) {
        return QColor::fromRgbF(a.redF()+(b.redF()-a.redF())*t,
                                a.greenF()+(b.greenF()-a.greenF())*t,
                                a.blueF()+(b.blueF()-a.blueF())*t);
    }
    void toggleMaximized() {
        if (QWidget *w = window()) w->isMaximized() ? w->showNormal() : w->showMaximized();
    }
    QLabel *m_titleLabel; QToolButton *m_minimizeButton, *m_maximizeButton, *m_closeButton;
    QVariantAnimation *m_pulse;
    QColor m_baseColor{QStringLiteral("#111827")}, m_pulseColor{QStringLiteral("#3b4252")};
    QColor m_currentColor{QStringLiteral("#111827")}, m_textColor{QStringLiteral("#eceff4")};
    QColor m_borderColor{QStringLiteral("#4c566a")}, m_accentColor{QStringLiteral("#5e81ac")};
    QColor m_dangerColor{QStringLiteral("#bf616a")};
    bool   m_dragging = false;
    QPoint m_dragOffset;
};

// ─── DebugTreeWidget — thin SceneTreeGraphicsWidget subclass ─────────────────
// Captures cursor position and exposes it via callback.

class DebugTreeWidget : public SceneTreeGraphicsWidget
{
public:
    explicit DebugTreeWidget(QWidget *parent = nullptr) : SceneTreeGraphicsWidget(parent) {}

    std::function<void(QPoint, QPointF)> onMouseMoved;

protected:
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (onMouseMoved)
            onMouseMoved(event->pos(), mapToScene(event->pos()));
        SceneTreeGraphicsWidget::mouseMoveEvent(event);
    }
};

// ─── TreeDebugWindow ──────────────────────────────────────────────────────────

TreeDebugWindow::TreeDebugWindow(QWidget *parent)
    : QMainWindow(parent)
{
    applyTheme(defaultTheme());
    buildUi();
    buildTestDocument();
    m_treeWidget->setSceneDocument(&m_scene);
    m_treeWidget->refresh();
    logSnapshot(QStringLiteral("initial build"));
}

// ─── formatRect helpers ───────────────────────────────────────────────────────

QString TreeDebugWindow::formatRect(const QRectF &r) const
{
    return QStringLiteral("(%1, %2, %3, %4)")
        .arg(r.x(), 0, 'f', 1).arg(r.y(), 0, 'f', 1)
        .arg(r.width(), 0, 'f', 1).arg(r.height(), 0, 'f', 1);
}

QString TreeDebugWindow::formatRectShort(const QRectF &r) const
{
    return QStringLiteral("(%1,%2 %3×%4)")
        .arg(qRound(r.x())).arg(qRound(r.y()))
        .arg(qRound(r.width())).arg(qRound(r.height()));
}

// ─── log() ───────────────────────────────────────────────────────────────────
// Prepends to the debug log (newest at top).

void TreeDebugWindow::log(const QString &line)
{
    if (!m_debugLog) return;
    // Prepend by inserting at top of document.
    QTextCursor c = m_debugLog->textCursor();
    c.movePosition(QTextCursor::Start);
    m_debugLog->setTextCursor(c);
    m_debugLog->insertPlainText(line + QStringLiteral("\n"));
}

void TreeDebugWindow::logCursor(const QPoint &widget, const QPointF &scene)
{
    m_lastWidget = widget;
    m_lastScene  = scene;
    // Throttle cursor spam: only log every ~10px movement.
    // (We store last, the scroll events reference it.)
}

// ─── Snapshot formatting ──────────────────────────────────────────────────────

// Returns a short type label for a tree node.
static QString nodeTypeLabel(const SceneDocument::TreeNode &node)
{
    using N = SceneDocument::TreeNode;
    if (node.type == N::Variable)
        return QStringLiteral("Var");
    if (node.type == N::ModuleCall)
        return QStringLiteral("Call");
    if (node.type == N::Primitive)
        return QStringLiteral("Prim");
    // Group
    switch (node.operation) {
    case N::Module:       return QStringLiteral("Module");
    case N::Scene:        return QStringLiteral("Scene");
    case N::Union:        return QStringLiteral("Union");
    case N::Difference:   return QStringLiteral("Diff");
    case N::Intersection: return QStringLiteral("Inter");
    case N::Translate:    return QStringLiteral("Trans");
    case N::Rotate:       return QStringLiteral("Rot");
    case N::Scale:        return QStringLiteral("Scale");
    case N::For:          return QStringLiteral("For");
    default:              return QStringLiteral("?");
    }
}

// Formats a pill/control list inline.
static QString formatControls(const QVector<ExpressionNumberControl> &ctrls)
{
    if (ctrls.isEmpty()) return QStringLiteral("  (no number controls)");
    QString out;
    for (const auto &c : ctrls) {
        const QRectF &r = c.rect;
        out += QStringLiteral("  pill \"%1\"  start=%2 len=%3"
                              "  hit=(%4,%5 %6×%7)\n")
            .arg(c.text).arg(c.start).arg(c.length)
            .arg(qRound(r.x())).arg(qRound(r.y()))
            .arg(qRound(r.width())).arg(qRound(r.height()));
    }
    return out.chopped(1); // remove trailing \n
}

// ─── debugBuildModuleCallParams ──────────────────────────────────────────────
// Builds a ModuleCallParam list for a ModuleCall node by:
//   1. Walking the referenced module group to collect its parameter variables
//      (in definition order).
//   2. Splitting moduleCallArguments at top-level commas → "name = expr" pairs.
//   3. Using each param's call-site expression (falling back to the default).
//
// This replicates the logic of the private resolveModuleArguments + split helpers
// that live inside scenetreegraphicswidget.cpp, for use in the debug window only.

static QVector<SceneTreeGraphics::ModuleCallParam> debugBuildModuleCallParams(
    const SceneDocument &scene, const SceneDocument::TreeNode &callNode)
{
    using N = SceneDocument::TreeNode;
    using Param = SceneTreeGraphics::ModuleCallParam;
    QVector<Param> result;

    // For a ModuleCall node, shapeId holds the referenced module group id.
    const N *modGroup = scene.treeNodeById(callNode.shapeId);
    if (!modGroup) return result;

    // Collect parameter variable nodes in definition order.
    QVector<const N *> paramNodes;
    for (const N &child : modGroup->children)
        if (child.type == N::Variable && child.isParameter)
            paramNodes.append(&child);

    // Split the argument string at top-level commas.
    // Bracket/paren depth prevents splitting inside nested expressions.
    const QString &args = callNode.moduleCallArguments;
    QStringList parts;
    {
        int depth = 0, start = 0;
        for (int i = 0; i < args.size(); ++i) {
            const QChar c = args[i];
            if (c == QLatin1Char('[') || c == QLatin1Char('(')) ++depth;
            else if (c == QLatin1Char(']') || c == QLatin1Char(')')) --depth;
            else if (c == QLatin1Char(',') && depth == 0) {
                parts << args.mid(start, i - start).trimmed();
                start = i + 1;
            }
        }
        const QString last = args.mid(start).trimmed();
        if (!last.isEmpty()) parts << last;
    }

    // Build a name → expression map.
    QHash<QString, QString> argMap;
    for (const QString &part : parts) {
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq < 0) continue;
        argMap.insert(part.left(eq).trimmed(), part.mid(eq + 1).trimmed());
    }

    // Build final params in module-definition order.
    for (const N *pn : paramNodes) {
        Param p;
        p.varNodeId  = pn->id;
        p.name       = pn->variableName;
        p.expression = argMap.value(pn->variableName, pn->variableExpression);
        result.append(p);
    }
    return result;
}

void TreeDebugWindow::appendSnapshotForNode(QString &out,
                                            const SceneDocument::TreeNode &node,
                                            int depth,
                                            const QString &indent) const
{
    using N = SceneDocument::TreeNode;
    const QFontMetricsF fm(sceneTreeGraphicsFont());

    const QString typeLabel = nodeTypeLabel(node);

    if (node.type == N::Variable) {
        const QRectF rect = m_treeWidget->debugChildRect(node.id);
        const qreal nameW = fm.horizontalAdvance(node.variableName);
        const auto ctrls  = expressionNumberControls(rect, node.variableExpression, fm, nameW);
        out += indent + QStringLiteral("[%1#%2  %3]  card=%4\n")
            .arg(typeLabel).arg(node.id)
            .arg(node.isParameter
                 ? QStringLiteral("param %1 = %2").arg(node.variableName, node.variableExpression)
                 : QStringLiteral("%1 = %2").arg(node.variableName, node.variableExpression))
            .arg(formatRectShort(rect));
        if (!ctrls.isEmpty())
            out += indent + formatControls(ctrls) + QStringLiteral("\n");
        return;
    }

    if (node.type == N::Primitive) {
        const QRectF rect = m_treeWidget->debugChildRect(node.id);
        const ShapeNode *shape = m_scene.shapeById(node.shapeId);
        const QString shapeDesc = shape
            ? (shape->type == ShapeNode::Cube     ? QStringLiteral("cube")
             : shape->type == ShapeNode::Sphere   ? QStringLiteral("sphere")
                                                  : QStringLiteral("cylinder"))
            : QStringLiteral("?");
        out += indent + QStringLiteral("[Prim#%1  %2]  card=%3\n")
            .arg(node.id).arg(shapeDesc).arg(formatRectShort(rect));
        if (shape) {
            const auto paramControls = shapeParameterControls(*shape);
            for (int i = 0; i < paramControls.size(); ++i) {
                const auto ctrls = shapeParameterNumberControls(
                    rect, i, paramControls.size(), paramControls[i].expression, fm);
                if (!ctrls.isEmpty())
                    out += indent + QStringLiteral("  [%1] ").arg(paramControls[i].label)
                        + formatControls(ctrls) + QStringLiteral("\n");
            }
        }
        return;
    }

    if (node.type == N::ModuleCall) {
        const QRectF rect = m_treeWidget->debugChildRect(node.id);
        out += indent + QStringLiteral("[Call#%1  \"%2\"(%3)]  card=%4\n")
            .arg(node.id).arg(node.moduleName).arg(node.moduleCallArguments)
            .arg(formatRectShort(rect));
        const auto params = debugBuildModuleCallParams(m_scene, node);
        const auto ctrls  = moduleCallParamControls(rect, node.moduleName, params, fm);
        for (const auto &c : ctrls) {
            QString pName, pExpr;
            for (const auto &p : params)
                if (p.varNodeId == c.paramVarNodeId) { pName = p.name; pExpr = p.expression; break; }
            const QString numText = pExpr.mid(c.numberStart, c.numberLength);
            const QRectF &r = c.rect;
            out += indent + QStringLiteral("  pill \"%1\" [%2 var#%3]  start=%4 len=%5  hit=(%6,%7 %8×%9)\n")
                .arg(numText).arg(pName).arg(c.paramVarNodeId)
                .arg(c.numberStart).arg(c.numberLength)
                .arg(qRound(r.x())).arg(qRound(r.y()))
                .arg(qRound(r.width())).arg(qRound(r.height()));
        }
        return;
    }

    // ── Group node ────────────────────────────────────────────────────────────
    const QRectF groupRect = m_treeWidget->debugGroupRect(node.id);

    if (node.operation == N::Scene) {
        out += indent + QStringLiteral("[Scene#%1]  card=%2\n")
            .arg(node.id).arg(formatRectShort(groupRect));
    } else if (node.operation == N::Module) {
        out += indent + QStringLiteral("[Module#%1  \"%2\"]  card=%3\n")
            .arg(node.id).arg(node.moduleName).arg(formatRectShort(groupRect));
    } else if (node.operation == N::For) {
        const QString varName  = forLoopVariableName(node);
        const QString rangeExp = forLoopRangeExpression(node);
        // rangeExp already contains the brackets (e.g. "[0 : 10 : 100]").
        out += indent + QStringLiteral("[For#%1  %2=%3]  card=%4\n")
            .arg(node.id).arg(varName).arg(rangeExp).arg(formatRectShort(groupRect));
        const auto ctrls = forLoopRangeNumberControls(groupRect, varName, rangeExp, fm);
        for (const auto &c : ctrls) {
            const QRectF &r = c.rect;
            out += indent + QStringLiteral("  pill \"%1\"  start=%2 len=%3  hit=(%4,%5 %6×%7)\n")
                .arg(c.text).arg(c.start).arg(c.length)
                .arg(qRound(r.x())).arg(qRound(r.y()))
                .arg(qRound(r.width())).arg(qRound(r.height()));
        }
    } else if (isTransformOperation(node.operation)) {
        const qreal hw = transformHeaderWidthForNode(node);
        out += indent + QStringLiteral("[%1#%2]  card=%3\n")
            .arg(typeLabel).arg(node.id).arg(formatRectShort(groupRect));
        const char *axes[] = {"X", "Y", "Z"};
        for (int ax = 0; ax < 3; ++ax) {
            const QString expr = transformAxisExpression(node, ax);
            const auto ctrls = transformParameterNumberControls(groupRect, ax, expr, fm, hw);
            if (!ctrls.isEmpty())
                out += indent + QStringLiteral("  %1 ").arg(QLatin1String(axes[ax]))
                    + formatControls(ctrls) + QStringLiteral("\n");
        }
    } else {
        out += indent + QStringLiteral("[%1#%2]  card=%3\n")
            .arg(typeLabel).arg(node.id).arg(formatRectShort(groupRect));
    }

    // ── Recurse into children ─────────────────────────────────────────────────
    const QString childIndent = indent + QStringLiteral("  ");
    for (const SceneDocument::TreeNode &child : node.children)
        appendSnapshotForNode(out, child, depth + 1, childIndent);
}

void TreeDebugWindow::logSnapshot(const QString &context)
{
    QString out;
    out += QStringLiteral("=== Snapshot #%1 (%2) ===\n")
        .arg(++m_eventSeq).arg(context);
    out += QStringLiteral("shapes=%1\n").arg(m_scene.shapeCount());

    appendSnapshotForNode(out, m_scene.treeRoot(), 0, QStringLiteral(""));

    out += QStringLiteral("---\n");
    log(out);
}

// ─── buildUi() ────────────────────────────────────────────────────────────────

void TreeDebugWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Scene Tree Debugger"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    resize(900, 800);

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
    auto *debugMenu = appMenuBar->addMenu(QStringLiteral("Debug"));
    {
        QAction *snapshotAction = debugMenu->addAction(QStringLiteral("Log Snapshot Now"));
        connect(snapshotAction, &QAction::triggered, this, [this]() {
            logSnapshot(QStringLiteral("manual"));
        });
        QAction *clearAction = debugMenu->addAction(QStringLiteral("Clear Log"));
        connect(clearAction, &QAction::triggered, this, [this]() {
            if (m_debugLog) m_debugLog->clear();
            m_eventSeq = 0;
        });
    }

    auto *chrome       = new QWidget(this);
    auto *chromeLayout = new QVBoxLayout(chrome);
    chromeLayout->setContentsMargins(0, 0, 0, 0);
    chromeLayout->setSpacing(0);
    chromeLayout->addWidget(titleBar);
    chromeLayout->addWidget(appMenuBar);
    setMenuWidget(chrome);

    // ── Main splitter: tree (left) + debug log (right) ────────────────────────
    m_treeWidget = new DebugTreeWidget(this);
    m_treeWidget->setMinimumWidth(300);

    m_debugLog = new QTextEdit(this);
    m_debugLog->setReadOnly(true);
    m_debugLog->setMinimumWidth(320);
    m_debugLog->setPlaceholderText(QStringLiteral("Debug log — events appear here (newest at top).\n"
                                                   "Copy and paste to share with Claude."));

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_treeWidget);
    splitter->addWidget(m_debugLog);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({500, 380});
    setCentralWidget(splitter);

    // ── Wire DebugTreeWidget callbacks ────────────────────────────────────────
    m_treeWidget->onMouseMoved = [this](QPoint widget, QPointF scene) {
        logCursor(widget, scene);
    };

    connect(m_treeWidget, &SceneTreeGraphicsWidget::toolDropped,
            this, [this](const QString &tool, int parentId, int idx) {
        // idx < -100000 → insert into module parameter zone (idx = -100000 - insertIndex)
        const int insertIndex = (idx < -100000) ? (-idx - 100000) : idx;

        // Create the actual node in the scene document.
        if (tool == QStringLiteral("cube")) {
            ShapeNode s; s.type = ShapeNode::Cube; s.size = QVector3D(10,10,10);
            s.parameterExpressions = QStringList{QStringLiteral("10"),QStringLiteral("10"),QStringLiteral("10")};
            m_scene.addShape(s, parentId, insertIndex);
        } else if (tool == QStringLiteral("sphere")) {
            ShapeNode s; s.type = ShapeNode::Sphere; s.radius = 5.0f;
            s.parameterExpressions = QStringList{QStringLiteral("5")};
            m_scene.addShape(s, parentId, insertIndex);
        } else if (tool == QStringLiteral("cylinder")) {
            ShapeNode s; s.type = ShapeNode::Cylinder; s.radius = 5.0f; s.height = 10.0f;
            s.parameterExpressions = QStringList{QStringLiteral("5"),QStringLiteral("10")};
            m_scene.addShape(s, parentId, insertIndex);
        } else if (tool == QStringLiteral("module")) {
            const int gId = m_scene.addGroup(SceneDocument::TreeNode::Module, 0, insertIndex);
            m_scene.setModuleName(gId, QStringLiteral("module"));
        } else if (isVariableToolName(tool)) {
            // "var" → plain variable; "par" → parameter.
            // If the drop target is a Module group, use addVariableToModule so the
            // variable ends up inside that module.  Otherwise fall back to addVariable
            // which inserts into the scene root.
            const SceneDocument::TreeNode *parentNode = m_scene.treeNodeById(parentId);
            const bool intoModule = parentNode
                                    && parentNode->operation == SceneDocument::TreeNode::Module;
            if (intoModule) {
                const bool isParam = (tool == QStringLiteral("par"));
                const int varId = m_scene.addVariableToModule(parentId, isParam, insertIndex);
                m_scene.updateVariableExpression(varId, QStringLiteral("0"));
            } else {
                const int varId = m_scene.addVariable(insertIndex);
                m_scene.updateVariableExpression(varId, QStringLiteral("0"));
            }
        } else {
            // Boolean / transform / for group
            SceneDocument::TreeNode::Operation op = SceneDocument::TreeNode::Union;
            if      (tool == QStringLiteral("difference"))   op = SceneDocument::TreeNode::Difference;
            else if (tool == QStringLiteral("intersection")) op = SceneDocument::TreeNode::Intersection;
            else if (tool == QStringLiteral("translate"))    op = SceneDocument::TreeNode::Translate;
            else if (tool == QStringLiteral("rotate"))       op = SceneDocument::TreeNode::Rotate;
            else if (tool == QStringLiteral("scale"))        op = SceneDocument::TreeNode::Scale;
            else if (tool == QStringLiteral("for"))          op = SceneDocument::TreeNode::For;
            const int gId = m_scene.addGroup(op, parentId, insertIndex);
            if (op == SceneDocument::TreeNode::Translate
             || op == SceneDocument::TreeNode::Rotate
             || op == SceneDocument::TreeNode::Scale) {
                m_scene.updateGroupTransform(gId, {}, {}, QVector3D(1,1,1),
                                             {QStringLiteral("0"),QStringLiteral("0"),QStringLiteral("0")});
            } else if (op == SceneDocument::TreeNode::For) {
                m_scene.updateForLoop(gId, QStringLiteral("i"), QStringLiteral("[0 : 1 : 10]"));
            }
        }

        log(QStringLiteral("[%1] toolDrop  \"%2\"  parent=#%3  idx=%4")
            .arg(++m_eventSeq, 4, 10, QLatin1Char('0')).arg(tool).arg(parentId).arg(insertIndex));
        m_treeWidget->refresh();
        logSnapshot(QStringLiteral("after toolDrop"));
    });
    connect(m_treeWidget, &SceneTreeGraphicsWidget::treeNodeDropped,
            this, [this](int nodeId, int parentId, int idx) {
        const bool ok = m_scene.moveTreeNode(nodeId, parentId, idx);
        log(QStringLiteral("[%1] nodeDrop  node=#%2  parent=#%3  idx=%4%5")
            .arg(++m_eventSeq, 4, 10, QLatin1Char('0'))
            .arg(nodeId).arg(parentId).arg(idx)
            .arg(ok ? QString() : QStringLiteral("  ⚠ moveTreeNode failed")));
        m_treeWidget->refresh();
        logSnapshot(QStringLiteral("after nodeDrop"));
    });
    connect(m_treeWidget, &SceneTreeGraphicsWidget::moduleCallDropped,
            this, [this](int moduleGroupId, int parentId, int idx) {
        const int callId = m_scene.addModuleCall(moduleGroupId, parentId, idx);
        const bool ok = callId > 0;
        log(QStringLiteral("[%1] callDrop  module=#%2  parent=#%3  idx=%4  callId=#%5%6")
            .arg(++m_eventSeq, 4, 10, QLatin1Char('0'))
            .arg(moduleGroupId).arg(parentId).arg(idx).arg(callId)
            .arg(ok ? QString() : QStringLiteral("  ⚠ addModuleCall failed")));
        m_treeWidget->refresh();
        logSnapshot(QStringLiteral("after callDrop"));
    });
    connect(m_treeWidget, &SceneTreeGraphicsWidget::treeNodeSelected,
            this, [this](int nodeId) {
        log(QStringLiteral("[%1] select    node=#%2  cursor=(%3,%4)")
            .arg(++m_eventSeq, 4, 10, QLatin1Char('0')).arg(nodeId)
            .arg(qRound(m_lastScene.x())).arg(qRound(m_lastScene.y())));
    });
    connect(m_treeWidget, &SceneTreeGraphicsWidget::treeNodeDeleteRequested,
            this, [this](int nodeId) {
        log(QStringLiteral("[%1] delete    node=#%2")
            .arg(++m_eventSeq, 4, 10, QLatin1Char('0')).arg(nodeId));
        m_treeWidget->refresh();
        logSnapshot(QStringLiteral("after delete"));
    });

    connect(m_treeWidget, &SceneTreeGraphicsWidget::transformValueAdjusted,
            this, [this](int groupId, int axis, int start, int len, qreal delta) {
            const SceneDocument::TreeNode *node = m_scene.treeNodeById(groupId);
            if (!node) return;
            const QString axisName = QStringList{QStringLiteral("X"),
                                                  QStringLiteral("Y"),
                                                  QStringLiteral("Z")}[axis];
            const QString oldExpr = transformAxisExpression(*node, axis);
            const QFontMetricsF fm(sceneTreeGraphicsFont());
            const qreal hw = transformHeaderWidthForNode(*node);
            const auto ctrls = transformParameterNumberControls(
                m_treeWidget->debugGroupRect(groupId), axis, oldExpr, fm, hw);
            QRectF pillRect;
            for (const auto &c : ctrls) if (c.start == start) { pillRect = c.rect; break; }

            // Apply update
            QVector3D pos = node->position, rot = node->rotation, scl = node->scale;
            QStringList exprs = node->transformExpressions;
            if (exprs.size() < 3)
                exprs = QStringList{QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0")};
            bool ok = false;
            const qreal cur  = exprs[axis].toDouble(&ok);
            const qreal next = (ok ? cur : 0.0) + delta;
            const QString newStr = QString::number(next, 'f', 1);
            exprs[axis] = newStr;
            m_scene.updateGroupTransform(groupId, pos, rot, scl, exprs);

            log(QStringLiteral("[%1] transform node=#%2 axis=%3  \"%4\"→\"%5\"  Δ=%6"
                               "  start=%7 len=%8"
                               "  pill=(%9,%10 %11×%12)"
                               "  cursor=(%13,%14)")
                .arg(++m_eventSeq, 4, 10, QLatin1Char('0'))
                .arg(groupId).arg(axisName)
                .arg(oldExpr.mid(start, len), newStr)
                .arg(delta, 0, 'f', 1)
                .arg(start).arg(len)
                .arg(qRound(pillRect.x())).arg(qRound(pillRect.y()))
                .arg(qRound(pillRect.width())).arg(qRound(pillRect.height()))
                .arg(qRound(m_lastScene.x())).arg(qRound(m_lastScene.y())));

            m_treeWidget->refresh();
            logSnapshot(QStringLiteral("after transform"));
    });

    connect(m_treeWidget, &SceneTreeGraphicsWidget::forLoopRangeAdjusted,
            this, [this](int nodeId, int start, int len, qreal delta) {
            const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
            if (!node) return;
            const QString oldExpr = node->loopRangeExpression;
            const QString varName = forLoopVariableName(*node);
            const QFontMetricsF fm(sceneTreeGraphicsFont());
            const auto ctrls = forLoopRangeNumberControls(
                m_treeWidget->debugGroupRect(nodeId), varName, oldExpr, fm);
            QRectF pillRect;
            for (const auto &c : ctrls) if (c.start == start) { pillRect = c.rect; break; }

            // Apply update
            bool ok = false;
            const qreal cur   = oldExpr.mid(start, len).toDouble(&ok);
            const qreal next  = (ok ? cur : 0.0) + delta;
            const QString newNum = QString::number(next, 'f', 0);
            QString newExpr = oldExpr;
            newExpr.replace(start, len, newNum);
            m_scene.updateForLoop(nodeId, node->loopVariable, newExpr);

            log(QStringLiteral("[%1] for-loop  node=#%2  \"%3\"→\"%4\"  Δ=%5"
                               "  start=%6 len=%7"
                               "  pill=(%8,%9 %10×%11)"
                               "  cursor=(%12,%13)")
                .arg(++m_eventSeq, 4, 10, QLatin1Char('0'))
                .arg(nodeId)
                .arg(oldExpr.mid(start, len), newNum)
                .arg(delta, 0, 'f', 1)
                .arg(start).arg(len)
                .arg(qRound(pillRect.x())).arg(qRound(pillRect.y()))
                .arg(qRound(pillRect.width())).arg(qRound(pillRect.height()))
                .arg(qRound(m_lastScene.x())).arg(qRound(m_lastScene.y())));

            m_treeWidget->refresh();
            logSnapshot(QStringLiteral("after for-loop"));
    });

    connect(m_treeWidget, &SceneTreeGraphicsWidget::variableNumberAdjusted,
            this, [this](int nodeId, int start, int len, qreal delta) {
            const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
            if (!node) return;
            const QString oldExpr = node->variableExpression;
            const QFontMetricsF fm(sceneTreeGraphicsFont());
            const qreal nameW = fm.horizontalAdvance(node->variableName);
            const auto ctrls  = expressionNumberControls(
                m_treeWidget->debugChildRect(nodeId), oldExpr, fm, nameW);
            QRectF pillRect;
            for (const auto &c : ctrls) if (c.start == start) { pillRect = c.rect; break; }

            bool ok = false;
            const qreal cur  = oldExpr.mid(start, len).toDouble(&ok);
            const qreal next = (ok ? cur : 0.0) + delta;
            const QString newNum = QString::number(next, 'f', 1);
            QString newExpr = oldExpr;
            newExpr.replace(start, len, newNum);
            m_scene.updateVariableExpression(nodeId, newExpr);

            log(QStringLiteral("[%1] variable  node=#%2 \"%3\"  \"%4\"→\"%5\"  Δ=%6"
                               "  start=%7 len=%8"
                               "  pill=(%9,%10 %11×%12)"
                               "  cursor=(%13,%14)")
                .arg(++m_eventSeq, 4, 10, QLatin1Char('0'))
                .arg(nodeId).arg(node->variableName)
                .arg(oldExpr.mid(start, len), newNum)
                .arg(delta, 0, 'f', 1)
                .arg(start).arg(len)
                .arg(qRound(pillRect.x())).arg(qRound(pillRect.y()))
                .arg(qRound(pillRect.width())).arg(qRound(pillRect.height()))
                .arg(qRound(m_lastScene.x())).arg(qRound(m_lastScene.y())));

            m_treeWidget->refresh();
            logSnapshot(QStringLiteral("after variable"));
    });

    connect(m_treeWidget, &SceneTreeGraphicsWidget::shapeParameterAdjusted,
            this, [this](int nodeId, int param, int start, int len, qreal delta) {
            const SceneDocument::TreeNode *treeNode = m_scene.treeNodeById(nodeId);
            if (!treeNode) return;
            ShapeNode *shape = m_scene.shapeById(treeNode->shapeId);
            if (!shape || param >= shape->parameterExpressions.size()) return;
            const QString oldExpr = shape->parameterExpressions[param];
            const auto paramControls = shapeParameterControls(*shape);
            const QFontMetricsF fm(sceneTreeGraphicsFont());
            const auto ctrls = shapeParameterNumberControls(
                m_treeWidget->debugChildRect(nodeId), param, paramControls.size(), oldExpr, fm);
            QRectF pillRect;
            for (const auto &c : ctrls) if (c.start == start) { pillRect = c.rect; break; }

            bool ok = false;
            const qreal cur  = oldExpr.mid(start, len).toDouble(&ok);
            const qreal next = (ok ? cur : 0.0) + delta;
            const QString newNum = QString::number(next, 'f', 1);
            QString newExpr = oldExpr;
            newExpr.replace(start, len, newNum);
            shape->parameterExpressions[param] = newExpr;
            m_scene.updateShape(*shape);

            log(QStringLiteral("[%1] shape     node=#%2 param=%3  \"%4\"→\"%5\"  Δ=%6"
                               "  start=%7 len=%8"
                               "  pill=(%9,%10 %11×%12)"
                               "  cursor=(%13,%14)")
                .arg(++m_eventSeq, 4, 10, QLatin1Char('0'))
                .arg(nodeId).arg(param)
                .arg(oldExpr.mid(start, len), newNum)
                .arg(delta, 0, 'f', 1)
                .arg(start).arg(len)
                .arg(qRound(pillRect.x())).arg(qRound(pillRect.y()))
                .arg(qRound(pillRect.width())).arg(qRound(pillRect.height()))
                .arg(qRound(m_lastScene.x())).arg(qRound(m_lastScene.y())));

            m_treeWidget->refresh();
            logSnapshot(QStringLiteral("after shape"));
    });

    connect(m_treeWidget, &SceneTreeGraphicsWidget::moduleCallArgumentAdjusted,
            this, [this](int moduleCallId, int paramVarNodeId, int start, int len, qreal delta) {
            const SceneDocument::TreeNode *callNode = m_scene.treeNodeById(moduleCallId);
            if (!callNode) return;
            const SceneDocument::TreeNode *varNode = m_scene.treeNodeById(paramVarNodeId);
            const QString paramName = varNode ? varNode->variableName : QStringLiteral("?");

            const QFontMetricsF fm(sceneTreeGraphicsFont());
            const auto params = debugBuildModuleCallParams(m_scene, *callNode);
            const auto ctrls  = moduleCallParamControls(
                m_treeWidget->debugChildRect(moduleCallId), callNode->moduleName, params, fm);

            // Locate pill rect and old expression for this param / position.
            QRectF pillRect;
            QString paramExpr;
            for (const auto &p : params)
                if (p.varNodeId == paramVarNodeId) { paramExpr = p.expression; break; }
            for (const auto &c : ctrls)
                if (c.paramVarNodeId == paramVarNodeId && c.numberStart == start) {
                    pillRect = c.rect; break;
                }

            const QString oldNum = paramExpr.mid(start, len);
            bool ok = false;
            const qreal cur  = oldNum.toDouble(&ok);
            const qreal next = (ok ? cur : 0.0) + delta;
            const QString newNum = QString::number(next, 'f', 1);

            // Apply: replace the number in the param expression, then write back.
            QString newExpr = paramExpr;
            newExpr.replace(start, len, newNum);
            m_scene.updateModuleCallArgument(moduleCallId, paramName, newExpr);

            log(QStringLiteral("[%1] modcall   node=#%2 param=%3[var#%4]  \"%5\"→\"%6\"  Δ=%7"
                               "  start=%8 len=%9"
                               "  pill=(%10,%11 %12×%13)"
                               "  cursor=(%14,%15)")
                .arg(++m_eventSeq, 4, 10, QLatin1Char('0'))
                .arg(moduleCallId).arg(paramName).arg(paramVarNodeId)
                .arg(oldNum, newNum)
                .arg(delta, 0, 'f', 1)
                .arg(start).arg(len)
                .arg(qRound(pillRect.x())).arg(qRound(pillRect.y()))
                .arg(qRound(pillRect.width())).arg(qRound(pillRect.height()))
                .arg(qRound(m_lastScene.x())).arg(qRound(m_lastScene.y())));

            m_treeWidget->refresh();
            logSnapshot(QStringLiteral("after modcall"));
    });

    // ── Hover signals — log highlight rect vs cursor on every change ──────────

    // Plain hover (no Ctrl): the teal pill border that appears on mouseover.
    connect(m_treeWidget, &SceneTreeGraphicsWidget::hoverScrollZoneChanged,
            this, [this](const QRectF &pillRect) {
        if (!pillRect.isValid()) return;   // cleared — don't spam "nothing" entries
        log(QStringLiteral("[....] hover      pill=(%1,%2 %3×%4)  cursor=(%5,%6)")
            .arg(qRound(pillRect.x())).arg(qRound(pillRect.y()))
            .arg(qRound(pillRect.width())).arg(qRound(pillRect.height()))
            .arg(qRound(m_lastScene.x())).arg(qRound(m_lastScene.y())));
    });

    // Drop preview — fires every time cursor moves during drag; deduplicated on target change.
    connect(m_treeWidget, &SceneTreeGraphicsWidget::dropPreviewChanged,
            this, [this, lastParent = -2, lastIdx = -2](
                const QString &tool, int movingNodeId,
                const SceneTreeLayout::DropTarget &t, const QPointF &scenePos) mutable
        {
            const int curParent = t.hasTarget ? t.parentGroupId : -1;
            const int curIdx    = t.hasTarget ? t.insertIndex   : -1;
            if (curParent == lastParent && curIdx == lastIdx)
                return;   // same slot — skip
            lastParent = curParent;
            lastIdx    = curIdx;

            // scenePos is the actual cursor from this exact event (no lag).
            // For node drags: show both the node id AND the actual tool string
            // the drag handle passed to the renderer.  If tool != expected type
            // (e.g. "radius" instead of "var") the bug is immediately visible.
            const QString subject = movingNodeId > 0
                ? QStringLiteral("node#%1[%2]").arg(movingNodeId).arg(tool)
                : QStringLiteral("\"%1\"").arg(tool);

            if (!t.hasTarget) {
                log(QStringLiteral("[....] preview  %1  no-target  cursor=(%2,%3)")
                    .arg(subject)
                    .arg(qRound(scenePos.x())).arg(qRound(scenePos.y())));
                return;
            }

            // What group type will the preview land in?
            const SceneDocument::TreeNode *parent = m_scene.treeNodeById(t.parentGroupId);
            const QString parentType = parent ? nodeTypeLabel(*parent) : QStringLiteral("?");

            // group=   → the parent card that opens up to receive the item
            // marker=  → the thin slot-line drawn at the insertion point
            // ghost=   → the preview placeholder card rect
            log(QStringLiteral("[....] preview  %1  →%2#%3[%4]"
                               "  marker=(%5,%6 %7×%8)"
                               "  ghost=(%9,%10 %11×%12)"
                               "  group=(%13,%14 %15×%16)"
                               "  cursor=(%17,%18)")
                .arg(subject)
                .arg(parentType).arg(t.parentGroupId).arg(t.insertIndex)
                .arg(qRound(t.slotMarkerRect.x())).arg(qRound(t.slotMarkerRect.y()))
                .arg(qRound(t.slotMarkerRect.width())).arg(qRound(t.slotMarkerRect.height()))
                .arg(qRound(t.placeholderRect.x())).arg(qRound(t.placeholderRect.y()))
                .arg(qRound(t.placeholderRect.width())).arg(qRound(t.placeholderRect.height()))
                .arg(qRound(t.previewGroupRect.x())).arg(qRound(t.previewGroupRect.y()))
                .arg(qRound(t.previewGroupRect.width())).arg(qRound(t.previewGroupRect.height()))
                .arg(qRound(scenePos.x())).arg(qRound(scenePos.y())));
        });

    // Ctrl hover — transform axis
    connect(m_treeWidget, &SceneTreeGraphicsWidget::transformControlHovered,
            this, [this](int groupId, SceneDocument::TreeNode::Operation /*op*/, int axis) {
            if (groupId <= 0 || axis < 0) return;
            const SceneDocument::TreeNode *node = m_scene.treeNodeById(groupId);
            if (!node) return;
            const QString axisName = QStringList{QStringLiteral("X"),
                                                  QStringLiteral("Y"),
                                                  QStringLiteral("Z")}[axis];
            const QString expr = transformAxisExpression(*node, axis);
            const QFontMetricsF fm(sceneTreeGraphicsFont());
            const qreal hw = transformHeaderWidthForNode(*node);
            const auto ctrls = transformParameterNumberControls(
                m_treeWidget->debugGroupRect(groupId), axis, expr, fm, hw);
            QString pills;
            for (const auto &c : ctrls)
                pills += QStringLiteral("  \"%1\" hit=(%2,%3 %4×%5)")
                    .arg(c.text)
                    .arg(qRound(c.rect.x())).arg(qRound(c.rect.y()))
                    .arg(qRound(c.rect.width())).arg(qRound(c.rect.height()));
            log(QStringLiteral("[....] ctrl-hover  transform  node=#%1 %2 \"%3\"%4  cursor=(%5,%6)")
                .arg(groupId).arg(axisName).arg(expr).arg(pills)
                .arg(qRound(m_lastScene.x())).arg(qRound(m_lastScene.y())));
    });

    // Ctrl hover — shape parameter
    connect(m_treeWidget, &SceneTreeGraphicsWidget::shapeParameterHovered,
            this, [this](int shapeId, int paramIndex) {
            if (shapeId < 0 || paramIndex < 0) return;
            const ShapeNode *shape = m_scene.shapeById(shapeId);
            if (!shape) return;
            // Find tree node that owns this shape.
            int treeNodeId = 0;
            {
                std::function<void(const SceneDocument::TreeNode &)> find;
                find = [&](const SceneDocument::TreeNode &n) {
                    if (n.type == SceneDocument::TreeNode::Primitive && n.shapeId == shapeId)
                        treeNodeId = n.id;
                    for (const auto &ch : n.children) find(ch);
                };
                find(m_scene.treeRoot());
            }
            const auto paramControls = shapeParameterControls(*shape);
            if (paramIndex >= paramControls.size()) return;
            const QFontMetricsF fm(sceneTreeGraphicsFont());
            const auto ctrls = shapeParameterNumberControls(
                m_treeWidget->debugChildRect(treeNodeId),
                paramIndex, paramControls.size(),
                paramControls[paramIndex].expression, fm);
            QString pills;
            for (const auto &c : ctrls)
                pills += QStringLiteral("  \"%1\" hit=(%2,%3 %4×%5)")
                    .arg(c.text)
                    .arg(qRound(c.rect.x())).arg(qRound(c.rect.y()))
                    .arg(qRound(c.rect.width())).arg(qRound(c.rect.height()));
            log(QStringLiteral("[....] ctrl-hover  shape     node=#%1 param=%2 \"%3\"%4  cursor=(%5,%6)")
                .arg(treeNodeId).arg(paramIndex)
                .arg(paramControls[paramIndex].expression).arg(pills)
                .arg(qRound(m_lastScene.x())).arg(qRound(m_lastScene.y())));
    });

    // Ctrl hover — variable number
    connect(m_treeWidget, &SceneTreeGraphicsWidget::variableNumberHovered,
            this, [this](int nodeId, int numberStart) {
            if (nodeId <= 0 || numberStart < 0) return;
            const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
            if (!node) return;
            const QString expr = node->variableExpression;
            const QFontMetricsF fm(sceneTreeGraphicsFont());
            const qreal nameW = fm.horizontalAdvance(node->variableName);
            const auto ctrls = expressionNumberControls(
                m_treeWidget->debugChildRect(nodeId), expr, fm, nameW);
            QString pills;
            for (const auto &c : ctrls)
                pills += QStringLiteral("  \"%1\" hit=(%2,%3 %4×%5)")
                    .arg(c.text)
                    .arg(qRound(c.rect.x())).arg(qRound(c.rect.y()))
                    .arg(qRound(c.rect.width())).arg(qRound(c.rect.height()));
            log(QStringLiteral("[....] ctrl-hover  variable  node=#%1 \"%2\" expr=\"%3\"%4  cursor=(%5,%6)")
                .arg(nodeId).arg(node->variableName).arg(expr).arg(pills)
                .arg(qRound(m_lastScene.x())).arg(qRound(m_lastScene.y())));
    });

    // Ctrl hover — for-loop range
    connect(m_treeWidget, &SceneTreeGraphicsWidget::forLoopRangeHovered,
            this, [this](int nodeId, int numberStart) {
            if (nodeId <= 0 || numberStart < 0) return;
            const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
            if (!node) return;
            const QString varName  = forLoopVariableName(*node);
            const QString rangeExp = forLoopRangeExpression(*node);
            const QFontMetricsF fm(sceneTreeGraphicsFont());
            const auto ctrls = forLoopRangeNumberControls(
                m_treeWidget->debugGroupRect(nodeId), varName, rangeExp, fm);
            QString pills;
            for (const auto &c : ctrls)
                pills += QStringLiteral("  \"%1\" hit=(%2,%3 %4×%5)")
                    .arg(c.text)
                    .arg(qRound(c.rect.x())).arg(qRound(c.rect.y()))
                    .arg(qRound(c.rect.width())).arg(qRound(c.rect.height()));
            // rangeExp already contains brackets, e.g. "[0 : 10 : 100]"
            log(QStringLiteral("[....] ctrl-hover  for-loop  node=#%1 %2=%3%4  cursor=(%5,%6)")
                .arg(nodeId).arg(varName).arg(rangeExp).arg(pills)
                .arg(qRound(m_lastScene.x())).arg(qRound(m_lastScene.y())));
    });

    // Ctrl hover — module-call param
    connect(m_treeWidget, &SceneTreeGraphicsWidget::moduleCallParamHovered,
            this, [this](int callNodeId, int varNodeId, int numberStart) {
            if (callNodeId <= 0 || numberStart < 0) return;
            const SceneDocument::TreeNode *callNode = m_scene.treeNodeById(callNodeId);
            if (!callNode) return;
            const SceneDocument::TreeNode *varNode = m_scene.treeNodeById(varNodeId);
            const QString paramName = varNode ? varNode->variableName : QStringLiteral("?");

            const QFontMetricsF fm(sceneTreeGraphicsFont());
            const auto params = debugBuildModuleCallParams(m_scene, *callNode);
            const auto ctrls  = moduleCallParamControls(
                m_treeWidget->debugChildRect(callNodeId), callNode->moduleName, params, fm);

            // Show every pill rect so cursor vs hit-rect mismatches are visible.
            QString pills;
            for (const auto &c : ctrls) {
                QString pName, pExpr;
                for (const auto &p : params)
                    if (p.varNodeId == c.paramVarNodeId) { pName = p.name; pExpr = p.expression; break; }
                const QString mark = (c.paramVarNodeId == varNodeId && c.numberStart == numberStart)
                                     ? QStringLiteral("*") : QStringLiteral(" ");
                pills += QStringLiteral("  %1\"%2\"[%3] hit=(%4,%5 %6×%7)")
                    .arg(mark)
                    .arg(pExpr.mid(c.numberStart, c.numberLength)).arg(pName)
                    .arg(qRound(c.rect.x())).arg(qRound(c.rect.y()))
                    .arg(qRound(c.rect.width())).arg(qRound(c.rect.height()));
            }

            log(QStringLiteral("[....] ctrl-hover  modcall   node=#%1 param=%2[var#%3] start=%4%5  cursor=(%6,%7)")
                .arg(callNodeId).arg(paramName).arg(varNodeId).arg(numberStart)
                .arg(pills)
                .arg(qRound(m_lastScene.x())).arg(qRound(m_lastScene.y())));
    });
    connect(m_treeWidget, &SceneTreeGraphicsWidget::moduleRenameRequested,
            this, [this](int groupId, const QString &newName) {
        log(QStringLiteral("[%1] modRename  #%2 → \"%3\"")
            .arg(++m_eventSeq, 4, 10, QLatin1Char('0')).arg(groupId).arg(newName));
        m_scene.setModuleName(groupId, newName);
        m_treeWidget->refresh();
    });
    connect(m_treeWidget, &SceneTreeGraphicsWidget::variableRenameRequested,
            this, [this](int varId, const QString &newName) {
        log(QStringLiteral("[%1] varRename  #%2 → \"%3\"")
            .arg(++m_eventSeq, 4, 10, QLatin1Char('0')).arg(varId).arg(newName));
        m_scene.renameVariable(varId, newName);
        m_treeWidget->refresh();
    });

    // ── Canvas-drag / magnetic-snap debug ────────────────────────────────────
    connect(m_treeWidget, &SceneTreeGraphicsWidget::canvasDrag,
            this, [this](const QString &msg) {
        log(msg);
    });
}

// ─── buildTestDocument() ─────────────────────────────────────────────────────

void TreeDebugWindow::buildTestDocument()
{
    // Variables
    const int varRadiusId = m_scene.addVariable();
    m_scene.renameVariable(varRadiusId, QStringLiteral("radius"));
    m_scene.updateVariableExpression(varRadiusId, QStringLiteral("15"));

    const int varStepId = m_scene.addVariable();
    m_scene.renameVariable(varStepId, QStringLiteral("step"));
    m_scene.updateVariableExpression(varStepId, QStringLiteral("2.5"));

    // for (i = [0 : 10 : 100])
    const int forId1 = m_scene.addGroup(SceneDocument::TreeNode::For);
    m_scene.updateForLoop(forId1, QStringLiteral("i"), QStringLiteral("[0 : 10 : 100]"));
    {
        const int trans1 = m_scene.addGroup(SceneDocument::TreeNode::Translate, forId1);
        m_scene.updateGroupTransform(trans1, QVector3D(10, 0, 0), {}, QVector3D(1,1,1),
                                     QStringList{QStringLiteral("10"), QStringLiteral("0"), QStringLiteral("0")});
        ShapeNode cube; cube.type = ShapeNode::Cube; cube.size = QVector3D(20, 20, 20);
        cube.parameterExpressions = QStringList{QStringLiteral("20"), QStringLiteral("20"), QStringLiteral("20")};
        m_scene.addShape(cube, trans1);
    }

    // for (a = [0 : 2.5 : -30])
    const int forId2 = m_scene.addGroup(SceneDocument::TreeNode::For);
    m_scene.updateForLoop(forId2, QStringLiteral("a"), QStringLiteral("[0 : 2.5 : -30]"));
    {
        ShapeNode sphere; sphere.type = ShapeNode::Sphere; sphere.radius = 5.0f;
        sphere.parameterExpressions = QStringList{QStringLiteral("5")};
        m_scene.addShape(sphere, forId2);
    }

    // union
    const int unionId = m_scene.addGroup(SceneDocument::TreeNode::Union);
    {
        ShapeNode cube; cube.type = ShapeNode::Cube; cube.size = QVector3D(30, 20, 10);
        cube.parameterExpressions = QStringList{QStringLiteral("30"), QStringLiteral("20"), QStringLiteral("10")};
        m_scene.addShape(cube, unionId);
        ShapeNode sphere; sphere.type = ShapeNode::Sphere; sphere.radius = 15.0f;
        sphere.parameterExpressions = QStringList{QStringLiteral("15")};
        m_scene.addShape(sphere, unionId);
    }

    // difference
    const int diffId = m_scene.addGroup(SceneDocument::TreeNode::Difference);
    {
        ShapeNode cube; cube.type = ShapeNode::Cube; cube.size = QVector3D(40, 40, 20);
        cube.parameterExpressions = QStringList{QStringLiteral("40"), QStringLiteral("40"), QStringLiteral("20")};
        m_scene.addShape(cube, diffId);
        ShapeNode cyl; cyl.type = ShapeNode::Cylinder; cyl.radius = 8.0f; cyl.height = 25.0f;
        cyl.parameterExpressions = QStringList{QStringLiteral("8"), QStringLiteral("25")};
        m_scene.addShape(cyl, diffId);
    }

    // translate [5 10 0] → rotate [0 0 45] → cube
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

    // scale [2 1.5 1] → cylinder
    const int scaleId = m_scene.addGroup(SceneDocument::TreeNode::Scale);
    m_scene.updateGroupTransform(scaleId, {}, {}, QVector3D(2, 1.5f, 1),
                                 QStringList{QStringLiteral("2"), QStringLiteral("1.5"), QStringLiteral("1")});
    {
        ShapeNode cyl; cyl.type = ShapeNode::Cylinder; cyl.radius = 5.0f; cyl.height = 30.0f;
        cyl.parameterExpressions = QStringList{QStringLiteral("5"), QStringLiteral("30")};
        m_scene.addShape(cyl, scaleId);
    }

    // module "gadget" with params
    const int gadgetId = m_scene.addGroup(SceneDocument::TreeNode::Module);
    m_scene.setModuleName(gadgetId, QStringLiteral("gadget"));
    {
        const int paramR = m_scene.addVariableToModule(gadgetId, true);
        m_scene.renameVariable(paramR, QStringLiteral("r"));
        m_scene.updateVariableExpression(paramR, QStringLiteral("5"));

        const int paramH = m_scene.addVariableToModule(gadgetId, true);
        m_scene.renameVariable(paramH, QStringLiteral("h"));
        m_scene.updateVariableExpression(paramH, QStringLiteral("20"));

        ShapeNode cyl; cyl.type = ShapeNode::Cylinder; cyl.radius = 5.0f; cyl.height = 20.0f;
        cyl.parameterExpressions = QStringList{QStringLiteral("r"), QStringLiteral("h")};
        m_scene.addShape(cyl, gadgetId);
    }

    // module call
    m_scene.addModuleCall(gadgetId, 0, -1, QStringLiteral("r = 12, h = 35"));
}

// ─── nativeEvent (frameless resize on Windows) ───────────────────────────────

bool TreeDebugWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
#ifdef Q_OS_WIN
    Q_UNUSED(eventType);
    MSG *msg = static_cast<MSG *>(message);
    if (!msg || msg->message != WM_NCHITTEST || isMaximized())
        return QMainWindow::nativeEvent(eventType, message, result);
    const QPoint pos   = QPoint(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
    const QPoint local = mapFromGlobal(pos);
    constexpr int B = 6;
    if (local.x() < B && local.y() < B)                        { *result = HTTOPLEFT;     return true; }
    if (local.x() > width()-B && local.y() < B)                { *result = HTTOPRIGHT;    return true; }
    if (local.x() < B && local.y() > height()-B)               { *result = HTBOTTOMLEFT;  return true; }
    if (local.x() > width()-B && local.y() > height()-B)       { *result = HTBOTTOMRIGHT; return true; }
    if (local.y() < B)              { *result = HTTOP;    return true; }
    if (local.y() > height()-B)     { *result = HTBOTTOM; return true; }
    if (local.x() < B)              { *result = HTLEFT;   return true; }
    if (local.x() > width()-B)      { *result = HTRIGHT;  return true; }
    return QMainWindow::nativeEvent(eventType, message, result);
#else
    return QMainWindow::nativeEvent(eventType, message, result);
#endif
}

void TreeDebugWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_treeWidget) m_treeWidget->refresh();
}
