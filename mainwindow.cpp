#include "mainwindow.h"
#include "csgevaluator.h"
#include "expression.h"
#include "openscadgenerator.h"
#include "openscadparser.h"
#include "scenecommands.h"
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
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QHash>
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
#include <QStringList>
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

#include <functional>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

// ---------------- MainWindow ----------------

static constexpr int ModuleParameterInsertSentinel = -100000;

static bool decodeModuleParameterInsertIndex(int *insertIndex)
{
    if (!insertIndex || *insertIndex > ModuleParameterInsertSentinel)
        return false;

    *insertIndex = -ModuleParameterInsertSentinel - *insertIndex;
    return true;
}

static bool isStandaloneNumericToken(const QString &expression, int start, int length)
{
    if (start < 0 || length <= 0 || start + length > expression.size())
        return false;

    return expression.mid(start, length) == expression.trimmed();
}

static QString adjustedNumericToken(const QString &expression,
                                    int start,
                                    int length,
                                    qreal delta,
                                    qreal step,
                                    qreal minimumValue,
                                    bool clampMagnitude)
{
    const QString numberText = expression.mid(start, length);
    bool ok = false;
    const qreal value = numberText.toDouble(&ok);
    if (!ok)
        return QString();

    const int decimalPoint = numberText.indexOf(QLatin1Char('.'));
    const int precision = decimalPoint >= 0 ? qMin(3, numberText.size() - decimalPoint - 1) : 0;
    const qreal adjusted = value + delta * step;
    const qreal newValue = clampMagnitude ? qMax(minimumValue, adjusted) : adjusted;
    QString replacement = QString::number(newValue, 'f', precision);
    if (precision == 0 && replacement == QStringLiteral("-0"))
        replacement = QStringLiteral("0");
    return replacement;
}

static QStringList splitAtTopLevelCommas(const QString &text)
{
    QStringList result;
    int depth = 0;
    int start = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text[i];
        if (ch == QLatin1Char('(') || ch == QLatin1Char('['))
            ++depth;
        else if (ch == QLatin1Char(')') || ch == QLatin1Char(']'))
            --depth;
        else if (ch == QLatin1Char(',') && depth == 0) {
            const QString part = text.mid(start, i - start).trimmed();
            if (!part.isEmpty())
                result.append(part);
            start = i + 1;
        }
    }
    const QString tail = text.mid(start).trimmed();
    if (!tail.isEmpty())
        result.append(tail);
    return result;
}

static QHash<QString, QString> parseNamedArgumentExpressions(const QString &arguments)
{
    QHash<QString, QString> result;
    for (const QString &part : splitAtTopLevelCommas(arguments)) {
        const int equal = part.indexOf(QLatin1Char('='));
        if (equal <= 0)
            continue;

        const QString name = part.left(equal).trimmed();
        const QString expression = part.mid(equal + 1).trimmed();
        if (!name.isEmpty() && !expression.isEmpty())
            result[name] = expression;
    }
    return result;
}

// Resolves both named and positional call arguments against a module's parameter list.
static QHash<QString, QString> resolveModuleArguments(
    const QString &callArguments,
    const SceneDocument::TreeNode &moduleNode)
{
    QStringList paramOrder;
    for (const SceneDocument::TreeNode &child : moduleNode.children)
        if (child.type == SceneDocument::TreeNode::Variable && child.isParameter)
            paramOrder.append(child.variableName);

    QHash<QString, QString> result;
    int positionalIndex = 0;
    for (const QString &part : splitAtTopLevelCommas(callArguments)) {
        const int equal = part.indexOf(QLatin1Char('='));
        if (equal > 0) {
            const QString name = part.left(equal).trimmed();
            const QString expr  = part.mid(equal + 1).trimmed();
            if (!name.isEmpty() && !expr.isEmpty())
                result[name] = expr;
        } else {
            const QString expr = part.trimmed();
            if (!expr.isEmpty() && positionalIndex < paramOrder.size())
                result[paramOrder[positionalIndex]] = expr;
            ++positionalIndex;
        }
    }
    return result;
}

static float normalizedRotationDegrees(float value)
{
    while (value > 180.0f)
        value -= 360.0f;
    while (value < -180.0f)
        value += 360.0f;
    return value;
}

static QVector3D normalizedRotation(const QVector3D &rotation)
{
    return QVector3D(normalizedRotationDegrees(rotation.x()),
                     normalizedRotationDegrees(rotation.y()),
                     normalizedRotationDegrees(rotation.z()));
}

static ShapeNode makeShapeForTool(const QString &toolName, int shapeNumber)
{
    ShapeNode shape;
    shape.name = QString("%1 %2").arg(toolName.left(1).toUpper() + toolName.mid(1)).arg(shapeNumber);

    if (toolName == "sphere") {
        shape.type = ShapeNode::Sphere;
        shape.radius = 10.0f;
    } else if (toolName == "cylinder") {
        shape.type = ShapeNode::Cylinder;
        shape.radius = 10.0f;
        shape.height = 30.0f;
    } else {
        shape.type = ShapeNode::Cube;
        shape.size = QVector3D(20, 20, 20);
    }

    return shape;
}

static bool operationForTool(const QString &toolName, SceneDocument::TreeNode::Operation *operation)
{
    if (!operation)
        return false;

    if (toolName == "module") {
        *operation = SceneDocument::TreeNode::Module;
        return true;
    }

    if (toolName == "union") {
        *operation = SceneDocument::TreeNode::Union;
        return true;
    }

    if (toolName == "difference") {
        *operation = SceneDocument::TreeNode::Difference;
        return true;
    }

    if (toolName == "intersection") {
        *operation = SceneDocument::TreeNode::Intersection;
        return true;
    }
    if (toolName == "translate") {
        *operation = SceneDocument::TreeNode::Translate;
        return true;
    }
    if (toolName == "rotate") {
        *operation = SceneDocument::TreeNode::Rotate;
        return true;
    }
    if (toolName == "scale") {
        *operation = SceneDocument::TreeNode::Scale;
        return true;
    }
    if (toolName == "for") {
        *operation = SceneDocument::TreeNode::For;
        return true;
    }

    return false;
}

static bool isVariableTool(const QString &toolName)
{
    return toolName == QStringLiteral("var") || toolName == QStringLiteral("variable");
}

struct ThemeSpec
{
    QString id;
    QString label;
    QColor window;
    QColor surface;
    QColor panel;
    QColor panelAlt;
    QColor text;
    QColor mutedText;
    QColor border;
    QColor accent;
    QColor accentHover;
    QColor danger;
    QColor titleBase;
    QColor titlePulse;
};

static QVector<ThemeSpec> availableThemes()
{
    return {
        {
            QStringLiteral("dark-nord"),
            QStringLiteral("Dark Nord"),
            QColor("#2e3440"), QColor("#252a34"), QColor("#3b4252"), QColor("#434c5e"),
            QColor("#eceff4"), QColor("#7f8898"), QColor("#4c566a"), QColor("#5e81ac"),
            QColor("#81a1c1"), QColor("#bf616a"), QColor("#111827"), QColor("#3b4252")
        },
        {
            QStringLiteral("olive-light"),
            QStringLiteral("Olive Light"),
            QColor("#eef0e6"), QColor("#f8f9f1"), QColor("#dde3cf"), QColor("#cfd8bd"),
            QColor("#23281c"), QColor("#65705a"), QColor("#a7b18f"), QColor("#6f8741"),
            QColor("#8fa85b"), QColor("#9f4f45"), QColor("#3b442e"), QColor("#586b3a")
        },
        {
            QStringLiteral("graphite"),
            QStringLiteral("Graphite"),
            QColor("#202124"), QColor("#17191c"), QColor("#2b2f34"), QColor("#353b42"),
            QColor("#eef1f4"), QColor("#8f98a3"), QColor("#4b5560"), QColor("#6ea8fe"),
            QColor("#8bbcff"), QColor("#e06c75"), QColor("#111315"), QColor("#303844")
        },
        {
            QStringLiteral("blueprint"),
            QStringLiteral("Blueprint"),
            QColor("#17202a"), QColor("#111820"), QColor("#223044"), QColor("#2f405a"),
            QColor("#e9f2ff"), QColor("#94a6bc"), QColor("#40556d"), QColor("#4ea1d3"),
            QColor("#78bee8"), QColor("#d66a6a"), QColor("#0b1320"), QColor("#243b55")
        },
        {
            QStringLiteral("warm-light"),
            QStringLiteral("Warm Light"),
            QColor("#f4f1ea"), QColor("#fffdf7"), QColor("#e8dfd1"), QColor("#d8cbbb"),
            QColor("#27221c"), QColor("#776a5d"), QColor("#b6a897"), QColor("#9b6b3f"),
            QColor("#bd8452"), QColor("#a85048"), QColor("#443326"), QColor("#6b4b35")
        }
    };
}

static ThemeSpec defaultTheme()
{
    return availableThemes().first();
}

static QString colorName(const QColor &color)
{
    return color.name(QColor::HexRgb);
}

