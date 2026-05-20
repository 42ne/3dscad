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
    using GroupPreview = SceneTreeLayout::GroupPreview;

    SceneTreePreviewRenderer(QGraphicsScene *scene,
                             QVector<QGraphicsItem *> *previewItems,
                             const SceneDocument *document,
                             const SceneTreeLayout *layout);

    void render(const DropTarget &target, const QString &previewTool, int movingNodeId);
    void clear();

private:
    void addExpandedGroupPreviews(const DropTarget &target);
    void addSourceGroupPreview(const DropTarget &target);
    void addTargetGroupPreview(const DropTarget &target, const QString &previewTool);
    bool addModuleTargetPreview(const DropTarget &target);
    void addPreviewExistingNode(int nodeId, const QRectF &rect);
    void addPreviewTreeItem(const QString &tool, int nodeId, const QRectF &rect);
    void addPreviewChildren(const QVector<ChildLayout> &children, const QRectF &excludedRect = QRectF());
    const GroupHitArea *groupAreaForId(int groupId) const;
    QString previewToolForNode(const SceneDocument::TreeNode &node) const;

private:
    QGraphicsScene *m_scene = nullptr;
    QVector<QGraphicsItem *> *m_previewItems = nullptr;
    const SceneDocument *m_document = nullptr;
    const SceneTreeLayout *m_layout = nullptr;
};

#endif
