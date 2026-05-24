#ifndef SCENETREENODERENDERER_H
#define SCENETREENODERENDERER_H

#include "scenedocument.h"
#include "scenetreegraphicshelpers.h"
#include "shapenode.h"

#include <QImage>
#include <QRectF>
#include <QVector>
#include <functional>

class QGraphicsScene;
class QGraphicsItem;

class SceneTreeNodeRenderer
{
public:
    using NodeSelectedCallback = std::function<void(int)>;

    SceneTreeNodeRenderer(QGraphicsScene *scene,
                          int selectedNodeId,
                          NodeSelectedCallback onSelected,
                          int activeTransformNodeId = 0,
                          int activeTransformAxis = -1,
                          int activeTransformNumberStart = -1,
                          int activeShapeNodeId = 0,
                          int activeShapeParameter = -1,
                          int activeShapeParamNumberStart = -1,
                          int activeVariableNodeId = 0,
                          int activeVariableNumberStart = -1,
                          int activeForLoopNodeId = 0,
                          int activeForLoopNumberStart = -1,
                          int activeModuleCallNodeId = 0,
                          int activeModuleCallVarNodeId = 0,
                          int activeModuleCallNumberStart = -1);

    void renderPrimitive(const SceneDocument::TreeNode &node,
                         const QRectF &rect,
                         const QString &label,
                         const ShapeNode *shape,
                         const QImage &thumbnail = QImage());

    void renderVariable(const SceneDocument::TreeNode &node,
                        const QRectF &rect);

    void renderModuleCall(const SceneDocument::TreeNode &node,
                          const QRectF &rect,
                          const QVector<SceneTreeGraphics::ModuleCallParam> &params = {});

    void renderGroup(const SceneDocument::TreeNode &node,
                     const QRectF &rect,
                     int depth,
                     qreal cutSeparatorY,
                     const QImage &thumbnail = QImage());

    // Set the active visual theme (0 = Frost, 1 = Glass, 2 = Embers, 3 = Deep).
    // Returns *this so it can be chained: SceneTreeNodeRenderer(...).setTheme(t).renderXxx(...)
    SceneTreeNodeRenderer &setTheme(int theme) { m_theme = theme; return *this; }

    static void renderPreviewTool(QGraphicsScene *scene,
                                  QVector<QGraphicsItem *> *items,
                                  const QString &tool,
                                  const QRectF &rect,
                                  int theme = 0);
    static void renderPreviewPrimitive(QGraphicsScene *scene,
                                       QVector<QGraphicsItem *> *items,
                                       const ShapeNode *shape,
                                       int shapeId,
                                       const QRectF &rect);
    static void renderPreviewVariable(QGraphicsScene *scene,
                                      QVector<QGraphicsItem *> *items,
                                      const QString &name,
                                      const QString &expression,
                                      bool isParameter,
                                      const QRectF &rect,
                                      int theme = 0);
    static void renderPreviewGroup(QGraphicsScene *scene,
                                   QVector<QGraphicsItem *> *items,
                                   SceneDocument::TreeNode::Operation operation,
                                   const QRectF &rect,
                                   qreal cutSeparatorY = 0.0,
                                   int theme = 0,
                                   int depth = 0,
                                   const QColor &color = QColor());

private:
    qreal zForDepth(int depth, qreal offset) const;

private:
    QGraphicsScene *m_scene = nullptr;
    int m_selectedNodeId = 0;
    int m_activeTransformNodeId = 0;
    int m_activeTransformAxis = -1;
    int m_activeTransformNumberStart = -1;
    int m_activeShapeNodeId = 0;
    int m_activeShapeParameter = -1;
    int m_activeShapeParamNumberStart = -1;
    int m_activeVariableNodeId = 0;
    int m_activeVariableNumberStart = -1;
    int m_activeForLoopNodeId = 0;
    int m_activeForLoopNumberStart = -1;
    int m_activeModuleCallNodeId = 0;
    int m_activeModuleCallVarNodeId = 0;
    int m_activeModuleCallNumberStart = -1;
    int m_theme = 0;  // SceneTreePalette::Theme cast to int; 0 = Frost
    NodeSelectedCallback m_onSelected;
};

#endif
