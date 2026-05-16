#include "scenetreepreviewrenderer.h"
#include "scenetreegraphicshelpers.h"
#include "scenetreenoderenderer.h"

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

void SceneTreePreviewRenderer::render(const DropTarget &target, const QString &previewTool, int)
{
    addExpandedGroupPreviews(target);
    addSourceGroupPreview(target);
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
        SceneTreeNodeRenderer::renderPreviewGroup(m_scene, m_previewItems, operation, expandedRect, 0.0);
        const QVector<ChildLayout> children = i < target.expandedGroupChildren.size()
                                                  ? target.expandedGroupChildren[i]
                                                  : QVector<ChildLayout>();
        addPreviewChildren(children, target.previewGroupRect);
    }
}

void SceneTreePreviewRenderer::addSourceGroupPreview(const DropTarget &target)
{
    const bool sourceGroupCoveredByTarget = target.sourceGroupRect.isValid()
                                            && target.previewGroupRect.isValid()
                                            && target.previewGroupRect.contains(target.sourceGroupRect.center());
    bool sourceGroupCoveredByExpandedGroup = false;
    if (target.sourceGroupRect.isValid()) {
        for (const QRectF &expandedRect : target.expandedGroupRects) {
            if (expandedRect.contains(target.sourceGroupRect.center())) {
                sourceGroupCoveredByExpandedGroup = true;
                break;
            }
        }
    }

    if (target.sourceGroupRect.isValid() && !sourceGroupCoveredByTarget && !sourceGroupCoveredByExpandedGroup) {
        SceneTreeNodeRenderer::renderPreviewGroup(m_scene, m_previewItems, target.sourceGroupOperation, target.sourceGroupRect, target.sourceCutSeparatorY);
        addPreviewChildren(target.sourceChildren);
    }
}

void SceneTreePreviewRenderer::addTargetGroupPreview(const DropTarget &target, const QString &previewTool)
{
    if (target.previewGroupRect.isValid()) {
        SceneTreeNodeRenderer::renderPreviewGroup(m_scene, m_previewItems, target.previewGroupOperation, target.previewGroupRect, target.previewCutSeparatorY);
    }

    addPreviewChildren(target.previewChildren);

    if (target.hasTarget) {
        addDropSlotMarker(m_scene,
                          m_previewItems,
                          target.slotMarkerRect.isValid() ? target.slotMarkerRect : target.placeholderRect,
                          88.0);
        SceneTreeNodeRenderer::renderPreviewTool(m_scene, m_previewItems, previewTool, target.placeholderRect);
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
        SceneTreeNodeRenderer::renderPreviewTool(m_scene, m_previewItems, tool, rect);
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

    SceneTreeNodeRenderer::renderPreviewGroup(m_scene, m_previewItems, node->operation, rect, cutSeparatorY);

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

    SceneTreeNodeRenderer::renderPreviewTool(m_scene, m_previewItems, tool, rect);
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
    return toolNameForPrimitiveType(shape ? shape->type : ShapeNode::Cube);
}