static QString themeStyleSheet(const ThemeSpec &theme)
{
    QString style = QStringLiteral(R"(
        QMainWindow,
        QWidget {
            background-color: __WINDOW__;
            color: __TEXT__;
            font-family: "Segoe UI";
            font-size: 10pt;
        }

        QMenuBar {
            background-color: __SURFACE__;
            color: __TEXT__;
            border-bottom: 1px solid __BORDER__;
            padding: 2px;
        }
        QMenuBar::item {
            background: transparent;
            padding: 4px 10px;
        }
        QMenuBar::item:selected,
        QMenuBar::item:pressed {
            background-color: __PANEL__;
            border-radius: 4px;
        }
        QMenu {
            background-color: __WINDOW__;
            color: __TEXT__;
            border: 1px solid __BORDER__;
            padding: 4px;
        }
        QMenu::item {
            padding: 5px 24px 5px 18px;
        }
        QMenu::item:selected {
            background-color: __ACCENT__;
            color: #ffffff;
        }
        QMenu::separator {
            height: 1px;
            background: __BORDER__;
            margin: 4px 8px;
        }

        QDockWidget {
            titlebar-close-icon: none;
            titlebar-normal-icon: none;
        }
        QDockWidget::title {
            background-color: __PANEL__;
            color: __TEXT__;
            padding: 5px 8px;
            border: 1px solid __BORDER__;
        }

        QLabel {
            background: transparent;
            color: __TEXT__;
        }
        QPushButton,
        QToolButton {
            background-color: __PANEL__;
            color: __TEXT__;
            border: 1px solid __BORDER__;
            border-radius: 5px;
            padding: 5px 10px;
            min-height: 22px;
        }
        QPushButton:hover,
        QToolButton:hover {
            background-color: __PANEL_ALT__;
            border-color: __ACCENT__;
        }
        QPushButton:pressed,
        QToolButton:pressed {
            background-color: __ACCENT__;
            color: #ffffff;
        }
        QPushButton:disabled,
        QToolButton:disabled {
            background-color: __PANEL__;
            color: __MUTED__;
            border-color: __BORDER__;
        }

        QTextEdit,
        QPlainTextEdit,
        QLineEdit,
        QAbstractSpinBox,
        QComboBox,
        QListView,
        QTreeView,
        QTableView {
            background-color: __SURFACE__;
            color: __TEXT__;
            selection-background-color: __ACCENT__;
            selection-color: #ffffff;
            border: 1px solid __BORDER__;
            border-radius: 4px;
            padding: 3px;
        }
        QTextEdit {
            font-family: "Consolas", "Courier New", monospace;
        }
        QAbstractSpinBox:disabled,
        QComboBox:disabled,
        QLineEdit:disabled {
            background-color: __PANEL__;
            color: __MUTED__;
            border-color: __BORDER__;
        }
        QComboBox::drop-down {
            width: 20px;
            border-left: 1px solid __BORDER__;
            background-color: __PANEL__;
        }
        QComboBox QAbstractItemView {
            background-color: __SURFACE__;
            color: __TEXT__;
            border: 1px solid __BORDER__;
            selection-background-color: __ACCENT__;
        }

        QGroupBox {
            color: __TEXT__;
            border: 1px solid __BORDER__;
            border-radius: 5px;
            margin-top: 10px;
            padding-top: 10px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 8px;
            padding: 0 4px;
            background-color: __WINDOW__;
        }

        QCheckBox {
            spacing: 6px;
            background: transparent;
        }
        QCheckBox::indicator {
            width: 14px;
            height: 14px;
            border: 1px solid __BORDER__;
            border-radius: 3px;
            background-color: __SURFACE__;
        }
        QCheckBox::indicator:checked {
            background-color: __ACCENT__;
            border-color: __ACCENT_HOVER__;
        }

        QSplitter::handle {
            background-color: __BORDER__;
        }
        QSplitter::handle:hover {
            background-color: __ACCENT__;
        }

        QScrollBar:vertical,
        QScrollBar:horizontal {
            background: __SURFACE__;
            border: none;
            margin: 0;
        }
        QScrollBar::handle:vertical,
        QScrollBar::handle:horizontal {
            background: __BORDER__;
            border-radius: 4px;
            min-height: 24px;
            min-width: 24px;
        }
        QScrollBar::handle:vertical:hover,
        QScrollBar::handle:horizontal:hover {
            background: __ACCENT__;
        }
        QScrollBar::add-line,
        QScrollBar::sub-line,
        QScrollBar::add-page,
        QScrollBar::sub-page {
            background: none;
            border: none;
            width: 0;
            height: 0;
        }

        QToolTip {
            background-color: __PANEL__;
            color: __TEXT__;
            border: 1px solid __ACCENT__;
            padding: 4px;
        }
    )");

    style.replace(QStringLiteral("__WINDOW__"), colorName(theme.window));
    style.replace(QStringLiteral("__SURFACE__"), colorName(theme.surface));
    style.replace(QStringLiteral("__PANEL__"), colorName(theme.panel));
    style.replace(QStringLiteral("__PANEL_ALT__"), colorName(theme.panelAlt));
    style.replace(QStringLiteral("__TEXT__"), colorName(theme.text));
    style.replace(QStringLiteral("__MUTED__"), colorName(theme.mutedText));
    style.replace(QStringLiteral("__BORDER__"), colorName(theme.border));
    style.replace(QStringLiteral("__ACCENT__"), colorName(theme.accent));
    style.replace(QStringLiteral("__ACCENT_HOVER__"), colorName(theme.accentHover));
    style.replace(QStringLiteral("__DANGER__"), colorName(theme.danger));
    return style;
}

