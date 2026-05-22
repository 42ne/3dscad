#ifndef SCENETREEGRAPHICSWIDGET_H
#define SCENETREEGRAPHICSWIDGET_H

#include "scenedocument.h"
#include "scenetreelayout.h"
#include "scenetreepalette.h"

#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QHash>
#include <QRectF>
#include <QVector>
#include <functional>

class QEvent;
class QGraphicsScene;
class QGraphicsItem;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QResizeEvent;
class QShowEvent;
class QTimer;
class NodeThumbnailCache;
class SceneTreeInlineTextInput;

class SceneTreeGraphicsWidget : public QGraphicsView
{
public:
    explicit SceneTreeGraphicsWidget(QWidget *parent = nullptr);

    void setSceneDocument(const SceneDocument *scene);
    void setToolDroppedCallback(std::function<void(const QString &, int, int)> callback);
    void setModuleCallDroppedCallback(std::function<void(int, int, int)> callback);
    void setTreeNodeDroppedCallback(std::function<void(int, int, int)> callback);
    void setTreeNodeSelectedCallback(std::function<void(int)> callback);
    void setTreeNodeDeleteRequestedCallback(std::function<void(int)> callback);
    void setTransformValueAdjustedCallback(std::function<void(int, int, int, int, qreal)> callback);
    void setTransformControlHoveredCallback(std::function<void(int, SceneDocument::TreeNode::Operation, int)> callback);
    void setShapeParameterAdjustedCallback(std::function<void(int, int, int, int, qreal)> callback);
    void setShapeParameterHoveredCallback(std::function<void(int, int)> callback);
    void setVariableNumberHoveredCallback(std::function<void(int, int)> callback);
    void setForLoopRangeHoveredCallback(std::function<void(int, int)> callback);
    void setModuleCallParamHoveredCallback(std::function<void(int, int, int)> callback);
    void setHoverScrollZoneChangedCallback(std::function<void(const QRectF &)> callback);
    void setDropPreviewChangedCallback(std::function<void(const QString &, int, const SceneTreeLayout::DropTarget &, const QPointF &)> callback);
    void setVariableNumberAdjustedCallback(std::function<void(int, int, int, qreal)> callback);
    void setModuleCallArgumentAdjustedCallback(std::function<void(int, int, int, int, qreal)> callback);
    void setForLoopRangeAdjustedCallback(std::function<void(int, int, int, qreal)> callback);
    void setCtrlReleasedCallback(std::function<void()> callback);
    void setModuleRenameRequestedCallback(std::function<void(int, const QString &)> callback);
    void setVariableRenameRequestedCallback(std::function<void(int, const QString &)> callback);
    // Canvas-drag debug hook.  Receives a pre-formatted log line on key events:
    //   "press"  — grip strip hit (drag pending)
    //   "start"  — drag activated, cluster resolved
    //   "move"   — position/snap update (throttled: snap-change or ≥25 px)
    //   "detach" — fast drag broke the cluster
    //   "commit" — drag released, positions committed
    void setCanvasDragCallback(std::function<void(const QString &)> callback);
    void setSelectedTreeNodeId(int nodeId);
    void setTreeTheme(int theme);
    int  treeTheme() const { return m_treeTheme; }
    void refresh();

    // Debug/inspection — for use by debug tools only.
    QRectF debugGroupRect(int groupId) const;
    QRectF debugChildRect(int nodeId) const;

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    using ChildLayout = SceneTreeLayout::ChildLayout;
    using DropTarget = SceneTreeLayout::DropTarget;
    using GroupHitArea = SceneTreeLayout::GroupHitArea;

