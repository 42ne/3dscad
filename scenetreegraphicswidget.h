#ifndef SCENETREEGRAPHICSWIDGET_H
#define SCENETREEGRAPHICSWIDGET_H

#include "scenedocument.h"

#include <QGraphicsView>
#include <QVector>
#include <functional>

class QGraphicsScene;
class QGraphicsItem;
class QKeyEvent;
class QMouseEvent;
class QPainter;

class SceneTreeGraphicsWidget : public QGraphicsView
{
public:
    explicit SceneTreeGraphicsWidget(QWidget *parent = nullptr);

    void setSceneDocument(const SceneDocument *scene);
    void setToolDroppedCallback(std::function<void(const QString &, int, int)> callback);
    void setTreeNodeDroppedCallback(std::function<void(int, int, int)> callback);
    void setTreeNodeSelectedCallback(std::function<void(int)> callback);
    void setTreeNodeDeleteRequestedCallback(std::function<void(int)> callback);
    void setSelectedTreeNodeId(int nodeId);
    void refresh();

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QRectF drawToolbar();
    QRectF drawNode(const SceneDocument::TreeNode &node, const QPointF &topLeft, int depth);
    QRectF drawPrimitive(const SceneDocument::TreeNode &node, const QPointF &topLeft);
    QRectF drawGroup(const SceneDocument::TreeNode &node, const QPointF &topLeft, int depth);
    QString previewToolForNode(const SceneDocument::TreeNode &node) const;
    void handleToolDrop(const QString &toolName, const QPointF &scenePosition);
    void handleTreeNodeDrop(int nodeId, const QPointF &scenePosition);
    void handleTreeNodeSelected(int nodeId);
    void showDropPreview(const QPointF &scenePosition, const QSizeF &previewSize, const QString &previewTool, int movingNodeId = 0);
    void clearDropPreview();
    struct DropTarget;
    DropTarget dropTargetAt(const QPointF &scenePosition, const QSizeF &previewSize = QSizeF(), int movingNodeId = 0) const;
    QString labelForPrimitive(int shapeId) const;
    ShapeNode::Type typeForPrimitive(int shapeId) const;
    QString labelForGroup(SceneDocument::TreeNode::Operation operation) const;
    QColor colorForGroup(SceneDocument::TreeNode::Operation operation) const;

private:
    struct GroupHitArea
    {
        QRectF rect;
        int groupId = 0;
        int depth = 0;
        SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
        qreal cutSeparatorY = 0.0;
        QVector<QRectF> childRects;
        QVector<QString> childPreviewTools;
        QVector<int> childNodeIds;
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
        QVector<QRectF> sourceChildRects;
        QVector<QString> sourceChildTools;
        QRectF placeholderRect;
        QRectF previewGroupRect;
        SceneDocument::TreeNode::Operation previewGroupOperation = SceneDocument::TreeNode::Union;
        qreal previewCutSeparatorY = 0.0;
        QVector<QRectF> expandedGroupRects;
        QVector<QRectF> previewChildRects;
        QVector<QString> previewChildTools;
        QVector<SceneDocument::TreeNode::Operation> expandedGroupOperations;
    };

    QGraphicsScene *m_graphicsScene = nullptr;
    const SceneDocument *m_scene = nullptr;
    QVector<GroupHitArea> m_groupHitAreas;
    QVector<QGraphicsItem *> m_dropPreviewItems;
    std::function<void(const QString &, int, int)> m_toolDroppedCallback;
    std::function<void(int, int, int)> m_treeNodeDroppedCallback;
    std::function<void(int)> m_treeNodeSelectedCallback;
    std::function<void(int)> m_treeNodeDeleteRequestedCallback;
    int m_selectedTreeNodeId = 0;
    QPoint m_lastPanPoint;
    bool m_panning = false;
};

#endif
