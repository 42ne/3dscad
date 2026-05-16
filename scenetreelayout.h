#ifndef SCENETREELAYOUT_H
#define SCENETREELAYOUT_H

#include "scenedocument.h"

#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

class QPointF;

class SceneTreeLayout
{
public:
    struct ChildLayout
    {
        QRectF rect;
        QString tool;
        int nodeId = 0;
    };

    struct GroupHitArea
    {
        QRectF rect;
        int groupId = 0;
        int depth = 0;
        SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
        qreal cutSeparatorY = 0.0;
        QVector<ChildLayout> children;
    };

    struct DropTarget
    {
        bool hasTarget = false;
        int parentGroupId = 0;
        int insertIndex = -1;
        QRectF zoneRect;
        QRectF sourceRect;
        QRectF sourceGroupRect;
        SceneDocument::TreeNode::Operation sourceGroupOperation = SceneDocument::TreeNode::Union;
        qreal sourceCutSeparatorY = 0.0;
        QVector<ChildLayout> sourceChildren;
        QRectF placeholderRect;
        QRectF previewGroupRect;
        SceneDocument::TreeNode::Operation previewGroupOperation = SceneDocument::TreeNode::Union;
        qreal previewCutSeparatorY = 0.0;
        QVector<QRectF> expandedGroupRects;
        QVector<QVector<ChildLayout>> expandedGroupChildren;
        QVector<ChildLayout> previewChildren;
        QVector<SceneDocument::TreeNode::Operation> expandedGroupOperations;
    };

    void clear();
    void addGroup(const GroupHitArea &area);
    const QVector<GroupHitArea> &groupHitAreas() const;

    DropTarget dropTargetAt(const QPointF &scenePosition,
                            const QSizeF &previewSize = QSizeF(),
                            int movingNodeId = 0) const;

private:
    const GroupHitArea *findSourceArea(int movingNodeId,
                                       DropTarget *target,
                                       int *sourceChildIndex,
                                       qreal *sourceRemovalShift) const;
    const GroupHitArea *findBestDropArea(const QPointF &scenePosition,
                                         int movingNodeId,
                                         const GroupHitArea *sourceArea,
                                         const QRectF &sourceRect) const;

    QVector<GroupHitArea> m_groupHitAreas;
};

#endif
