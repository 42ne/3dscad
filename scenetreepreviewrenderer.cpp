#include "scenetreepreviewrenderer.h"
#include "scenetreegraphicshelpers.h"
#include "scenetreenoderenderer.h"

#include <QGraphicsSimpleTextItem>
#include <QPen>

using namespace SceneTreeGraphics;

namespace {

SceneDocument::TreeNode::Operation operationForTargetPreview(const SceneTreeLayout::DropTarget &target,
                                                             const SceneDocument *document,
                                                             const SceneTreeLayout *layout)
{
    if (document) {
        const SceneDocument::TreeNode *node = document->treeNodeById(target.parentGroupId);
        if (node && node->type == SceneDocument::TreeNode::Group)
            return node->operation;
    }

    if (!layout || !target.previewGroupRect.isValid())
        return target.previewGroupOperation;

    int bestDepth = -1;
    SceneDocument::TreeNode::Operation operation = target.previewGroupOperation;
    for (const SceneTreeLayout::GroupHitArea &area : layout->groupHitAreas()) {
        if (area.depth <= bestDepth)
            continue;

        const QRectF overlap = area.rect.intersected(target.previewGroupRect);
        const qreal requiredWidth = qMin(area.rect.width(), target.previewGroupRect.width()) * 0.65;
        const qreal requiredHeight = qMin(area.rect.height(), target.previewGroupRect.height()) * 0.45;
        if (overlap.width() < requiredWidth || overlap.height() < requiredHeight)
            continue;

        bestDepth = area.depth;
        operation = area.operation;
    }

    return operation;
}

QVector<ModuleCallParam> moduleParamsForNode(const SceneDocument::TreeNode &node)
{
    QVector<ModuleCallParam> params;
    for (const SceneDocument::TreeNode &child : node.children) {
        if (child.type != SceneDocument::TreeNode::Variable || !child.isParameter)
            continue;

        const QString expr = child.variableExpression.trimmed().isEmpty()
                                 ? QString::number(child.variableValue)
                                 : child.variableExpression.trimmed();
        params.append({child.id, child.variableName, expr});
    }
    return params;
}

void appendSimpleText(QGraphicsScene *scene,
                      QVector<QGraphicsItem *> *items,
                      const QString &text,
                      const QPointF &pos,
                      qreal zValue)
{
    auto *label = scene->addSimpleText(text);
    label->setBrush(QColor(84, 95, 116));
    label->setPos(pos);
    label->setZValue(zValue);
    appendPreviewItem(items, label);
}

void appendModuleCallChip(QGraphicsScene *scene,
                          QVector<QGraphicsItem *> *items,
                          const QRectF &rect,
                          const QString &moduleName,
                          const QVector<ModuleCallParam> &params,
                          qreal zValue)
{
    auto *panel = addRoundedPanel(scene,
                                  rect,
                                  CornerRadius,
                                  QPen(QColor(128, 166, 206), 1.0),
                                  QBrush(QColor(246, 251, 255, 230)),
                                  zValue);
    appendPreviewItem(items, panel);

    const QRectF badgeRect(rect.left() + 6.0, rect.top() + 6.0, 28.0, rect.height() - 12.0);
    auto *badge = addRoundedPanel(scene,
                                  badgeRect,
                                  3.0,
                                  QPen(QColor(0, 112, 192), 1.0),
                                  QBrush(QColor(255, 255, 255, 238)),
                                  zValue + 1.0);
    appendPreviewItem(items, badge);

    auto *badgeText = scene->addSimpleText(QStringLiteral("CALL"));
    badgeText->setBrush(QColor(0, 88, 160));
    badgeText->setScale(0.62);
    badgeText->setPos(badgeRect.left() + 3.0, badgeRect.top() + 4.0);
    badgeText->setZValue(zValue + 2.0);
    appendPreviewItem(items, badgeText);

    QString text = moduleName.trimmed().isEmpty() ? QStringLiteral("module") : moduleName.trimmed();
    text += QLatin1Char('(');
    for (int i = 0; i < params.size(); ++i) {
        if (i > 0)
            text += QStringLiteral(", ");
        text += params[i].name + QStringLiteral(" = ") + params[i].expression;
    }
    text += QLatin1Char(')');

    auto *nameText = scene->addSimpleText(text);
    nameText->setBrush(QColor(25, 92, 156));
    nameText->setPos(rect.left() + 40.0, rect.top() + 4.0);
    nameText->setZValue(zValue + 2.0);
    appendPreviewItem(items, nameText);
}

} // namespace

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
    for (const GroupPreview &expandedGroup : target.expandedGroups) {
        const QRectF expandedRect = expandedGroup.rect;
        if ((target.previewGroupRect.isValid() && expandedRect == target.previewGroupRect)
            || (target.sourceGroupRect.isValid() && expandedRect == target.sourceGroupRect)) {
            continue;
        }

        SceneTreeNodeRenderer::renderPreviewGroup(m_scene, m_previewItems, expandedGroup.operation, expandedRect, 0.0);
        addPreviewChildren(expandedGroup.children, target.previewGroupRect);
    }
}

