#ifndef TESTRUNNERWINDOW_H
#define TESTRUNNERWINDOW_H

#include "../../scenedocument.h"

#include <QMainWindow>
#include <QTimer>
#include <QVector>
#include <functional>

class TreeDebugWindow;
class SceneTreeGraphicsWidget;
class QPushButton;
class QTextEdit;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QSplitter;

// ── Test step / scenario types ─────────────────────────────────────────────

using TestStepFn = std::function<void()>;

struct TestStep {
    QString label;
    TestStepFn fn;
};

struct TestScenario {
    QString id;
    QString name;
    QString description;
    QString docRef;      // section in scene_tree_behavior.md
    QVector<TestStep> steps;
};

// ── TestRunnerWindow ───────────────────────────────────────────────────────

class TestRunnerWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit TestRunnerWindow(TreeDebugWindow *debugWindow, QWidget *parent = nullptr);

private slots:
    void onScenarioClicked(QListWidgetItem *item);
    void advanceStep();
    void stopSequence();

private:
    void buildUi();
    void buildAllScenarios();
    void runScenario(const TestScenario &scenario);

    void log(const QString &text);
    void setStatus(const QString &text);
    void clearLog();

    // Helpers
    SceneDocument           *scene();
    SceneTreeGraphicsWidget *tree();
    void buildNodeRefs();
    int  findNodeIdByType(SceneDocument::TreeNode::Type type);
    int  findGroupIdByOp(SceneDocument::TreeNode::Operation op);
    int  findNodeByShapeId(int shapeId);
    void wheelAt(int nodeId, int delta, Qt::KeyboardModifiers mods = Qt::NoModifier);
    void clickNode(int nodeId);

    // Scenario builders
    TestScenario makeScenario_moduleBasics();
    TestScenario makeScenario_variableRules();
    TestScenario makeScenario_booleanGroups();
    TestScenario makeScenario_deleteOps();
    TestScenario makeScenario_nodeReorder();
    TestScenario makeScenario_moduleCalls();
    TestScenario makeScenario_fullRegression();
    TestScenario makeScenario_clusterDrag();

    // Shared step primitives
    void step_clearScene();
    int  step_addVar(const QString &name, const QString &expr, int parentId = 0);
    int  step_addGroup(SceneDocument::TreeNode::Operation op, int parentId = 0);
    int  step_addPrimitive(ShapeNode::Type type, int parentId,
                           const QStringList &exprs);
    int  step_addModule(const QString &name);
    int  step_addModuleCall(int moduleId, int parentId,
                            const QString &args = QString());
    void step_takeUndoSnapshot();
    void step_restoreUndo();
    void step_redoDelete(int nodeId);
    // Drag simulation
    void dragNodeTo(int nodeId, const QPointF &targetScenePos, bool slowDrag);
    QPointF gripCenter(int nodeId) const;

    // State
    TreeDebugWindow         *m_debugWindow  = nullptr;
    SceneDocument           *m_scene        = nullptr;
    SceneTreeGraphicsWidget *m_treeWidget   = nullptr;

    QListWidget *m_scenarioList = nullptr;
    QPushButton *m_stopButton   = nullptr;
    QPushButton *m_clearButton  = nullptr;
    QTextEdit   *m_logOutput    = nullptr;
    QLabel      *m_statusLabel  = nullptr;
    QLabel      *m_stepLabel    = nullptr;
    QTimer      *m_stepTimer    = nullptr;

    QVector<TestScenario> m_scenarios;
    int  m_runningScenarioIdx = -1;
    int  m_currentStep = -1;
    int  m_totalSteps  = 0;
    int  m_delayMs     = 800;

    struct StepState {
        int selectedNodeId   = 0;
        int addedShapeNodeId = 0;
        int savedDeletedId   = 0;
    };
    StepState m_state;
    SceneDocument::Snapshot m_undoSnapshot;

    struct NodeRef {
        int translateId = 0;
        int forId       = 0;
        int unionId     = 0;
        int diffId      = 0;
        int rotateId    = 0;
        int scaleId     = 0;
        int moduleId    = 0;
        int callId      = 0;
        int sceneId     = 0;
        int firstVarId  = 0;
        int firstPrimId = 0;
    };
    NodeRef m_refs;
};

#endif // TESTRUNNERWINDOW_H
