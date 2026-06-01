#ifndef SCENETREEGRAPHICSWIDGET_H
#define SCENETREEGRAPHICSWIDGET_H

#include "scenedocument.h"
#include "scenetreelayout.h"
#include "scenetreenoderenderer.h"
#include "scenetreepalette.h"

#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QHash>
#include <QRectF>
#include <QSet>
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
class QVariantAnimation;
class GroupThumbnailCache;
class NodeThumbnailCache;
class SceneTreeInlineTextInput;
class SceneTreeColorEditMode;
class SceneTreeHoverManager;

class SceneTreeGraphicsWidget : public QGraphicsView
{
    Q_OBJECT

public:
    explicit SceneTreeGraphicsWidget(QWidget *parent = nullptr);

    void setSceneDocument(const SceneDocument *scene);
    const SceneDocument *sceneDocument() const { return m_scene; }
    SceneDocument *sceneDocument() { return const_cast<SceneDocument*>(m_scene); }
    void setSelectedTreeNodeId(int nodeId);
    void setPolyhedronElementSelection(const QVector<int> &nodeIds);
    void setTreeTheme(int theme);
    int  treeTheme() const { return m_treeTheme; }
    void setCanvasBackgroundTheme(int theme);
    int  canvasBackgroundThemeIndex() const { return m_canvasBackgroundTheme; }
    void setCustomAppearanceTheme(const TreeAppearanceTheme &theme);
    void clearCustomAppearanceTheme();
    void refresh();

    // Enable / disable the in-app color-edit paint mode.
    void setColorEditMode(bool enabled);
    bool colorEditMode() const;
    void compactRootBlocksAndFit();
    void focusSelectedNodeAnimated();

    // Debug/inspection — for use by debug tools only.
    QRectF debugGroupRect(int groupId) const;
    QRectF debugChildRect(int nodeId) const;

signals:
    void toolDropped(const QString &toolName, int parentGroupId, int insertIndex, bool isParameterZone);
    void moduleCallDropped(int moduleGroupId, int parentGroupId, int insertIndex);
    void treeNodeDropped(int nodeId, int parentGroupId, int insertIndex, bool isParameterZone);
    void treeNodeSelected(int nodeId);
    void treeNodeDeleteRequested(int nodeId);
    void transformValueAdjusted(int groupId, int axis, int numberStart, int numberLength, qreal delta);
    void transformExpressionEdited(int groupId, int axis, const QString &expression);
    void colorChannelAdjusted(int groupId, int channel, qreal delta);
    void transformControlHovered(int groupId, SceneDocument::TreeNode::Operation operation, int axis);
    void shapeParameterAdjusted(int nodeId, int paramIndex, int numberStart, int numberLength, qreal delta);
    void shapeParameterExpressionEdited(int nodeId, int paramIndex, const QString &expression);
    void shapeParameterHovered(int shapeId, int parameter);
    void variableNumberHovered(int nodeId, int start);
    void forLoopRangeHovered(int nodeId, int start);
    void moduleCallParamHovered(int moduleCallNodeId, int paramVarNodeId, int start);
    void hoverScrollZoneChanged(const QRectF &rect);
    void dropPreviewChanged(const QString &previewTool, int movingNodeId,
                            const SceneTreeLayout::DropTarget &target, const QPointF &scenePosition);
    void variableNumberAdjusted(int nodeId, int start, int length, qreal delta);
    void variableExpressionEdited(int nodeId, const QString &expression);
    void moduleCallArgumentAdjusted(int moduleCallId, int parameterVariableId,
                                    int start, int length, qreal delta);
    void moduleCallArgumentExpressionEdited(int moduleCallId, int parameterVariableId, const QString &expression);
    void forLoopRangeAdjusted(int nodeId, int start, int length, qreal delta);
    void ctrlReleased();
    void moduleRenameRequested(int groupId, const QString &newName);
    void variableRenameRequested(int variableId, const QString &newName);
    void canvasDrag(const QString &logLine);
    void treeThemeChanged(int theme);
    void canvasBackgroundThemeChanged(int theme);
    void builtInAppearanceSelected();
    void colorEditModeChanged(bool enabled);
    void inlineThemeEdited();
    void polyhedronAddPointRequested(int groupNodeId);
    void polyhedronAddFaceRequested(int groupNodeId);
    void polyhedronAutofaceRequested(int groupNodeId);
    void polyhedronFaceParticipationAdjusted(int faceNodeId, int pointNodeId, int newPosition);
    void polyhedronTemplateRequested(int groupNodeId, int templateIndex);
    void polyhedronClearRequested(int groupNodeId);
    void polyhedronElementSelectionChanged(const QVector<int> &nodeIds);
    void polyhedronElementHoverChanged(int nodeId);
    void polygon2DPointAddRequested(int nodeId);
    void polygon2DPointRemoveRequested(int nodeId, int pointIndex);
    void polygon2DPointAdjusted(int nodeId, int pointIndex, int coord, qreal delta);
    void polygon2DPointExpressionEdited(int nodeId, int pointIndex, int coord, const QString &expression);

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
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

