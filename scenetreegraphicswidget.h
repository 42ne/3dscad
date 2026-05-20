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
class QResizeEvent;
class QShowEvent;

class SceneTreeGraphicsWidget : public QGraphicsView
{
public:
    explicit SceneTreeGraphicsWidget(QWidget *parent = nullptr);

    void setSceneDocument(const SceneDocument *scene);
    void setToolDroppedCallback(std::function<void(const QString &, int, int)> callback);
    void setTreeNodeDroppedCallback(std::function<void(int, int, int)> callback);
    void setTreeNodeSelectedCallback(std::function<void(int)> callback);
    void setTreeNodeDeleteRequestedCallback(std::function<void(int)> callback);
    void setTransformValueAdjustedCallback(std::function<void(int, int, int, int, qreal)> callback);
    void setTransformControlHoveredCallback(std::function<void(int, SceneDocument::TreeNode::Operation, int)> callback);
    void setShapeParameterAdjustedCallback(std::function<void(int, int, int, int, qreal)> callback);
    void setShapeParameterHoveredCallback(std::function<void(int, int)> callback);
    void setVariableNumberAdjustedCallback(std::function<void(int, int, int, qreal)> callback);
    void setModuleCallArgumentAdjustedCallback(std::function<void(int, int, int, int, qreal)> callback);
    void setForLoopRangeAdjustedCallback(std::function<void(int, int, int, qreal)> callback);
    void setCtrlReleasedCallback(std::function<void()> callback);
    void setSelectedTreeNodeId(int nodeId);
    void refresh();

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
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
    QRectF drawModuleCall(const SceneDocument::TreeNode &node, const QPointF &topLeft);
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
    bool handleTransformWheel(const QPointF &scenePosition, int wheelSteps);
    bool handleShapeParameterWheel(const QPointF &scenePosition, int wheelSteps);
    bool handleVariableNumberWheel(const QPointF &scenePosition, int wheelSteps);
    bool handleForLoopRangeWheel(const QPointF &scenePosition, int wheelSteps);
    bool handleModuleCallParamWheel(const QPointF &scenePosition, int wheelSteps);
    bool transformControlAt(const QPointF &scenePosition, int *groupId, SceneDocument::TreeNode::Operation *operation, int *axis, int *numberStart = nullptr, int *numberLength = nullptr) const;
    bool shapeParameterControlAt(const QPointF &scenePosition, int *shapeId, int *nodeId, int *parameter, int *numberStart, int *numberLength) const;
    bool variableNumberControlAt(const QPointF &scenePosition, int *nodeId, int *start, int *length) const;
    bool forLoopRangeControlAt(const QPointF &scenePosition, int *nodeId, int *start, int *length) const;
    bool moduleCallParamControlAt(const QPointF &scenePosition, int *moduleCallNodeId, int *paramVarNodeId, int *start, int *length) const;
    void updateControlTooltip(const QPoint &globalPosition, const QPointF &scenePosition, bool controlDown);
    void updateActiveTransformControl(const QPointF &scenePosition, bool enabled);
    void updateActiveShapeParameterControl(const QPointF &scenePosition, bool enabled);
    void updateActiveVariableNumberControl(const QPointF &scenePosition, bool enabled);
    void updateActiveForLoopRangeControl(const QPointF &scenePosition, bool enabled);
    void updateActiveModuleCallParamControl(const QPointF &scenePosition, bool enabled);
    void showDropPreview(const QPointF &scenePosition, const QSizeF &previewSize, const QString &previewTool, int movingNodeId = 0);
    void clearDropPreview();
    void setTreeItemsVisible(bool visible);
    void updateSceneRect(const QRectF &toolbarRect);
    void centerToolbarHorizontallyOnNextEvent();
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
    std::function<void(int, int, int, int, qreal)> m_transformValueAdjustedCallback;
    std::function<void(int, SceneDocument::TreeNode::Operation, int)> m_transformControlHoveredCallback;
    std::function<void(int, int, int, int, qreal)> m_shapeParameterAdjustedCallback;
    std::function<void(int, int)> m_shapeParameterHoveredCallback;
    std::function<void(int, int, int, qreal)> m_variableNumberAdjustedCallback;
    std::function<void(int, int, int, int, qreal)> m_moduleCallArgumentAdjustedCallback;
    std::function<void(int, int, int, qreal)> m_forLoopRangeAdjustedCallback;
    std::function<void()> m_ctrlReleasedCallback;
    int m_selectedTreeNodeId = 0;
    int m_activeTransformControlNodeId = 0;
    int m_activeTransformControlAxis = -1;
    int m_activeTransformControlNumberStart = -1;
    SceneDocument::TreeNode::Operation m_activeTransformControlOperation = SceneDocument::TreeNode::Union;
    int m_activeShapeParameterNodeId = 0;
    int m_activeShapeParameter = -1;
    int m_activeShapeParameterNumberStart = -1;
    int m_activeVariableNodeId = 0;
    int m_activeVariableNumberStart = -1;
    int m_activeForLoopNodeId = 0;
    int m_activeForLoopNumberStart = -1;
    int m_activeModuleCallNodeId = 0;
    int m_activeModuleCallVarNodeId = 0;
    int m_activeModuleCallNumberStart = -1;
    QString m_lastControlTooltipKey;
    QPoint m_lastPanPoint;
    QPoint m_lastMousePosition;
    QRectF m_lastToolbarRect;
    bool m_panning = false;
    bool m_dragActive = false;
    bool m_initialToolbarCentered = false;
};

#endif
