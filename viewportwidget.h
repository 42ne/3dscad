#ifndef VIEWPORTWIDGET_H
#define VIEWPORTWIDGET_H

#include "csgevaluator.h"
#include "appearancethemes.h"
#include "shapenode.h"
#include "viewportglrenderer.h"
#include "viewportcamera.h"

#include <QFutureWatcher>
#include <QImage>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPoint>
#include <QRectF>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QVector2D>

class QMouseEvent;
class QPainter;
class QCheckBox;
class QComboBox;
class QResizeEvent;
class QWheelEvent;

struct ViewportSelectionEdgeCandidate
{
    QVector3D from;
    QVector3D to;
    QVector<QVector3D> normals;
    bool structural = false;
};

class ViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    enum RenderBackend {
        SoftwareRenderBackend,
        OpenGLRenderBackend
    };

    explicit ViewportWidget(QWidget *parent = nullptr);
    void setScene(const SceneDocument *scene);
    void setShapes(const QVector<ShapeNode> *shapes);
    void setSelectedIndex(int index);
    void setSelectedGroupId(int groupId);
    void setPolyhedronElementSelection(const QVector<int> &nodeIds);
    void setPolyhedronElementHover(int nodeId);
    void setTreeTransformControlPreview(int groupId, SceneDocument::TreeNode::Operation operation, int axis);
    void setTreeShapeParameterPreview(int shapeId, int parameter);
    void setRenderBackend(RenderBackend backend);
    RenderBackend renderBackend() const;
    void setDarkViewportTheme(bool darkTheme);
    bool darkViewportTheme() const { return m_darkViewportTheme; }
    void setViewportColorVariant(int variant);
    int viewportColorVariant() const { return m_viewportColorVariant; }
    void setCustomAppearanceTheme(const ViewportAppearanceTheme &theme);
    void clearCustomAppearanceTheme();
    QString renderBackendName() const;
    bool isOpenGLRenderBackendAvailable() const;
    void invalidateCsgPreview();
    QString csgStatusText();

    // Render a standalone thumbnail image of the given scene (no grid, no UI).
    // Intended for off-thread use: all data comes from the caller's SceneDocument.
    // bgColor controls the empty-pixel fill (default dark, use card colour for tree icons).
    static QImage renderThumbnail(const SceneDocument &scene, QSize size,
                                  const QColor &bgColor = QColor(30, 32, 36));

signals:
    void shapeClicked(int index);
    void treeNodeClicked(int nodeId);
    void emptyClicked();
    void csgPreviewReady();
    void shapeDragStarted(int index);
    void shapeDragged(int index, const QVector3D &delta);
    void shapeDragFinished(int index);
    void shapeRotationDragStarted(int index);
    void shapeRotated(int index, const QVector3D &deltaDegrees);
    void shapeRotationDragFinished(int index);
    void groupDragStarted(int groupId);
    void groupDragged(int groupId, const QVector3D &delta);
    void groupDragFinished(int groupId);
    void groupRotationDragStarted(int groupId);
    void groupRotated(int groupId, const QVector3D &deltaDegrees);
    void groupRotationDragFinished(int groupId);
    void polyhedronElementsDragStarted(const QVector<int> &elementNodeIds);
    void polyhedronElementsDragged(const QVector3D &delta);
    void polyhedronElementsDragFinished();
    void darkViewportThemeChanged(bool darkTheme);
    void viewportColorVariantChanged(int variant);
    void builtInAppearanceSelected();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

public:
    enum DragMode {
        NoDrag,
        PlaneDrag,
        AxisXDrag,
        AxisYDrag,
        AxisZDrag,
        RotateXDrag,
        RotateYDrag,
        RotateZDrag
    };