    friend class SceneTreeColorEditMode;
    friend class SceneTreeHoverManager;
    void clearColorEditHighlight();
    void updateColorEditHighlight(const QPointF &scenePos);

    QRectF drawToolbar();
    void drawThemeSwitcher();
    void drawCanvasBackgroundSwitcher();
    void clearToolbar();
    void updateToolbarOverlay();
    void handleThemeSwitcherClick(int themeIndex);
    void handleCanvasBackgroundSwitcherClick(int backgroundIndex);
    void resetGraphicsScene();
    void drawTreeOrPlaceholder();
    void addNodeDragHandle(int nodeId, const QString &label, const QRectF &handleRect, const QRectF &sourceRect, const QSizeF &previewSize);
    QRectF drawNode(const SceneDocument::TreeNode &node, const QPointF &topLeft, int depth);
    QRectF drawPrimitive(const SceneDocument::TreeNode &node, const QPointF &topLeft);
    QRectF drawModuleCall(const SceneDocument::TreeNode &node, const QPointF &topLeft);
    QRectF drawGroup(const SceneDocument::TreeNode &node, const QPointF &topLeft, int depth);
    void drawModuleSectionLabels(const QRectF &rect, qreal sepY, int depth,
                                 const QColor &color, QVector<QGraphicsItem *> *outItems = nullptr);
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
    bool groupCollapseControlAt(const QPointF &scenePosition, int *groupId) const;
    void toggleGroupCollapsed(int groupId);
    void snapZoom();
    bool handleTransformWheel(const QPointF &scenePosition, int wheelSteps);
    bool handleColorChannelWheel(const QPointF &scenePosition, int wheelSteps);
    bool handleShapeParameterWheel(const QPointF &scenePosition, int wheelSteps);
    bool handlePolyhedronTableWheel(const QPointF &scenePosition, int wheelSteps);
    bool handlePolygon2DTableWheel(const QPointF &scenePosition, int wheelSteps);
    bool handleVariableNumberWheel(const QPointF &scenePosition, int wheelSteps);
    bool handleForLoopRangeWheel(const QPointF &scenePosition, int wheelSteps);
    bool handleModuleCallParamWheel(const QPointF &scenePosition, int wheelSteps);
    void startInlineRename(int nodeId, bool isModule, const QRectF &sceneRect, const QString &currentName);
    struct ExpressionEditTarget;
    bool expressionEditTargetAt(const QPointF &scenePosition, ExpressionEditTarget *target) const;
    void startInlineExpressionEdit(const ExpressionEditTarget &target);
    bool validateInlineExpression(const ExpressionEditTarget &target, const QString &expression, QString *errorMessage) const;
    void updateInlineInputGeometry();
    QRectF groupRectForNode(int groupId) const;
    QRectF rectForChildNode(int nodeId) const;
    bool transformControlAt(const QPointF &scenePosition, int *groupId, SceneDocument::TreeNode::Operation *operation, int *axis, int *numberStart = nullptr, int *numberLength = nullptr) const;
    bool colorChannelControlAt(const QPointF &scenePosition, int *groupId, int *channel) const;
    bool shapeParameterControlAt(const QPointF &scenePosition, int *shapeId, int *nodeId, int *parameter, int *numberStart, int *numberLength) const;
    bool polyhedronTableControlAt(const QPointF &scenePosition, PolyhedronTableItem::Cell *cell) const;
    int polyhedronGroupIdForCell(const QPointF &scenePosition) const;
    bool polygon2DTableControlAt(const QPointF &scenePosition, Polygon2DTableItem::Cell *cell) const;
    bool variableNumberControlAt(const QPointF &scenePosition, int *nodeId, int *start, int *length) const;
    bool forLoopRangeControlAt(const QPointF &scenePosition, int *nodeId, int *start, int *length) const;
    bool moduleCallParamControlAt(const QPointF &scenePosition, int *moduleCallNodeId, int *paramVarNodeId, int *start, int *length) const;
    void showDropPreview(const QPointF &scenePosition, const QSizeF &previewSize, const QString &previewTool, int movingNodeId = 0);
    void finishDropPreview();
    void clearDropPreview();
    void startDropPreviewAnimation(const DropTarget &target, const QString &previewTool, int movingNodeId, qreal durationMs);
    void advanceDropPreviewAnimation();
    void renderDropPreviewFrame(const DropTarget &target);
    void scheduleDropCommit(std::function<void()> action);
    void setTreeItemsVisible(bool visible);
    void updateSceneRect();
    QString labelForPrimitive(int shapeId) const;
    ShapeNode::Type typeForPrimitive(int shapeId) const;
    void syncThumbnailCache();
    void syncGroupThumbnailCache();
    void collectPrimitiveNodeShapes(const SceneDocument::TreeNode &node,
                                    QHash<int, ShapeNode> *out) const;

private:
    QGraphicsScene *m_graphicsScene = nullptr;
    const SceneDocument *m_scene = nullptr;
    NodeThumbnailCache *m_thumbnailCache = nullptr;
    GroupThumbnailCache *m_groupThumbnailCache = nullptr;
    SceneTreeLayout m_treeLayout;
    QSet<int> m_collapsedGroupIds;
    QVector<QGraphicsItem *> m_treeItems;
    QVector<QGraphicsItem *> m_toolbarItems;
    QVector<QGraphicsItem *> m_dropPreviewItems;
    QTimer *m_dropPreviewAnimationTimer = nullptr;
    QVariantAnimation *m_focusAnimation = nullptr;
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

