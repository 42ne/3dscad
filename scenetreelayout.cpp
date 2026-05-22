#include "scenetreelayout.h"
#include "scenetreegraphicshelpers.h"

#include <QtGlobal>
#include <algorithm>

using namespace SceneTreeGraphics;

namespace {

QRectF groupContentRect(const QRectF &groupRect, SceneDocument::TreeNode::Operation operation)
{
    if (isTransformOperation(operation)) {
        return groupRect.adjusted(TransformHeaderWidth + GroupPadding,
                                  GroupPadding,
                                  -GroupPadding,
                                  -GroupPadding);
    }

    return groupRect.adjusted(GroupPadding,
                             GroupHeaderHeight + GroupPadding,
                             -GroupPadding,
                             -GroupPadding);
}

QVector<SceneTreeLayout::ChildLayout> childrenWithoutMovingNode(const SceneTreeLayout::GroupHitArea &area,
                                                                int movingNodeId,
                                                                int sourceChildIndex,
                                                                qreal sourceRemovalShift,
                                                                bool compactAfterRemoval)
{
    QVector<SceneTreeLayout::ChildLayout> children;
    for (int i = 0; i < area.children.size(); ++i) {
        SceneTreeLayout::ChildLayout child = area.children[i];
        if (child.nodeId == movingNodeId)
            continue;

        if (compactAfterRemoval && sourceChildIndex >= 0 && i > sourceChildIndex)
            child.rect.translate(0.0, -sourceRemovalShift);

        children.append(child);
    }

    return children;
}

bool expandedParentPreview(const SceneTreeLayout::GroupHitArea &area,
                           const QVector<SceneTreeLayout::ChildLayout> &baseChildren,
                           const QRectF &changedOldRect,
                           const QRectF &changedNewRect,
                           QRectF *expandedRect,
                           QVector<SceneTreeLayout::ChildLayout> *expandedChildren)
{
    QVector<QRectF> childRects;
    for (const SceneTreeLayout::ChildLayout &child : baseChildren)
        childRects.append(child.rect);

    QRectF oldChildRect;
    int changedChildIndex = -1;
    for (int childIndex = 0; childIndex < baseChildren.size(); ++childIndex) {
        if (baseChildren[childIndex].rect.contains(changedOldRect.center())) {
            oldChildRect = baseChildren[childIndex].rect;
            changedChildIndex = childIndex;
            break;
        }
    }

    if (!oldChildRect.isValid())
        return false;

    QVector<SceneTreeLayout::ChildLayout> children = baseChildren;
    const qreal childShift = changedNewRect.height() - oldChildRect.height();
    children[changedChildIndex].rect = changedNewRect;
    if (!qFuzzyIsNull(childShift)) {
        for (int childIndex = changedChildIndex + 1; childIndex < children.size(); ++childIndex)
            children[childIndex].rect.translate(0.0, childShift);
    }

    if (expandedRect)
        *expandedRect = expandedGroupRectForChangedChild(area.rect, childRects, oldChildRect, changedNewRect);
    if (expandedChildren)
        *expandedChildren = children;
    return true;
}

void cancelTargetPreview(SceneTreeLayout::DropTarget *target)
{
    if (!target)
        return;

    target->hasTarget = false;
    target->parentGroupId = 0;
    target->insertIndex = -1;
    target->moduleParameterZone = false;
    target->zoneRect = QRectF();
    target->placeholderRect = QRectF();
    target->slotMarkerRect = QRectF();
    target->previewGroupRect = QRectF();
    target->previewChildren.clear();
    target->expandedGroups.clear();
}

QVector<SceneTreeLayout::ChildLayout> childrenShiftedFromIndex(const QVector<SceneTreeLayout::ChildLayout> &children,
                                                               int startIndex,
                                                               qreal shift)
{
    QVector<SceneTreeLayout::ChildLayout> shiftedChildren;
    const int boundedStartIndex = qBound(0, startIndex, children.size());
    for (int i = 0; i < children.size(); ++i) {
        SceneTreeLayout::ChildLayout child = children[i];
        if (i >= boundedStartIndex)
            child.rect.translate(0.0, shift);
        shiftedChildren.append(child);
    }

    return shiftedChildren;
}

QRectF placeholderRectForInsertIndexWithGap(const QRectF &contentRect,
                                            const QVector<QRectF> &childRects,
                                            int insertIndex,
                                            const QSizeF &previewSize,
                                            qreal gap)
{
    if (childRects.isEmpty())
        return QRectF(contentRect.left(), contentRect.top(), previewSize.width(), previewSize.height());

    if (insertIndex <= 0)
        return QRectF(contentRect.left(), contentRect.top(), previewSize.width(), previewSize.height());

    if (insertIndex >= childRects.size())
        return QRectF(contentRect.left(), childRects.last().bottom() + gap, previewSize.width(), previewSize.height());

    return QRectF(contentRect.left(), childRects[insertIndex - 1].bottom() + gap, previewSize.width(), previewSize.height());
}

QRectF slotMarkerRectForInsertIndexWithGap(const QRectF &contentRect,
                                           const QVector<QRectF> &childRects,
                                           int insertIndex,
                                           qreal gap)
{
    const qreal markerWidth = childRects.isEmpty()
                                  ? qMax(contentRect.width(), PrimitiveWidth)
                                  : qMax(contentRect.width(), childRects.first().width());
    qreal y = contentRect.top();

    if (childRects.isEmpty()) {
        y = contentRect.top();
    } else if (insertIndex <= 0) {
        y = childRects.first().top() - gap * 0.5;
    } else if (insertIndex >= childRects.size()) {
        y = childRects.last().bottom() + gap * 0.5;
    } else {
        y = childRects[insertIndex - 1].bottom() + gap * 0.5;
    }

    return QRectF(contentRect.left(), y, markerWidth, 1.0);
}

QRectF expandedGroupRectForPreviewWithGap(const QRectF &groupRect,
                                          const QRectF &placeholderRect,
                                          const QVector<QRectF> &childRects,
                                          int insertIndex,
                                          const QSizeF &previewSize,
                                          qreal gap)
{
    QRectF expanded = groupRect;
    QRectF futureContent = placeholderRect;
    const qreal shift = previewSize.height() + gap;

    for (int i = 0; i < childRects.size(); ++i) {
        const QRectF childRect = i >= insertIndex ? childRects[i].translated(0.0, shift) : childRects[i];
        futureContent = futureContent.united(childRect);
    }

    expanded.setRight(qMax(expanded.right(), futureContent.right() + GroupPadding));
    expanded.setBottom(qMax(expanded.bottom(), futureContent.bottom() + GroupPadding));
    return expanded;
}

qreal moduleBodyContentTop(qreal parameterSeparatorY)
{
    return parameterSeparatorY
           + 16.0
           + ChildGap
           + VariableHeight
           + ChildGap
           + 16.0
           + ChildGap;
}

void translatePreview(SceneTreeLayout::DropTarget *target, qreal dy)
{
    if (!target || qFuzzyIsNull(dy))
        return;

    target->previewGroupRect.translate(0.0, dy);
    target->placeholderRect.translate(0.0, dy);
    target->slotMarkerRect.translate(0.0, dy);
    for (SceneTreeLayout::ChildLayout &child : target->previewChildren)
        child.rect.translate(0.0, dy);
    for (SceneTreeLayout::GroupPreview &expandedGroup : target->expandedGroups) {
        expandedGroup.rect.translate(0.0, dy);
        for (SceneTreeLayout::ChildLayout &child : expandedGroup.children)
            child.rect.translate(0.0, dy);
    }
}

} // namespace