static void applyTheme(const ThemeSpec &theme)
{
    QApplication::setStyle(QStringLiteral("Fusion"));

    QPalette palette;
    palette.setColor(QPalette::Window, theme.window);
    palette.setColor(QPalette::WindowText, theme.text);
    palette.setColor(QPalette::Base, theme.surface);
    palette.setColor(QPalette::AlternateBase, theme.panel);
    palette.setColor(QPalette::ToolTipBase, theme.panel);
    palette.setColor(QPalette::ToolTipText, theme.text);
    palette.setColor(QPalette::Text, theme.text);
    palette.setColor(QPalette::Button, theme.panel);
    palette.setColor(QPalette::ButtonText, theme.text);
    palette.setColor(QPalette::BrightText, theme.danger);
    palette.setColor(QPalette::Highlight, theme.accent);
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::Disabled, QPalette::Text, theme.mutedText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, theme.mutedText);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, theme.mutedText);
    qApp->setPalette(palette);

    qApp->setStyleSheet(themeStyleSheet(theme));
}

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

        for (QToolButton *button : {m_minimizeButton, m_maximizeButton, m_closeButton}) {
            button->setFixedSize(36, 24);
            button->setAutoRaise(true);
            button->setFocusPolicy(Qt::NoFocus);
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
        connect(m_pulse, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            m_currentColor = mix(m_baseColor, m_pulseColor, value.toReal());
            update();
        });

        connect(m_minimizeButton, &QToolButton::clicked, this, [this]() {
            if (QWidget *w = window())
                w->showMinimized();
        });
        connect(m_maximizeButton, &QToolButton::clicked, this, [this]() {
            toggleMaximized();
        });
        connect(m_closeButton, &QToolButton::clicked, this, [this]() {
            if (QWidget *w = window())
                w->close();
        });
    }

    void setTitle(const QString &title)
    {
        m_titleLabel->setText(title);
    }

    void setTheme(const ThemeSpec &theme)
    {
        m_baseColor = theme.titleBase;
        m_pulseColor = theme.titlePulse;
        m_currentColor = m_baseColor;
        m_textColor = theme.text;
        m_borderColor = theme.border;
        m_accentColor = theme.accent;
        m_dangerColor = theme.danger;

        const QString buttonStyle = QStringLiteral(R"(
            QToolButton {
                background-color: transparent;
                color: %1;
                border: none;
                border-radius: 5px;
                padding: 0;
                font-weight: 600;
            }
            QToolButton:hover {
                background-color: %2;
                color: #ffffff;
            }
            QToolButton:pressed {
                background-color: %3;
                color: #ffffff;
            }
        )").arg(colorName(m_textColor), colorName(m_accentColor), colorName(theme.accentHover));

        const QString closeStyle = QStringLiteral(R"(
            QToolButton {
                background-color: transparent;
                color: %1;
                border: none;
                border-radius: 5px;
                padding: 0;
                font-weight: 700;
            }
            QToolButton:hover {
                background-color: %2;
                color: #ffffff;
            }
            QToolButton:pressed {
                background-color: %3;
                color: #ffffff;
            }
        )").arg(colorName(m_textColor), colorName(m_dangerColor), colorName(theme.accentHover));

        m_titleLabel->setStyleSheet(QStringLiteral("background: transparent; color: %1; font-weight: 600;")
                                        .arg(colorName(m_textColor)));
        m_minimizeButton->setStyleSheet(buttonStyle);
        m_maximizeButton->setStyleSheet(buttonStyle);
        m_closeButton->setStyleSheet(closeStyle);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), m_currentColor);
        QColor line = m_borderColor;
        line.setAlpha(180);
        painter.setPen(QPen(line, 1));
        painter.drawLine(QPoint(0, height() - 1), QPoint(width(), height() - 1));
    }

    void enterEvent(QEvent *) override
    {
        m_pulse->start();
    }

    void leaveEvent(QEvent *) override
    {
        m_pulse->stop();
        m_currentColor = m_baseColor;
        update();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragOffset = event->globalPos() - window()->frameGeometry().topLeft();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_dragging && (event->buttons() & Qt::LeftButton)) {
            QWidget *w = window();
            if (w && !w->isMaximized())
                w->move(event->globalPos() - m_dragOffset);
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        m_dragging = false;
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            toggleMaximized();
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

private:
    static QColor mix(const QColor &a, const QColor &b, qreal t)
    {
        return QColor::fromRgbF(
            a.redF() + (b.redF() - a.redF()) * t,
            a.greenF() + (b.greenF() - a.greenF()) * t,
            a.blueF() + (b.blueF() - a.blueF()) * t);
    }

    void toggleMaximized()
    {
        QWidget *w = window();
        if (!w) return;
        if (w->isMaximized()) {
            w->showNormal();
            return;
        }
        // ShowWindow(SW_SHOWMAXIMIZED) uses MonitorFromWindow() to pick the target
        // monitor based on the window's current position.  For a frameless Qt window
        // this can resolve to the primary monitor even when the window is visually on
        // a secondary screen, causing the window to maximize there and all QMenu
        // popups to appear on the primary monitor (invisible to the user).
        // Moving the window onto the correct screen first fixes the resolution.
        if (QScreen *screen = QApplication::screenAt(w->frameGeometry().center()))
            w->move(screen->availableGeometry().topLeft());
        w->showMaximized();
    }

    QLabel *m_titleLabel = nullptr;
    QToolButton *m_minimizeButton = nullptr;
    QToolButton *m_maximizeButton = nullptr;
    QToolButton *m_closeButton = nullptr;
    QVariantAnimation *m_pulse = nullptr;
    QColor m_baseColor = QColor("#111827");
    QColor m_pulseColor = QColor("#3b4252");
    QColor m_currentColor = QColor("#111827");
    QColor m_textColor = QColor("#eceff4");
    QColor m_borderColor = QColor("#4c566a");
    QColor m_accentColor = QColor("#5e81ac");
    QColor m_dangerColor = QColor("#bf616a");
    bool m_dragging = false;
    QPoint m_dragOffset;
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
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

    // Convert the physical-pixel screen position to logical widget-local coordinates.
    // mapFromGlobal handles DPI scaling and multi-monitor offsets automatically,
    // matching exactly the approach used by the tree-debug tool (which works correctly
    // on secondary monitors).
    const QPoint pos(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
    const QPoint local = mapFromGlobal(pos);
    constexpr int B = 8;

    if (local.x() < B && local.y() < B)                      *result = HTTOPLEFT;
    else if (local.x() > width() - B && local.y() < B)       *result = HTTOPRIGHT;
    else if (local.x() < B && local.y() > height() - B)      *result = HTBOTTOMLEFT;
    else if (local.x() > width() - B && local.y() > height() - B) *result = HTBOTTOMRIGHT;
    else if (local.y() < B)                                   *result = HTTOP;
    else if (local.y() > height() - B)                        *result = HTBOTTOM;
    else if (local.x() < B)                                   *result = HTLEFT;
    else if (local.x() > width() - B)                         *result = HTRIGHT;
    else
        return QMainWindow::nativeEvent(eventType, message, result);

    return true;
#else
    return QMainWindow::nativeEvent(eventType, message, result);
#endif
}

void MainWindow::buildUi()
{
    setWindowTitle("OpenSCAD Visual Editor Prototype");
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground, false);

    ThemeSpec activeTheme = defaultTheme();
    auto *titleBar = new AnimatedTitleBar(this);
    titleBar->setTitle(windowTitle());
    titleBar->setTheme(activeTheme);

    m_undoStack = new QUndoStack(this);
    m_undoAction = m_undoStack->createUndoAction(this, "Undo");
    m_redoAction = m_undoStack->createRedoAction(this, "Redo");
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction->setShortcut(QKeySequence::Redo);

    QMenuBar *appMenuBar = menuBar();
    auto *fileMenu = appMenuBar->addMenu("File");
    auto *examplesMenu = fileMenu->addMenu("Open Example");
    populateExamplesMenu(examplesMenu);

    auto *editMenu = appMenuBar->addMenu("Edit");
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);

    auto *settingsMenu = appMenuBar->addMenu("Settings");
    auto *themeMenu = settingsMenu->addMenu("Theme");
    auto *themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    for (const ThemeSpec &theme : availableThemes()) {
        QAction *action = themeMenu->addAction(theme.label);
        action->setCheckable(true);
        action->setData(theme.id);
        themeGroup->addAction(action);
        if (theme.id == activeTheme.id)
            action->setChecked(true);

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
    m_examplePreview = new ExamplePreviewPopup;   // top-level window, no parent

    m_exampleHoverTimer = new QTimer(this);
    m_exampleHoverTimer->setSingleShot(true);
    m_exampleHoverTimer->setInterval(900); // ms before preview appears
    connect(m_exampleHoverTimer, &QTimer::timeout, this, &MainWindow::onExampleHoverTimeout);

    m_thumbnailWatcher = new QFutureWatcher<QImage>(this);
    connect(m_thumbnailWatcher, &QFutureWatcher<QImage>::finished,
            this, &MainWindow::onExampleThumbnailReady);

    m_viewport = new ViewportWidget;
    m_viewport->setScene(&m_scene);

    m_codeEditor = new QTextEdit;
    m_codeEditor->setReadOnly(false);
    m_codeEditor->setMinimumHeight(180);
    m_codeEditor->setFontFamily("Consolas");

    m_applyCodeButton = new QPushButton("Apply code");
    m_sendToOpenScadButton = new QPushButton("Send to OpenSCAD");
    m_openScadPreviewLabel = new QLabel;
    m_openScadPreviewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_openScadPreviewLabel->setWordWrap(true);
    m_openScadPreviewLabel->setText(QString("Preview file: %1").arg(QDir::toNativeSeparators(previewScadPath())));

    m_parseErrorLabel = new QLabel;
    m_parseErrorLabel->setWordWrap(true);
    m_parseErrorLabel->setContentsMargins(4, 2, 4, 2);
    m_parseErrorLabel->hide();

    auto *codePanel = new QWidget;
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

    // Left dock: shapes
    auto *leftDock = new QDockWidget("Shapes", this);
    auto *leftPanel = new QWidget;
    auto *leftLayout = new QVBoxLayout(leftPanel);

    m_sceneTreeGraphics = new SceneTreeGraphicsWidget;
    m_sceneTreeGraphics->setSceneDocument(&m_scene);
    m_sceneTreeGraphics->setToolDroppedCallback([this](const QString &toolName, int parentGroupId, int insertIndex) {
        onGraphicsTreeToolDropped(toolName, parentGroupId, insertIndex);
    });
    m_sceneTreeGraphics->setModuleCallDroppedCallback([this](int moduleGroupId, int parentGroupId, int insertIndex) {
        onGraphicsTreeModuleCallDropped(moduleGroupId, parentGroupId, insertIndex);
    });
    m_sceneTreeGraphics->setTreeNodeDroppedCallback([this](int nodeId, int parentGroupId, int insertIndex) {
        moveTreeNodeToGroup(nodeId, parentGroupId, insertIndex);
    });
    m_sceneTreeGraphics->setTreeNodeSelectedCallback([this](int nodeId) {
        onGraphicsTreeNodeSelected(nodeId);
    });
    m_sceneTreeGraphics->setTreeNodeDeleteRequestedCallback([this](int nodeId) {
        onGraphicsTreeNodeDeleteRequested(nodeId);
    });
    m_sceneTreeGraphics->setTransformValueAdjustedCallback([this](int groupId, int axis, int numberStart, int numberLength, qreal delta) {
        onGraphicsTreeTransformValueAdjusted(groupId, axis, numberStart, numberLength, delta);
    });
    m_sceneTreeGraphics->setTransformControlHoveredCallback([this](int groupId, SceneDocument::TreeNode::Operation operation, int axis) {
        onGraphicsTreeTransformControlHovered(groupId, operation, axis);
    });
    m_sceneTreeGraphics->setShapeParameterAdjustedCallback([this](int nodeId, int paramIndex, int numberStart, int numberLength, qreal delta) {
        onGraphicsTreeShapeParameterAdjusted(nodeId, paramIndex, numberStart, numberLength, delta);
    });
    m_sceneTreeGraphics->setShapeParameterHoveredCallback([this](int shapeId, int parameter) {
        onGraphicsTreeShapeParameterHovered(shapeId, parameter);
    });
    m_sceneTreeGraphics->setVariableNumberAdjustedCallback([this](int nodeId, int start, int length, qreal delta) {
        onGraphicsTreeVariableNumberAdjusted(nodeId, start, length, delta);
    });
    m_sceneTreeGraphics->setModuleCallArgumentAdjustedCallback([this](int moduleCallId, int parameterVariableId, int start, int length, qreal delta) {
        onGraphicsTreeModuleCallArgumentAdjusted(moduleCallId, parameterVariableId, start, length, delta);
    });
    m_sceneTreeGraphics->setForLoopRangeAdjustedCallback([this](int nodeId, int start, int length, qreal delta) {
        onGraphicsTreeForLoopRangeAdjusted(nodeId, start, length, delta);
    });
    m_sceneTreeGraphics->setCtrlReleasedCallback([this]() {
        m_ctrlHighlight.active = false;
        highlightOpenScadSelection();
    });
    m_sceneTreeGraphics->setModuleRenameRequestedCallback([this](int groupId, const QString &newName) {
        onGraphicsTreeModuleRenameRequested(groupId, newName);
    });
    m_sceneTreeGraphics->setVariableRenameRequestedCallback([this](int variableId, const QString &newName) {
        onGraphicsTreeVariableRenameRequested(variableId, newName);
    });


    leftLayout->addWidget(m_sceneTreeGraphics, 1);
    m_csgStatusLabel = new QLabel;
    m_csgStatusLabel->setWordWrap(true);
    leftLayout->addWidget(m_csgStatusLabel);

    leftDock->setWidget(leftPanel);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);

    connect(m_applyCodeButton, &QPushButton::clicked, this, &MainWindow::applyOpenScadCode);
    connect(m_sendToOpenScadButton, &QPushButton::clicked, this, &MainWindow::sendToOpenScad);
    connect(m_viewport, &ViewportWidget::shapeClicked, this, [this](int index) {
        const ShapeNode *shape = m_scene.shapeAt(index);
        m_scene.setSelectedIndex(index);
        selectShapeInSceneTree(shape ? shape->id : -1);
        m_viewport->setSelectedIndex(m_scene.selectedIndex());
        m_viewport->setSelectedGroupId(0);
        refreshProperties();
    });
    connect(m_viewport, &ViewportWidget::emptyClicked, this, &MainWindow::clearSelection);
    connect(m_viewport, &ViewportWidget::shapeDragStarted, this, &MainWindow::onViewportShapeDragStarted);
    connect(m_viewport, &ViewportWidget::shapeDragged, this, &MainWindow::onViewportShapeDragged);
    connect(m_viewport, &ViewportWidget::shapeDragFinished, this, &MainWindow::onViewportShapeDragFinished);
    connect(m_viewport, &ViewportWidget::shapeRotationDragStarted, this, &MainWindow::onViewportShapeRotationDragStarted);
    connect(m_viewport, &ViewportWidget::shapeRotated, this, &MainWindow::onViewportShapeRotated);
    connect(m_viewport, &ViewportWidget::shapeRotationDragFinished, this, &MainWindow::onViewportShapeRotationDragFinished);
    connect(m_viewport, &ViewportWidget::groupDragStarted, this, &MainWindow::onViewportGroupDragStarted);
    connect(m_viewport, &ViewportWidget::groupDragged, this, &MainWindow::onViewportGroupDragged);
    connect(m_viewport, &ViewportWidget::groupDragFinished, this, &MainWindow::onViewportGroupDragFinished);
    connect(m_viewport, &ViewportWidget::groupRotationDragStarted, this, &MainWindow::onViewportGroupRotationDragStarted);
    connect(m_viewport, &ViewportWidget::groupRotated, this, &MainWindow::onViewportGroupRotated);
    connect(m_viewport, &ViewportWidget::groupRotationDragFinished, this, &MainWindow::onViewportGroupRotationDragFinished);

}

