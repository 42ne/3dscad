#ifndef SCENECONTROLLER_H
#define SCENECONTROLLER_H

#include "scenedocument.h"
#include "shapenode.h"

#include <QObject>
#include <QVector3D>

class QUndoCommand;
class QUndoStack;
class QAction;

// Owns SceneDocument and QUndoStack.
// Handles all scene mutations; emits signals so the UI layer (MainWindow)
// can refresh widgets without being part of the mutation logic.
class SceneController : public QObject
{
    Q_OBJECT

public:
    // State passed to the code editor for Ctrl+wheel highlight.
    struct CtrlParamHighlight {
        bool    active        = false;
        int     nodeId        = 0;
        QString contextPrefix; // unique text immediately before expression in generated code
        QString expression;    // full expression containing the number
        int     numberStart   = 0;
        int     numberLength  = 0;
    };

    explicit SceneController(QObject *parent = nullptr);

    // ── Read access ──────────────────────────────────────────────────────────
    const SceneDocument &scene() const { return m_scene; }
    SceneDocument &scene() { return m_scene; }
    QUndoStack *undoStack()  const { return m_undoStack;  }
    QAction    *undoAction() const { return m_undoAction; }
    QAction    *redoAction() const { return m_redoAction; }

    int selectedTreeNodeId()   const { return m_selectedTreeNodeId; }
    int selectedTreeGroupId()  const; // closest Group ancestor (or self if Group)
    int selectedDirectGroupId() const; // non-zero only when selected node IS a Group

    const CtrlParamHighlight &ctrlHighlight() const { return m_ctrlHighlight; }

    // ── Scene mutations ───────────────────────────────────────────────────────
    // Each one pushes an undoable command and emits sceneChanged().
    void addCube();
    void addSphere();
    void addCylinder();
    void addGroup(SceneDocument::TreeNode::Operation operation);
    void moveTreeNodeToGroup(int nodeId, int parentGroupId, int insertIndex = -1);

    // Returns false and fills errorMessage/errorLine when parsing fails.
    bool applyCode(const QString &code,
                   QString *errorMessage = nullptr,
                   int     *errorLine    = nullptr);

    // ── Selection ─────────────────────────────────────────────────────────────
    // Emit selectionChanged(nodeId); nodeId == 0 means cleared.
    void selectShape(int shapeId);
    void selectTreeNode(int treeNodeId);
    void clearSelection();

    // ── Viewport drag handlers ────────────────────────────────────────────────
    // Shape drag: directly mutates scene during move; commits undo on finish.
    void handleShapeDragStarted(int index);
    void handleShapeDragged(int index, const QVector3D &delta);
    void handleShapeDragFinished(int index);
    void handleShapeRotationDragStarted(int index);
    void handleShapeRotated(int index, const QVector3D &deltaDegrees);
    void handleShapeRotationDragFinished(int index);

    // Group drag: calls updateGroupTransform live; commits on finish.
    void handleGroupDragStarted(int groupId);
    void handleGroupDragged(int groupId, const QVector3D &delta);
    void handleGroupDragFinished(int groupId);
    void handleGroupRotationDragStarted(int groupId);
    void handleGroupRotated(int groupId, const QVector3D &deltaDegrees);
    void handleGroupRotationDragFinished(int groupId);

    // ── Graphics-tree event handlers ──────────────────────────────────────────
    void handleToolDrop(const QString &toolName, int parentGroupId, int insertIndex);
    void handleModuleCallDrop(int moduleGroupId, int parentGroupId, int insertIndex);
    void handleNodeSelected(int nodeId);
    void handleNodeDeleteRequested(int nodeId);
    void handleTransformValueAdjusted(int groupId, int axis,
                                       int numberStart, int numberLength,
                                       qreal delta);
    void handleModuleRenameRequested(int groupId, const QString &newName);
    void handleVariableRenameRequested(int variableId, const QString &newName);
    void handleShapeParameterAdjusted(int nodeId, int paramIndex,
                                       int numberStart, int numberLength,
                                       qreal delta);
    void handleVariableNumberAdjusted(int nodeId, int start, int length, qreal delta);
    void handleModuleCallArgumentAdjusted(int moduleCallId, int parameterVariableId,
                                           int start, int length, qreal delta);
    void handleForLoopRangeAdjusted(int nodeId, int start, int length, qreal delta);
    void handleCtrlReleased();

signals:
    // Scene content changed (undo command committed).
    void sceneChanged();
    // Selection changed; nodeId == 0 → cleared.
    void selectionChanged(int nodeId);
    // Ctrl+wheel highlight state changed (code-editor highlight needs refresh).
    void ctrlHighlightChanged();
    // Live update during drag (no undo push; viewport should invalidate + repaint).
    void liveViewportUpdate();

private:
    void pushCommandIfValid(QUndoCommand *command);
    void selectShapeInternal(int shapeId); // updates scene + m_selectedTreeNodeId, emits selectionChanged

    SceneDocument m_scene;
    QUndoStack   *m_undoStack  = nullptr;
    QAction      *m_undoAction = nullptr;
    QAction      *m_redoAction = nullptr;

    int m_selectedTreeNodeId = 0;
    CtrlParamHighlight m_ctrlHighlight;

    // Viewport drag state
    bool      m_shapeDragActive   = false;
    ShapeNode m_shapeDragStartShape;
    bool      m_groupDragActive   = false;
    int       m_groupDragId       = 0;
    QVector3D m_groupDragStartPos;
    QVector3D m_groupDragStartRot;
    QVector3D m_groupDragStartScale;
};

#endif // SCENECONTROLLER_H
