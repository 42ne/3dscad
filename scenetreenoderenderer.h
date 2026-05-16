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

    SceneTreeNodeRenderer(QGraphicsScene *scene, int selectedNodeId, NodeSelectedCallback onSelected);

    void renderPrimitive(const SceneDocument::TreeNode &node,
                         const QRectF &rect,
                         const QString &label,
                         ShapeNode::Type type);

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
    NodeSelectedCallback m_onSelected;
};

#endif
