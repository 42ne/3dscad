#include "subsettestwindow.h"
#include "../../openscadparser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTextEdit>
#include <QToolBar>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QTextStream>

using TreeNode = SceneDocument::TreeNode;

// ─────────────────────────────────────────────────────────────────────────────
// UI construction
// ─────────────────────────────────────────────────────────────────────────────

SubsetTestWindow::SubsetTestWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("OpenSCAD Subset Test"));
    buildUi();
    loadFileList();
}

void SubsetTestWindow::buildUi()
{
    auto *toolbar = addToolBar(QStringLiteral("Main"));
    toolbar->setMovable(false);

    auto *btnRun = new QPushButton(QStringLiteral("Analyze All"), this);
    btnRun->setFixedHeight(28);
    toolbar->addWidget(btnRun);
    connect(btnRun, &QPushButton::clicked, this, &SubsetTestWindow::analyzeAll);

    // ── splitter ──────────────────────────────────────────────────────────
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(splitter);

    // left: file list
    m_fileList = new QListWidget(splitter);
    m_fileList->setFixedWidth(220);
    m_fileList->setAlternatingRowColors(true);
    connect(m_fileList, &QListWidget::currentRowChanged,
            this, &SubsetTestWindow::onFileSelected);

    // right: tabs
    m_tabs = new QTabWidget(splitter);

    m_report = new QTextEdit(m_tabs);
    m_report->setReadOnly(true);
    m_tabs->addTab(m_report, QStringLiteral("Report"));

    m_codeView = new QTextEdit(m_tabs);
    m_codeView->setReadOnly(true);
    m_codeView->setFont(QFont(QStringLiteral("Courier New"), 9));
    m_tabs->addTab(m_codeView, QStringLiteral("Generated Code"));

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    m_status = new QLabel(QStringLiteral("Ready — press Analyze All"), this);
    statusBar()->addWidget(m_status);
}