void MainWindow::addCube()
{
    ShapeNode s;
    s.type = ShapeNode::Cube;
    s.name = QString("Cube %1").arg(m_scene.shapeCount() + 1);
    s.size = QVector3D(20, 20, 20);

    m_undoStack->push(new AddShapeCommand(&m_scene, s, [this]() {
        refreshSceneViews();
    }));
}

void MainWindow::addSphere()
{
    ShapeNode s;
    s.type = ShapeNode::Sphere;
    s.name = QString("Sphere %1").arg(m_scene.shapeCount() + 1);
    s.radius = 10;

    m_undoStack->push(new AddShapeCommand(&m_scene, s, [this]() {
        refreshSceneViews();
    }));
}

void MainWindow::addCylinder()
{
    ShapeNode s;
    s.type = ShapeNode::Cylinder;
    s.name = QString("Cylinder %1").arg(m_scene.shapeCount() + 1);
    s.radius = 10;
    s.height = 30;

    m_undoStack->push(new AddShapeCommand(&m_scene, s, [this]() {
        refreshSceneViews();
    }));
}

void MainWindow::addUnionGroup()
{
    addGroup(SceneDocument::TreeNode::Union);
}

void MainWindow::addDifferenceGroup()
{
    addGroup(SceneDocument::TreeNode::Difference);
}

void MainWindow::addIntersectionGroup()
{
    addGroup(SceneDocument::TreeNode::Intersection);
}

