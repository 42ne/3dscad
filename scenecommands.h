#ifndef SCENECOMMANDS_H
#define SCENECOMMANDS_H

#include "scenedocument.h"

#include <QUndoCommand>
#include <functional>

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

class ReplaceSceneCommand : public QUndoCommand
{
public:
    ReplaceSceneCommand(SceneDocument *scene, const QVector<ShapeNode> &newShapes, std::function<void()> onChanged);
    ReplaceSceneCommand(SceneDocument *scene, const SceneDocument::Snapshot &newSnapshot, std::function<void()> onChanged);

    bool isValid() const;
    void undo() override;
    void redo() override;

private:
    SceneDocument *m_scene = nullptr;
    SceneDocument::Snapshot m_oldSnapshot;
    SceneDocument::Snapshot m_newSnapshot;
    QVector<ShapeNode> m_newShapes;
    bool m_valid = false;
    std::function<void()> m_onChanged;
};

class AddGroupCommand : public QUndoCommand
{
public:
    AddGroupCommand(SceneDocument *scene,
                    SceneDocument::TreeNode::Operation operation,
                    int parentGroupId,
                    int insertIndex,
                    std::function<void()> onChanged);

    bool isValid() const;
    void undo() override;
    void redo() override;

private:
    SceneDocument *m_scene = nullptr;
    SceneDocument::Snapshot m_oldSnapshot;
    SceneDocument::Snapshot m_newSnapshot;
    bool m_valid = false;
    std::function<void()> m_onChanged;
};

class RemoveGroupCommand : public QUndoCommand
{
public:
    RemoveGroupCommand(SceneDocument *scene, int groupId, std::function<void()> onChanged);

    bool isValid() const;
    void undo() override;
    void redo() override;

private:
    SceneDocument *m_scene = nullptr;
    SceneDocument::Snapshot m_oldSnapshot;
    SceneDocument::Snapshot m_newSnapshot;
    bool m_valid = false;
    std::function<void()> m_onChanged;
};

class AddVariableCommand : public QUndoCommand
{
public:
    AddVariableCommand(SceneDocument *scene, int insertIndex, std::function<void()> onChanged);

    bool isValid() const;
    void undo() override;
    void redo() override;

private:
    SceneDocument *m_scene = nullptr;
    SceneDocument::Snapshot m_oldSnapshot;
    SceneDocument::Snapshot m_newSnapshot;
    bool m_valid = false;
    std::function<void()> m_onChanged;
};

class RemoveVariableCommand : public QUndoCommand
{
public:
    RemoveVariableCommand(SceneDocument *scene, int variableId, std::function<void()> onChanged);

    bool isValid() const;
    void undo() override;
    void redo() override;

private:
    SceneDocument *m_scene = nullptr;
    SceneDocument::Snapshot m_oldSnapshot;
    SceneDocument::Snapshot m_newSnapshot;
    bool m_valid = false;
    std::function<void()> m_onChanged;
};

class UpdateVariableExpressionCommand : public QUndoCommand
{
public:
    UpdateVariableExpressionCommand(SceneDocument *scene, int variableId, const QString &expression, std::function<void()> onChanged);

    bool isValid() const;
    void undo() override;
    void redo() override;

private:
    SceneDocument *m_scene = nullptr;
    SceneDocument::Snapshot m_oldSnapshot;
    SceneDocument::Snapshot m_newSnapshot;
    bool m_valid = false;
    std::function<void()> m_onChanged;
};

class UpdateForLoopCommand : public QUndoCommand
{
public:
    UpdateForLoopCommand(SceneDocument *scene, int groupId, const QString &loopVariable, const QString &rangeExpression, std::function<void()> onChanged);

    bool isValid() const;
    void undo() override;
    void redo() override;

private:
    SceneDocument *m_scene = nullptr;
    SceneDocument::Snapshot m_oldSnapshot;
    SceneDocument::Snapshot m_newSnapshot;
    bool m_valid = false;
    std::function<void()> m_onChanged;
};

class MoveTreeNodeCommand : public QUndoCommand
{
public:
    MoveTreeNodeCommand(SceneDocument *scene, int nodeId, int parentGroupId, int insertIndex, std::function<void()> onChanged);

    bool isValid() const;
    void undo() override;
    void redo() override;

private:
    SceneDocument *m_scene = nullptr;
    SceneDocument::Snapshot m_oldSnapshot;
    SceneDocument::Snapshot m_newSnapshot;
    bool m_valid = false;
    std::function<void()> m_onChanged;
};

class UpdateGroupTransformCommand : public QUndoCommand
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

    bool isValid() const;
    void undo() override;
    void redo() override;

private:
    SceneDocument *m_scene = nullptr;
    SceneDocument::Snapshot m_oldSnapshot;
    SceneDocument::Snapshot m_newSnapshot;
    bool m_valid = false;
    std::function<void()> m_onChanged;
};

#endif