    QRectF drawToolbar();
    void drawThemeSwitcher();
    void clearToolbar();
    void updateToolbarOverlay();
    void handleThemeSwitcherClick(int themeIndex);
    void rebuildScene(bool resetDropPreview);
    void resetGraphicsScene(bool resetDropPreview);
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
    void handleModuleCallTemplateDrop(int moduleGroupId, const QPointF &scenePosition);
    void handleTreeNodeDrop(int nodeId, const QPointF &scenePosition);
    void handleTreeNodeSelected(int nodeId);
    bool handleTransformWheel(const QPointF &scenePosition, int wheelSteps);
    bool handleShapeParameterWheel(const QPointF &scenePosition, int wheelSteps);
    bool handleVariableNumberWheel(const QPointF &scenePosition, int wheelSteps);
    bool handleForLoopRangeWheel(const QPointF &scenePosition, int wheelSteps);
    bool handleModuleCallParamWheel(const QPointF &scenePosition, int wheelSteps);
    void updateHoverHighlights(const QPointF &scenePosition);
    QRectF hoverScrollZoneRect(const QPointF &scenePosition) const;
    bool hoverRenameZoneAt(const QPointF &scenePosition, int *nodeId, QRectF *zoneRect) const;
    void startInlineRename(int nodeId, bool isModule, const QRectF &sceneRect, const QString &currentName);
    void updateInlineInputGeometry();
    QRectF groupRectForNode(int groupId) const;
    QRectF rectForChildNode(int nodeId) const;
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
    void finishDropPreview();
    void clearDropPreview();
    void setDropGapTarget(qreal target);
    void advanceDropGapAnimation();
    void refreshDropPreviewAfterLayoutChange();
    void startDropPreviewAnimation(const DropTarget &target, const QString &previewTool, int movingNodeId, qreal durationMs);
    void advanceDropPreviewAnimation();
    void renderDropPreviewFrame(const DropTarget &target);
    void scheduleDropCommit(std::function<void()> action);
    void setTreeItemsVisible(bool visible);
    void updateSceneRect();
    QString labelForPrimitive(int shapeId) const;
    ShapeNode::Type typeForPrimitive(int shapeId) const;
    void syncThumbnailCache();
    void collectPrimitiveNodeShapes(const SceneDocument::TreeNode &node,
                                    QHash<int, ShapeNode> *out) const;

private:
    QGraphicsScene *m_graphicsScene = nullptr;
    const SceneDocument *m_scene = nullptr;
    NodeThumbnailCache *m_thumbnailCache = nullptr;
    SceneTreeLayout m_treeLayout;
    QVector<QGraphicsItem *> m_treeItems;
    QVector<QGraphicsItem *> m_toolbarItems;
    QVector<QGraphicsItem *> m_dropPreviewItems;
    QTimer *m_dropPreviewAnimationTimer = nullptr;
    QTimer *m_dropGapAnimationTimer = nullptr;
    std::function<void(const QString &, int, int)> m_toolDroppedCallback;
    std::function<void(int, int, int)> m_moduleCallDroppedCallback;
    std::function<void(int, int, int)> m_treeNodeDroppedCallback;
    std::function<void(int)> m_treeNodeSelectedCallback;
    std::function<void(int)> m_treeNodeDeleteRequestedCallback;
    std::function<void(int, int, int, int, qreal)> m_transformValueAdjustedCallback;
    std::function<void(int, SceneDocument::TreeNode::Operation, int)> m_transformControlHoveredCallback;
    std::function<void(int, int, int, int, qreal)> m_shapeParameterAdjustedCallback;
    std::function<void(int, int)> m_shapeParameterHoveredCallback;
    std::function<void(int, int)> m_variableNumberHoveredCallback;
    std::function<void(int, int)> m_forLoopRangeHoveredCallback;
    std::function<void(int, int, int)> m_moduleCallParamHoveredCallback;
    std::function<void(const QRectF &)> m_hoverScrollZoneChangedCallback;
    std::function<void(const QString &, int, const SceneTreeLayout::DropTarget &, const QPointF &)> m_dropPreviewChangedCallback;
    std::function<void(int, int, int, qreal)> m_variableNumberAdjustedCallback;
    std::function<void(int, int, int, int, qreal)> m_moduleCallArgumentAdjustedCallback;
    std::function<void(int, int, int, qreal)> m_forLoopRangeAdjustedCallback;
    std::function<void()> m_ctrlReleasedCallback;
    std::function<void(int, const QString &)> m_moduleRenameRequestedCallback;
    std::function<void(int, const QString &)> m_variableRenameRequestedCallback;
    std::function<void(const QString &)>      m_canvasDragCallback;
    // Throttle state for canvas-drag debug logging (not used in production paths).
    bool    m_dbgPrevSnapped    = false;
    QPointF m_dbgLastLoggedPos;