void SubsetTestWindow::loadFileList()
{
    QDir dir(QStringLiteral(TESTCODES_DIR));
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.scad"),
                                             QDir::Files, QDir::Name);
    for (const QString &f : files) {
        m_fileList->addItem(f);
        FileResult placeholder;
        placeholder.fileName = f;
        placeholder.filePath = dir.absoluteFilePath(f);
        m_results.append(placeholder);
    }

    if (m_results.isEmpty())
        m_report->setHtml(QStringLiteral("<p style='color:red'>No .scad files found in: ")
                          + dir.absolutePath() + QStringLiteral("</p>"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Slots
// ─────────────────────────────────────────────────────────────────────────────

void SubsetTestWindow::analyzeAll()
{
    m_results.clear();

    QDir dir(QStringLiteral(TESTCODES_DIR));
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.scad"),
                                             QDir::Files, QDir::Name);
    m_fileList->clear();

    int willRenderCount = 0;
    int parseOkCount    = 0;

    for (const QString &f : files) {
        m_fileList->addItem(f);

        FileResult r = analyzeFile(dir.absoluteFilePath(f));
        m_results.append(r);

        if (r.parseOk) ++parseOkCount;
        if (r.willRender) ++willRenderCount;

        // colour-code the list item
        QListWidgetItem *item = m_fileList->item(m_fileList->count() - 1);
        if (!r.parseOk)
            item->setBackground(QColor(255, 200, 200));
        else if (!r.willRender)
            item->setBackground(QColor(255, 245, 180));
        else
            item->setBackground(QColor(200, 240, 200));
    }

    m_status->setText(QString("Parsed OK: %1/%2   Will render: %3/%2")
                          .arg(parseOkCount).arg(m_results.size()).arg(willRenderCount));

    // Show summary in report tab
    m_report->setHtml(summaryHtml());
    m_codeView->clear();
}

void SubsetTestWindow::onFileSelected(int row)
{
    if (row < 0 || row >= m_results.size())
        return;
    const FileResult &r = m_results[row];
    if (r.filePath.isEmpty())
        return;

    // Re-analyze if not yet done (placeholder state)
    if (!r.parseOk && r.parseError.isEmpty() && r.allPrimitiveCount == 0 && r.varCount == 0) {
        m_results[row] = analyzeFile(r.filePath);
    }

    m_report->setHtml(buildReportHtml(m_results[row]));
    m_codeView->setPlainText(m_results[row].generatedCode);
}

// ─────────────────────────────────────────────────────────────────────────────
// Analysis
// ─────────────────────────────────────────────────────────────────────────────

FileResult SubsetTestWindow::analyzeFile(const QString &filePath) const
{
    FileResult r;
    r.filePath = filePath;
    r.fileName = QFileInfo(filePath).fileName();

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        r.parseError = QStringLiteral("Cannot open file");
        return r;
    }
    const QString code = QTextStream(&f).readAll();
    f.close();

    // ── parse ─────────────────────────────────────────────────────────────
    r.parseOk = OpenScadParser::parseScene(code, &r.snapshot,
                                            &r.parseError, &r.parseErrorLine);

    if (!r.parseOk)
        return r;

    // ── walk tree ─────────────────────────────────────────────────────────
    collectStats(r.snapshot.treeRoot, false, &r);
    determineRender(&r);

    // ── generate code from tree ───────────────────────────────────────────
    r.generatedCode = generateCode(r.snapshot);

    // ── roundtrip check ───────────────────────────────────────────────────
    if (!r.generatedCode.isEmpty()) {
        SceneDocument::Snapshot rt;
        QString rtErr;
        int rtLine = 0;
        if (OpenScadParser::parseScene(r.generatedCode, &rt, &rtErr, &rtLine)) {
            // count primitives in roundtrip tree
            FileResult rtResult;
            rtResult.snapshot = rt;
            collectStats(rt.treeRoot, false, &rtResult);
            r.roundtripOk       = true;
            r.roundtripNodeDiff = rtResult.allPrimitiveCount - r.allPrimitiveCount;
        }
    }

    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tree stats collector (recursive)
// ─────────────────────────────────────────────────────────────────────────────

void SubsetTestWindow::collectStats(const TreeNode &node,
                                     bool insideModule,
                                     FileResult *r)
{
    using Op   = TreeNode::Operation;
    using Type = TreeNode::Type;

    for (const TreeNode &child : node.children) {
        switch (child.type) {
        case Type::Variable:
            ++r->varCount;
            break;

        case Type::Primitive: {
            ++r->allPrimitiveCount;
            if (!insideModule) ++r->topPrimitiveCount;
            // find shape type
            for (const ShapeNode &s : r->snapshot.shapes) {
                if (s.id == child.shapeId) {
                    r->primitiveTypes[typeName(s.type)]++;
                    break;
                }
            }
            break;
        }

        case Type::ModuleCall:
            ++r->moduleCallCount;
            if (!child.moduleName.isEmpty()
                && !r->moduleNames.contains(child.moduleName))
                r->moduleNames.append(child.moduleName + QStringLiteral("()"));
            break;

        case Type::Group: {
            bool nowInsideModule = insideModule;
            switch (child.operation) {
            case Op::Scene:
                break; // transparent
            case Op::Module:
                ++r->moduleDefCount;
                if (!child.moduleName.isEmpty()
                    && !r->moduleNames.contains(child.moduleName))
                    r->moduleNames.append(child.moduleName);
                nowInsideModule = true;
                break;
            case Op::Translate:
                ++r->transformCount;
                r->transformTypes[QStringLiteral("translate")]++;
                break;
            case Op::Rotate:
                ++r->transformCount;
                r->transformTypes[QStringLiteral("rotate")]++;
                break;
            case Op::Scale:
                ++r->transformCount;
                r->transformTypes[QStringLiteral("scale")]++;
                break;
            case Op::Mirror:
                ++r->transformCount;
                r->transformTypes[QStringLiteral("mirror")]++;
                break;
            case Op::Color:
                ++r->colorCount;
                break;
            case Op::LinearExtrude:
            case Op::RotateExtrude:
                ++r->extrudeCount;
                break;
            case Op::Union:
            case Op::Difference:
            case Op::Intersection:
            case Op::Hull:
            case Op::Minkowski:
                ++r->booleanCount;
                r->booleanTypes[opName(child.operation)]++;
                break;
            case Op::For:
                ++r->forLoopCount;
                break;
            default:
                ++r->otherGroupCount;
                break;
            }
            collectStats(child, nowInsideModule, r);
            break;
        }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Render feasibility
// ─────────────────────────────────────────────────────────────────────────────

void SubsetTestWindow::determineRender(FileResult *r)
{
    if (r->topPrimitiveCount > 0) {
        r->willRender   = true;
        r->renderReason = QString("Has %1 primitive(s) at top level").arg(r->topPrimitiveCount);
        return;
    }

    if (r->moduleCallCount > 0 && r->moduleDefCount > 0) {
        r->willRender   = true;
        r->renderReason = QString("Module(s) defined and called (%1 call(s))").arg(r->moduleCallCount);
        return;
    }

    if (r->moduleCallCount > 0 && r->moduleDefCount == 0) {
        r->willRender   = false;
        r->renderReason = QString("Module call(s) found but no definitions parsed (external modules)");
        return;
    }

    if (r->moduleDefCount > 0 && r->moduleCallCount == 0) {
        r->willRender   = false;
        r->renderReason = QString("%1 module(s) defined but never called").arg(r->moduleDefCount);
        return;
    }

    if (r->varCount > 0 && r->allPrimitiveCount == 0) {
        r->willRender   = false;
        r->renderReason = QStringLiteral("Only variable definitions, no geometry");
        return;
    }

    if (r->allPrimitiveCount == 0) {
        r->willRender   = false;
        r->renderReason = QStringLiteral("No primitives found in tree");
        return;
    }

    // primitives exist but only inside module defs with no calls
    r->willRender   = false;
    r->renderReason = QStringLiteral("All geometry is inside module definitions with no top-level calls");
}

// ─────────────────────────────────────────────────────────────────────────────
// Simple tree → code generator
// ─────────────────────────────────────────────────────────────────────────────

QString SubsetTestWindow::generateCode(const SceneDocument::Snapshot &snap)
{
    QString code;
    code += QStringLiteral("// Generated from scene tree\n\n");
    code += nodeToCode(snap.treeRoot, snap.shapes, 0);
    return code;
}

QString SubsetTestWindow::shapeToCode(const ShapeNode &s)
{
    auto fmt = [](float v) -> QString {
        if (v == static_cast<int>(v))
            return QString::number(static_cast<int>(v));
        return QString::number(static_cast<double>(v), 'g', 6);
    };

    switch (s.type) {
    case ShapeNode::Cube: {
        QString args;
        if (!s.parameterExpressions.isEmpty() && s.parameterExpressions.size() >= 3) {
            args = QString("[%1, %2, %3]").arg(s.parameterExpressions[0],
                                                s.parameterExpressions[1],
                                                s.parameterExpressions[2]);
        } else {
            args = QString("[%1, %2, %3]").arg(fmt(s.size.x()), fmt(s.size.y()), fmt(s.size.z()));
        }
        return QString("cube(%1%2);").arg(args, s.center ? QStringLiteral(", center=true") : QString());
    }
    case ShapeNode::Sphere:
        return QString("sphere(r=%1);").arg(fmt(s.radius));
    case ShapeNode::Cylinder:
        return QString("cylinder(h=%1, r=%2%3);")
            .arg(fmt(s.height), fmt(s.radius),
                 s.center ? QStringLiteral(", center=true") : QString());
    case ShapeNode::Cone:
        return QString("cylinder(h=%1, r1=%2, r2=%3%4);")
            .arg(fmt(s.height), fmt(s.radius), fmt(s.radius2),
                 s.center ? QStringLiteral(", center=true") : QString());
    case ShapeNode::Circle:
        return QString("circle(r=%1);").arg(fmt(s.radius));
    case ShapeNode::Square:
        return QString("square([%1, %2]%3);")
            .arg(fmt(s.size.x()), fmt(s.size.y()),
                 s.center ? QStringLiteral(", center=true") : QString());
    default:
        return QStringLiteral("/* unsupported shape */");
    }
}

QString SubsetTestWindow::nodeToCode(const TreeNode &node,
                                      const QVector<ShapeNode> &shapes,
                                      int depth)
{
    using Op   = TreeNode::Operation;
    using Type = TreeNode::Type;

    const QString ind(depth * 4, QLatin1Char(' '));
    const QString ind1((depth + 1) * 4, QLatin1Char(' '));
    QString out;

    auto childrenCode = [&]() -> QString {
        QString c;
        for (const TreeNode &ch : node.children)
            c += nodeToCode(ch, shapes, depth + 1);
        return c;
    };

    auto vecStr = [](const QVector3D &v, const QStringList &exprs) -> QString {
        if (exprs.size() == 3)
            return QString("[%1, %2, %3]").arg(exprs[0], exprs[1], exprs[2]);
        auto f = [](float x) -> QString {
            if (x == static_cast<int>(x)) return QString::number(static_cast<int>(x));
            return QString::number(static_cast<double>(x), 'g', 6);
        };
        return QString("[%1, %2, %3]").arg(f(v.x()), f(v.y()), f(v.z()));
    };

    switch (node.type) {
    case Type::Variable: {
        const QString expr = node.variableExpression.trimmed().isEmpty()
                                 ? QString::number(node.variableValue)
                                 : node.variableExpression.trimmed();
        out = ind + node.variableName + QStringLiteral(" = ") + expr + QStringLiteral(";\n");
        break;
    }
    case Type::Primitive: {
        for (const ShapeNode &s : shapes) {
            if (s.id == node.shapeId) {
                out = ind + shapeToCode(s) + QStringLiteral("\n");
                break;
            }
        }
        break;
    }
    case Type::ModuleCall: {
        const QString args = node.moduleCallArguments.trimmed();
        out = ind + node.moduleName + QStringLiteral("(")
              + args + QStringLiteral(");\n");
        break;
    }
    case Type::Group: {
        switch (node.operation) {
        case Op::Scene:
            for (const TreeNode &ch : node.children)
                out += nodeToCode(ch, shapes, depth);
            break;
        case Op::Module: {
            out  = ind + QStringLiteral("module ") + node.moduleName + QStringLiteral("() {\n");
            for (const TreeNode &ch : node.children)
                if (!ch.isParameter)
                    out += nodeToCode(ch, shapes, depth + 1);
            out += ind + QStringLiteral("}\n");
            break;
        }
        case Op::Translate:
            out  = ind + QStringLiteral("translate(") + vecStr(node.position, node.transformExpressions) + QStringLiteral(") {\n");
            out += childrenCode();
            out += ind + QStringLiteral("}\n");
            break;
        case Op::Rotate:
            out  = ind + QStringLiteral("rotate(") + vecStr(node.rotation, node.transformExpressions) + QStringLiteral(") {\n");
            out += childrenCode();
            out += ind + QStringLiteral("}\n");
            break;
        case Op::Scale:
            out  = ind + QStringLiteral("scale(") + vecStr(node.scale, node.transformExpressions) + QStringLiteral(") {\n");
            out += childrenCode();
            out += ind + QStringLiteral("}\n");
            break;
        case Op::Mirror:
            out  = ind + QStringLiteral("mirror(") + vecStr(node.position, node.transformExpressions) + QStringLiteral(") {\n");
            out += childrenCode();
            out += ind + QStringLiteral("}\n");
            break;
        case Op::Color: {
            const QColor &c = node.color;
            out  = ind + QString("color([%1, %2, %3]) {\n")
                             .arg(c.redF(), 0, 'f', 3)
                             .arg(c.greenF(), 0, 'f', 3)
                             .arg(c.blueF(), 0, 'f', 3);
            out += childrenCode();
            out += ind + QStringLiteral("}\n");
            break;
        }
        case Op::LinearExtrude: {
            const QString hExpr = !node.transformExpressions.isEmpty()
                                  && !node.transformExpressions[0].trimmed().isEmpty()
                                  ? node.transformExpressions[0].trimmed()
                                  : QString::number(static_cast<double>(node.scale.x()), 'g');
            out  = ind + QStringLiteral("linear_extrude(height=") + hExpr + QStringLiteral(") {\n");
            out += childrenCode();
            out += ind + QStringLiteral("}\n");
            break;
        }
        case Op::RotateExtrude: {
            const QString aExpr = !node.transformExpressions.isEmpty()
                                  && !node.transformExpressions[0].trimmed().isEmpty()
                                  ? node.transformExpressions[0].trimmed()
                                  : QString::number(static_cast<double>(node.scale.x()), 'g');
            out  = ind + QStringLiteral("rotate_extrude(angle=") + aExpr + QStringLiteral(") {\n");
            out += childrenCode();
            out += ind + QStringLiteral("}\n");
            break;
        }
        case Op::Union:
            out  = ind + QStringLiteral("union() {\n");
            out += childrenCode();
            out += ind + QStringLiteral("}\n");
            break;
        case Op::Difference:
            out  = ind + QStringLiteral("difference() {\n");
            out += childrenCode();
            out += ind + QStringLiteral("}\n");
            break;
        case Op::Intersection:
            out  = ind + QStringLiteral("intersection() {\n");
            out += childrenCode();
            out += ind + QStringLiteral("}\n");
            break;
        case Op::Hull:
            out  = ind + QStringLiteral("hull() {\n");
            out += childrenCode();
            out += ind + QStringLiteral("}\n");
            break;
        case Op::Minkowski:
            out  = ind + QStringLiteral("minkowski() {\n");
            out += childrenCode();
            out += ind + QStringLiteral("}\n");
            break;
        case Op::For:
            out  = ind + QString("for (%1 = %2) {\n")
                             .arg(node.loopVariable, node.loopRangeExpression);
            out += childrenCode();
            out += ind + QStringLiteral("}\n");
            break;
        default:
            out += childrenCode();
            break;
        }
        break;
    }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// HTML report builders
// ─────────────────────────────────────────────────────────────────────────────

static QString h2(const QString &t) {
    return QStringLiteral("<h3 style='margin:6px 0 2px 0'>") + t + QStringLiteral("</h3>");
}
static QString row(const QString &k, const QString &v, const QString &color = QString()) {
    const QString style = color.isEmpty() ? QString() :
        QStringLiteral(" style='color:") + color + QStringLiteral("'");
    return QStringLiteral("<tr><td style='padding-right:12px'>") + k
         + QStringLiteral("</td><td") + style + QStringLiteral("><b>") + v
         + QStringLiteral("</b></td></tr>");
}

QString SubsetTestWindow::buildReportHtml(const FileResult &r) const
{
    QString html;
    html += QStringLiteral("<html><body style='font-family:sans-serif;font-size:13px'>");
    html += QStringLiteral("<h2>") + r.fileName + QStringLiteral("</h2>");

    // ── parse status ──────────────────────────────────────────────────────
    html += h2(QStringLiteral("Parse"));
    html += QStringLiteral("<table>");
    if (r.parseOk) {
        html += row(QStringLiteral("Status"), QStringLiteral("OK"), QStringLiteral("green"));
    } else {
        html += row(QStringLiteral("Status"), QStringLiteral("FAILED"), QStringLiteral("red"));
        html += row(QStringLiteral("Error"),
                    QString("Line %1: %2").arg(r.parseErrorLine).arg(r.parseError.toHtmlEscaped()),
                    QStringLiteral("red"));
        html += QStringLiteral("</table></body></html>");
        return html;
    }
    html += QStringLiteral("</table>");

    // ── nodes ─────────────────────────────────────────────────────────────
    html += h2(QStringLiteral("Nodes"));
    html += QStringLiteral("<table>");
    html += row(QStringLiteral("Variables"), QString::number(r.varCount));
    html += row(QStringLiteral("Primitives (top-level)"),
                QString("%1 / %2 total").arg(r.topPrimitiveCount).arg(r.allPrimitiveCount));

    if (!r.primitiveTypes.isEmpty()) {
        QStringList pts;
        for (auto it = r.primitiveTypes.begin(); it != r.primitiveTypes.end(); ++it)
            pts << QString("%1×%2").arg(it.value()).arg(it.key());
        html += row(QStringLiteral("  types"), pts.join(QStringLiteral(", ")));
    }

    html += row(QStringLiteral("Transforms"), QString::number(r.transformCount));
    if (!r.transformTypes.isEmpty()) {
        QStringList tt;
        for (auto it = r.transformTypes.begin(); it != r.transformTypes.end(); ++it)
            tt << QString("%1×%2").arg(it.value()).arg(it.key());
        html += row(QStringLiteral("  types"), tt.join(QStringLiteral(", ")));
    }

    html += row(QStringLiteral("Booleans"), QString::number(r.booleanCount));
    if (!r.booleanTypes.isEmpty()) {
        QStringList bt;
        for (auto it = r.booleanTypes.begin(); it != r.booleanTypes.end(); ++it)
            bt << QString("%1×%2").arg(it.value()).arg(it.key());
        html += row(QStringLiteral("  types"), bt.join(QStringLiteral(", ")));
    }

    html += row(QStringLiteral("Colors"), QString::number(r.colorCount));
    html += row(QStringLiteral("Extrudes"), QString::number(r.extrudeCount));
    html += row(QStringLiteral("For loops"), QString::number(r.forLoopCount));
    html += row(QStringLiteral("Module definitions"), QString::number(r.moduleDefCount));
    html += row(QStringLiteral("Module calls"),
                r.moduleCallCount == 0
                    ? QStringLiteral("0")
                    : QString("<span style='color:orange'>%1 — no direct tree equivalent</span>")
                          .arg(r.moduleCallCount));

    html += QStringLiteral("</table>");

    // ── module names ──────────────────────────────────────────────────────
    if (!r.moduleNames.isEmpty()) {
        html += h2(QStringLiteral("Modules"));
        html += QStringLiteral("<ul style='margin:2px'>");
        for (const QString &m : r.moduleNames)
            html += QStringLiteral("<li>") + m.toHtmlEscaped() + QStringLiteral("</li>");
        html += QStringLiteral("</ul>");
    }

    // ── render verdict ────────────────────────────────────────────────────
    html += h2(QStringLiteral("Render"));
    html += QStringLiteral("<table>");
    if (r.willRender) {
        html += row(QStringLiteral("Verdict"),
                    QStringLiteral("WILL RENDER"), QStringLiteral("green"));
    } else {
        html += row(QStringLiteral("Verdict"),
                    QStringLiteral("WILL NOT RENDER"), QStringLiteral("darkorange"));
    }
    html += row(QStringLiteral("Reason"), r.renderReason.toHtmlEscaped());
    html += QStringLiteral("</table>");

    // ── roundtrip ─────────────────────────────────────────────────────────
    html += h2(QStringLiteral("Code → Tree → Code Roundtrip"));
    html += QStringLiteral("<table>");
    if (r.roundtripOk) {
        const QString diff = r.roundtripNodeDiff == 0
            ? QStringLiteral("✓ primitive count unchanged")
            : QString("⚠ primitive count Δ%1").arg(r.roundtripNodeDiff);
        const QString color = r.roundtripNodeDiff == 0
            ? QStringLiteral("green") : QStringLiteral("darkorange");
        html += row(QStringLiteral("Re-parse"), diff, color);
    } else {
        html += row(QStringLiteral("Re-parse"),
                    QStringLiteral("Generated code did not re-parse cleanly"),
                    QStringLiteral("red"));
    }
    html += QStringLiteral("</table>");
    html += QStringLiteral("<p style='color:gray;font-size:11px'>"
                           "See \"Generated Code\" tab for tree→code output.</p>");

    html += QStringLiteral("</body></html>");
    return html;
}

QString SubsetTestWindow::summaryHtml() const
{
    int ok = 0, render = 0;
    for (const FileResult &r : m_results) {
        if (r.parseOk) ++ok;
        if (r.willRender) ++render;
    }

    QString html;
    html += QStringLiteral("<html><body style='font-family:sans-serif;font-size:13px'>");
    html += QStringLiteral("<h2>Analysis Summary</h2>");
    html += QStringLiteral("<table>");
    html += row(QStringLiteral("Files"),      QString::number(m_results.size()));
    html += row(QStringLiteral("Parsed OK"),  QString::number(ok));
    html += row(QStringLiteral("Will render"),QString::number(render));
    html += QStringLiteral("</table>");

    html += QStringLiteral("<br><table border='1' cellpadding='4' cellspacing='0' "
                           "style='border-collapse:collapse'>");
    html += QStringLiteral("<tr style='background:#ddd'>"
                           "<th>File</th><th>Parse</th><th>Primitives</th>"
                           "<th>Modules</th><th>Calls</th><th>Render</th></tr>");

    for (const FileResult &r : m_results) {
        const QString parseTd = r.parseOk
            ? QStringLiteral("<td style='color:green'>OK</td>")
            : QStringLiteral("<td style='color:red'>ERROR L") + QString::number(r.parseErrorLine)
              + QStringLiteral("</td>");
        const QString renderTd = r.willRender
            ? QStringLiteral("<td style='color:green'>YES</td>")
            : QStringLiteral("<td style='color:darkorange'>NO</td>");

        html += QStringLiteral("<tr><td>") + r.fileName.toHtmlEscaped()
             + QStringLiteral("</td>") + parseTd
             + QStringLiteral("<td>") + QString::number(r.topPrimitiveCount)
             + QStringLiteral("/") + QString::number(r.allPrimitiveCount)
             + QStringLiteral("</td><td>") + QString::number(r.moduleDefCount)
             + QStringLiteral("</td><td>") + QString::number(r.moduleCallCount)
             + QStringLiteral("</td>") + renderTd
             + QStringLiteral("</tr>");
    }
    html += QStringLiteral("</table>");
    html += QStringLiteral("<p style='color:gray;font-size:11px'>"
                           "Click a file in the list for detailed report.</p>");
    html += QStringLiteral("</body></html>");
    return html;
}

// ─────────────────────────────────────────────────────────────────────────────
// Name helpers
// ─────────────────────────────────────────────────────────────────────────────

QString SubsetTestWindow::opName(TreeNode::Operation op)
{
    using Op = TreeNode::Operation;
    switch (op) {
    case Op::Union:         return QStringLiteral("union");
    case Op::Difference:    return QStringLiteral("difference");
    case Op::Intersection:  return QStringLiteral("intersection");
    case Op::Hull:          return QStringLiteral("hull");
    case Op::Minkowski:     return QStringLiteral("minkowski");
    case Op::Translate:     return QStringLiteral("translate");
    case Op::Rotate:        return QStringLiteral("rotate");
    case Op::Scale:         return QStringLiteral("scale");
    case Op::Mirror:        return QStringLiteral("mirror");
    case Op::Color:         return QStringLiteral("color");
    case Op::LinearExtrude: return QStringLiteral("linear_extrude");
    case Op::RotateExtrude: return QStringLiteral("rotate_extrude");
    case Op::For:           return QStringLiteral("for");
    case Op::Module:        return QStringLiteral("module");
    default:                return QStringLiteral("?");
    }
}

QString SubsetTestWindow::typeName(ShapeNode::Type t)
{
    switch (t) {
    case ShapeNode::Cube:       return QStringLiteral("Cube");
    case ShapeNode::Sphere:     return QStringLiteral("Sphere");
    case ShapeNode::Cylinder:   return QStringLiteral("Cylinder");
    case ShapeNode::Cone:       return QStringLiteral("Cone");
    case ShapeNode::Circle:     return QStringLiteral("Circle");
    case ShapeNode::Square:     return QStringLiteral("Square");
    case ShapeNode::Polygon2D:  return QStringLiteral("Polygon2D");
    case ShapeNode::Polyhedron: return QStringLiteral("Polyhedron");
    case ShapeNode::Point3D:    return QStringLiteral("Point3D");
    case ShapeNode::Face3D:     return QStringLiteral("Face3D");
    default:                    return QStringLiteral("Unknown");
    }
}
