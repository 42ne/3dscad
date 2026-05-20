#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "openscadgenerator.h"
#include "scenedocument.h"

#include <QMainWindow>
#include <QVector>

class QTextEdit;
class QLabel;
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
    void loadExample(const QString &filePath);

    void onSceneTreeSelectionChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void showSceneTreeContextMenu(const QPoint &position);
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
    void onGraphicsTreeModuleCallDropped(int moduleGroupId, int parentGroupId, int insertIndex);
    void onGraphicsTreeNodeSelected(int nodeId);
    void onGraphicsTreeNodeDeleteRequested(int nodeId);
    void onGraphicsTreeTransformValueAdjusted(int groupId, int axis, int numberStart, int numberLength, qreal delta);
    void onGraphicsTreeTransformControlHovered(int groupId, SceneDocument::TreeNode::Operation operation, int axis);
    void onGraphicsTreeModuleRenameRequested(int groupId, const QString &newName);
    void onGraphicsTreeVariableRenameRequested(int variableId, const QString &newName);
    void onGraphicsTreeShapeParameterAdjusted(int nodeId, int paramIndex, int numberStart, int numberLength, qreal delta);
    void onGraphicsTreeShapeParameterHovered(int shapeId, int parameter);
    void onGraphicsTreeVariableNumberAdjusted(int nodeId, int start, int length, qreal delta);
    void onGraphicsTreeModuleCallArgumentAdjusted(int moduleCallId, int parameterVariableId, int start, int length, qreal delta);
    void onGraphicsTreeForLoopRangeAdjusted(int nodeId, int start, int length, qreal delta);

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
    void clearSelection();
    int selectedTreeNodeIdForCodeHighlight() const;
    int selectedTreeGroupId() const;
    int selectedDirectGroupId() const;
    void addGroup(SceneDocument::TreeNode::Operation operation);
    void moveTreeNodeToGroup(int nodeId, int parentGroupId, int insertIndex = -1);
    QString previewScadPath() const;
    QString examplesPath() const;
    void populateExamplesMenu(QMenu *menu);
    void applyCtrlParamHighlight();

    struct CtrlParamHighlight {
        bool    active        = false;
        int     nodeId        = 0;
        QString contextPrefix; // unique text immediately before expression in generated code
        QString expression;    // full expression containing the number
        int     numberStart   = 0;  // offset of number within expression
        int     numberLength  = 0;  // length of number in expression
    } m_ctrlHighlight;
    bool writeOpenScadPreview(bool notify);

private:
    SceneDocument m_scene;
    QUndoStack *m_undoStack = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
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
    QLabel *m_csgStatusLabel = nullptr;
    QLabel *m_openScadPreviewLabel = nullptr;
    QLabel *m_parseErrorLabel = nullptr;

};

#endif
