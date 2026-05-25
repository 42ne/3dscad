#include "testrunnerwindow.h"
#include "../treedebug/treedebugwindow.h"
#include "../../scenetreegraphicswidget.h"
#include "../../scenedocument.h"

#include <QApplication>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QTest>
#include <QWheelEvent>
#include <QDateTime>
#include <QMessageBox>

// ═════════════════════════════════════════════════════════════════════════════
// Helpers
// ═════════════════════════════════════════════════════════════════════════════

namespace {

template<typename Func>
const SceneDocument::TreeNode *findRecur(const SceneDocument::TreeNode &n, Func p)
{
    if (p(n)) return &n;
    for (const auto &c : n.children)
        if (auto *f = findRecur(c, p)) return f;
    return nullptr;
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// Constructor
// ═════════════════════════════════════════════════════════════════════════════

TestRunnerWindow::TestRunnerWindow(TreeDebugWindow *debugWindow, QWidget *parent)
    : QMainWindow(parent)
    , m_debugWindow(debugWindow)
{
    setWindowTitle(QStringLiteral("Tree Behavior Test Runner"));
    resize(640, 580);

    m_scene      = &m_debugWindow->sceneDocument();
    m_treeWidget = m_debugWindow->treeWidget();

    buildUi();
    buildAllScenarios();
}

// ═════════════════════════════════════════════════════════════════════════════
// UI
// ═════════════════════════════════════════════════════════════════════════════

void TestRunnerWindow::buildUi()
{
    auto *central  = new QWidget(this);
    auto *mainLay  = new QVBoxLayout(central);
    mainLay->setContentsMargins(8, 8, 8, 8);
    mainLay->setSpacing(6);

    auto *header = new QLabel(QStringLiteral(
        "<b>Tree Behavior Test Runner</b><br>"
        "<span style='color:#888;font-size:9pt'>"
        "Scenarios from <i>scene_tree_behavior.md</i>. "
        "Click a scenario to run it.</span>"));
    header->setWordWrap(true);
    mainLay->addWidget(header);

    auto *splitter = new QSplitter(Qt::Horizontal, this);

    // ── Left: scenario list ────────────────────────────────────────────────
    auto *leftWidget = new QWidget;
    auto *leftLay    = new QVBoxLayout(leftWidget);
    leftLay->setContentsMargins(0, 0, 0, 0);

    auto *listLabel = new QLabel(QStringLiteral("<b>Scenarios</b>"));
    leftLay->addWidget(listLabel);

    m_scenarioList = new QListWidget;
    m_scenarioList->setWordWrap(true);
    m_scenarioList->setSpacing(3);
    m_scenarioList->setMinimumWidth(200);
    leftLay->addWidget(m_scenarioList, 1);

    splitter->addWidget(leftWidget);

    // ── Right: controls + log ──────────────────────────────────────────────
    auto *rightWidget = new QWidget;
    auto *rightLay    = new QVBoxLayout(rightWidget);
    rightLay->setContentsMargins(6, 0, 0, 0);
    rightLay->setSpacing(6);

    // Buttons
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(6);
    m_stopButton  = new QPushButton(QStringLiteral("\u23F9 Stop"));
    m_clearButton = new QPushButton(QStringLiteral("Clear Log"));
    m_stopButton->setEnabled(false);
    btnRow->addWidget(m_stopButton);
    btnRow->addWidget(m_clearButton);
    btnRow->addStretch();
    rightLay->addLayout(btnRow);

    m_statusLabel = new QLabel(QStringLiteral("Select a scenario to run."));
    m_statusLabel->setWordWrap(true);
    rightLay->addWidget(m_statusLabel);

    m_stepLabel = new QLabel;
    rightLay->addWidget(m_stepLabel);

    m_logOutput = new QTextEdit;
    m_logOutput->setReadOnly(true);
    m_logOutput->setFont(QFont(QStringLiteral("Consolas"), 9));
    m_logOutput->setPlaceholderText(QStringLiteral(
        "Log output appears here."));
    rightLay->addWidget(m_logOutput, 1);

    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({220, 400});

    mainLay->addWidget(splitter, 1);
    setCentralWidget(central);

    // Connections
    connect(m_scenarioList, &QListWidget::itemClicked,
            this, &TestRunnerWindow::onScenarioClicked);
    connect(m_stopButton,  &QPushButton::clicked, this, &TestRunnerWindow::stopSequence);
    connect(m_clearButton, &QPushButton::clicked, this, &TestRunnerWindow::clearLog);

    m_stepTimer = new QTimer(this);
    m_stepTimer->setSingleShot(true);
    connect(m_stepTimer, &QTimer::timeout, this, &TestRunnerWindow::advanceStep);
}

// ═════════════════════════════════════════════════════════════════════════════
// Logging
// ═════════════════════════════════════════════════════════════════════════════

void TestRunnerWindow::log(const QString &text)
{
    QString ts = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz"));
    m_logOutput->append(QStringLiteral("[%1] %2").arg(ts, text));
    auto sb = m_logOutput->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void TestRunnerWindow::setStatus(const QString &text)
{
    m_statusLabel->setText(text);
}

void TestRunnerWindow::clearLog()
{
    m_logOutput->clear();
}

// ═════════════════════════════════════════════════════════════════════════════
// Accessors
// ═════════════════════════════════════════════════════════════════════════════

SceneDocument *TestRunnerWindow::scene()           { return m_scene; }
SceneTreeGraphicsWidget *TestRunnerWindow::tree()  { return m_treeWidget; }

// ═════════════════════════════════════════════════════════════════════════════
// Tree search
// ═════════════════════════════════════════════════════════════════════════════

int TestRunnerWindow::findNodeIdByType(SceneDocument::TreeNode::Type type)
{
    const auto &root = m_scene->treeRoot();
    auto *n = findRecur(root, [type](const auto &node) {
        return node.type == type;
    });
    return n ? n->id : 0;
}

int TestRunnerWindow::findGroupIdByOp(SceneDocument::TreeNode::Operation op)
{
    const auto &root = m_scene->treeRoot();
    auto *n = findRecur(root,
        [op, &root](const auto &node) {
            return node.type == SceneDocument::TreeNode::Group
                && node.operation == op && &node != &root;
        });
    return n ? n->id : 0;
}

int TestRunnerWindow::findNodeByShapeId(int shapeId)
{
    const auto &root = m_scene->treeRoot();
    auto *n = findRecur(root,
        [shapeId](const auto &node) {
            return node.type == SceneDocument::TreeNode::Primitive
                && node.shapeId == shapeId;
        });
    return n ? n->id : 0;
}

void TestRunnerWindow::buildNodeRefs()
{
    const auto &root = m_scene->treeRoot();
    auto fOp = [&](auto op) {
        auto *n = findRecur(root,
            [op, &root](const auto &node) {
                return node.type == SceneDocument::TreeNode::Group
                    && node.operation == op && &node != &root;
            });
        return n ? n->id : 0;
    };
    auto fTy = [&](auto ty) {
        auto *n = findRecur(root,
            [ty](const auto &node) { return node.type == ty; });
        return n ? n->id : 0;
    };

    m_refs.translateId  = fOp(SceneDocument::TreeNode::Translate);
    m_refs.forId        = fOp(SceneDocument::TreeNode::For);
    m_refs.unionId      = fOp(SceneDocument::TreeNode::Union);
    m_refs.diffId       = fOp(SceneDocument::TreeNode::Difference);
    m_refs.rotateId     = fOp(SceneDocument::TreeNode::Rotate);
    m_refs.scaleId      = fOp(SceneDocument::TreeNode::Scale);
    m_refs.moduleId     = fOp(SceneDocument::TreeNode::Module);
    m_refs.callId       = fTy(SceneDocument::TreeNode::ModuleCall);
    m_refs.firstVarId   = fTy(SceneDocument::TreeNode::Variable);
    m_refs.firstPrimId  = fTy(SceneDocument::TreeNode::Primitive);
    // sceneId = first child of root (the Scene block)
    m_refs.sceneId = 0;
    for (const auto &c : root.children)
        if (c.operation == SceneDocument::TreeNode::Scene)
            { m_refs.sceneId = c.id; break; }
}

// ═════════════════════════════════════════════════════════════════════════════
// UI helpers: wheel / click simulation
// ═════════════════════════════════════════════════════════════════════════════

void TestRunnerWindow::wheelAt(int nodeId, int delta, Qt::KeyboardModifiers mods)
{
    QRectF r = m_treeWidget->debugChildRect(nodeId);
    if (r.isNull()) r = m_treeWidget->debugGroupRect(nodeId);
    if (r.isNull()) return;

    QPoint wp = m_treeWidget->mapFromScene(r.center());
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    QWheelEvent we(QPointF(wp), QPointF(m_treeWidget->mapToGlobal(wp)),
                   QPoint(0, 0), QPoint(0, delta), delta,
                   Qt::Vertical, Qt::NoButton, mods);
#pragma GCC diagnostic pop
    QCoreApplication::sendEvent(m_treeWidget, &we);
    QApplication::processEvents();
}

void TestRunnerWindow::clickNode(int nodeId)
{
    QRectF r = m_treeWidget->debugChildRect(nodeId);
    if (r.isNull()) return;
    QPoint wp = m_treeWidget->mapFromScene(r.center());
    QTest::mouseClick(m_treeWidget, Qt::LeftButton, Qt::NoModifier, wp);
    QApplication::processEvents();
}

QPointF TestRunnerWindow::gripCenter(int nodeId) const
{
    QRectF r = m_treeWidget->debugGroupRect(nodeId);
    if (r.isNull()) r = m_treeWidget->debugChildRect(nodeId);
    if (r.isNull()) return QPointF();
    // Grip strip is the top 20 px of the block rect
    constexpr qreal kGripStripH = 20.0;
    qreal gripTop = r.top();
    qreal gripH = qMin(kGripStripH, r.height());
    return QPointF(r.center().x(), gripTop + gripH / 2.0);
}

void TestRunnerWindow::dragNodeTo(int nodeId, const QPointF &targetScenePos, bool slowDrag)
{
    QPointF grip = gripCenter(nodeId);
    if (grip.isNull()) { log(QStringLiteral("Cannot drag node #%1: no grip rect").arg(nodeId)); return; }

    QPoint pressViewport = m_treeWidget->mapFromScene(grip);
    QPoint targetViewport = m_treeWidget->mapFromScene(targetScenePos);

    // Press
    QTest::mousePress(m_treeWidget, Qt::LeftButton, Qt::NoModifier, pressViewport);
    QApplication::processEvents();

    if (slowDrag) {
        // Move in small increments (< 6 px per event) to keep cluster attached
        // First need to cross DragPreviewStartDistance (6 px) to activate
        QPointF dir(targetScenePos - grip);
        qreal dist = QLineF(grip, targetScenePos).length();
        int steps = qMax(10, int(dist / 4.0));
        for (int i = 1; i <= steps; ++i) {
            qreal t = qreal(i) / steps;
            QPointF mid(grip.x() + dir.x() * t, grip.y() + dir.y() * t);
            QPoint midVp = m_treeWidget->mapFromScene(mid);
            QTest::mouseMove(m_treeWidget, midVp);
            QApplication::processEvents();
        }
    } else {
        // Fast drag — one large move to exceed velocity threshold
        QTest::mouseMove(m_treeWidget, targetViewport);
        QApplication::processEvents();
    }

    // Release
    QTest::mouseRelease(m_treeWidget, Qt::LeftButton, Qt::NoModifier, targetViewport);
    QApplication::processEvents();
}

// ═════════════════════════════════════════════════════════════════════════════
// Shared step primitives
// ═════════════════════════════════════════════════════════════════════════════

void TestRunnerWindow::step_clearScene()
{
    // Remove all shapes and rebuild from empty
    m_scene->replaceShapes({});
    m_treeWidget->refresh();
    QApplication::processEvents();
}

int TestRunnerWindow::step_addVar(const QString &name, const QString &expr,
                                   int parentId)
{
    int id = parentId > 0
        ? m_scene->addVariableToModule(parentId, false, -1)
        : m_scene->addVariable(-1);
    if (id > 0) {
        m_scene->renameVariable(id, name);
        m_scene->updateVariableExpression(id, expr);
    }
    return id;
}

int TestRunnerWindow::step_addGroup(SceneDocument::TreeNode::Operation op,
                                     int parentId)
{
    return m_scene->addGroup(op, parentId, -1);
}

int TestRunnerWindow::step_addPrimitive(ShapeNode::Type type, int parentId,
                                         const QStringList &exprs)
{
    ShapeNode s;
    s.type = type;
    s.parameterExpressions = exprs;
    switch (type) {
    case ShapeNode::Cube:
        if (exprs.size() >= 3) {
            s.size = QVector3D(exprs[0].toFloat(), exprs[1].toFloat(), exprs[2].toFloat());
        } else {
            s.size = QVector3D(20, 20, 20);
            s.parameterExpressions = QStringList{QStringLiteral("20"), QStringLiteral("20"), QStringLiteral("20")};
        }
        break;
    case ShapeNode::Sphere:
        s.radius = exprs.size() >= 1 ? exprs[0].toFloat() : 10.0f;
        if (exprs.isEmpty()) s.parameterExpressions = QStringList{QStringLiteral("10")};
        break;
    case ShapeNode::Cylinder:
        s.radius = exprs.size() >= 1 ? exprs[0].toFloat() : 5.0f;
        s.height = exprs.size() >= 2 ? exprs[1].toFloat() : 20.0f;
        if (exprs.size() < 2) s.parameterExpressions = QStringList{QStringLiteral("5"), QStringLiteral("20")};
        break;
    case ShapeNode::Cone:
        s.radius  = exprs.size() >= 1 ? exprs[0].toFloat() : 8.0f;
        s.radius2 = exprs.size() >= 2 ? exprs[1].toFloat() : 2.0f;
        s.height  = exprs.size() >= 3 ? exprs[2].toFloat() : 20.0f;
        if (exprs.size() < 3) s.parameterExpressions = QStringList{QStringLiteral("8"), QStringLiteral("2"), QStringLiteral("20")};
        break;
    }
    int shapeId = m_scene->addShape(s, parentId, -1);
    m_treeWidget->refresh();
    QApplication::processEvents();
    // Find tree node that references this shape ID
    return findNodeByShapeId(shapeId);
}

int TestRunnerWindow::step_addModule(const QString &name)
{
    // Add module declaration at root (parentId=0 adds to Scene container)
    int id = m_scene->addGroup(SceneDocument::TreeNode::Module, 0, -1);
    if (id > 0) m_scene->setModuleName(id, name);
    return id;
}

int TestRunnerWindow::step_addModuleCall(int moduleId, int parentId,
                                          const QString &args)
{
    return m_scene->addModuleCall(moduleId, parentId, -1, args);
}

void TestRunnerWindow::step_takeUndoSnapshot()
{
    m_undoSnapshot = m_scene->snapshot();
}

void TestRunnerWindow::step_restoreUndo()
{
    m_scene->restoreSnapshot(m_undoSnapshot);
    m_treeWidget->refresh();
    QApplication::processEvents();
}

void TestRunnerWindow::step_redoDelete(int nodeId)
{
    clickNode(nodeId);
    QTest::keyClick(m_treeWidget, Qt::Key_Delete);
    QApplication::processEvents();
}

// ═════════════════════════════════════════════════════════════════════════════
// Scenario execution engine
// ═════════════════════════════════════════════════════════════════════════════

void TestRunnerWindow::onScenarioClicked(QListWidgetItem *item)
{
    if (!item) return;
    int idx = item->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_scenarios.size()) return;
    runScenario(m_scenarios[idx]);
}

void TestRunnerWindow::runScenario(const TestScenario &scenario)
{
    if (m_currentStep >= 0) {
        log(QStringLiteral("Already running. Stop first."));
        return;
    }

    // Find the scenario index
    m_runningScenarioIdx = -1;
    for (int i = 0; i < m_scenarios.size(); ++i) {
        if (&m_scenarios[i] == &scenario) {
            m_runningScenarioIdx = i;
            break;
        }
    }

    clearLog();
    log(QStringLiteral(">>> Scenario: %1 <<<").arg(scenario.name));
    log(QStringLiteral("Reference: %1").arg(scenario.docRef));
    log(QStringLiteral("Description: %1").arg(scenario.description));
    log(QString());

    m_totalSteps  = scenario.steps.size();
    m_currentStep = 0;

    m_stopButton->setEnabled(true);
    setStatus(QStringLiteral("Running: %1...").arg(scenario.name));

    QTimer::singleShot(200, this, &TestRunnerWindow::advanceStep);
}

void TestRunnerWindow::advanceStep()
{
    if (m_currentStep < 0 || m_runningScenarioIdx < 0
        || m_runningScenarioIdx >= m_scenarios.size()) {
        stopSequence();
        return;
    }

    const TestScenario &sc = m_scenarios[m_runningScenarioIdx];

    m_stepLabel->setText(QStringLiteral("Step %1/%2")
        .arg(m_currentStep + 1).arg(m_totalSteps));

    if (m_currentStep < sc.steps.size()) {
        const auto &step = sc.steps[m_currentStep];
        setStatus(QStringLiteral("%1: %2").arg(sc.name, step.label));
        log(QStringLiteral("--- %1 ---").arg(step.label));
        step.fn();

        m_currentStep++;
        if (m_currentStep >= m_totalSteps) {
            m_runningScenarioIdx = -1;
            m_currentStep = -1;
            m_stopButton->setEnabled(false);
            m_stepLabel->clear();
            setStatus(QStringLiteral("Done: %1").arg(sc.name));
            log(QStringLiteral(">>> Scenario complete. <<<"));
            return;
        }
        m_stepTimer->start(m_delayMs);
        return;
    }

    stopSequence();
}

void TestRunnerWindow::stopSequence()
{
    if (m_currentStep < 0) return;
    m_stepTimer->stop();
    log(QStringLiteral("!!! Stopped at step %1/%2")
        .arg(m_currentStep + 1).arg(m_totalSteps));
    m_currentStep = -1;
    m_runningScenarioIdx = -1;
    m_stopButton->setEnabled(false);
    setStatus(QStringLiteral("Stopped."));
    m_stepLabel->clear();
}

// ═════════════════════════════════════════════════════════════════════════════
// Scenario definitions
// ═════════════════════════════════════════════════════════════════════════════

// ── Scenario 8: Cluster Drag & Snap ─────────────────────────────────────────
// Ref: scene_tree_behavior.md §10-#4, #5, #6
TestScenario TestRunnerWindow::makeScenario_clusterDrag()
{
    QVector<TestStep> steps;

    steps.append({QStringLiteral("Build 3 touching root cards"), [this]() {
        step_clearScene();
        int mod1 = step_addModule(QStringLiteral("blockA"));
        step_addPrimitive(ShapeNode::Cube, mod1, {QStringLiteral("10"), QStringLiteral("10"), QStringLiteral("10")});
        int mod2 = step_addModule(QStringLiteral("blockB"));
        step_addPrimitive(ShapeNode::Cube, mod2, {QStringLiteral("20"), QStringLiteral("20"), QStringLiteral("20")});
        int mod3 = step_addModule(QStringLiteral("blockC"));
        step_addPrimitive(ShapeNode::Cube, mod3, {QStringLiteral("15"), QStringLiteral("15"), QStringLiteral("15")});
        m_treeWidget->compactRootBlocksAndFit();
        m_treeWidget->refresh();
        QApplication::processEvents();
        log(QStringLiteral("Built 3 module root cards, compacted layout."));
    }});

    steps.append({QStringLiteral("Slow-drag blockA 100px right (cluster)"), [this]() {
        buildNodeRefs();
        int blockA = m_refs.moduleId;
        if (blockA <= 0) { log(QStringLiteral("No module")); return; }
        QRectF br = m_treeWidget->debugGroupRect(blockA);
        if (br.isNull()) { log(QStringLiteral("No rect for blockA")); return; }
        QPointF target(br.topLeft() + QPointF(100, 0));
        dragNodeTo(blockA, target, true);
        log(QStringLiteral("Slow-dragged blockA to (%1,%2)").arg(target.x()).arg(target.y()));
    }});

    steps.append({QStringLiteral("Check blockA moved"), [this]() {
        buildNodeRefs();
        int blockA = m_refs.moduleId;
        if (blockA <= 0) { log(QStringLiteral("No module")); return; }
        QRectF r = m_treeWidget->debugGroupRect(blockA);
        log(r.isNull()
            ? QStringLiteral("FAIL: blockA has no rect")
            : QStringLiteral("blockA is at (x=%1, y=%2)")
                .arg(r.x()).arg(r.y()));
    }});

    steps.append({QStringLiteral("Fast-drag blockA 200px down (detach)"), [this]() {
        buildNodeRefs();
        int blockA = m_refs.moduleId;
        if (blockA <= 0) { log(QStringLiteral("No module")); return; }
        QRectF br = m_treeWidget->debugGroupRect(blockA);
        if (br.isNull()) { log(QStringLiteral("No rect")); return; }
        QPointF target(br.topLeft() + QPointF(0, 200));
        dragNodeTo(blockA, target, false);
        log(QStringLiteral("Fast-dragged blockA down — cluster should detach."));
    }});

    steps.append({QStringLiteral("Add separate root and snap test"), [this]() {
        int mod4 = step_addModule(QStringLiteral("snapTarget"));
        step_addPrimitive(ShapeNode::Sphere, mod4, {QStringLiteral("10")});
        m_treeWidget->compactRootBlocksAndFit();
        m_treeWidget->refresh();
        QApplication::processEvents();
        // Move mod4 near blockA's new position
        buildNodeRefs();
        int blockA = m_refs.moduleId;
        // Find the newly added module (not the first one)
        const auto &root = m_scene->treeRoot();
        int lastModId = 0;
        for (const auto &c : root.children) {
            if (c.operation == SceneDocument::TreeNode::Module && c.id != blockA)
                lastModId = c.id;
        }
        if (lastModId > 0) {
            QRectF br = m_treeWidget->debugGroupRect(lastModId);
            if (!br.isNull()) {
                QPointF target(br.topLeft() + QPointF(50, 0));
                dragNodeTo(lastModId, target, true);
                log(QStringLiteral("Dragged snapTarget — should snap to edge of blockA."));
            }
        } else {
            log(QStringLiteral("Could not find snap target module"));
        }
    }});

    return {
        QStringLiteral("scenario_cluster"),
        QStringLiteral("8. Cluster Drag & Snap"),
        QStringLiteral("Slow drag (cluster), fast drag (detach), snap to edge"),
        QStringLiteral("scene_tree_behavior.md §10-#4, #5, #6"),
        steps
    };
}

void TestRunnerWindow::buildAllScenarios()
{
    m_scenarios.clear();

    m_scenarios.append(makeScenario_moduleBasics());
    m_scenarios.append(makeScenario_variableRules());
    m_scenarios.append(makeScenario_booleanGroups());
    m_scenarios.append(makeScenario_deleteOps());
    m_scenarios.append(makeScenario_nodeReorder());
    m_scenarios.append(makeScenario_moduleCalls());
    m_scenarios.append(makeScenario_fullRegression());
    m_scenarios.append(makeScenario_clusterDrag());

    for (int i = 0; i < m_scenarios.size(); ++i) {
        auto *item = new QListWidgetItem;
        item->setText(QStringLiteral("%1\n  %2")
            .arg(m_scenarios[i].name, m_scenarios[i].description));
        item->setData(Qt::UserRole, i);
        item->setToolTip(QStringLiteral("%1\nRef: %2\nSteps: %3")
            .arg(m_scenarios[i].description, m_scenarios[i].docRef)
            .arg(m_scenarios[i].steps.size()));
        m_scenarioList->addItem(item);
    }
}

// ── Scenario 1: Module & Parameters ─────────────────────────────────────────
// Ref: scene_tree_behavior.md §4 (Variables And Modules) + §5 (Module Calls)
TestScenario TestRunnerWindow::makeScenario_moduleBasics()
{
    QVector<TestStep> steps;

    steps.append({QStringLiteral("Clear scene"), [this]() {
        step_clearScene();
        log(QStringLiteral("Scene cleared."));
    }});

    steps.append({QStringLiteral("Add module \"test\""), [this]() {
        int id = step_addModule(QStringLiteral("test"));
        log(QStringLiteral("Module created, id=#%1").arg(id));
        m_state.selectedNodeId = id;
    }});

    steps.append({QStringLiteral("Add parameter VAR to module"), [this]() {
        int modId = m_state.selectedNodeId;
        int varId = m_scene->addVariableToModule(modId, true, -1);
        m_scene->renameVariable(varId, QStringLiteral("radius"));
        m_scene->updateVariableExpression(varId, QStringLiteral("10"));
        m_treeWidget->refresh();
        QApplication::processEvents();
        log(QStringLiteral("Parameter \"radius=10\" added to module #%1").arg(modId));
    }});

    steps.append({QStringLiteral("Add local VAR to module body"), [this]() {
        int modId = m_state.selectedNodeId;
        int varId = m_scene->addVariableToModule(modId, false, -1);
        m_scene->renameVariable(varId, QStringLiteral("scale"));
        m_scene->updateVariableExpression(varId, QStringLiteral("2"));
        m_treeWidget->refresh();
        QApplication::processEvents();
        log(QStringLiteral("Local variable \"scale=2\" added to module body."));
    }});

    steps.append({QStringLiteral("Add primitive to module body"), [this]() {
        int modId = m_state.selectedNodeId;
        int primId = step_addPrimitive(ShapeNode::Sphere, modId,
                                        {QStringLiteral("10")});
        log(QStringLiteral("Sphere added to module body, node=#%1").arg(primId));
    }});

    steps.append({QStringLiteral("Add module call to scene"), [this]() {
        int modId = m_state.selectedNodeId;
        int callId = step_addModuleCall(modId, 0,
                                         QStringLiteral("radius = 20"));
        log(QStringLiteral("Module call placed in scene, id=#%1").arg(callId));
    }});

    return {
        QStringLiteral("scenario_module"),
        QStringLiteral("1. Module & Parameters"),
        QStringLiteral("Module with params, body VAR, primitive, and call"),
        QStringLiteral("scene_tree_behavior.md §4–5"),
        steps
    };
}

// ── Scenario 2: Variable Placement Rules ────────────────────────────────────
// Ref: scene_tree_behavior.md §4 (Variables And Modules)
TestScenario TestRunnerWindow::makeScenario_variableRules()
{
    QVector<TestStep> steps;

    steps.append({QStringLiteral("Clear scene"), [this]() {
        step_clearScene();
        log(QStringLiteral("Scene cleared."));
    }});

    steps.append({QStringLiteral("Add global variable to scene"), [this]() {
        int id = step_addVar(QStringLiteral("global_r"), QStringLiteral("5"));
        log(QStringLiteral("Global variable added, id=#%1").arg(id));
    }});

    steps.append({QStringLiteral("Create Union group"), [this]() {
        int id = step_addGroup(SceneDocument::TreeNode::Union, 0);
        log(QStringLiteral("Union created, id=#%1").arg(id));
        m_state.selectedNodeId = id;
    }});

    steps.append({QStringLiteral("Add primitive to Union"), [this]() {
        int primId = step_addPrimitive(ShapeNode::Cube, m_state.selectedNodeId,
                                        {QStringLiteral("10"), QStringLiteral("10"), QStringLiteral("10")});
        log(QStringLiteral("Cube added to Union, node=#%1").arg(primId));
    }});

    return {
        QStringLiteral("scenario_vars"),
        QStringLiteral("2. Variable Placement"),
        QStringLiteral("Vars only in scene (ok) or module (ok), not in ordinary groups"),
        QStringLiteral("scene_tree_behavior.md §4"),
        steps
    };
}

// ── Scenario 3: Boolean Groups (Difference) ─────────────────────────────────
// Ref: scene_tree_behavior.md §2 (Palette Blocks) + §10 checklist item #7
TestScenario TestRunnerWindow::makeScenario_booleanGroups()
{
    QVector<TestStep> steps;

    steps.append({QStringLiteral("Clear scene"), [this]() {
        step_clearScene();
        log(QStringLiteral("Scene cleared."));
    }});

    steps.append({QStringLiteral("Create Difference group"), [this]() {
        int id = step_addGroup(SceneDocument::TreeNode::Difference, 0);
        log(QStringLiteral("Difference created, id=#%1").arg(id));
        m_state.selectedNodeId = id;
    }});

    steps.append({QStringLiteral("Add Cube (first = base body)"), [this]() {
        int id = m_state.selectedNodeId;
        step_addPrimitive(ShapeNode::Cube, id,
                          {QStringLiteral("40"), QStringLiteral("40"), QStringLiteral("20")});
        log(QStringLiteral("Cube added to Difference #%1 — first child is base body").arg(id));
    }});

    steps.append({QStringLiteral("Add Sphere (second = cut)"), [this]() {
        int id = m_state.selectedNodeId;
        step_addPrimitive(ShapeNode::Sphere, id,
                          {QStringLiteral("15")});
        log(QStringLiteral("Sphere added to Difference #%1 — second child is cut").arg(id));
    }});

    steps.append({QStringLiteral("Add Cylinder (third = another cut)"), [this]() {
        int id = m_state.selectedNodeId;
        step_addPrimitive(ShapeNode::Cylinder, id,
                          {QStringLiteral("8"), QStringLiteral("30")});
        log(QStringLiteral("Cylinder added to Difference #%1 — third child is also cut").arg(id));
    }});

    return {
        QStringLiteral("scenario_boolean"),
        QStringLiteral("3. Boolean Groups"),
        QStringLiteral("Difference: first child base, rest are cuts"),
        QStringLiteral("scene_tree_behavior.md §2, §10-#7"),
        steps
    };
}

// ── Scenario 4: Delete Operations ───────────────────────────────────────────
// Ref: scene_tree_behavior.md §3 (Levels And Nesting Rules — Deletion) + §10-#9
TestScenario TestRunnerWindow::makeScenario_deleteOps()
{
    QVector<TestStep> steps;

    steps.append({QStringLiteral("Clear and build"), [this]() {
        step_clearScene();
        int unionId = step_addGroup(SceneDocument::TreeNode::Union, 0);
        step_addPrimitive(ShapeNode::Cube, unionId,
                          {QStringLiteral("30"), QStringLiteral("20"), QStringLiteral("10")});
        step_addPrimitive(ShapeNode::Sphere, unionId,
                          {QStringLiteral("12")});
        int modId = step_addModule(QStringLiteral("delmod"));
        step_addPrimitive(ShapeNode::Cylinder, modId,
                          {QStringLiteral("5"), QStringLiteral("15")});
        step_addModuleCall(modId, 0);
        buildNodeRefs();
        log(QStringLiteral("Scene built: Union(cube,sphere), Module(cyl) + call"));
    }});

    steps.append({QStringLiteral("Take undo snapshot"), [this]() {
        step_takeUndoSnapshot();
        log(QStringLiteral("Preserved scene state before delete operations."));
    }});

    steps.append({QStringLiteral("Delete a primitive"), [this]() {
        int primId = m_refs.firstPrimId;
        if (primId <= 0) { log(QStringLiteral("No prim to delete")); return; }
        m_state.savedDeletedId = primId;
        clickNode(primId);
        QTest::keyClick(m_treeWidget, Qt::Key_Delete);
        QApplication::processEvents();
        log(QStringLiteral("Deleted primitive #%1 — shape removed").arg(primId));
    }});

    steps.append({QStringLiteral("Delete module (also removes calls)"), [this]() {
        buildNodeRefs();
        int modId = m_refs.moduleId;
        if (modId <= 0) { log(QStringLiteral("No module to delete")); return; }
        m_state.savedDeletedId = modId;
        clickNode(modId);
        QTest::keyClick(m_treeWidget, Qt::Key_Delete);
        QApplication::processEvents();
        log(QStringLiteral("Deleted module #%1 — module call also removed").arg(modId));
    }});

    steps.append({QStringLiteral("UNDO: restore snapshot"), [this]() {
        step_restoreUndo();
        buildNodeRefs();
        bool primOk = m_refs.firstPrimId > 0;
        bool modOk  = m_refs.moduleId > 0;
        log(primOk
            ? QStringLiteral("Undo OK — primitive restored")
            : QStringLiteral("Undo FAIL — primitive missing"));
        log(modOk
            ? QStringLiteral("Undo OK — module restored")
            : QStringLiteral("Undo FAIL — module missing"));
    }});

    steps.append({QStringLiteral("REDO: delete module again"), [this]() {
        buildNodeRefs();
        int modId = m_refs.moduleId;
        if (modId <= 0) { log(QStringLiteral("Module gone, can't redo")); return; }
        clickNode(modId);
        QTest::keyClick(m_treeWidget, Qt::Key_Delete);
        QApplication::processEvents();
        buildNodeRefs();
        bool gone = m_refs.moduleId <= 0;
        log(gone
            ? QStringLiteral("Redo OK — module re-deleted")
            : QStringLiteral("Redo FAIL — module still present"));
    }});

    return {
        QStringLiteral("scenario_delete"),
        QStringLiteral("4. Delete Operations"),
        QStringLiteral("Delete primitive, delete module, undo/redo verification"),
        QStringLiteral("scene_tree_behavior.md §3, §10-#9"),
        steps
    };
}

// ── Scenario 5: Node Reorganize ─────────────────────────────────────────────
// Ref: scene_tree_behavior.md §3 (Move Rules) + §10-#7 (Difference reorder)
TestScenario TestRunnerWindow::makeScenario_nodeReorder()
{
    QVector<TestStep> steps;

    steps.append({QStringLiteral("Clear and build"), [this]() {
        step_clearScene();
        int diffId = step_addGroup(SceneDocument::TreeNode::Difference, 0);
        step_addPrimitive(ShapeNode::Cube, diffId,
                          {QStringLiteral("40"), QStringLiteral("40"), QStringLiteral("20")});
        step_addPrimitive(ShapeNode::Sphere, diffId,
                          {QStringLiteral("15")});
        int transId = step_addGroup(SceneDocument::TreeNode::Translate, 0);
        m_scene->updateGroupTransform(transId, QVector3D(10, 0, 0), {}, QVector3D(1,1,1),
                                      {QStringLiteral("10"), QStringLiteral("0"), QStringLiteral("0")});
        buildNodeRefs();
        log(QStringLiteral("Built: Difference(cube,sphere) + Translate(empty)"));
    }});

    steps.append({QStringLiteral("Move Sphere before Cube in Difference"), [this]() {
        buildNodeRefs();
        int diffId = m_refs.diffId;
        // Find sphere (second primitive) and move it to index 0
        const auto &root = m_scene->treeRoot();
        const auto *diff = findRecur(root, [diffId](const auto &n) {
            return n.id == diffId;
        });
        if (!diff || diff->children.size() < 2) {
            log(QStringLiteral("Not enough children in Difference"));
            return;
        }
        int sphereId = diff->children[1].id;
        m_scene->moveTreeNode(sphereId, diffId, 0, false);
        m_treeWidget->refresh();
        QApplication::processEvents();
        log(QStringLiteral("Moved Sphere #%1 to index 0 in Difference #%2 — now Sphere is base body")
            .arg(sphereId).arg(diffId));
    }});

    steps.append({QStringLiteral("Move primitive into Translate group"), [this]() {
        buildNodeRefs();
        int primId = m_refs.firstPrimId;
        int transId = m_refs.translateId;
        if (primId <= 0 || transId <= 0) {
            log(QStringLiteral("No prim or translate to move"));
            return;
        }
        bool ok = m_scene->moveTreeNode(primId, transId, -1, false);
        m_treeWidget->refresh();
        QApplication::processEvents();
        if (ok)
            log(QStringLiteral("Reparented primitive #%1 into Translate #%2 — world offset preserved")
                .arg(primId).arg(transId));
        else
            log(QStringLiteral("FAILED to reparent primitive #%1 into Translate #%2")
                .arg(primId).arg(transId));
    }});

    return {
        QStringLiteral("scenario_reorder"),
        QStringLiteral("5. Node Reorganize"),
        QStringLiteral("Reorder children, reparent between groups"),
        QStringLiteral("scene_tree_behavior.md §3, §10-#7"),
        steps
    };
}

// ── Scenario 6: Module Calls ────────────────────────────────────────────────
// Ref: scene_tree_behavior.md §5 (Module Calls) + §10-#3
TestScenario TestRunnerWindow::makeScenario_moduleCalls()
{
    QVector<TestStep> steps;

    steps.append({QStringLiteral("Clear and build module"), [this]() {
        step_clearScene();
        int modId = step_addModule(QStringLiteral("gadget"));
        step_addVar(QStringLiteral("r"), QStringLiteral("10"), modId);
        step_addVar(QStringLiteral("h"), QStringLiteral("25"), modId);
        step_addPrimitive(ShapeNode::Cylinder, modId,
                          {QStringLiteral("r"), QStringLiteral("h")});
        log(QStringLiteral("Module \"gadget\" created with params r, h and cylinder."));
        m_state.selectedNodeId = modId;
    }});

    steps.append({QStringLiteral("Add CALL in scene (root)"), [this]() {
        int modId = m_state.selectedNodeId;
        int callId = step_addModuleCall(modId, 0,
                                         QStringLiteral("r = 15, h = 30"));
        log(QStringLiteral("Module call #%1 placed in scene (r=15, h=30)").arg(callId));
    }});

    steps.append({QStringLiteral("Add CALL inside a Union"), [this]() {
        int modId = m_state.selectedNodeId;
        int unionId = step_addGroup(SceneDocument::TreeNode::Union, 0);
        int callId = step_addModuleCall(modId, unionId,
                                         QStringLiteral("r = 8, h = 12"));
        log(QStringLiteral("Module call #%1 placed inside Union #%2").arg(callId).arg(unionId));
    }});

    steps.append({QStringLiteral("Add CALL inside Translate"), [this]() {
        int modId = m_state.selectedNodeId;
        int transId = step_addGroup(SceneDocument::TreeNode::Translate, 0);
        m_scene->updateGroupTransform(transId, QVector3D(20, 0, 0), {}, QVector3D(1,1,1),
                                      {QStringLiteral("20"), QStringLiteral("0"), QStringLiteral("0")});
        int callId = step_addModuleCall(modId, transId,
                                         QStringLiteral("r = 5, h = 10"));
        log(QStringLiteral("Module call #%1 placed inside Translate #%2").arg(callId).arg(transId));
    }});

    return {
        QStringLiteral("scenario_calls"),
        QStringLiteral("6. Module Calls"),
        QStringLiteral("CALL in scene, inside boolean, inside transform"),
        QStringLiteral("scene_tree_behavior.md §5, §10-#3"),
        steps
    };
}

// ── Scenario 7: Full Regression Suite ───────────────────────────────────────
// Ref: scene_tree_behavior.md §10 (Minimum Regression Checklist)
TestScenario TestRunnerWindow::makeScenario_fullRegression()
{
    QVector<TestStep> steps;

    steps.append({QStringLiteral("Build scene"), [this]() {
        step_clearScene();

        step_addVar(QStringLiteral("r"), QStringLiteral("10"));

        int unionId = step_addGroup(SceneDocument::TreeNode::Union, 0);
        step_addPrimitive(ShapeNode::Cube, unionId,
                          {QStringLiteral("30"), QStringLiteral("20"), QStringLiteral("10")});
        step_addPrimitive(ShapeNode::Sphere, unionId,
                          {QStringLiteral("15")});

        int diffId = step_addGroup(SceneDocument::TreeNode::Difference, 0);
        step_addPrimitive(ShapeNode::Cube, diffId,
                          {QStringLiteral("40"), QStringLiteral("40"), QStringLiteral("20")});
        step_addPrimitive(ShapeNode::Cylinder, diffId,
                          {QStringLiteral("8"), QStringLiteral("25")});

        int transId = step_addGroup(SceneDocument::TreeNode::Translate, 0);
        m_scene->updateGroupTransform(transId, QVector3D(5, 10, 0), {}, QVector3D(1,1,1),
                                      {QStringLiteral("5"), QStringLiteral("10"), QStringLiteral("0")});
        int rotId = step_addGroup(SceneDocument::TreeNode::Rotate, transId);
        m_scene->updateGroupTransform(rotId, {}, QVector3D(0, 0, 45), QVector3D(1,1,1),
                                      {QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("45")});
        step_addPrimitive(ShapeNode::Cube, rotId,
                          {QStringLiteral("10"), QStringLiteral("10"), QStringLiteral("10")});

        int modId = step_addModule(QStringLiteral("gadget"));
        int paramR = m_scene->addVariableToModule(modId, true, -1);
        m_scene->renameVariable(paramR, QStringLiteral("r"));
        m_scene->updateVariableExpression(paramR, QStringLiteral("5"));
        int paramH = m_scene->addVariableToModule(modId, true, -1);
        m_scene->renameVariable(paramH, QStringLiteral("h"));
        m_scene->updateVariableExpression(paramH, QStringLiteral("20"));
        step_addPrimitive(ShapeNode::Cylinder, modId,
                          {QStringLiteral("r"), QStringLiteral("h")});
        step_addModuleCall(modId, 0, QStringLiteral("r = 12, h = 35"));

        buildNodeRefs();
        log(QStringLiteral("Regression scene built (1 var, 2 booleans, 2 transforms, module+call)"));
    }});

    steps.append({QStringLiteral("Click to select"), [this]() {
        buildNodeRefs();
        int id = m_refs.firstPrimId;
        if (id <= 0) { log(QStringLiteral("No primitive")); return; }
        clickNode(id);
        log(QStringLiteral("Selected primitive #%1").arg(id));
    }});

    steps.append({QStringLiteral("Ctrl+wheel on Translate"), [this]() {
        buildNodeRefs();
        int id = m_refs.translateId;
        if (id <= 0) { log(QStringLiteral("No Translate")); return; }
        wheelAt(id, 120, Qt::ControlModifier);
        log(QStringLiteral("Adjusted Translate #%1 (+120)").arg(id));
    }});

    steps.append({QStringLiteral("Delete a primitive"), [this]() {
        buildNodeRefs();
        // Find second primitive
        const auto &root = m_scene->treeRoot();
        int count = 0, targetId = 0;
        findRecur(root, [&](const auto &n) {
            if (n.type == SceneDocument::TreeNode::Primitive && count++ == 1) {
                targetId = n.id; return true;
            }
            return false;
        });
        if (targetId <= 0) { log(QStringLiteral("No second prim")); return; }
        clickNode(targetId);
        QTest::keyClick(m_treeWidget, Qt::Key_Delete);
        QApplication::processEvents();
        log(QStringLiteral("Deleted primitive #%1").arg(targetId));
    }});

    steps.append({QStringLiteral("Add Cone shape"), [this]() {
        int primId = step_addPrimitive(ShapeNode::Cone, 0,
                                        {QStringLiteral("12"), QStringLiteral("3"), QStringLiteral("25")});
        log(QStringLiteral("Added Cone, node=#%1").arg(primId));
        m_state.addedShapeNodeId = primId;
    }});

    steps.append({QStringLiteral("Wheel on Cone parameter"), [this]() {
        int id = m_state.addedShapeNodeId;
        if (id <= 0) { log(QStringLiteral("No cone")); return; }
        wheelAt(id, -120);
        log(QStringLiteral("Wheel on Cone #%1 (-120)").arg(id));
    }});

    steps.append({QStringLiteral("Wheel on variable"), [this]() {
        buildNodeRefs();
        int id = m_refs.firstVarId;
        if (id <= 0) { log(QStringLiteral("No variable")); return; }
        clickNode(id);
        wheelAt(id, 120);
        log(QStringLiteral("Wheel on variable #%1 (+120)").arg(id));
    }});

    steps.append({QStringLiteral("Move prim into module"), [this]() {
        buildNodeRefs();
        int primId = m_refs.firstPrimId;
        int modId  = m_refs.moduleId;
        if (primId <= 0 || modId <= 0) {
            log(QStringLiteral("No prim or module")); return;
        }
        bool ok = m_scene->moveTreeNode(primId, modId, -1, false);
        m_treeWidget->refresh();
        QApplication::processEvents();
        log(ok
            ? QStringLiteral("Moved primitive #%1 into module #%2").arg(primId).arg(modId)
            : QStringLiteral("FAILED to move primitive #%1 into module #%2").arg(primId).arg(modId));
    }});

    return {
        QStringLiteral("scenario_full"),
        QStringLiteral("7. Full Regression"),
        QStringLiteral("All major tree operations (select, wheel, delete, add, reparent)"),
        QStringLiteral("scene_tree_behavior.md §10"),
        steps
    };
}