void SceneTreeLayout::clear()
{
    m_groupHitAreas.clear();
}

void SceneTreeLayout::addGroup(const GroupHitArea &area)
{
    m_groupHitAreas.append(area);
}

const QVector<SceneTreeLayout::GroupHitArea> &SceneTreeLayout::groupHitAreas() const
{
    return m_groupHitAreas;
}

SceneTreeLayout::DropTarget SceneTreeLayout::dropTargetAt(const QPointF &scenePosition,
                                                          const QSizeF &previewSize,
                                                          int movingNodeId,
                                                          bool variableDrop,
                                                          qreal insertionGapMultiplier) const
{
    DropTarget target;
    const QSizeF effectivePreviewSize = previewSize.isValid() ? previewSize : defaultPreviewSize();
    const qreal insertionGap = ChildGap * qBound<qreal>(1.0, insertionGapMultiplier, 4.0);
    int sourceChildIndex = -1;
    qreal sourceRemovalShift = 0.0;
    const GroupHitArea *sourceArea = findSourceArea(movingNodeId,
                                                    &target,
                                                    &sourceChildIndex,
                                                    &sourceRemovalShift);
    const GroupHitArea *bestArea = findBestDropArea(scenePosition,
                                                    effectivePreviewSize,
                                                    movingNodeId,
                                                    sourceArea,
                                                    target.sourceRect);

    if (sourceArea) {
        buildSourcePreview(sourceArea,
                           movingNodeId,
                           sourceChildIndex,
                           sourceRemovalShift,
                           &target);
    }

    if (!bestArea) {
        buildSourceAncestorPreviews(sourceArea, &target);
        return target;
    }

    const int bestDepth = bestArea->depth;
    qreal targetPreviewShift = 0.0;
    if (sourceArea && sourceArea != bestArea && sourceChildIndex >= 0) {
        for (int i = sourceChildIndex + 1; i < sourceArea->children.size(); ++i) {
            if (sourceArea->children[i].rect.contains(bestArea->rect.center())) {
                targetPreviewShift = -sourceRemovalShift;
                break;
            }
        }
    }

    target.hasTarget = true;
    target.parentGroupId = bestArea->groupId;
    target.previewGroupOperation = bestArea->operation;
    target.previewCutSeparatorY = bestArea->cutSeparatorY;
    QRectF contentRect = groupContentRect(bestArea->rect, bestArea->operation);
    if (isTransformOperation(bestArea->operation) && !bestArea->children.isEmpty())
        contentRect.setLeft(bestArea->children.first().rect.left());
    const bool reorderInSourceGroup = sourceArea == bestArea && sourceChildIndex >= 0;
    QVector<ChildLayout> candidateChildren = childrenWithoutMovingNode(*bestArea,
                                                                       movingNodeId,
                                                                       sourceChildIndex,
                                                                       sourceRemovalShift,
                                                                       reorderInSourceGroup);
    QVector<QRectF> candidateChildRects;
    for (const ChildLayout &child : candidateChildren)
        candidateChildRects.append(child.rect);

    target.zoneRect = contentRect;
    target.insertIndex = insertionIndexForY(candidateChildRects, scenePosition.y());
    QVector<QRectF> placementChildRects = candidateChildRects;
    int placementInsertIndex = target.insertIndex;

    if (bestArea->operation == SceneDocument::TreeNode::Module
        && bestArea->moduleParameterSeparatorY > 0.0) {
        const int parameterCount = qBound(0, bestArea->moduleParameterCount, candidateChildren.size());
        const bool parameterZone = variableDrop && scenePosition.y() < bestArea->moduleParameterSeparatorY;
        target.moduleParameterZone = parameterZone;

        QVector<QRectF> zoneChildRects;
        if (parameterZone) {
            for (int i = 0; i < parameterCount && i < candidateChildren.size(); ++i)
                zoneChildRects.append(candidateChildren[i].rect);
            target.zoneRect = QRectF(contentRect.left(),
                                     contentRect.top(),
                                     contentRect.width(),
                                     qMax<qreal>(PrimitiveHeight, bestArea->moduleParameterSeparatorY - contentRect.top()));
            placementInsertIndex = insertionIndexForY(zoneChildRects, scenePosition.y());
            target.insertIndex = placementInsertIndex;
        } else {
            for (int i = parameterCount; i < candidateChildren.size(); ++i)
                zoneChildRects.append(candidateChildren[i].rect);
            const qreal bodyContentTop = moduleBodyContentTop(bestArea->moduleParameterSeparatorY);
            target.zoneRect = QRectF(contentRect.left(),
                                     bodyContentTop,
                                     contentRect.width(),
                                     qMax<qreal>(PrimitiveHeight, contentRect.bottom() - bodyContentTop));
            placementInsertIndex = insertionIndexForY(zoneChildRects, scenePosition.y());
            target.insertIndex = parameterCount + placementInsertIndex;
        }

        placementChildRects = zoneChildRects;
    }

    target.placeholderRect = placeholderRectForInsertIndexWithGap(target.zoneRect,
                                                                  placementChildRects,
                                                                  placementInsertIndex,
                                                                  effectivePreviewSize,
                                                                  insertionGap);
    if (bestArea->operation == SceneDocument::TreeNode::Module
        && bestArea->moduleParameterSeparatorY > 0.0
        && variableDrop
        && !target.moduleParameterZone
        && target.placeholderRect.top() < bestArea->moduleParameterSeparatorY)
        target.placeholderRect.moveTop(bestArea->moduleParameterSeparatorY + insertionGap * 0.5);
    target.slotMarkerRect = slotMarkerRectForInsertIndexWithGap(target.zoneRect,
                                                               placementChildRects,
                                                               placementInsertIndex,
                                                               insertionGap);
    if (bestArea->operation == SceneDocument::TreeNode::Module
        && bestArea->moduleParameterSeparatorY > 0.0
        && variableDrop
        && !target.moduleParameterZone
        && target.slotMarkerRect.top() < bestArea->moduleParameterSeparatorY)
        target.slotMarkerRect.moveTop(bestArea->moduleParameterSeparatorY + insertionGap * 0.5);
    target.previewChildren = childrenShiftedFromIndex(candidateChildren,
                                                     target.insertIndex,
                                                     effectivePreviewSize.height() + insertionGap);

    if (bestArea->operation == SceneDocument::TreeNode::Difference && bestArea->cutSeparatorY > 0.0) {
        const bool baseZone = scenePosition.y() < bestArea->cutSeparatorY;
        target.insertIndex = baseZone
                                 ? 0
                                 : insertionIndexForY(candidateChildRects, scenePosition.y(), 1);
        target.zoneRect = baseZone
                              ? QRectF(contentRect.left(),
                                       contentRect.top(),
                                       contentRect.width(),
                                       qMax<qreal>(PrimitiveHeight, bestArea->cutSeparatorY - contentRect.top()))
                              : QRectF(contentRect.left(),
                                       bestArea->cutSeparatorY,
                                       contentRect.width(),
                                       qMax<qreal>(PrimitiveHeight, contentRect.bottom() - bestArea->cutSeparatorY));
        target.placeholderRect = placeholderRectForInsertIndexWithGap(contentRect,
                                                                      candidateChildRects,
                                                                      target.insertIndex,
                                                                      effectivePreviewSize,
                                                                      insertionGap);
        if (!baseZone && target.placeholderRect.top() < bestArea->cutSeparatorY)
            target.placeholderRect.moveTop(bestArea->cutSeparatorY + insertionGap * 0.5);
        target.slotMarkerRect = slotMarkerRectForInsertIndexWithGap(contentRect,
                                                                   candidateChildRects,
                                                                   target.insertIndex,
                                                                   insertionGap);
        if (!baseZone && target.slotMarkerRect.top() < bestArea->cutSeparatorY)
            target.slotMarkerRect.moveTop(bestArea->cutSeparatorY + insertionGap * 0.5);
        target.previewChildren = childrenShiftedFromIndex(candidateChildren,
                                                         target.insertIndex,
                                                         effectivePreviewSize.height() + insertionGap);
        if (baseZone)
            target.previewCutSeparatorY = target.placeholderRect.bottom() + insertionGap * 0.5;
    }

    if (sourceArea == bestArea && sourceChildIndex >= 0 && target.insertIndex == sourceChildIndex) {
        cancelTargetPreview(&target);
        buildSourcePreview(sourceArea,
                           movingNodeId,
                           sourceChildIndex,
                           sourceRemovalShift,
                           &target);
        buildSourceAncestorPreviews(sourceArea, &target);
        return target;
    }

    QRectF changedOldRect = bestArea->rect;
    QRectF changedNewRect = expandedGroupRectForPreviewWithGap(bestArea->rect,
                                                               target.placeholderRect,
                                                               candidateChildRects,
                                                               target.insertIndex,
                                                               effectivePreviewSize,
                                                               insertionGap);
    target.previewGroupRect = changedNewRect;

    QVector<const GroupHitArea *> containingAreas;
    for (const GroupHitArea &area : m_groupHitAreas) {
        if (area.depth <= bestDepth && area.rect.contains(bestArea->rect.center()))
            containingAreas.append(&area);
    }
    std::sort(containingAreas.begin(),
              containingAreas.end(),
              [](const GroupHitArea *left, const GroupHitArea *right) {
                  return left->depth > right->depth;
              });

    for (int i = 0; i < containingAreas.size(); ++i) {
        const GroupHitArea *area = containingAreas[i];
        QRectF expandedRect;
        bool containsChangedChild = false;
        QVector<ChildLayout> expandedChildren = area == sourceArea
                                                    ? childrenWithoutMovingNode(*area,
                                                                                movingNodeId,
                                                                                sourceChildIndex,
                                                                                sourceRemovalShift,
                                                                                false)
                                                    : area->children;

        if (area == bestArea) {
            expandedRect = changedNewRect;
        } else {
            if (!expandedParentPreview(*area,
                                       expandedChildren,
                                       changedOldRect,
                                       changedNewRect,
                                       &expandedRect,
                                       &expandedChildren)) {
                continue;
            }
            containsChangedChild = true;
        }

        const bool isAncestorOfChangedGroup = area != bestArea && containsChangedChild;
        if (expandedRect != area->rect || isAncestorOfChangedGroup) {
            target.expandedGroups.prepend({expandedRect, area->operation, expandedChildren});
        }

        changedOldRect = area->rect;
        changedNewRect = expandedRect;
    }

    translatePreview(&target, targetPreviewShift);

    return target;
}

