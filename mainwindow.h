#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "scenedocument.h"

#include <QMainWindow>

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
    void onPropertyChanged();
    void onViewportShapeDragStarted(int index);
    void onViewportShapeDragged(int index, const QVector3D &delta);
    void onViewportShapeDragFinished(int index);

private:
    void buildUi();
    void refreshShapeList();
    void refreshProperties();
    void refreshOpenScadCode();
    void refreshCsgStatus();
    void refreshSceneViews();
    void selectShapeInSceneTree(int shapeId);
    void selectTreeNodeInSceneTree(int treeNodeId);
    int selectedTreeGroupId() const;
    void addGroup(SceneDocument::TreeNode::Operation operation);
    void moveTreeNodeToGroup(int nodeId, int parentGroupId);
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

    ViewportWidget *m_viewport = nullptr;
    QTreeWidget *m_shapeTree = nullptr;
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
