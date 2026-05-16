#include "scenetreepreviewrenderer.h"
#include "scenetreegraphicshelpers.h"

using namespace SceneTreeGraphics;

SceneTreePreviewRenderer::SceneTreePreviewRenderer(QGraphicsScene *scene,
                                                   QVector<QGraphicsItem *> *previewItems,
                                                   const SceneDocument *document,
                                                   const SceneTreeLayout *layout)
    : m_scene(scene)
    , m_previewItems(previewItems)
    , m_document(document)
    , m_layout(layout)
{
}

void SceneTreePreviewRenderer::render(const DropTarget &target, const QString &previewTool, int movingNodeId)
{
    addExpandedGroupPreviews(target);
    addSourceGroupPreview(target, movingNodeId);
    addTargetGroupPreview(target, previewTool);
}

void SceneTreePreviewRenderer::clear()
{
    if (!m_previewItems)
        return;

    for (QGraphicsItem *item : *m_previewItems)
        delete item;
    m_previewItems->clear();
}

void SceneTreePreviewRenderer::addPreviewGroupFrameForOperation(const QRectF &rect,
                                                                SceneDocument::TreeNode::Operation operation,
                                                                qreal cutSeparatorY)
{
    addPreviewGroupFrame(m_scene,
                         m_previewItems,
                         rect,
                         operation,
                         cutSeparatorY,
                         fillForOperation(operation));
}

void SceneTreePreviewRenderer::addExpandedGroupPreviews(const DropTarget &target)
{
    for (int i = 0; i < target.expandedGroupRects.size(); ++i) {
        const QRectF expandedRect = target.expandedGroupRects[i];
        if ((target.previewGroupRect.isValid() && expandedRect == target.previewGroupRect)
            || (target.sourceGroupRect.isValid() && expandedRect == target.sourceGroupRect)) {
            continue;
        }

        const SceneDocument::TreeNode::Operation operation = i < target.expandedGroupOperations.size()
                                                                 ? target.expandedGroupOperations[i]
                                                                 : SceneDocument::TreeNode::Union;
        addPreviewGroupFrameForOperation(expandedRect, operation, 0.0);
        const QVector<ChildLayout> children = i < target.expandedGroupChildren.size()
                                                  ? target.expandedGroupChildren[i]
                                                  : QVector<ChildLayout>();
        addPreviewChildren(children, target.previewGroupRect);
    }
}

void SceneTreePreviewRenderer::addSourceGroupPreview(const DropTarget &target, int movingNodeId)
{
    const bool sourceGroupCoveredByTarget = target.sourceGroupRect.isValid()
                                            && target.previewGroupRect.isValid()
                                            && target.previewGroupRect.contains(target.sourceGroupRect.center());
    if (target.sourceGroupRect.isValid() && !sourceGroupCoveredByTarget) {
        addPreviewGroupFrameForOperation(target.sourceGroupRect, target.sourceGroupOperation, target.sourceCutSeparatorY);
        if (movingNodeId > 0 && target.sourceRect.isValid()) {
            addSourceRemovalMask(m_scene,
                                 m_previewItems,
                                 target.sourceRect,
                                 fillForOperation(target.sourceGroupOperation));
        }
        addPreviewChildren(target.sourceChildren);
    }
}

void SceneTreePreviewRenderer::addTargetGroupPreview(const DropTarget &target, const QString &previewTool)
{
    if (target.previewGroupRect.isValid()) {
        addPreviewGroupFrameForOperation(target.previewGroupRect, target.previewGroupOperation, target.previewCutSeparatorY);
    }

    addPreviewChildren(target.previewChildren);

    if (target.hasTarget) {
        addPreviewBlock(m_scene,
                        m_previewItems,
                        previewTool,
                        target.placeholderRect,
                        fillForTool(previewTool));
        addDragFocusOutline(m_scene,
                            m_previewItems,
                            previewTool,
                            target.placeholderRect,
                            90.0);
    }
}

void SceneTreePreviewRenderer::addPreviewExistingNode(int nodeId, const QRectF &rect)
{
    if (!m_document || !m_layout || nodeId <= 0)
        return;

    const SceneDocument::TreeNode *node = m_document->treeNodeById(nodeId);
    if (!node)
        return;

    const QString tool = previewToolForNode(*node);
    if (node->type == SceneDocument::TreeNode::Primitive) {
        addPreviewBlock(m_scene,
                        m_previewItems,
                        tool,
                        rect,
                        fillForTool(tool));
        return;
    }

    qreal cutSeparatorY = 0.0;
    const GroupHitArea *area = nullptr;
    for (const GroupHitArea &candidate : m_layout->groupHitAreas()) {
        if (candidate.groupId == nodeId) {
            area = &candidate;
            break;
        }
    }

    if (area)
        cutSeparatorY = area->cutSeparatorY + (rect.top() - area->rect.top());

    addPreviewGroupFrameForOperation(rect, node->operation, cutSeparatorY);

    if (!area)
        return;

    const QPointF offset = rect.topLeft() - area->rect.topLeft();
    for (const ChildLayout &child : area->children) {
        addPreviewExistingNode(child.nodeId, child.rect.translated(offset));
    }
}

void SceneTreePreviewRenderer::addPreviewTreeItem(const QString &tool, int nodeId, const QRectF &rect)
{
    if (nodeId > 0) {
        addPreviewExistingNode(nodeId, rect);
        return;
    }

    addPreviewBlock(m_scene,
                    m_previewItems,
                    tool,
                    rect,
                    fillForTool(tool));
}

void SceneTreePreviewRenderer::addPreviewChildren(const QVector<ChildLayout> &children, const QRectF &excludedRect)
{
    for (const ChildLayout &child : children) {
        if (excludedRect.isValid() && child.rect.contains(excludedRect.center()))
            continue;

        addPreviewTreeItem(child.tool.isEmpty() ? QStringLiteral("cube") : child.tool,
                           child.nodeId,
                           child.rect);
    }
}

QString SceneTreePreviewRenderer::previewToolForNode(const SceneDocument::TreeNode &node) const
{
    if (node.type != SceneDocument::TreeNode::Primitive)
        return labelForOperation(node.operation);

    const ShapeNode *shape = m_document ? m_document->shapeById(node.shapeId) : nullptr;
    const ShapeNode::Type type = shape ? shape->type : ShapeNode::Cube;
    if (type == ShapeNode::Sphere)
        return "sphere";
    if (type == ShapeNode::Cylinder)
        return "cylinder";
    return "cube";
}