void SceneTreePreviewRenderer::addSourceGroupPreview(const DropTarget &target)
{
    const bool sourceGroupCoveredByTarget = target.sourceGroupRect.isValid()
                                            && target.previewGroupRect.isValid()
                                            && target.previewGroupRect.contains(target.sourceGroupRect.center());
    bool sourceGroupCoveredByExpandedGroup = false;
    if (target.sourceGroupRect.isValid()) {
        for (const GroupPreview &expandedGroup : target.expandedGroups) {
            if (expandedGroup.rect.contains(target.sourceGroupRect.center())) {
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
    // Only draw the target-group preview when there is an active drop target;
    // otherwise interpolation can leave a stale phantom group rectangle visible
    // while animating away from a now-invalid position (e.g. VAR dragged over
    // a union group where it is not allowed).
    if (target.previewGroupRect.isValid() && target.hasTarget) {
        const SceneDocument::TreeNode::Operation operation = operationForTargetPreview(target, m_document, m_layout);
        if (operation == SceneDocument::TreeNode::Module) {
            addModuleTargetPreview(target);
        } else {
            SceneTreeNodeRenderer::renderPreviewGroup(m_scene,
                                                      m_previewItems,
                                                      operation,
                                                      target.previewGroupRect,
                                                      target.previewCutSeparatorY);
        }
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

bool SceneTreePreviewRenderer::addModuleTargetPreview(const DropTarget &target)
{
    if (!m_scene || !target.previewGroupRect.isValid())
        return false;

    const SceneDocument::TreeNode *moduleNode = m_document ? m_document->treeNodeById(target.parentGroupId) : nullptr;
    if (!moduleNode || moduleNode->operation != SceneDocument::TreeNode::Module)
        return false;

    SceneTreeNodeRenderer::renderPreviewGroup(m_scene,
                                              m_previewItems,
                                              SceneDocument::TreeNode::Module,
                                              target.previewGroupRect,
                                              0.0);

    const GroupHitArea *area = groupAreaForId(target.parentGroupId);
    qreal separatorY = target.previewGroupRect.top() + GroupHeaderHeight + GroupPadding + 16.0 + VariableHeight + ChildGap + ChildGap * 0.5;
    int parameterCount = 0;
    if (area) {
        separatorY = area->moduleParameterSeparatorY + (target.previewGroupRect.top() - area->rect.top());
        parameterCount = qBound(0, area->moduleParameterCount, target.previewChildren.size());
    }

    for (int i = 0; i < parameterCount && i < target.previewChildren.size(); ++i)
        separatorY = qMax(separatorY, target.previewChildren[i].rect.bottom() + ChildGap * 0.5);
    if (target.moduleParameterZone && target.placeholderRect.isValid())
        separatorY = qMax(separatorY, target.placeholderRect.bottom() + ChildGap * 0.5);

    const QVector<ModuleCallParam> params = moduleParamsForNode(*moduleNode);
    const QRectF callRect(QPointF(target.previewGroupRect.left() + GroupPadding,
                                  separatorY + 16.0 + ChildGap),
                          moduleCallPreviewSize(moduleNode->moduleName, params));
    const qreal bodyLabelY = callRect.bottom() + ChildGap + 4.0;
    const qreal z = 60.0;

    appendSimpleText(m_scene,
                     m_previewItems,
                     QStringLiteral("parameters"),
                     QPointF(target.previewGroupRect.left() + GroupPadding,
                             target.previewGroupRect.top() + GroupHeaderHeight + 4.0),
                     z);
    appendSimpleText(m_scene,
                     m_previewItems,
                     QStringLiteral("call handle"),
                     QPointF(target.previewGroupRect.left() + GroupPadding, separatorY + 4.0),
                     z);
    appendSimpleText(m_scene,
                     m_previewItems,
                     QStringLiteral("body"),
                     QPointF(target.previewGroupRect.left() + GroupPadding, bodyLabelY),
                     z);

    auto *separator = m_scene->addLine(target.previewGroupRect.left() + GroupPadding,
                                      separatorY,
                                      target.previewGroupRect.right() - GroupPadding,
                                      separatorY,
                                      QPen(QColor(142, 151, 166), 1, Qt::DashLine));
    separator->setZValue(z - 1.0);
    appendPreviewItem(m_previewItems, separator);

    appendModuleCallChip(m_scene, m_previewItems, callRect, moduleNode->moduleName, params, z - 0.5);
    return true;
}

void SceneTreePreviewRenderer::addPreviewExistingNode(int nodeId, const QRectF &rect)
{
    if (!m_document || !m_layout || nodeId <= 0)
        return;

    const SceneDocument::TreeNode *node = m_document->treeNodeById(nodeId);
    if (!node)
        return;

    const QString tool = previewToolForNode(*node);
    if (node->type == SceneDocument::TreeNode::Primitive || node->type == SceneDocument::TreeNode::Variable) {
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

const SceneTreePreviewRenderer::GroupHitArea *SceneTreePreviewRenderer::groupAreaForId(int groupId) const
{
    if (!m_layout)
        return nullptr;

    for (const GroupHitArea &area : m_layout->groupHitAreas()) {
        if (area.groupId == groupId)
            return &area;
    }
    return nullptr;
}

QString SceneTreePreviewRenderer::previewToolForNode(const SceneDocument::TreeNode &node) const
{
    if (node.type == SceneDocument::TreeNode::Variable)
        return node.isParameter ? QStringLiteral("par") : QStringLiteral("var");
    if (node.type == SceneDocument::TreeNode::ModuleCall)
        return QStringLiteral("call");
    if (node.type != SceneDocument::TreeNode::Primitive)
        return labelForOperation(node.operation);

    const ShapeNode *shape = m_document ? m_document->shapeById(node.shapeId) : nullptr;
    return toolNameForPrimitiveType(shape ? shape->type : ShapeNode::Cube);
}
