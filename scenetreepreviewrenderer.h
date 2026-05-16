#ifndef SCENETREEPREVIEWRENDERER_H
#define SCENETREEPREVIEWRENDERER_H

#include "scenedocument.h"
#include "scenetreelayout.h"

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QRectF>
#include <QString>
#include <QVector>

class SceneTreePreviewRenderer
{
public:
    using ChildLayout = SceneTreeLayout::ChildLayout;
    using DropTarget = SceneTreeLayout::DropTarget;
    using GroupHitArea = SceneTreeLayout::GroupHitArea;

    SceneTreePreviewRenderer(QGraphicsScene *scene,
                             QVector<QGraphicsItem *> *previewItems,
                             const SceneDocument *document,
                             const SceneTreeLayout *layout);

    void render(const DropTarget &target, const QString &previewTool, int movingNodeId);
    void clear();

private:
    void addPreviewGroupFrameForOperation(const QRectF &rect,
                                          SceneDocument::TreeNode::Operation operation,
                                          qreal cutSeparatorY);
    void addExpandedGroupPreviews(const DropTarget &target);
    void addSourceGroupPreview(const DropTarget &target, int movingNodeId);
    void addTargetGroupPreview(const DropTarget &target, const QString &previewTool);
    void addPreviewExistingNode(int nodeId, const QRectF &rect);
    void addPreviewTreeItem(const QString &tool, int nodeId, const QRectF &rect);
    void addPreviewChildren(const QVector<ChildLayout> &children, const QRectF &excludedRect = QRectF());
    QString previewToolForNode(const SceneDocument::TreeNode &node) const;

private:
    QGraphicsScene *m_scene = nullptr;
    QVector<QGraphicsItem *> *m_previewItems = nullptr;
    const SceneDocument *m_document = nullptr;
    const SceneTreeLayout *m_layout = nullptr;
};

#endif