private:
    void paintSoftware(QPainter &painter, bool drawSceneMeshes = true);

    void drawViewportHintOverlay(QPainter &painter, const QString &csgStatus) const;
    void drawSelectionBreadcrumb(QPainter &painter);
    bool canUseOpenGLRenderBackend() const;
    void updateSelectionShimmerTimer();
    void updateViewportControls();
    void startAsyncCsgCompute();
    void onCsgPreviewReady();
    void startAsyncSelectionMeshCompute(int groupId);
    void onSelectionMeshReady();
    QVector<SceneDocument::TreeNode> parentGroupStackForGroup(int groupId) const;
    QVector3D transformOriginForGroup(int groupId) const;
    QVector3D worldAxisVectorForGroup(int groupId, const QVector3D &localAxis) const;
    QVector<SceneDocument::TreeNode> selectedParentGroupStack() const;
    friend class ViewportGLRenderer;
    friend class ViewportAxisGizmo;
    friend class ViewportOverlayPreview;
    bool pickBreadcrumbNode(const QPoint &position, int *nodeId) const;
    QVector3D dragDeltaForMousePosition(const QPoint &position) const;
    QVector3D rotationDeltaForMousePosition(const QPoint &position) const;
    bool isRotationDragMode(DragMode dragMode) const;

    QVector3D selectedTransformOrigin() const;
    QVector3D selectedWorldAxisVector(const QVector3D &localAxis) const;
    QVector3D selectedLocalDeltaFromWorldDelta(const QVector3D &worldDelta) const;
    const SceneDocument *m_scene = nullptr;
    const QVector<ShapeNode> *m_shapes = nullptr;
    RenderBackend m_renderBackend = SoftwareRenderBackend;
    bool m_darkViewportTheme = true;
    bool m_navigationOverlayEnabled = true;
    int m_viewportColorVariant = 0;
    int m_lightingPreset = 0;
    bool m_hasCustomAppearanceTheme = false;
    ViewportAppearanceTheme m_customAppearanceTheme;
    int m_selectedIndex = -1;
    int m_selectedGroupId = 0;
    QVector<int> m_selectedPolyhedronElementNodeIds;
    int m_hoveredPolyhedronElementNodeId = 0;
    int m_treeTransformPreviewGroupId = 0;
    int m_treeTransformPreviewAxis = -1;
    SceneDocument::TreeNode::Operation m_treeTransformPreviewOperation = SceneDocument::TreeNode::Union;
    int m_treeShapePreviewShapeId = -1;
    int m_treeShapePreviewParameter = -1;
    struct BreadcrumbHit {
        int nodeId = 0;
        QRectF rect;
    };
    QVector<BreadcrumbHit> m_breadcrumbHits;
    ViewportCamera m_camera;
    QPoint m_lastMousePosition;
    QPoint m_dragStartMousePosition;
    QPoint m_emptyClickStartPosition;
    QVector3D m_lastDragDelta;
    QVector3D m_lastRotationDelta;
    QVector2D m_rotationDragScreenTangent;
    bool m_draggingShape = false;
    bool m_draggingGroup = false;
    bool m_draggingPolyhedronElements = false;
    bool m_polyhedronSelectionToolHovered = false;
    bool m_panningViewport = false;
    bool m_emptyClickCandidate = false;
    DragMode m_dragMode = NoDrag;
    int m_dragShapeIndex = -1;
    int m_dragGroupId = 0;
    QVector<int> m_dragPolyhedronElementNodeIds;
    CsgPreview m_cachedCsgPreview;
    uint m_cachedCsgFingerprint = 0;
    uint m_pendingCsgFingerprint = 0;
    bool m_csgPreviewDirty = true;
    bool m_csgComputing = false;
    QFutureWatcher<CsgPreview> m_csgWatcher;
    // Separate lightweight async compute for the selection group mesh.
    // Keyed by (groupId, sceneFingerprint) so stale results are discarded.
    SceneMesh m_cachedSelectionMesh;
    int  m_cachedSelectionMeshGroupId = 0;
    uint m_cachedSelectionMeshFingerprint = 0;
    int  m_pendingSelectionGroupId = 0;
    QFutureWatcher<QPair<int, SceneMesh>> m_selectionMeshWatcher;
    QTimer m_selectionShimmerTimer;
    float m_selectionShimmerPhase = 0.0f;
    QVector<int> m_pickBuffer;
    QVector<float> m_depthBuffer;
    QImage m_renderImage;
    QSize m_pickBufferSize;
    QCheckBox *m_openGLViewportCheckBox = nullptr;
    QCheckBox *m_darkViewportCheckBox = nullptr;
    QCheckBox *m_navigationOverlayCheckBox = nullptr;
    QComboBox *m_colorVariantComboBox = nullptr;
    QComboBox *m_lightingPresetComboBox = nullptr;
    QCheckBox *m_orthographicCheckBox = nullptr;
    ViewportGLRenderer m_glRenderer;
};

#endif
