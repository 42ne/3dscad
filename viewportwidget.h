#ifndef VIEWPORTWIDGET_H
#define VIEWPORTWIDGET_H

#include "csgevaluator.h"
#include "shapenode.h"

#include <QImage>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPoint>
#include <QString>
#include <QVector>
#include <QVector2D>

class QMouseEvent;
class QOpenGLShaderProgram;
class QPainter;
class QCheckBox;
class QComboBox;
class QResizeEvent;
class QWheelEvent;

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

signals:
    void shapeClicked(int index);
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
    void drawTreeTransformControlPreview(QPainter &painter) const;
    void drawTreeShapeParameterPreview(QPainter &painter) const;
    void updateViewportControls();
    bool canUseOpenGLRenderBackend() const;
    QVector<SceneDocument::TreeNode> parentGroupStackForGroup(int groupId) const;
    QVector3D transformOriginForGroup(int groupId) const;
    QVector3D worldAxisVectorForGroup(int groupId, const QVector3D &localAxis) const;
    QVector<SceneDocument::TreeNode> selectedParentGroupStack() const;
    QVector3D selectedTransformOrigin() const;
    QVector3D selectedWorldAxisVector(const QVector3D &localAxis) const;
    QVector3D selectedLocalDeltaFromWorldDelta(const QVector3D &worldDelta) const;
    bool pickSelectedTransformAxis(const QPoint &position, DragMode *dragMode) const;
    QVector3D dragDeltaForMousePosition(const QPoint &position) const;
    QVector3D rotationDeltaForMousePosition(const QPoint &position) const;
    bool isRotationDragMode(DragMode dragMode) const;

    const SceneDocument *m_scene = nullptr;
    const QVector<ShapeNode> *m_shapes = nullptr;
    RenderBackend m_renderBackend = SoftwareRenderBackend;
    bool m_darkViewportTheme = true;
    int m_viewportColorVariant = 0;
    int m_lightingPreset = 0;
    int m_selectedIndex = -1;
    int m_selectedGroupId = 0;
    int m_treeTransformPreviewGroupId = 0;
    int m_treeTransformPreviewAxis = -1;
    SceneDocument::TreeNode::Operation m_treeTransformPreviewOperation = SceneDocument::TreeNode::Union;
    int m_treeShapePreviewShapeId = -1;
    int m_treeShapePreviewParameter = -1;
    float m_cameraYaw = -35.0f;
    float m_cameraPitch = -28.0f;
    float m_cameraDistance = 220.0f;
    QVector3D m_cameraTarget;
    QPoint m_lastMousePosition;
    QPoint m_dragStartMousePosition;
    QVector3D m_lastDragDelta;
    QVector3D m_lastRotationDelta;
    QVector2D m_rotationDragScreenTangent;
    bool m_draggingShape = false;
    bool m_draggingGroup = false;
    bool m_panningViewport = false;
    DragMode m_dragMode = NoDrag;
    int m_dragShapeIndex = -1;
    int m_dragGroupId = 0;
    CsgPreview m_cachedCsgPreview;
    uint m_cachedCsgFingerprint = 0;
    bool m_csgPreviewDirty = true;
    QVector<int> m_pickBuffer;
    QVector<float> m_depthBuffer;
    QImage m_renderImage;
    QSize m_pickBufferSize;
    QCheckBox *m_openGLViewportCheckBox = nullptr;
    QCheckBox *m_darkViewportCheckBox = nullptr;
    QComboBox *m_colorVariantComboBox = nullptr;
    QComboBox *m_lightingPresetComboBox = nullptr;
    QOpenGLShaderProgram *m_glMeshProgram = nullptr;
    QOpenGLShaderProgram *m_glLineProgram = nullptr;
    QOpenGLShaderProgram *m_glFlatProgram = nullptr;
};

#endif