void MainWindow::applyOpenScadCode()
{
    SceneDocument::Snapshot snapshot;
    QString errorMessage;
    int errorLine = -1;

    if (!OpenScadParser::parseScene(m_codeEditor->toPlainText(), &snapshot, &errorMessage, &errorLine)) {
        m_parseErrorLabel->setText(QString("<span style='color:#d04040;'>%1</span>")
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

    auto *command = new ReplaceSceneCommand(&m_scene, snapshot, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::sendToOpenScad()
{
    if (!writeOpenScadPreview(true))
        return;

    const QString nativePath = QDir::toNativeSeparators(previewScadPath());
    QApplication::clipboard()->setText(nativePath);

    QMessageBox::information(
        this,
        "OpenSCAD preview file",
        QString("Saved the current model to:\n\n%1\n\n"
                "The path was copied to the clipboard. Open this file in OpenSCAD and enable automatic reload/preview there.")
            .arg(nativePath));
}

void MainWindow::onViewportShapeDragStarted(int index)
{
    m_scene.setSelectedIndex(index);
    selectShapeInSceneTree(m_scene.selectedShapeId());
    m_viewport->setSelectedIndex(m_scene.selectedIndex());

    const ShapeNode *shape = m_scene.selectedShape();
    if (!shape)
        return;

    m_viewportDragStartShape = *shape;
    m_viewportDragActive = true;
}

void MainWindow::onViewportShapeDragged(int index, const QVector3D &delta)
{
    if (!m_viewportDragActive || m_scene.selectedIndex() != index)
        return;

    ShapeNode *shape = m_scene.selectedShape();
    if (!shape)
        return;

    *shape = m_viewportDragStartShape;
    shape->position = m_viewportDragStartShape.position + delta;

    m_viewport->update();
}

void MainWindow::onViewportShapeDragFinished(int index)
{
    if (!m_viewportDragActive || m_scene.selectedIndex() != index)
        return;

    m_viewportDragActive = false;

    const ShapeNode *shape = m_scene.selectedShape();
    if (!shape)
        return;

    auto *command = new UpdateShapeCommand(&m_scene, m_viewportDragStartShape, *shape, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        refreshProperties();
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::onViewportShapeRotationDragStarted(int index)
{
    onViewportShapeDragStarted(index);
}

void MainWindow::onViewportShapeRotated(int index, const QVector3D &deltaDegrees)
{
    if (!m_viewportDragActive || m_scene.selectedIndex() != index)
        return;

    ShapeNode *shape = m_scene.selectedShape();
    if (!shape)
        return;

    *shape = m_viewportDragStartShape;
    shape->rotation = normalizedRotation(m_viewportDragStartShape.rotation + deltaDegrees);

    m_viewport->invalidateCsgPreview();
    m_viewport->update();
    refreshProperties();
}

void MainWindow::onViewportShapeRotationDragFinished(int index)
{
    onViewportShapeDragFinished(index);
}

void MainWindow::onViewportGroupDragStarted(int groupId)
{
    selectTreeNodeInSceneTree(groupId);
    m_viewport->setSelectedGroupId(groupId);

    const SceneDocument::TreeNode *group = m_scene.treeNodeById(groupId);
    if (!group || group->type != SceneDocument::TreeNode::Group)
        return;

    m_viewportDragGroupId = groupId;
    m_viewportDragStartGroupPosition = group->position;
    m_viewportDragStartGroupRotation = group->rotation;
    m_viewportDragStartGroupScale = group->scale;
    m_viewportGroupDragActive = true;
}

void MainWindow::onViewportGroupDragged(int groupId, const QVector3D &delta)
{
    if (!m_viewportGroupDragActive || m_viewportDragGroupId != groupId)
        return;

    m_scene.updateGroupTransform(groupId,
                                 m_viewportDragStartGroupPosition + delta,
                                 m_viewportDragStartGroupRotation,
                                 m_viewportDragStartGroupScale);
    m_viewport->invalidateCsgPreview();
    m_viewport->update();
}

void MainWindow::onViewportGroupDragFinished(int groupId)
{
    if (!m_viewportGroupDragActive || m_viewportDragGroupId != groupId)
        return;

    m_viewportGroupDragActive = false;

    const SceneDocument::TreeNode *group = m_scene.treeNodeById(groupId);
    if (!group || group->type != SceneDocument::TreeNode::Group)
        return;

    const QVector3D finalPosition = group->position;
    const QVector3D finalRotation = group->rotation;
    const QVector3D finalScale = group->scale;
    if (finalPosition == m_viewportDragStartGroupPosition
        && finalRotation == m_viewportDragStartGroupRotation
        && finalScale == m_viewportDragStartGroupScale) {
        refreshProperties();
        return;
    }

    const SceneDocument::Snapshot newSnapshot = m_scene.snapshot();
    m_scene.updateGroupTransform(groupId,
                                 m_viewportDragStartGroupPosition,
                                 m_viewportDragStartGroupRotation,
                                 m_viewportDragStartGroupScale);
    const SceneDocument::Snapshot oldSnapshot = m_scene.snapshot();
    m_scene.restoreSnapshot(newSnapshot);

    auto *command = new UpdateGroupTransformCommand(&m_scene, oldSnapshot, newSnapshot, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        refreshProperties();
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::onViewportGroupRotationDragStarted(int groupId)
{
    onViewportGroupDragStarted(groupId);
}

void MainWindow::onViewportGroupRotated(int groupId, const QVector3D &deltaDegrees)
{
    if (!m_viewportGroupDragActive || m_viewportDragGroupId != groupId)
        return;

    m_scene.updateGroupTransform(groupId,
                                 m_viewportDragStartGroupPosition,
                                 normalizedRotation(m_viewportDragStartGroupRotation + deltaDegrees),
                                 m_viewportDragStartGroupScale);
    m_viewport->invalidateCsgPreview();
    m_viewport->update();
    refreshProperties();
}

void MainWindow::onViewportGroupRotationDragFinished(int groupId)
{
    onViewportGroupDragFinished(groupId);
}

void MainWindow::onGraphicsTreeToolDropped(const QString &toolName, int parentGroupId, int insertIndex)
{
    const bool moduleParameterZone = decodeModuleParameterInsertIndex(&insertIndex);

    if (isVariableTool(toolName)) {
        const int rootId = m_scene.treeRoot().id;
        const int sceneId = m_scene.sceneNodeId();
        // Variable dropped inside a Module node → becomes a module parameter.
        const SceneDocument::TreeNode *parentNode =
            parentGroupId > 0 ? m_scene.treeNodeById(parentGroupId) : nullptr;
        const bool inModule = parentNode
                              && parentNode->type == SceneDocument::TreeNode::Group
                              && parentNode->operation == SceneDocument::TreeNode::Module;
        if (parentGroupId > 0 && !inModule && parentGroupId != rootId && parentGroupId != sceneId)
            return;

        if (inModule) {
            // Add into the module parameter strip or regular module body.
            struct AddModuleParamCommand : public QUndoCommand {
                SceneDocument *scene; int moduleId; int insertIdx; bool parameter; std::function<void()> refresh;
                int addedId = 0;
                AddModuleParamCommand(SceneDocument *s, int mid, int idx, bool isParameter, std::function<void()> r)
                    : scene(s), moduleId(mid), insertIdx(idx), parameter(isParameter), refresh(r) {}
                void redo() override { addedId = scene->addVariableToModule(moduleId, parameter, insertIdx); if (refresh) refresh(); }
                void undo() override { if (addedId > 0) { scene->removeVariableById(addedId); if (refresh) refresh(); } }
                bool isValid() const { return scene && moduleId > 0; }
            };
            auto *cmd = new AddModuleParamCommand(&m_scene, parentGroupId, insertIndex, moduleParameterZone, [this]() { refreshSceneViews(); });
            if (!cmd->isValid()) { delete cmd; return; }
            m_undoStack->push(cmd);
            return;
        }

        auto *command = new AddVariableCommand(&m_scene, insertIndex, [this]() {
            refreshSceneViews();
        });

        if (!command->isValid()) {
            delete command;
            return;
        }

        m_undoStack->push(command);
        return;
    }

    SceneDocument::TreeNode::Operation operation;
    if (operationForTool(toolName, &operation)) {
        if (operation == SceneDocument::TreeNode::Module) {
            if (parentGroupId > 0 && parentGroupId != m_scene.treeRoot().id)
                return;
            parentGroupId = 0;
        } else if (parentGroupId <= 0 || parentGroupId == m_scene.treeRoot().id) {
            parentGroupId = m_scene.sceneNodeId();
        }

        auto *command = new AddGroupCommand(&m_scene, operation, parentGroupId, insertIndex, [this]() {
            refreshSceneViews();
        });

        if (!command->isValid()) {
            delete command;
            return;
        }

        m_undoStack->push(command);
        return;
    }

    if (toolName != "cube" && toolName != "sphere" && toolName != "cylinder")
        return;

    ShapeNode shape = makeShapeForTool(toolName, m_scene.shapeCount() + 1);
    auto *command = new AddShapeCommand(&m_scene, shape, parentGroupId, insertIndex, [this]() {
        refreshSceneViews();
    });

    m_undoStack->push(command);
}

void MainWindow::onGraphicsTreeModuleCallDropped(int moduleGroupId, int parentGroupId, int insertIndex)
{
    auto *command = new AddModuleCallCommand(&m_scene, moduleGroupId, parentGroupId, insertIndex, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::onGraphicsTreeNodeSelected(int nodeId)
{
    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node) {
        clearSelection();
        return;
    }

    if (node->type == SceneDocument::TreeNode::Primitive) {
        m_scene.setSelectedShapeId(node->shapeId);
        selectShapeInSceneTree(node->shapeId);
        m_viewport->setSelectedIndex(m_scene.selectedIndex());
        m_viewport->setSelectedGroupId(0);
    } else if (node->type == SceneDocument::TreeNode::ModuleCall) {
        m_scene.setSelectedShapeId(-1);
        selectTreeNodeInSceneTree(node->id);
        m_viewport->setSelectedIndex(-1);
        m_viewport->setSelectedGroupId(node->id);
    } else if (node->type == SceneDocument::TreeNode::Variable) {
        m_scene.setSelectedShapeId(-1);
        selectTreeNodeInSceneTree(node->id);
        m_viewport->setSelectedIndex(-1);
        m_viewport->setSelectedGroupId(0);
    } else {
        m_scene.setSelectedShapeId(-1);
        selectTreeNodeInSceneTree(node->id);
        m_viewport->setSelectedGroupId(node->id);
    }

    refreshProperties();
    m_viewport->update();
}

void MainWindow::onGraphicsTreeNodeDeleteRequested(int nodeId)
{
    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node)
        return;

    if (node->type == SceneDocument::TreeNode::ModuleCall) {
        auto *command = new RemoveModuleCallCommand(&m_scene, node->id, [this]() {
            refreshSceneViews();
        });

        if (!command->isValid()) {
            delete command;
            return;
        }

        m_undoStack->push(command);
        return;
    }

    if (node->type == SceneDocument::TreeNode::Primitive) {
        auto *command = new DeleteShapeCommand(&m_scene, node->shapeId, [this]() {
            refreshSceneViews();
        });

        if (!command->isValid()) {
            delete command;
            return;
        }

        m_undoStack->push(command);
        return;
    }

    if (node->type == SceneDocument::TreeNode::Variable) {
        auto *command = new RemoveVariableCommand(&m_scene, node->id, [this]() {
            refreshSceneViews();
        });

        if (!command->isValid()) {
            delete command;
            return;
        }

        m_undoStack->push(command);
        return;
    }

    if (node->id == m_scene.treeRoot().id)
        return;

    auto *command = new RemoveGroupCommand(&m_scene, node->id, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::onGraphicsTreeTransformValueAdjusted(int groupId, int axis, int numberStart, int numberLength, qreal delta)
{
    if (axis < 0 || axis > 2 || qFuzzyIsNull(delta))
        return;

    const SceneDocument::TreeNode *group = m_scene.treeNodeById(groupId);
    if (!group || group->type != SceneDocument::TreeNode::Group)
        return;

    // Get effective expression for this axis
    const QString currentExpr = SceneTreeGraphics::transformAxisExpression(*group, axis);

    QStringList newExpressions = group->transformExpressions;
    while (newExpressions.size() < 3)
        newExpressions.append(QString());

    QVector3D position = group->position;
    QVector3D rotation = group->rotation;
    QVector3D scale = group->scale;

    if (numberStart >= 0 && numberLength > 0 && numberStart + numberLength <= currentExpr.size()) {
        const bool isScale = group->operation == SceneDocument::TreeNode::Scale;
        const qreal minVal = isScale ? 0.01 : -1e9;
        const QString numberText = currentExpr.mid(numberStart, numberLength);
        const int decimalPoint = numberText.indexOf(QLatin1Char('.'));
        const int precision = decimalPoint >= 0 ? qMin(3, numberText.size() - decimalPoint - 1) : 0;
        const qreal step = precision > 0 ? 0.1 : 1.0;
        const bool standaloneNumber = isStandaloneNumericToken(currentExpr, numberStart, numberLength);
        const qreal tokenMin = isScale ? 0.01 : (standaloneNumber ? -1e9 : 0.0);
        QString replacement = adjustedNumericToken(currentExpr, numberStart, numberLength, delta, step, tokenMin, isScale || !standaloneNumber);
        if (replacement.isEmpty())
            return;
        if (isScale && precision == 0 && !replacement.contains(QLatin1Char('.')))
            replacement = QString::number(replacement.toDouble(), 'f', 1);
        const QString newExpr = currentExpr.left(numberStart) + replacement + currentExpr.mid(numberStart + numberLength);
        newExpressions[axis] = newExpr;

        if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
            // Generated: translate([X, Y, Z]) — build prefix up to the Nth value so
            // the search is unique even when all three axes have the same numeric value.
            QString contextPrefix = QStringLiteral("[");
            for (int i = 0; i < axis; ++i)
                contextPrefix += SceneTreeGraphics::transformAxisExpression(*group, i) + QStringLiteral(", ");
            m_ctrlHighlight.active        = true;
            m_ctrlHighlight.nodeId        = groupId;
            m_ctrlHighlight.contextPrefix = contextPrefix;
            m_ctrlHighlight.expression    = newExpr;
            m_ctrlHighlight.numberStart   = numberStart;
            m_ctrlHighlight.numberLength  = int(replacement.size());
        } else {
            m_ctrlHighlight.active = false;
        }

        // Evaluate new expression to get numeric value
        QHash<QString, qreal> varValues;
        for (const SceneDocument::TreeNode &child : m_scene.treeRoot().children) {
            if (child.type == SceneDocument::TreeNode::Variable)
                varValues[child.variableName] = child.variableValue;
        }
        qreal newNumeric = replacement.toDouble();
        ExpressionSyntax::evaluate(newExpr, varValues, &newNumeric);
        newNumeric = qMax(minVal, newNumeric);

        if (axis == 0) {
            if (group->operation == SceneDocument::TreeNode::Translate) position.setX(static_cast<float>(newNumeric));
            else if (group->operation == SceneDocument::TreeNode::Rotate) rotation.setX(static_cast<float>(newNumeric));
            else scale.setX(static_cast<float>(newNumeric));
        } else if (axis == 1) {
            if (group->operation == SceneDocument::TreeNode::Translate) position.setY(static_cast<float>(newNumeric));
            else if (group->operation == SceneDocument::TreeNode::Rotate) rotation.setY(static_cast<float>(newNumeric));
            else scale.setY(static_cast<float>(newNumeric));
        } else {
            if (group->operation == SceneDocument::TreeNode::Translate) position.setZ(static_cast<float>(newNumeric));
            else if (group->operation == SceneDocument::TreeNode::Rotate) rotation.setZ(static_cast<float>(newNumeric));
            else scale.setZ(static_cast<float>(newNumeric));
        }
    } else {
        // Plain numeric adjustment (no expression or no number found)
        const bool isScale = group->operation == SceneDocument::TreeNode::Scale;
        const qreal step = group->operation == SceneDocument::TreeNode::Rotate ? 5.0
                         : isScale ? 0.1 : 1.0;
        QVector3D *targetVector = group->operation == SceneDocument::TreeNode::Translate ? &position
                                : group->operation == SceneDocument::TreeNode::Rotate    ? &rotation
                                                                                         : &scale;
        auto adjustAxis = [&](float current) -> float {
            return static_cast<float>(isScale ? qMax(0.01, static_cast<qreal>(current) + delta * step)
                                              : static_cast<qreal>(current) + delta * step);
        };
        if (axis == 0)      targetVector->setX(adjustAxis(targetVector->x()));
        else if (axis == 1) targetVector->setY(adjustAxis(targetVector->y()));
        else                targetVector->setZ(adjustAxis(targetVector->z()));
        // Clear expression for this axis so numeric value takes over
        newExpressions[axis].clear();
        m_ctrlHighlight.active = false;
    }

    const SceneDocument::Snapshot oldSnapshot = m_scene.snapshot();
    if (!m_scene.updateGroupTransform(groupId, position, rotation, scale, newExpressions))
        return;
    const SceneDocument::Snapshot newSnapshot = m_scene.snapshot();
    m_scene.restoreSnapshot(oldSnapshot);

    auto *command = new UpdateGroupTransformCommand(&m_scene, oldSnapshot, newSnapshot, [this]() {
        refreshSceneViews();
    });
    m_undoStack->push(command);
}

void MainWindow::onGraphicsTreeTransformControlHovered(int groupId, SceneDocument::TreeNode::Operation operation, int axis)
{
    if (m_viewport)
        m_viewport->setTreeTransformControlPreview(groupId, operation, axis);
}

void MainWindow::onGraphicsTreeModuleRenameRequested(int groupId, const QString &newName)
{
    auto *command = new RenameModuleCommand(&m_scene, groupId, newName, [this]() {
        refreshSceneViews();
    });
    if (!command->isValid()) {
        delete command;
        return;
    }
    m_undoStack->push(command);
}

void MainWindow::onGraphicsTreeVariableRenameRequested(int variableId, const QString &newName)
{
    auto *command = new RenameVariableCommand(&m_scene, variableId, newName, [this]() {
        refreshSceneViews();
    });
    if (!command->isValid()) {
        delete command;
        return;
    }
    m_undoStack->push(command);
}

void MainWindow::onGraphicsTreeShapeParameterAdjusted(int nodeId, int paramIndex, int numberStart, int numberLength, qreal delta)
{
    if (nodeId <= 0 || paramIndex < 0 || numberStart < 0 || numberLength <= 0 || qFuzzyIsNull(delta))
        return;

    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node || node->type != SceneDocument::TreeNode::Primitive)
        return;

    const ShapeNode *shape = m_scene.shapeById(node->shapeId);
    if (!shape)
        return;

    const QVector<SceneTreeGraphics::ShapeParameterControl> controls =
        SceneTreeGraphics::shapeParameterControls(*shape);
    if (paramIndex >= controls.size())
        return;

    const QString &currentExpr = controls[paramIndex].expression;
    if (numberStart + numberLength > currentExpr.size())
        return;

    const QString numberText = currentExpr.mid(numberStart, numberLength);
    const int decimalPoint = numberText.indexOf(QLatin1Char('.'));
    const int precision = decimalPoint >= 0 ? qMin(3, numberText.size() - decimalPoint - 1) : 0;
    const qreal step = precision > 0 ? 0.1 : 1.0;
    const bool standaloneNumber = isStandaloneNumericToken(currentExpr, numberStart, numberLength);
    const QString replacement = adjustedNumericToken(currentExpr, numberStart, numberLength, delta, step, 0.1, !standaloneNumber);
    if (replacement.isEmpty())
        return;

    const QString newExpr = currentExpr.left(numberStart) + replacement + currentExpr.mid(numberStart + numberLength);

    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        // Build a contextPrefix that uniquely identifies this parameter in the generated
        // code so the highlight does not land on the wrong token when multiple parameters
        // share the same numeric value (e.g. cube([20,20,20]) or translate([10,10,10])).
        QString contextPrefix;
        if (shape->type == ShapeNode::Cylinder) {
            // Generated: cylinder(h=H, r=R, center=true)
            contextPrefix = (paramIndex == 0) ? QStringLiteral(", r=") : QStringLiteral("h=");
        } else if (shape->type == ShapeNode::Sphere) {
            contextPrefix = QStringLiteral("r=");
        } else {
            // Cube: cube([X, Y, Z], center=true) — prefix accumulates preceding values
            contextPrefix = QStringLiteral("[");
            for (int i = 0; i < paramIndex && i < controls.size(); ++i)
                contextPrefix += controls[i].expression + QStringLiteral(", ");
        }
        m_ctrlHighlight.active        = true;
        m_ctrlHighlight.nodeId        = nodeId;
        m_ctrlHighlight.contextPrefix = contextPrefix;
        m_ctrlHighlight.expression    = newExpr;
        m_ctrlHighlight.numberStart   = numberStart;
        m_ctrlHighlight.numberLength  = int(replacement.size());
    } else {
        m_ctrlHighlight.active = false;
    }

    // Build variable context for re-evaluation.
    QHash<QString, qreal> varValues;
    for (const SceneDocument::TreeNode &child : m_scene.treeRoot().children) {
        if (child.type == SceneDocument::TreeNode::Variable)
            varValues[child.variableName] = child.variableValue;
    }

    qreal newNumericValue = replacement.toDouble();
    ExpressionSyntax::evaluate(newExpr, varValues, &newNumericValue);
    newNumericValue = qMax(0.1, newNumericValue);

    ShapeNode updatedShape = *shape;
    while (updatedShape.parameterExpressions.size() < controls.size())
        updatedShape.parameterExpressions.append(QString());
    updatedShape.parameterExpressions[paramIndex] = newExpr;

    if (updatedShape.type == ShapeNode::Cube) {
        QVector3D size = updatedShape.size;
        if (paramIndex == 0)      size.setX(newNumericValue);
        else if (paramIndex == 1) size.setY(newNumericValue);
        else if (paramIndex == 2) size.setZ(newNumericValue);
        updatedShape.size = size;
    } else if (updatedShape.type == ShapeNode::Sphere) {
        if (paramIndex == 0) updatedShape.radius = newNumericValue;
    } else if (updatedShape.type == ShapeNode::Cylinder) {
        if (paramIndex == 0)      updatedShape.radius = newNumericValue;
        else if (paramIndex == 1) updatedShape.height = newNumericValue;
    }

    auto *command = new UpdateShapeCommand(&m_scene, *shape, updatedShape, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::onGraphicsTreeShapeParameterHovered(int shapeId, int parameter)
{
    if (m_viewport)
        m_viewport->setTreeShapeParameterPreview(shapeId, parameter);
}

void MainWindow::onGraphicsTreeVariableNumberAdjusted(int nodeId, int start, int length, qreal delta)
{
    if (nodeId <= 0 || start < 0 || length <= 0 || qFuzzyIsNull(delta))
        return;

    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node || node->type != SceneDocument::TreeNode::Variable)
        return;

    QString expression = node->variableExpression;
    if (start + length > expression.size())
        return;

    const QString numberText = expression.mid(start, length);
    const int decimalPoint = numberText.indexOf(QLatin1Char('.'));
    const int precision = decimalPoint >= 0 ? qMin(3, numberText.size() - decimalPoint - 1) : 0;
    const qreal step = precision > 0 ? 0.1 : 1.0;
    const bool standaloneNumber = isStandaloneNumericToken(expression, start, length);
    QString replacement = adjustedNumericToken(expression, start, length, delta, step, 0.0, !standaloneNumber);
    if (replacement.isEmpty())
        return;

    expression.replace(start, length, replacement);

    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        m_ctrlHighlight.active        = true;
        m_ctrlHighlight.nodeId        = nodeId;
        m_ctrlHighlight.contextPrefix = QString(); // variable expression is unique in its range
        m_ctrlHighlight.expression    = expression;
        m_ctrlHighlight.numberStart   = start;
        m_ctrlHighlight.numberLength  = int(replacement.size());
    } else {
        m_ctrlHighlight.active = false;
    }

    auto *command = new UpdateVariableExpressionCommand(&m_scene, nodeId, expression, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::onGraphicsTreeModuleCallArgumentAdjusted(int moduleCallId,
                                                          int parameterVariableId,
                                                          int start,
                                                          int length,
                                                          qreal delta)
{
    if (moduleCallId <= 0 || parameterVariableId <= 0 || start < 0 || length <= 0 || qFuzzyIsNull(delta))
        return;

    const SceneDocument::TreeNode *callNode = m_scene.treeNodeById(moduleCallId);
    const SceneDocument::TreeNode *parameterNode = m_scene.treeNodeById(parameterVariableId);
    if (!callNode || callNode->type != SceneDocument::TreeNode::ModuleCall
        || !parameterNode || parameterNode->type != SceneDocument::TreeNode::Variable
        || !parameterNode->isParameter) {
        return;
    }

    const SceneDocument::TreeNode *moduleGroupNode = m_scene.treeNodeById(callNode->shapeId);
    const QHash<QString, QString> overrides = moduleGroupNode
        ? resolveModuleArguments(callNode->moduleCallArguments, *moduleGroupNode)
        : parseNamedArgumentExpressions(callNode->moduleCallArguments);
    QString expression = overrides.value(parameterNode->variableName,
                                         parameterNode->variableExpression.trimmed().isEmpty()
                                             ? QString::number(parameterNode->variableValue)
                                             : parameterNode->variableExpression.trimmed());
    if (start + length > expression.size())
        return;

    const QString numberText = expression.mid(start, length);
    const int decimalPoint = numberText.indexOf(QLatin1Char('.'));
    const int precision = decimalPoint >= 0 ? qMin(3, numberText.size() - decimalPoint - 1) : 0;
    const qreal step = precision > 0 ? 0.1 : 1.0;
    const bool standaloneNumber = isStandaloneNumericToken(expression, start, length);
    QString replacement = adjustedNumericToken(expression, start, length, delta, step, 0.0, !standaloneNumber);
    if (replacement.isEmpty())
        return;

    expression.replace(start, length, replacement);

    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        m_ctrlHighlight.active        = true;
        m_ctrlHighlight.nodeId        = moduleCallId;
        m_ctrlHighlight.contextPrefix = QString();
        m_ctrlHighlight.expression    = expression;
        m_ctrlHighlight.numberStart   = start;
        m_ctrlHighlight.numberLength  = int(replacement.size());
    } else {
        m_ctrlHighlight.active = false;
    }

    auto *command = new UpdateModuleCallArgumentCommand(&m_scene,
                                                        moduleCallId,
                                                        parameterNode->variableName,
                                                        expression,
                                                        [this]() {
                                                            refreshSceneViews();
                                                        });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::onGraphicsTreeForLoopRangeAdjusted(int nodeId, int start, int length, qreal delta)
{
    if (nodeId <= 0 || start < 0 || length <= 0 || qFuzzyIsNull(delta))
        return;

    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node || node->type != SceneDocument::TreeNode::Group || node->operation != SceneDocument::TreeNode::For)
        return;

    QString expression = SceneTreeGraphics::forLoopRangeExpression(*node);
    if (start + length > expression.size())
        return;

    const QString numberText = expression.mid(start, length);
    const int decimalPoint = numberText.indexOf(QLatin1Char('.'));
    const int precision = decimalPoint >= 0 ? qMin(3, numberText.size() - decimalPoint - 1) : 0;
    const qreal step = precision > 0 ? 0.1 : 1.0;
    const bool standaloneNumber = isStandaloneNumericToken(expression, start, length);
    const QString replacement = adjustedNumericToken(expression, start, length, delta, step, 0.0, !standaloneNumber);
    if (replacement.isEmpty())
        return;

    expression.replace(start, length, replacement);

    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        m_ctrlHighlight.active        = true;
        m_ctrlHighlight.nodeId        = nodeId;
        m_ctrlHighlight.contextPrefix = QString(); // full range expression is unique in for-loop range
        m_ctrlHighlight.expression    = expression;
        m_ctrlHighlight.numberStart   = start;
        m_ctrlHighlight.numberLength  = int(replacement.size());
    } else {
        m_ctrlHighlight.active = false;
    }

    auto *command = new UpdateForLoopCommand(&m_scene, nodeId, SceneTreeGraphics::forLoopVariableName(*node), expression, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::refreshShapeList()
{
    if (m_sceneTreeGraphics)
        m_sceneTreeGraphics->refresh();

    refreshOpenScadCode();
    m_viewport->update();
    refreshCsgStatus();
}

void MainWindow::refreshSceneViews()
{
    refreshShapeList();

    m_viewport->setSelectedIndex(m_scene.selectedIndex());
    m_viewport->setSelectedGroupId(selectedDirectGroupId());
    refreshProperties();
}

void MainWindow::selectShapeInSceneTree(int shapeId)
{
    int nodeId = 0;
    if (shapeId >= 0) {
        // Find the Primitive tree node that references this shapeId
        std::function<int(const SceneDocument::TreeNode &)> findNode =
            [&](const SceneDocument::TreeNode &n) -> int {
            if (n.type == SceneDocument::TreeNode::Primitive && n.shapeId == shapeId)
                return n.id;
            for (const auto &child : n.children) {
                const int found = findNode(child);
                if (found > 0)
                    return found;
            }
            return 0;
        };
        nodeId = findNode(m_scene.treeRoot());
    }

    m_selectedTreeNodeId = nodeId;
    if (m_sceneTreeGraphics)
        m_sceneTreeGraphics->setSelectedTreeNodeId(nodeId);
    highlightOpenScadSelection();
}

void MainWindow::selectTreeNodeInSceneTree(int treeNodeId)
{
    m_selectedTreeNodeId = treeNodeId;
    if (m_sceneTreeGraphics)
        m_sceneTreeGraphics->setSelectedTreeNodeId(treeNodeId);
    highlightOpenScadSelection();
}

void MainWindow::clearSelection()
{
    m_scene.setSelectedShapeId(-1);
    m_ctrlHighlight = CtrlParamHighlight();
    m_selectedTreeNodeId = 0;

    if (m_sceneTreeGraphics)
        m_sceneTreeGraphics->setSelectedTreeNodeId(0);

    if (m_viewport) {
        m_viewport->setSelectedIndex(-1);
        m_viewport->setSelectedGroupId(0);
        m_viewport->setTreeTransformControlPreview(0, SceneDocument::TreeNode::Union, -1);
        m_viewport->setTreeShapeParameterPreview(-1, -1);
        m_viewport->update();
    }

    highlightOpenScadSelection();
    refreshProperties();
}

int MainWindow::selectedTreeNodeIdForCodeHighlight() const
{
    return m_selectedTreeNodeId;
}

int MainWindow::selectedTreeGroupId() const
{
    if (m_selectedTreeNodeId <= 0)
        return 0;

    const SceneDocument::TreeNode *node = m_scene.treeNodeById(m_selectedTreeNodeId);
    if (!node)
        return 0;

    // If the selected node is itself a group, return its own ID.
    if (node->type == SceneDocument::TreeNode::Group)
        return m_selectedTreeNodeId;

    // Otherwise find the closest parent group via tree traversal.
    std::function<int(const SceneDocument::TreeNode &)> findParent =
        [&](const SceneDocument::TreeNode &parent) -> int {
        for (const auto &child : parent.children) {
            if (child.id == m_selectedTreeNodeId)
                return parent.id;
            const int found = findParent(child);
            if (found > 0)
                return found;
        }
        return 0;
    };
    return findParent(m_scene.treeRoot());
}

int MainWindow::selectedDirectGroupId() const
{
    if (m_selectedTreeNodeId <= 0)
        return 0;

    const SceneDocument::TreeNode *node = m_scene.treeNodeById(m_selectedTreeNodeId);
    if (!node || node->type != SceneDocument::TreeNode::Group)
        return 0;

    return m_selectedTreeNodeId;
}

void MainWindow::addGroup(SceneDocument::TreeNode::Operation operation)
{
    auto *command = new AddGroupCommand(&m_scene, operation, selectedTreeGroupId(), -1, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::moveTreeNodeToGroup(int nodeId, int parentGroupId, int insertIndex)
{
    const bool moduleParameterZone = decodeModuleParameterInsertIndex(&insertIndex);
    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (node && node->type == SceneDocument::TreeNode::Variable && parentGroupId > 0) {
        const SceneDocument::TreeNode *parentNode = m_scene.treeNodeById(parentGroupId);
        const bool targetIsRoot = parentGroupId == m_scene.treeRoot().id;
        const bool targetIsModule = parentNode
                                    && parentNode->type == SceneDocument::TreeNode::Group
                                    && parentNode->operation == SceneDocument::TreeNode::Module;
        if (!targetIsRoot && !targetIsModule)
            return;
    }
    if (node && node->type == SceneDocument::TreeNode::ModuleCall) {
        const SceneDocument::TreeNode *parentNode = m_scene.treeNodeById(parentGroupId);
        if (!parentNode || parentNode->type != SceneDocument::TreeNode::Group)
            return;
    }

    auto *command = new MoveTreeNodeCommand(&m_scene, nodeId, parentGroupId, insertIndex, [this]() {
        refreshSceneViews();
    }, moduleParameterZone);

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::refreshProperties()
{
}

void MainWindow::refreshOpenScadCode()
{
    // setPlainText resets the scroll position to the top — preserve it so that
    // adjusting a parameter in the tree doesn't jump the code view away.
    const int savedScroll = m_codeEditor->verticalScrollBar()->value();
    const QString code = OpenScadGenerator::generateWithSourceMap(m_scene, &m_openScadSourceRanges);
    m_codeEditor->setPlainText(code);
    m_codeEditor->verticalScrollBar()->setValue(savedScroll);
    highlightOpenScadSelection();
    writeOpenScadPreview(false);
}

void MainWindow::scrollCodeEditorToShowCursor(const QTextCursor &cursor)
{
    if (!m_codeEditor || cursor.isNull())
        return;
    const QRect r = m_codeEditor->cursorRect(cursor);
    const int vpH = m_codeEditor->viewport()->height();
    if (r.top() < 0 || r.bottom() > vpH) {
        QScrollBar *sb = m_codeEditor->verticalScrollBar();
        // Place the target line roughly one-third from the top.
        sb->setValue(sb->value() + r.top() - vpH / 3);
    }
}

void MainWindow::applyCtrlParamHighlight()
{
    const QString code = m_codeEditor->toPlainText();

    for (const OpenScadGenerator::SourceRange &range : m_openScadSourceRanges) {
        if (range.treeNodeId != m_ctrlHighlight.nodeId || range.length <= 0)
            continue;

        // Search within the node's range (capped to avoid matching children's code).
        // Prefix + expression together uniquely identify the token even when the same
        // numeric value appears in multiple parameters (e.g. cube([20,20,20])).
        const int searchCap = qMin(range.length, 300);
        const QString needle = m_ctrlHighlight.contextPrefix + m_ctrlHighlight.expression;
        const int hitPos = code.indexOf(needle, range.start);
        if (hitPos < 0 || hitPos >= range.start + searchCap)
            break;

        const int exprPos = hitPos + m_ctrlHighlight.contextPrefix.size();
        const int numStart = exprPos + m_ctrlHighlight.numberStart;
        const int numLen   = m_ctrlHighlight.numberLength;
        if (numStart + numLen > code.size())
            break;

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

    // Expression not found — code may not have been refreshed yet (selectShapeInSceneTree
    // is called before refreshOpenScadCode inside refreshShapeList). Keep active so the
    // next call from refreshOpenScadCode, which runs with the updated code, can apply it.
}

void MainWindow::highlightOpenScadSelection()
{
    if (!m_codeEditor)
        return;

    if (m_ctrlHighlight.active) {
        applyCtrlParamHighlight();
        return;
    }

    const int selectedTreeNodeId = selectedTreeNodeIdForCodeHighlight();
    QTextEdit::ExtraSelection selection;
    bool hasSelection = false;

    for (const OpenScadGenerator::SourceRange &range : m_openScadSourceRanges) {
        if (range.treeNodeId != selectedTreeNodeId || range.length <= 0)
            continue;

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

    m_codeEditor->setExtraSelections(hasSelection ? QList<QTextEdit::ExtraSelection>{selection}
                                                  : QList<QTextEdit::ExtraSelection>{});
    if (hasSelection)
        scrollCodeEditorToShowCursor(selection.cursor);
}

void MainWindow::refreshCsgStatus()
{
    if (!m_csgStatusLabel)
        return;

    if (m_viewport) {
        m_csgStatusLabel->setText(m_viewport->csgStatusText());
        return;
    }

    m_csgStatusLabel->setText(buildCsgPreview(m_scene).statusText);
}

QString MainWindow::previewScadPath() const
{
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("openscad_preview.scad");
}

QString MainWindow::examplesPath() const
{
    // Walk up from the exe until we find a directory that contains docs/sample_codes
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        QDir candidate(dir.absoluteFilePath("docs/sample_codes"));
        if (candidate.exists())
            return candidate.absolutePath();
        if (!dir.cdUp())
            break;
    }
    return QString();
}

void MainWindow::populateExamplesMenu(QMenu *menu)
{
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
        const QString name = QFileInfo(fileName).completeBaseName();
        QAction *action = menu->addAction(name);

        connect(action, &QAction::triggered, this, [this, filePath]() {
            loadExample(filePath);
        });

        // Start the hover preview timer when this action is highlighted.
        connect(action, &QAction::hovered, this, [this, filePath, name, menu]() {
            m_pendingPreviewFile = filePath;
            m_pendingPreviewName = name;
            // Right edge of the menu in global coordinates — used as popup's X anchor.
            m_pendingMenuRight   = menu->mapToGlobal(menu->rect().topRight()).x();
            m_pendingCursorY     = QCursor::pos().y();
            m_exampleHoverTimer->start(); // restarts if already running
        });
    }

    // Hide preview when the menu closes.
    connect(menu, &QMenu::aboutToHide, this, &MainWindow::hideExamplePreview);
}

void MainWindow::hideExamplePreview()
{
    m_exampleHoverTimer->stop();
    if (m_thumbnailWatcher->isRunning())
        m_thumbnailWatcher->cancel();
    m_examplePreview->hidePopup();
}

void MainWindow::onExampleHoverTimeout()
{
    // Show a "loading" placeholder immediately so the user gets feedback.
    m_examplePreview->setLoading(m_pendingPreviewName);
    m_examplePreview->showAt(m_pendingMenuRight, QCursor::pos().y());

    // If a previous render is still running, let it finish — its result will be
    // discarded in onExampleThumbnailReady if the file no longer matches.
    if (m_thumbnailWatcher->isRunning())
        return;

    const QString filePath = m_pendingPreviewFile;
    const QString name     = m_pendingPreviewName;

    // Parse + build SceneDocument + render thumbnail — all in a worker thread.
    QFuture<QImage> future = QtConcurrent::run([filePath]() -> QImage {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return QImage();
        const QString code = QString::fromUtf8(file.readAll());

        SceneDocument::Snapshot snapshot;
        if (!OpenScadParser::parseScene(code, &snapshot, nullptr, nullptr))
            return QImage();

        SceneDocument scene;
        scene.restoreSnapshot(snapshot);

        return ViewportWidget::renderThumbnail(scene, QSize(280, 210));
    });

    m_thumbnailWatcher->setFuture(future);
}

void MainWindow::onExampleThumbnailReady()
{
    if (m_thumbnailWatcher->isCanceled())
        return;

    const QImage image = m_thumbnailWatcher->result();
    // Only update if the popup is still visible (user hasn't moved away).
    if (m_examplePreview->isVisible())
        m_examplePreview->setImage(image, m_pendingPreviewName);
}

void MainWindow::loadExample(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Open Example", QString("Cannot open:\n%1").arg(filePath));
        return;
    }
    m_codeEditor->setPlainText(QString::fromUtf8(file.readAll()));
    applyOpenScadCode();
}

bool MainWindow::writeOpenScadPreview(bool notify)
{
    const QString path = previewScadPath();
    const QString directoryPath = QFileInfo(path).absolutePath();
    QDir().mkpath(directoryPath);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (notify) {
            QMessageBox::warning(this, "OpenSCAD preview error",
                                 QString("Cannot write preview file:\n%1").arg(path));
        }
        return false;
    }

    const QByteArray data = m_codeEditor->toPlainText().toUtf8();
    if (file.write(data) != data.size() || !file.commit()) {
        if (notify) {
            QMessageBox::warning(this, "OpenSCAD preview error",
                                 QString("Cannot finish writing preview file:\n%1").arg(path));
        }
        return false;
    }

    if (m_openScadPreviewLabel)
        m_openScadPreviewLabel->setText(QString("Preview file: %1").arg(QDir::toNativeSeparators(path)));

    return true;
}