    // ── Canvas-move drag ───────────────────────────────────────────────────────
    // Grip strip above each root-level block used to reposition it on the canvas.
    struct CanvasMoveHandle {
        QRectF gripRect;  // 8 px strip above the block (scene coords)
        QRectF blockRect; // full block rect including grip strip
        int    nodeId = 0;
    };
    QVector<CanvasMoveHandle>  m_canvasMoveHandles;
    QHash<int, QPointF>        m_nodeCanvasPositions; // custom top-lefts; absent → auto

    // Snap settings
    static constexpr qreal kGripStripH   = 20.0;
    static constexpr qreal kMagnetRadius = 80.0;

    // Canvas drag state
    bool              m_canvasDragPending  = false;
    bool              m_canvasDragActive   = false;
    int               m_canvasDragNodeId   = 0;
    QPointF           m_canvasDragPressScene;
    QPointF           m_canvasDragOrigPos;   // top-left of full block rect at drag start
    QSizeF            m_canvasDragBlockSize;
    QPointF           m_canvasDragCurrentPos;
    bool              m_canvasDragSnapped   = false;
    QGraphicsPathItem *m_canvasDragGhost   = nullptr;

    // Cluster-movement state (blocks edge-touching the dragged block move together).
    QVector<int>                          m_canvasDragCluster;       // nodeIds in the cluster
    QHash<int, QPointF>                   m_canvasDragClusterOrigPos;// top-left at drag start
    bool                                  m_canvasDragDetached  = false; // true = fast drag broke cluster
    QPointF                               m_canvasDragPrevEventScene; // for velocity calculation
    // Items that are physically moved during the drag (primary block + cluster members).
    QVector<QGraphicsItem *>              m_canvasDragItems;
    QHash<int, QVector<QGraphicsItem *>>  m_clusterDragItems;

    static constexpr qreal kClusterVelocityThreshold = 12.0; // px/event → detaches cluster

    // Pending canvas position for the next toolbar-drop insertion.
    bool    m_hasPendingInsertPos         = false;
    QPointF m_pendingInsertCanvasPosition;

    bool applyMagneticSnap(const QPointF &candidate, const QSizeF &size,
                           int excludeId, QPointF *snapped) const;
    void showDragPlaceholder(const QPointF &pos, const QSizeF &size);
    void clearCanvasDragGhost();
    QVector<int>              findConnectedCluster(int startNodeId) const;
    QVector<QGraphicsItem *>  itemsInBlockRect(const QRectF &blockRect) const;

    // ── Rename zones ──────────────────────────────────────────────────────────
    struct RenameZone {
        QRectF rect;
        int    nodeId   = 0;
        bool   isModule = false; // true = module group, false = variable
        QString currentName;
    };
    QVector<RenameZone> m_renameZones;

    SceneTreeInlineTextInput *m_inlineInput     = nullptr;
    bool                      m_inlineInputActive = false;
    QRectF                    m_inlineInputSceneRect;

    int m_treeTheme = 0;    // SceneTreePalette::Theme cast to int; 0 = Frost
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
    QRectF m_hoveredScrollRect;
    QRectF m_hoveredRenameRect;
    DropTarget m_dropPreviewStartTarget;
    DropTarget m_dropPreviewTarget;
    DropTarget m_dropPreviewCurrentTarget;
    QString m_dropPreviewTool;
    int m_dropPreviewMovingNodeId = 0;
    QPointF m_lastDropPreviewScenePosition;
    QSizeF m_lastDropPreviewSize;
    qreal m_dropPreviewProgress = 0.0;
    qreal m_dropPreviewDurationMs = 180.0;
    qreal m_dropGapFactor = 1.0;
    qreal m_dropGapTarget = 1.0;
    bool m_panning = false;
    bool m_dragActive = false;
    bool m_dropPreviewActive = false;
    bool m_dropPreviewFinishing = false;
    bool m_treeItemsVisible = true;
};

#endif