void SceneTreeLayout::buildSourcePreview(const GroupHitArea *sourceArea,
                                         int movingNodeId,
                                         int sourceChildIndex,
                                         qreal sourceRemovalShift,
                                         DropTarget *target) const
{
    if (!sourceArea || !target)
        return;

    target->sourceGroupRect = sourceArea->rect;
    target->sourceGroupOperation = sourceArea->operation;
    target->sourceCutSeparatorY = sourceArea->cutSeparatorY;
    target->sourceChildren = childrenWithoutMovingNode(*sourceArea,
                                                       movingNodeId,
                                                       sourceChildIndex,
                                                       sourceRemovalShift,
                                                       true);

    QRectF futureContent;
    bool hasFutureContent = false;
    for (const ChildLayout &child : target->sourceChildren) {
        futureContent = hasFutureContent ? futureContent.united(child.rect) : child.rect;
        hasFutureContent = true;
    }

    qreal minContentHeight = PrimitiveHeight;
    if (sourceArea->operation == SceneDocument::TreeNode::Difference)
        minContentHeight = DifferenceMinContentHeight;

    const qreal headerHeight = isTransformOperation(sourceArea->operation) ? 0.0 : GroupHeaderHeight;
    const qreal minBottom = sourceArea->rect.top() + headerHeight + GroupPadding * 2.0 + minContentHeight;
    const qreal contentBottom = hasFutureContent ? futureContent.bottom() + GroupPadding : minBottom;
    target->sourceGroupRect.setBottom(qMax(minBottom, contentBottom));
    if (sourceArea->operation == SceneDocument::TreeNode::Difference && target->sourceCutSeparatorY > 0.0) {
        target->sourceCutSeparatorY = qMin(target->sourceCutSeparatorY,
                                          target->sourceGroupRect.bottom() - GroupPadding - PrimitiveHeight * 0.5);
    }
}

