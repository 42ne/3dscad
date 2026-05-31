#ifndef SCENECOMMANDS_H
#define SCENECOMMANDS_H

#include "scenedocument.h"

#include <QUndoCommand>
#include <functional>

// ── Base for snapshot-based commands ─────────────────────────────────────────
// Subclasses only need to provide a constructor that sets m_oldSnapshot,
// m_newSnapshot, and m_valid.  isValid / undo / redo are implemented here.
class SnapshotCommand : public QUndoCommand
{
public:
    bool isValid() const;
    void undo() override;
    void redo() override;

protected:
    explicit SnapshotCommand(const QString &text,
                             SceneDocument *scene,
                             std::function<void()> onChanged);

    SceneDocument *m_scene = nullptr;
    SceneDocument::Snapshot m_oldSnapshot;
    SceneDocument::Snapshot m_newSnapshot;
    bool m_valid = false;
    std::function<void()> m_onChanged;
};

class AddShapeCommand : public QUndoCommand
{
public:
    AddShapeCommand(SceneDocument *scene, const ShapeNode &shape, std::function<void()> onChanged);
    AddShapeCommand(SceneDocument *scene, const ShapeNode &shape, int parentGroupId, std::function<void()> onChanged);
    AddShapeCommand(SceneDocument *scene, const ShapeNode &shape, int parentGroupId, int treeInsertIndex, std::function<void()> onChanged);

    void undo() override;
    void redo() override;

private:
    SceneDocument *m_scene = nullptr;
    ShapeNode m_shape;
    int m_index = -1;
    int m_parentGroupId = 0;
    int m_treeInsertIndex = -1;
    bool m_firstRedo = true;
    std::function<void()> m_onChanged;
};

class DeleteShapeCommand : public QUndoCommand
{
public:
    DeleteShapeCommand(SceneDocument *scene, int shapeId, std::function<void()> onChanged);

    bool isValid() const;
    void undo() override;
    void redo() override;

private:
    SceneDocument *m_scene = nullptr;
    ShapeNode m_shape;
    int m_index = -1;
    bool m_valid = false;
    std::function<void()> m_onChanged;
};

class UpdateShapeCommand : public QUndoCommand
{
public:
    UpdateShapeCommand(SceneDocument *scene, const ShapeNode &oldShape, const ShapeNode &newShape, std::function<void()> onChanged);

    bool isValid() const;
    void undo() override;
    void redo() override;

private:
    SceneDocument *m_scene = nullptr;
    ShapeNode m_oldShape;
    ShapeNode m_newShape;
    bool m_valid = false;
    std::function<void()> m_onChanged;
};

class ReplaceSceneCommand : public SnapshotCommand
{
public:
    ReplaceSceneCommand(SceneDocument *scene, const QVector<ShapeNode> &newShapes, std::function<void()> onChanged);
    ReplaceSceneCommand(SceneDocument *scene, const SceneDocument::Snapshot &newSnapshot, std::function<void()> onChanged);
};

class AddGroupCommand : public SnapshotCommand
{
public:
    AddGroupCommand(SceneDocument *scene,
                    SceneDocument::TreeNode::Operation operation,
                    int parentGroupId,
                    int insertIndex,
                    std::function<void()> onChanged);
};

class AddPolyhedronGroupCommand : public SnapshotCommand
{
public:
    AddPolyhedronGroupCommand(SceneDocument *scene,
                              int parentGroupId,
                              int insertIndex,
                              std::function<void()> onChanged);
};

class RemoveGroupCommand : public SnapshotCommand
{
public:
    RemoveGroupCommand(SceneDocument *scene, int groupId, std::function<void()> onChanged);
};

class AddVariableCommand : public SnapshotCommand
{
public:
    AddVariableCommand(SceneDocument *scene, int insertIndex, std::function<void()> onChanged);
};

class AddModuleCallCommand : public SnapshotCommand
{
public:
    AddModuleCallCommand(SceneDocument *scene,
                         int moduleGroupId,
                         int parentGroupId,
                         int insertIndex,
                         std::function<void()> onChanged);
};

class RemoveVariableCommand : public SnapshotCommand
{
public:
    RemoveVariableCommand(SceneDocument *scene, int variableId, std::function<void()> onChanged);
};

class RemoveModuleCallCommand : public SnapshotCommand
{
public:
    RemoveModuleCallCommand(SceneDocument *scene, int moduleCallId, std::function<void()> onChanged);
};

class UpdateVariableExpressionCommand : public SnapshotCommand
{
public:
    UpdateVariableExpressionCommand(SceneDocument *scene, int variableId, const QString &expression, std::function<void()> onChanged);
};

class UpdateModuleCallArgumentCommand : public SnapshotCommand
{
public:
    UpdateModuleCallArgumentCommand(SceneDocument *scene,
                                    int moduleCallId,
                                    const QString &parameterName,
                                    const QString &expression,
                                    std::function<void()> onChanged);
};

class UpdateForLoopCommand : public SnapshotCommand
{
public:
    UpdateForLoopCommand(SceneDocument *scene, int groupId, const QString &loopVariable, const QString &rangeExpression, std::function<void()> onChanged);
};

class UpdateGroupColorCommand : public SnapshotCommand
{
public:
    UpdateGroupColorCommand(SceneDocument *scene, int groupId, const QColor &color, std::function<void()> onChanged);
};

class MoveTreeNodeCommand : public SnapshotCommand
{
public:
    MoveTreeNodeCommand(SceneDocument *scene, int nodeId, int parentGroupId, int insertIndex, std::function<void()> onChanged, bool moduleParameterZone = false);
};

class RenameModuleCommand : public SnapshotCommand
{
public:
    RenameModuleCommand(SceneDocument *scene, int groupId, const QString &newName, std::function<void()> onChanged);
};

class RenameVariableCommand : public SnapshotCommand
{
public:
    RenameVariableCommand(SceneDocument *scene, int variableId, const QString &newName, std::function<void()> onChanged);
};

class UpdateGroupTransformCommand : public SnapshotCommand
{
public:
    UpdateGroupTransformCommand(SceneDocument *scene,
                                int groupId,
                                const QVector3D &position,
                                const QVector3D &rotation,
                                const QVector3D &scale,
                                std::function<void()> onChanged);
    UpdateGroupTransformCommand(SceneDocument *scene,
                                int groupId,
                                const QVector3D &newPosition,
                                const QVector3D &newRotation,
                                const QVector3D &newScale,
                                std::function<void()> onChanged,
                                const QVector3D &oldPosition,
                                const QVector3D &oldRotation,
                                const QVector3D &oldScale);
    UpdateGroupTransformCommand(SceneDocument *scene,
                                const SceneDocument::Snapshot &oldSnapshot,
                                const SceneDocument::Snapshot &newSnapshot,
                                std::function<void()> onChanged);
};

#endif
