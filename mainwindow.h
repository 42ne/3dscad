#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "openscadgenerator.h"
#include "scenedocument.h"

#include <QMainWindow>
#include <QVector>

class QTextEdit;
class QLabel;
class QDoubleSpinBox;
class QComboBox;
class QPushButton;
class QAction;
class QUndoStack;
class QTreeWidget;
class QTreeWidgetItem;
class ViewportWidget;
class SceneTreeGraphicsWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void addCube();
    void addSphere();
    void addCylinder();
    void addUnionGroup();
    void addDifferenceGroup();
    void addIntersectionGroup();
    void deleteSelectedShape();
    void deleteSelectedGroup();
    void applyOpenScadCode();
    void sendToOpenScad();

    void onSceneTreeSelectionChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void showSceneTreeContextMenu(const QPoint &position);
    void onPropertyChanged();
    void onBooleanModeChanged(int index);
    void onViewportShapeDragStarted(int index);
    void onViewportShapeDragged(int index, const QVector3D &delta);
    void onViewportShapeDragFinished(int index);
    void onViewportShapeRotationDragStarted(int index);
    void onViewportShapeRotated(int index, const QVector3D &deltaDegrees);
    void onViewportShapeRotationDragFinished(int index);
    void onViewportGroupDragStarted(int groupId);
    void onViewportGroupDragged(int groupId, const QVector3D &delta);
    void onViewportGroupDragFinished(int groupId);
    void onViewportGroupRotationDragStarted(int groupId);
    void onViewportGroupRotated(int groupId, const QVector3D &deltaDegrees);
    void onViewportGroupRotationDragFinished(int groupId);
    void onGraphicsTreeToolDropped(const QString &toolName, int parentGroupId, int insertIndex);
    void onGraphicsTreeNodeSelected(int nodeId);
    void onGraphicsTreeNodeDeleteRequested(int nodeId);
    void onGraphicsTreeTransformValueAdjusted(int groupId, int axis, qreal delta);
    void onGraphicsTreeTransformControlHovered(int groupId, SceneDocument::TreeNode::Operation operation, int axis);
    void onGraphicsTreeShapeParameterAdjusted(int shapeId, int parameter, qreal delta);
    void onGraphicsTreeShapeParameterHovered(int shapeId, int parameter);
    void onGraphicsTreeVariableNumberAdjusted(int nodeId, int start, int length, qreal delta);

private:
    void buildUi();
    void refreshShapeList();
    void refreshProperties();
    void refreshOpenScadCode();
    void refreshCsgStatus();
    void refreshSceneViews();
    void highlightOpenScadSelection();
    void selectShapeInSceneTree(int shapeId);
    void selectTreeNodeInSceneTree(int treeNodeId);
    int selectedTreeNodeIdForCodeHighlight() const;
    int selectedTreeGroupId() const;
    int selectedDirectGroupId() const;
    void addGroup(SceneDocument::TreeNode::Operation operation);
    void moveTreeNodeToGroup(int nodeId, int parentGroupId, int insertIndex = -1);
    void changeShapeBooleanMode(int shapeId, ShapeNode::BooleanMode booleanMode);
    QString previewScadPath() const;
    bool writeOpenScadPreview(bool notify);

private:
    SceneDocument m_scene;
    QUndoStack *m_undoStack = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    bool m_updatingProperties = false;
    bool m_viewportDragActive = false;
    ShapeNode m_viewportDragStartShape;
    bool m_viewportGroupDragActive = false;
    int m_viewportDragGroupId = 0;
    QVector3D m_viewportDragStartGroupPosition;
    QVector3D m_viewportDragStartGroupRotation;
    QVector3D m_viewportDragStartGroupScale;
    QVector<OpenScadGenerator::SourceRange> m_openScadSourceRanges;

    ViewportWidget *m_viewport = nullptr;
    QTreeWidget *m_shapeTree = nullptr;
    SceneTreeGraphicsWidget *m_sceneTreeGraphics = nullptr;
    QTextEdit *m_codeEditor = nullptr;
    QPushButton *m_applyCodeButton = nullptr;
    QPushButton *m_sendToOpenScadButton = nullptr;
    QPushButton *m_deleteShapeButton = nullptr;
    QPushButton *m_deleteGroupButton = nullptr;
    QLabel *m_csgStatusLabel = nullptr;
    QLabel *m_openScadPreviewLabel = nullptr;

    QDoubleSpinBox *m_posX = nullptr;
    QDoubleSpinBox *m_posY = nullptr;
    QDoubleSpinBox *m_posZ = nullptr;

    QDoubleSpinBox *m_rotX = nullptr;
    QDoubleSpinBox *m_rotY = nullptr;
    QDoubleSpinBox *m_rotZ = nullptr;

    QDoubleSpinBox *m_sizeX = nullptr;
    QDoubleSpinBox *m_sizeY = nullptr;
    QDoubleSpinBox *m_sizeZ = nullptr;

    QDoubleSpinBox *m_radius = nullptr;
    QDoubleSpinBox *m_height = nullptr;
    QComboBox *m_booleanMode = nullptr;
};

#endif
