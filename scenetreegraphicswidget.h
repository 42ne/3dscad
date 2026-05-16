#ifndef SCENETREEGRAPHICSWIDGET_H
#define SCENETREEGRAPHICSWIDGET_H

#include "scenedocument.h"
#include "scenetreelayout.h"

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
    using ChildLayout = SceneTreeLayout::ChildLayout;
    using DropTarget = SceneTreeLayout::DropTarget;
    using GroupHitArea = SceneTreeLayout::GroupHitArea;

    QRectF drawToolbar();
    void resetGraphicsScene();
    void drawTreeOrPlaceholder();
    void addNodeDragHandle(int nodeId, const QString &label, const QRectF &handleRect, const QRectF &sourceRect, const QSizeF &previewSize);
    QRectF drawNode(const SceneDocument::TreeNode &node, const QPointF &topLeft, int depth);
    QRectF drawPrimitive(const SceneDocument::TreeNode &node, const QPointF &topLeft);
    QRectF drawGroup(const SceneDocument::TreeNode &node, const QPointF &topLeft, int depth);
    QString previewToolForNode(const SceneDocument::TreeNode &node) const;
    DropTarget dropTargetForToolAt(const QPointF &scenePosition,
                                   const QSizeF &previewSize,
                                   const QString &previewTool,
                                   int movingNodeId,
                                   bool allowFreeFloatingInsertion) const;
    void handleToolDrop(const QString &toolName, const QPointF &scenePosition);
    void handleTreeNodeDrop(int nodeId, const QPointF &scenePosition);
    void handleTreeNodeSelected(int nodeId);
    void showDropPreview(const QPointF &scenePosition, const QSizeF &previewSize, const QString &previewTool, int movingNodeId = 0);
    void clearDropPreview();
    void setTreeItemsVisible(bool visible);
    void updateSceneRect(const QRectF &toolbarRect);
    QString labelForPrimitive(int shapeId) const;
    ShapeNode::Type typeForPrimitive(int shapeId) const;

private:
    QGraphicsScene *m_graphicsScene = nullptr;
    const SceneDocument *m_scene = nullptr;
    SceneTreeLayout m_treeLayout;
    QVector<QGraphicsItem *> m_treeItems;
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
