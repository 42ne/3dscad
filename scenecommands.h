#ifndef SCENECOMMANDS_H
#define SCENECOMMANDS_H

#include "scenedocument.h"

#include <QUndoCommand>
#include <functional>

class AddShapeCommand : public QUndoCommand
{
public:
    AddShapeCommand(SceneDocument *scene, const ShapeNode &shape, std::function<void()> onChanged);

    void undo() override;
    void redo() override;

private:
    SceneDocument *m_scene = nullptr;
    ShapeNode m_shape;
    int m_index = -1;
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

#endif