void SceneTreeLayout::buildSourceAncestorPreviews(const GroupHitArea *sourceArea,
                                                  DropTarget *target) const
{
    if (!sourceArea || !target || !target->sourceGroupRect.isValid())
        return;

    QRectF changedOldRect = sourceArea->rect;
    QRectF changedNewRect = target->sourceGroupRect;
    for (const GroupHitArea &area : m_groupHitAreas) {
        if (area.depth >= sourceArea->depth || !area.rect.contains(sourceArea->rect.center()))
            continue;

        QVector<ChildLayout> expandedChildren;
        QRectF expandedRect;
        if (!expandedParentPreview(area,
                                   area.children,
                                   changedOldRect,
                                   changedNewRect,
                                   &expandedRect,
                                   &expandedChildren)) {
            continue;
        }

        target->expandedGroups.prepend({expandedRect, area.operation, expandedChildren});
        changedOldRect = area.rect;
        changedNewRect = expandedRect;
    }
}

const SceneTreeLayout::GroupHitArea *SceneTreeLayout::findSourceArea(int movingNodeId,
                                                                                    DropTarget *target,
                                                                                    int *sourceChildIndex,
                                                                                    qreal *sourceRemovalShift) const
{
    if (movingNodeId <= 0 || !target)
        return nullptr;

    for (const GroupHitArea &area : m_groupHitAreas) {
        for (int i = 0; i < area.children.size(); ++i) {
            if (area.children[i].nodeId != movingNodeId)
                continue;

            target->sourceRect = area.children[i].rect;
            if (sourceChildIndex)
                *sourceChildIndex = i;
            if (sourceRemovalShift)
                *sourceRemovalShift = target->sourceRect.height() + ChildGap;
            return &area;
        }
    }

    return nullptr;
}

