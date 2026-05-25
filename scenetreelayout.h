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
        qreal moduleParameterSeparatorY = 0.0;
        int moduleParameterCount = 0;
        bool collapsed = false;
        QVector<ChildLayout> children;
    };

    struct GroupPreview
    {
        QRectF rect;
        SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
        QVector<ChildLayout> children;
    };

    struct DropTarget
    {
        bool hasTarget = false;
        int parentGroupId = 0;
        int insertIndex = -1;
        bool moduleParameterZone = false;
        QRectF zoneRect;
        QRectF sourceRect;
        QRectF sourceGroupRect;
        SceneDocument::TreeNode::Operation sourceGroupOperation = SceneDocument::TreeNode::Union;
        qreal sourceCutSeparatorY = 0.0;
        QVector<ChildLayout> sourceChildren;
        QRectF placeholderRect;
        QRectF slotMarkerRect;
        QRectF previewGroupRect;
        SceneDocument::TreeNode::Operation previewGroupOperation = SceneDocument::TreeNode::Union;
        qreal previewCutSeparatorY = 0.0;
        QVector<GroupPreview> expandedGroups;
        QVector<ChildLayout> previewChildren;
    };

    void clear();
    void addGroup(const GroupHitArea &area);
    void translateRootBlock(int rootGroupId, const QPointF &delta);
    const QVector<GroupHitArea> &groupHitAreas() const;

    DropTarget dropTargetAt(const QPointF &scenePosition,
                            const QSizeF &previewSize = QSizeF(),
                            int movingNodeId = 0,
                            bool variableDrop = false) const;

private:
    void buildSourcePreview(const GroupHitArea *sourceArea,
                            int movingNodeId,
                            int sourceChildIndex,
                            qreal sourceRemovalShift,
                            DropTarget *target) const;
    void buildSourceAncestorPreviews(const GroupHitArea *sourceArea,
                                     DropTarget *target) const;
    const GroupHitArea *findSourceArea(int movingNodeId,
                                       DropTarget *target,
                                       int *sourceChildIndex,
                                       qreal *sourceRemovalShift) const;
    const GroupHitArea *findBestDropArea(const QPointF &scenePosition,
                                         const QSizeF &previewSize,
                                         int movingNodeId,
                                         const GroupHitArea *sourceArea,
                                         const QRectF &sourceRect) const;

    QVector<GroupHitArea> m_groupHitAreas;
};

#endif
