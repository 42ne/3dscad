#ifndef VIEWPORTWIDGET_H
#define VIEWPORTWIDGET_H

#include "csgevaluator.h"
#include "shapenode.h"

#include <QFutureWatcher>
#include <QImage>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPoint>
#include <QRectF>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QVector2D>

class QMouseEvent;
class QOpenGLShaderProgram;
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
    void setTreeTransformControlPreview(int groupId, SceneDocument::TreeNode::Operation operation, int axis);
    void setTreeShapeParameterPreview(int shapeId, int parameter);
    void setRenderBackend(RenderBackend backend);
    RenderBackend renderBackend() const;
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
    void paintOpenGLGrid();
    void paintOpenGLContactShadows();
    void paintOpenGLPreview();
    void drawAxisGizmo(QPainter &painter) const;
    void drawViewportHintOverlay(QPainter &painter, const QString &csgStatus) const;
    void drawSelectionBreadcrumb(QPainter &painter);
    void drawTreeTransformControlPreview(QPainter &painter) const;
    void drawTreeShapeParameterPreview(QPainter &painter) const;
    void updateViewportControls();
    void updateSelectionShimmerTimer();
    bool canUseOpenGLRenderBackend() const;
    void startAsyncCsgCompute();
    void onCsgPreviewReady();
    QVector<SceneDocument::TreeNode> parentGroupStackForGroup(int groupId) const;
    QVector3D transformOriginForGroup(int groupId) const;
    QVector3D worldAxisVectorForGroup(int groupId, const QVector3D &localAxis) const;
    QVector<SceneDocument::TreeNode> selectedParentGroupStack() const;
    QVector3D selectedTransformOrigin() const;
    QVector3D selectedWorldAxisVector(const QVector3D &localAxis) const;
    QVector3D selectedLocalDeltaFromWorldDelta(const QVector3D &worldDelta) const;
    bool pickSelectedTransformAxis(const QPoint &position, DragMode *dragMode) const;
    bool pickBreadcrumbNode(const QPoint &position, int *nodeId) const;
    QVector3D dragDeltaForMousePosition(const QPoint &position) const;
    QVector3D rotationDeltaForMousePosition(const QPoint &position) const;
    bool isRotationDragMode(DragMode dragMode) const;

    const SceneDocument *m_scene = nullptr;
    const QVector<ShapeNode> *m_shapes = nullptr;
    RenderBackend m_renderBackend = SoftwareRenderBackend;
    bool m_darkViewportTheme = true;
    bool m_navigationOverlayEnabled = true;
    int m_viewportColorVariant = 0;
    int m_lightingPreset = 0;
    int m_selectedIndex = -1;
    int m_selectedGroupId = 0;
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
    float m_cameraYaw = -35.0f;
    float m_cameraPitch = -28.0f;
    float m_cameraDistance = 220.0f;
    QVector3D m_cameraTarget;
    QPoint m_lastMousePosition;
    QPoint m_dragStartMousePosition;
    QPoint m_emptyClickStartPosition;
    QVector3D m_lastDragDelta;
    QVector3D m_lastRotationDelta;
    QVector2D m_rotationDragScreenTangent;
    bool m_draggingShape = false;
    bool m_draggingGroup = false;
    bool m_panningViewport = false;
    bool m_emptyClickCandidate = false;
    DragMode m_dragMode = NoDrag;
    int m_dragShapeIndex = -1;
    int m_dragGroupId = 0;
    CsgPreview m_cachedCsgPreview;
    uint m_cachedCsgFingerprint = 0;
    uint m_pendingCsgFingerprint = 0;
    bool m_csgPreviewDirty = true;
    bool m_csgComputing = false;
    QFutureWatcher<CsgPreview> m_csgWatcher;
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
    QOpenGLShaderProgram *m_glMeshProgram = nullptr;
    QOpenGLShaderProgram *m_glLineProgram = nullptr;
    QOpenGLShaderProgram *m_glFlatProgram = nullptr;

    // Persistent VBOs — rebuilt only when scene/camera-independent data changes.
    // Grid: world-space line vertices, built once in initializeGL.
    // Mesh/helper/shadow: world-space triangle vertices, rebuilt when scene fingerprint
    // or selection/theme changes. Camera movement only updates MVP uniforms.
    QOpenGLBuffer m_vboGrid        { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_vboMesh        { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_vboSelectionEdges { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_vboHelperFront { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_vboHelperXray  { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_vboShadow      { QOpenGLBuffer::VertexBuffer };
    int     m_vboGridCount        = 0;
    int     m_vboMeshCount        = 0;
    int     m_vboSelectionHiddenEdgeCount = 0;
    int     m_vboSelectionEdgeCount = 0;
    int     m_vboSelectionSilhouetteCount = 0;
    int     m_vboHelperFrontCount = 0;
    int     m_vboHelperXrayCount  = 0;
    int     m_vboShadowCount      = 0;
    QString m_vboMeshKey;   // fingerprint|selectedIndex|theme|colorVariant
    QVector<ViewportSelectionEdgeCandidate> m_selectionEdgeCandidates;
    QString m_selectionEdgeTopologyKey; // fingerprint|selection; independent of camera
    QString m_vboSelectionEdgesKey; // fingerprint|selection|camera direction
    QString m_vboShadowKey; // fingerprint only
};

#endif