    // Snap settings for placement and root-block dragging.
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

    static constexpr qreal kClusterVelocityThreshold = 16.0; // px/event -> detaches cluster

    // Pending canvas position for the next toolbar-drop insertion.
    bool    m_hasPendingInsertPos         = false;
    QPointF m_pendingInsertCanvasPosition;

    bool applyMagneticSnap(const QPointF &candidate, const QSizeF &size,
                           int excludeId, QPointF *snapped,
                           const QVector<int> &additionalExcludeIds = QVector<int>()) const;
    QPointF nonOverlappingCanvasPosition(const QPointF &candidate,
                                         const QSizeF &size,
                                         const QVector<QRectF> &placedBlocks) const;
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

    struct ExpressionEditTarget {
        enum Kind {
            None,
            Transform,
            ShapeParameter,
            Variable,
            ModuleCallArgument,
            PolyhedronParticipation,
            Polygon2DPoint
        };
        Kind kind = None;
        QRectF hoverRect;
        QRectF editRect;
        QString label;
        QString expression;
        int nodeId = 0;
        int secondaryId = 0;
    };

    SceneTreeInlineTextInput *m_inlineInput     = nullptr;
    bool                      m_inlineInputActive = false;
    QRectF                    m_inlineInputSceneRect;

    int m_treeTheme = 1;    // SceneTreePalette::Theme cast to int; 1 = second tree theme
    int m_canvasBackgroundTheme = 0;
    int m_selectedTreeNodeId = 0;
    QSet<int> m_selectedPolyhedronElementNodeIds;
    QSet<QString> m_selectedPolygonPointKeys;

    // ── Color-edit paint mode ─────────────────────────────────────────────────
    SceneTreeHoverManager *m_hoverManager = nullptr;
    SceneTreeColorEditMode *m_colorEdit = nullptr;
    QGraphicsItem  *m_colorEditToggleItem  = nullptr; // pointer to the toggle for priority check
    QPoint m_lastPanPoint;
    QPoint m_lastMousePosition;
    QRectF m_lastToolbarRect;
    DropTarget m_dropPreviewStartTarget;
    DropTarget m_dropPreviewTarget;
    DropTarget m_dropPreviewCurrentTarget;
    QString m_dropPreviewTool;
    int m_dropPreviewMovingNodeId = 0;
    qreal m_dropPreviewProgress = 0.0;
    qreal m_dropPreviewDurationMs = 180.0;
    bool m_panning = false;
    QPointF m_panVelocity;
    QTimer *m_panInertiaTimer = nullptr;
    // ── Acceleration-based zoom ────────────────────────────────────────────────
    //   wheel → m_zoomAccel → m_zoomVelocity → scale(1+v)
    //   longer scroll = more accel = faster zoom
    qreal   m_zoomAccel      = 0.0;
    qreal   m_zoomVelocity   = 0.0;
    qreal   m_zoomLevel      = 1.0;
    bool    m_zoomIdle        = true;
    QTimer *m_zoomAnimTimer  = nullptr;
    QTimer *m_zoomIdleTimer  = nullptr;
    QPointF m_zoomAnchorScene;
    bool m_dragActive = false;
    bool m_dropPreviewActive = false;
    bool m_dropPreviewFinishing = false;
    bool m_treeItemsVisible = true;
};

#endif