const SceneTreeLayout::GroupHitArea *SceneTreeLayout::findBestDropArea(const QPointF &scenePosition,
                                                                       const QSizeF &previewSize,
                                                                       int movingNodeId,
                                                                       const GroupHitArea *sourceArea,
                                                                       const QRectF &sourceRect) const
{
    int bestDepth = -1;
    const GroupHitArea *bestArea = nullptr;
    const QRectF previewRect(scenePosition - QPointF(previewSize.width() * 0.5,
                                                     previewSize.height() * 0.5),
                             previewSize);

    for (const GroupHitArea &area : m_groupHitAreas) {
        const QRectF hitRect = area.rect.adjusted(-GroupPadding, 0.0, GroupPadding, 0.0);
        const QRectF overlap = previewRect.intersected(hitRect);
        const bool pointHitsArea = area.rect.contains(scenePosition);
        const bool centerYHitsArea = scenePosition.y() >= area.rect.top()
                                     && scenePosition.y() <= area.rect.bottom();
        const bool draggedRectHitsArea = movingNodeId > 0
                                         && centerYHitsArea
                                         && overlap.width() >= GroupPadding
                                         && overlap.height() >= qMin<qreal>(PrimitiveHeight, previewSize.height()) * 0.35;

        if (area.depth <= bestDepth || (!pointHitsArea && !draggedRectHitsArea))
            continue;
        if (movingNodeId > 0 && area.groupId == movingNodeId)
            continue;
        if (movingNodeId > 0 && sourceArea && sourceRect.isValid()
            && area.depth > sourceArea->depth
            && sourceRect.contains(area.rect.center())) {
            continue;
        }

        bestArea = &area;
        bestDepth = area.depth;
    }

    return bestArea;
}
