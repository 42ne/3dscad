#ifndef SCENETREENODERENDERER_H
#define SCENETREENODERENDERER_H

#include "scenedocument.h"
#include "shapenode.h"

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
                          int activeVariableNumberStart = -1);

    void renderPrimitive(const SceneDocument::TreeNode &node,
                         const QRectF &rect,
                         const QString &label,
                         const ShapeNode *shape);

    void renderVariable(const SceneDocument::TreeNode &node,
                        const QRectF &rect);

    void renderGroup(const SceneDocument::TreeNode &node,
                     const QRectF &rect,
                     int depth,
                     qreal cutSeparatorY);

    static void renderPreviewTool(QGraphicsScene *scene,
                                  QVector<QGraphicsItem *> *items,
                                  const QString &tool,
                                  const QRectF &rect);
    static void renderPreviewGroup(QGraphicsScene *scene,
                                   QVector<QGraphicsItem *> *items,
                                   SceneDocument::TreeNode::Operation operation,
                                   const QRectF &rect,
                                   qreal cutSeparatorY = 0.0);

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
    NodeSelectedCallback m_onSelected;
};

#endif
