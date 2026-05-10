#include "mainwindow.h"
#include "csgevaluator.h"
#include "openscadgenerator.h"
#include "openscadparser.h"
#include "scenecommands.h"
#include "viewportwidget.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QClipboard>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDir>
#include <QDropEvent>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPoint>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QTextEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QUndoStack>
#include <QVBoxLayout>

#include <functional>

// ---------------- MainWindow ----------------

static constexpr int ShapeIdRole = Qt::UserRole;
static constexpr int TreeNodeIdRole = Qt::UserRole + 1;
static constexpr int GroupOperationRole = Qt::UserRole + 2;

class SceneTreeWidget : public QTreeWidget
{
public:
    explicit SceneTreeWidget(QWidget *parent = nullptr)
        : QTreeWidget(parent)
    {
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::MoveAction);
    }

    std::function<void(int, int)> onTreeNodeDroppedOnGroup;

protected:
    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (dropTarget(event->pos()).nodeId > 0)
            event->acceptProposedAction();
        else
            event->ignore();
    }

    void dropEvent(QDropEvent *event) override
    {
        const DropTarget target = dropTarget(event->pos());
        if (target.nodeId <= 0) {
            event->ignore();
            return;
        }

        if (onTreeNodeDroppedOnGroup)
            onTreeNodeDroppedOnGroup(target.nodeId, target.parentGroupId);

        event->acceptProposedAction();
    }

private:
    struct DropTarget
    {
        int nodeId = 0;
        int parentGroupId = 0;
    };

    DropTarget dropTarget(const QPoint &position) const
    {
        DropTarget target;
        const QList<QTreeWidgetItem *> selected = selectedItems();
        if (selected.size() != 1)
            return target;

        const int nodeId = selected.first()->data(0, TreeNodeIdRole).toInt();
        if (nodeId <= 0)
            return target;

        QTreeWidgetItem *targetItem = itemAt(position);
        if (!targetItem)
            return target;

        if (targetItem->data(0, ShapeIdRole).toInt() >= 0)
            targetItem = targetItem->parent();

        if (!targetItem)
            return target;

        const QVariant operationData = targetItem->data(0, GroupOperationRole);
        if (!operationData.isValid())
            return target;

        target.nodeId = nodeId;
        target.parentGroupId = targetItem->data(0, TreeNodeIdRole).toInt();
        return target;
    }
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    refreshOpenScadCode();
    refreshCsgStatus();
    refreshProperties();
}

static QDoubleSpinBox *makeSpinBox()
{
    auto *box = new QDoubleSpinBox;
    box->setRange(-10000.0, 10000.0);
    box->setDecimals(2);
    box->setSingleStep(1.0);
    return box;
}

static QString booleanGroupLabel(SceneDocument::TreeNode::Operation operation)
{
    if (operation == SceneDocument::TreeNode::Difference)
        return "difference()";
    if (operation == SceneDocument::TreeNode::Intersection)
        return "intersection()";
    return "union()";
}

static void markGroupItem(QTreeWidgetItem *item, const SceneDocument::TreeNode &node)
{
    QFont font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    item->setData(0, ShapeIdRole, -1);
    item->setData(0, TreeNodeIdRole, node.id);
    item->setData(0, GroupOperationRole, node.operation);
    item->setForeground(0, QColor(82, 82, 82));
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
}

static QTreeWidgetItem *appendBooleanTreeItem(QTreeWidgetItem *parent,
                                              const SceneDocument::TreeNode &node,
                                              const SceneDocument &scene)
{
    if (node.type == SceneDocument::TreeNode::Primitive) {
        const ShapeNode *shape = scene.shapeById(node.shapeId);
        if (!shape)
            return nullptr;

        auto *item = new QTreeWidgetItem(parent);
        item->setText(0, shape->name);
        item->setData(0, ShapeIdRole, shape->id);
        item->setData(0, TreeNodeIdRole, node.id);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        return item;
    }

    auto *groupItem = new QTreeWidgetItem(parent);
    groupItem->setText(0, booleanGroupLabel(node.operation));
    markGroupItem(groupItem, node);

    for (const SceneDocument::TreeNode &child : node.children)
        appendBooleanTreeItem(groupItem, child, scene);

    return groupItem;
}

void MainWindow::buildUi()
{
    setWindowTitle("OpenSCAD Visual Editor Prototype");

    m_undoStack = new QUndoStack(this);
    m_undoAction = m_undoStack->createUndoAction(this, "Undo");
    m_redoAction = m_undoStack->createRedoAction(this, "Redo");
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction->setShortcut(QKeySequence::Redo);

    auto *editMenu = menuBar()->addMenu("Edit");
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);

    m_viewport = new ViewportWidget;
    m_viewport->setScene(&m_scene);

    m_codeEditor = new QTextEdit;
    m_codeEditor->setReadOnly(false);
    m_codeEditor->setMinimumHeight(180);
    m_codeEditor->setFontFamily("Consolas");

    m_applyCodeButton = new QPushButton("Apply code");
    m_sendToOpenScadButton = new QPushButton("Send to OpenSCAD");
    m_openScadPreviewLabel = new QLabel;
    m_openScadPreviewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_openScadPreviewLabel->setWordWrap(true);
    m_openScadPreviewLabel->setText(QString("Preview file: %1").arg(QDir::toNativeSeparators(previewScadPath())));

    auto *codePanel = new QWidget;
    auto *codeLayout = new QVBoxLayout(codePanel);
    codeLayout->setContentsMargins(0, 0, 0, 0);
    codeLayout->addWidget(m_codeEditor);
    codeLayout->addWidget(m_applyCodeButton);
    codeLayout->addWidget(m_sendToOpenScadButton);
    codeLayout->addWidget(m_openScadPreviewLabel);

    auto *mainSplitter = new QSplitter(Qt::Vertical);
    mainSplitter->addWidget(m_viewport);
    mainSplitter->addWidget(codePanel);
    mainSplitter->setStretchFactor(0, 4);
    mainSplitter->setStretchFactor(1, 1);

    setCentralWidget(mainSplitter);

    // Left dock: shapes
    auto *leftDock = new QDockWidget("Shapes", this);
    auto *leftPanel = new QWidget;
    auto *leftLayout = new QVBoxLayout(leftPanel);

    auto *addCubeButton = new QPushButton("Add cube");
    auto *addSphereButton = new QPushButton("Add sphere");
    auto *addCylinderButton = new QPushButton("Add cylinder");
    auto *addUnionGroupButton = new QPushButton("Add union group");
    auto *addDifferenceGroupButton = new QPushButton("Add difference group");
    auto *addIntersectionGroupButton = new QPushButton("Add intersection group");
    m_deleteShapeButton = new QPushButton("Delete selected");
    m_deleteGroupButton = new QPushButton("Delete group");
    m_deleteShapeButton->setEnabled(false);
    m_deleteGroupButton->setEnabled(false);

    auto *shapeTree = new SceneTreeWidget;
    shapeTree->onTreeNodeDroppedOnGroup = [this](int nodeId, int parentGroupId) {
        moveTreeNodeToGroup(nodeId, parentGroupId);
    };
    m_shapeTree = shapeTree;
    m_shapeTree->setHeaderHidden(true);
    m_shapeTree->setContextMenuPolicy(Qt::CustomContextMenu);

    leftLayout->addWidget(addCubeButton);
    leftLayout->addWidget(addSphereButton);
    leftLayout->addWidget(addCylinderButton);
    leftLayout->addWidget(addUnionGroupButton);
    leftLayout->addWidget(addDifferenceGroupButton);
    leftLayout->addWidget(addIntersectionGroupButton);
    leftLayout->addWidget(m_deleteShapeButton);
    leftLayout->addWidget(m_deleteGroupButton);
    leftLayout->addWidget(new QLabel("Scene tree:"));
    leftLayout->addWidget(m_shapeTree);
    m_csgStatusLabel = new QLabel;
    m_csgStatusLabel->setWordWrap(true);
    leftLayout->addWidget(m_csgStatusLabel);

    leftDock->setWidget(leftPanel);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);

    connect(addCubeButton, &QPushButton::clicked, this, &MainWindow::addCube);
    connect(addSphereButton, &QPushButton::clicked, this, &MainWindow::addSphere);
    connect(addCylinderButton, &QPushButton::clicked, this, &MainWindow::addCylinder);
    connect(addUnionGroupButton, &QPushButton::clicked, this, &MainWindow::addUnionGroup);
    connect(addDifferenceGroupButton, &QPushButton::clicked, this, &MainWindow::addDifferenceGroup);
    connect(addIntersectionGroupButton, &QPushButton::clicked, this, &MainWindow::addIntersectionGroup);
    connect(m_deleteShapeButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedShape);
    connect(m_deleteGroupButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedGroup);
    connect(m_applyCodeButton, &QPushButton::clicked, this, &MainWindow::applyOpenScadCode);
    connect(m_sendToOpenScadButton, &QPushButton::clicked, this, &MainWindow::sendToOpenScad);
    connect(m_viewport, &ViewportWidget::shapeClicked, this, [this](int index) {
        const ShapeNode *shape = m_scene.shapeAt(index);
        selectShapeInSceneTree(shape ? shape->id : -1);
    });
    connect(m_viewport, &ViewportWidget::shapeDragStarted, this, &MainWindow::onViewportShapeDragStarted);
    connect(m_viewport, &ViewportWidget::shapeDragged, this, &MainWindow::onViewportShapeDragged);
    connect(m_viewport, &ViewportWidget::shapeDragFinished, this, &MainWindow::onViewportShapeDragFinished);
    connect(m_shapeTree, &QTreeWidget::currentItemChanged,
            this, &MainWindow::onSceneTreeSelectionChanged);
    connect(m_shapeTree, &QTreeWidget::customContextMenuRequested,
            this, &MainWindow::showSceneTreeContextMenu);

    // Right dock: properties
    auto *rightDock = new QDockWidget("Properties", this);
    auto *rightPanel = new QWidget;
    auto *rightLayout = new QVBoxLayout(rightPanel);

    auto *transformBox = new QGroupBox("Transform");
    auto *transformLayout = new QFormLayout(transformBox);

    m_posX = makeSpinBox();
    m_posY = makeSpinBox();
    m_posZ = makeSpinBox();

    m_rotX = makeSpinBox();
    m_rotY = makeSpinBox();
    m_rotZ = makeSpinBox();

    transformLayout->addRow("Position X", m_posX);
    transformLayout->addRow("Position Y", m_posY);
    transformLayout->addRow("Position Z", m_posZ);

    transformLayout->addRow("Rotation X", m_rotX);
    transformLayout->addRow("Rotation Y", m_rotY);
    transformLayout->addRow("Rotation Z", m_rotZ);

    auto *shapeBox = new QGroupBox("Shape parameters");
    auto *shapeLayout = new QFormLayout(shapeBox);

    m_sizeX = makeSpinBox();
    m_sizeY = makeSpinBox();
    m_sizeZ = makeSpinBox();

    m_radius = makeSpinBox();
    m_height = makeSpinBox();
    m_booleanMode = new QComboBox;
    m_booleanMode->addItem("Add solid", ShapeNode::Add);
    m_booleanMode->addItem("Subtract hole", ShapeNode::Subtract);
    m_booleanMode->addItem("Intersect mask", ShapeNode::Intersect);

    m_sizeX->setMinimum(0.1);
    m_sizeY->setMinimum(0.1);
    m_sizeZ->setMinimum(0.1);
    m_radius->setMinimum(0.1);
    m_height->setMinimum(0.1);

    shapeLayout->addRow("Size X", m_sizeX);
    shapeLayout->addRow("Size Y", m_sizeY);
    shapeLayout->addRow("Size Z", m_sizeZ);
    shapeLayout->addRow("Radius", m_radius);
    shapeLayout->addRow("Height", m_height);
    shapeLayout->addRow("Boolean mode", m_booleanMode);

    rightLayout->addWidget(transformBox);
    rightLayout->addWidget(shapeBox);
    rightLayout->addStretch();

    rightDock->setWidget(rightPanel);
    addDockWidget(Qt::RightDockWidgetArea, rightDock);

    QList<QDoubleSpinBox *> boxes = {
        m_posX, m_posY, m_posZ,
        m_rotX, m_rotY, m_rotZ,
        m_sizeX, m_sizeY, m_sizeZ,
        m_radius, m_height
    };

    for (QDoubleSpinBox *box : boxes) {
        connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, &MainWindow::onPropertyChanged);
    }

    connect(m_booleanMode, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::onPropertyChanged);
}

void MainWindow::addCube()
{
    ShapeNode s;
    s.type = ShapeNode::Cube;
    s.name = QString("Cube %1").arg(m_scene.shapeCount() + 1);
    s.size = QVector3D(20, 20, 20);

    m_undoStack->push(new AddShapeCommand(&m_scene, s, [this]() {
        refreshSceneViews();
    }));
}

void MainWindow::addSphere()
{
    ShapeNode s;
    s.type = ShapeNode::Sphere;
    s.name = QString("Sphere %1").arg(m_scene.shapeCount() + 1);
    s.radius = 10;

    m_undoStack->push(new AddShapeCommand(&m_scene, s, [this]() {
        refreshSceneViews();
    }));
}

void MainWindow::addCylinder()
{
    ShapeNode s;
    s.type = ShapeNode::Cylinder;
    s.name = QString("Cylinder %1").arg(m_scene.shapeCount() + 1);
    s.radius = 10;
    s.height = 30;

    m_undoStack->push(new AddShapeCommand(&m_scene, s, [this]() {
        refreshSceneViews();
    }));
}

void MainWindow::addUnionGroup()
{
    addGroup(SceneDocument::TreeNode::Union);
}

void MainWindow::addDifferenceGroup()
{
    addGroup(SceneDocument::TreeNode::Difference);
}

void MainWindow::addIntersectionGroup()
{
    addGroup(SceneDocument::TreeNode::Intersection);
}

void MainWindow::deleteSelectedShape()
{
    auto *command = new DeleteShapeCommand(&m_scene, m_scene.selectedShapeId(), [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::deleteSelectedGroup()
{
    if (!m_shapeTree || !m_shapeTree->currentItem()
        || !m_shapeTree->currentItem()->data(0, GroupOperationRole).isValid()) {
        return;
    }

    const int groupId = m_shapeTree->currentItem()->data(0, TreeNodeIdRole).toInt();
    auto *command = new RemoveGroupCommand(&m_scene, groupId, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::applyOpenScadCode()
{
    QVector<ShapeNode> shapes;
    QString errorMessage;

    if (!OpenScadParser::parse(m_codeEditor->toPlainText(), &shapes, &errorMessage)) {
        QMessageBox::warning(this, "OpenSCAD parse error", errorMessage);
        return;
    }

    auto *command = new ReplaceSceneCommand(&m_scene, shapes, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::sendToOpenScad()
{
    if (!writeOpenScadPreview(true))
        return;

    const QString nativePath = QDir::toNativeSeparators(previewScadPath());
    QApplication::clipboard()->setText(nativePath);

    QMessageBox::information(
        this,
        "OpenSCAD preview file",
        QString("Saved the current model to:\n\n%1\n\n"
                "The path was copied to the clipboard. Open this file in OpenSCAD and enable automatic reload/preview there.")
            .arg(nativePath));
}

void MainWindow::onSceneTreeSelectionChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous)
{
    Q_UNUSED(previous);

    const int shapeId = current ? current->data(0, ShapeIdRole).toInt() : -1;
    m_scene.setSelectedShapeId(shapeId);
    m_viewport->setSelectedIndex(m_scene.selectedIndex());
    refreshProperties();
}

void MainWindow::showSceneTreeContextMenu(const QPoint &position)
{
    if (!m_shapeTree)
        return;

    QTreeWidgetItem *item = m_shapeTree->itemAt(position);
    if (item)
        m_shapeTree->setCurrentItem(item);

    const bool hasItem = item != nullptr;
    const bool isShape = hasItem && item->data(0, ShapeIdRole).toInt() >= 0;
    const int groupId = hasItem && item->data(0, GroupOperationRole).isValid()
                            ? item->data(0, TreeNodeIdRole).toInt()
                            : selectedTreeGroupId();
    const bool canDeleteGroup = hasItem
                                && item->data(0, GroupOperationRole).isValid()
                                && groupId > 0
                                && groupId != m_scene.treeRoot().id;

    QMenu menu(this);
    QAction *addUnionAction = menu.addAction("Add union group");
    QAction *addDifferenceAction = menu.addAction("Add difference group");
    QAction *addIntersectionAction = menu.addAction("Add intersection group");
    menu.addSeparator();
    QAction *deleteShapeAction = menu.addAction("Delete shape");
    QAction *deleteGroupAction = menu.addAction("Delete group");

    deleteShapeAction->setEnabled(isShape);
    deleteGroupAction->setEnabled(canDeleteGroup);

    QAction *selectedAction = menu.exec(m_shapeTree->viewport()->mapToGlobal(position));
    if (!selectedAction)
        return;

    if (selectedAction == addUnionAction)
        addGroup(SceneDocument::TreeNode::Union);
    else if (selectedAction == addDifferenceAction)
        addGroup(SceneDocument::TreeNode::Difference);
    else if (selectedAction == addIntersectionAction)
        addGroup(SceneDocument::TreeNode::Intersection);
    else if (selectedAction == deleteShapeAction)
        deleteSelectedShape();
    else if (selectedAction == deleteGroupAction)
        deleteSelectedGroup();
}

void MainWindow::onPropertyChanged()
{
    if (m_updatingProperties)
        return;

    const ShapeNode *selectedShape = m_scene.selectedShape();
    if (!selectedShape)
        return;

    ShapeNode updatedShape = *selectedShape;
    updatedShape.position = QVector3D(m_posX->value(), m_posY->value(), m_posZ->value());
    updatedShape.rotation = QVector3D(m_rotX->value(), m_rotY->value(), m_rotZ->value());
    updatedShape.size = QVector3D(m_sizeX->value(), m_sizeY->value(), m_sizeZ->value());

    updatedShape.radius = m_radius->value();
    updatedShape.height = m_height->value();
    updatedShape.booleanMode = static_cast<ShapeNode::BooleanMode>(m_booleanMode->currentData().toInt());

    auto *command = new UpdateShapeCommand(&m_scene, *selectedShape, updatedShape, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::onViewportShapeDragStarted(int index)
{
    m_scene.setSelectedIndex(index);
    selectShapeInSceneTree(m_scene.selectedShapeId());
    m_viewport->setSelectedIndex(m_scene.selectedIndex());

    const ShapeNode *shape = m_scene.selectedShape();
    if (!shape)
        return;

    m_viewportDragStartShape = *shape;
    m_viewportDragActive = true;
}

void MainWindow::onViewportShapeDragged(int index, const QVector3D &delta)
{
    if (!m_viewportDragActive || m_scene.selectedIndex() != index)
        return;

    ShapeNode *shape = m_scene.selectedShape();
    if (!shape)
        return;

    *shape = m_viewportDragStartShape;
    shape->position = m_viewportDragStartShape.position + delta;

    m_viewport->update();
}

void MainWindow::onViewportShapeDragFinished(int index)
{
    if (!m_viewportDragActive || m_scene.selectedIndex() != index)
        return;

    m_viewportDragActive = false;

    const ShapeNode *shape = m_scene.selectedShape();
    if (!shape)
        return;

    auto *command = new UpdateShapeCommand(&m_scene, m_viewportDragStartShape, *shape, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        refreshProperties();
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::refreshShapeList()
{
    const int selectedShapeId = m_scene.selectedShapeId();
    const int selectedTreeNodeId = (selectedShapeId < 0 && m_shapeTree && m_shapeTree->currentItem())
                                       ? m_shapeTree->currentItem()->data(0, TreeNodeIdRole).toInt()
                                       : 0;

    m_shapeTree->blockSignals(true);
    m_shapeTree->clear();

    appendBooleanTreeItem(m_shapeTree->invisibleRootItem(), m_scene.treeRoot(), m_scene);
    m_shapeTree->expandAll();

    m_shapeTree->blockSignals(false);
    if (selectedShapeId >= 0)
        selectShapeInSceneTree(selectedShapeId);
    else
        selectTreeNodeInSceneTree(selectedTreeNodeId);

    refreshOpenScadCode();
    m_viewport->update();
    refreshCsgStatus();
}

void MainWindow::refreshSceneViews()
{
    refreshShapeList();

    m_viewport->setSelectedIndex(m_scene.selectedIndex());
    refreshProperties();
    refreshCsgStatus();
}

void MainWindow::selectShapeInSceneTree(int shapeId)
{
    if (!m_shapeTree)
        return;

    m_shapeTree->blockSignals(true);

    QTreeWidgetItem *selectedItem = nullptr;
    if (shapeId >= 0) {
        QTreeWidgetItemIterator it(m_shapeTree);
        while (*it) {
            if ((*it)->data(0, ShapeIdRole).toInt() == shapeId) {
                selectedItem = *it;
                break;
            }
            ++it;
        }
    }

    m_shapeTree->setCurrentItem(selectedItem);
    m_shapeTree->blockSignals(false);
}

void MainWindow::selectTreeNodeInSceneTree(int treeNodeId)
{
    if (!m_shapeTree)
        return;

    m_shapeTree->blockSignals(true);

    QTreeWidgetItem *selectedItem = nullptr;
    if (treeNodeId > 0) {
        QTreeWidgetItemIterator it(m_shapeTree);
        while (*it) {
            if ((*it)->data(0, TreeNodeIdRole).toInt() == treeNodeId) {
                selectedItem = *it;
                break;
            }
            ++it;
        }
    }

    m_shapeTree->setCurrentItem(selectedItem);
    m_shapeTree->blockSignals(false);
}

int MainWindow::selectedTreeGroupId() const
{
    if (!m_shapeTree || !m_shapeTree->currentItem())
        return 0;

    QTreeWidgetItem *item = m_shapeTree->currentItem();
    if (item->data(0, GroupOperationRole).isValid())
        return item->data(0, TreeNodeIdRole).toInt();

    item = item->parent();
    if (!item || !item->data(0, GroupOperationRole).isValid())
        return 0;

    return item->data(0, TreeNodeIdRole).toInt();
}

void MainWindow::addGroup(SceneDocument::TreeNode::Operation operation)
{
    auto *command = new AddGroupCommand(&m_scene, operation, selectedTreeGroupId(), -1, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::moveTreeNodeToGroup(int nodeId, int parentGroupId)
{
    auto *command = new MoveTreeNodeCommand(&m_scene, nodeId, parentGroupId, -1, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::changeShapeBooleanMode(int shapeId, ShapeNode::BooleanMode booleanMode)
{
    const ShapeNode *shape = m_scene.shapeById(shapeId);
    if (!shape || shape->booleanMode == booleanMode)
        return;

    ShapeNode updatedShape = *shape;
    updatedShape.booleanMode = booleanMode;

    auto *command = new UpdateShapeCommand(&m_scene, *shape, updatedShape, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::refreshProperties()
{
    bool hasSelection = m_scene.hasSelection();

    QList<QDoubleSpinBox *> boxes = {
        m_posX, m_posY, m_posZ,
        m_rotX, m_rotY, m_rotZ,
        m_sizeX, m_sizeY, m_sizeZ,
        m_radius, m_height
    };

    for (QDoubleSpinBox *box : boxes)
        box->setEnabled(hasSelection);

    m_deleteShapeButton->setEnabled(hasSelection);
    const int selectedGroupId = (m_shapeTree && m_shapeTree->currentItem()
                                 && m_shapeTree->currentItem()->data(0, GroupOperationRole).isValid())
                                    ? m_shapeTree->currentItem()->data(0, TreeNodeIdRole).toInt()
                                    : 0;
    m_deleteGroupButton->setEnabled(selectedGroupId > 0 && selectedGroupId != m_scene.treeRoot().id);
    m_booleanMode->setEnabled(hasSelection);

    if (!hasSelection)
        return;

    const ShapeNode *s = m_scene.selectedShape();
    if (!s)
        return;

    m_updatingProperties = true;

    QList<QDoubleSpinBox *> allBoxes = boxes;
    for (QDoubleSpinBox *box : allBoxes)
        box->blockSignals(true);
    m_booleanMode->blockSignals(true);

    m_posX->setValue(s->position.x());
    m_posY->setValue(s->position.y());
    m_posZ->setValue(s->position.z());

    m_rotX->setValue(s->rotation.x());
    m_rotY->setValue(s->rotation.y());
    m_rotZ->setValue(s->rotation.z());

    m_sizeX->setValue(s->size.x());
    m_sizeY->setValue(s->size.y());
    m_sizeZ->setValue(s->size.z());

    m_radius->setValue(s->radius);
    m_height->setValue(s->height);
    m_booleanMode->setCurrentIndex(m_booleanMode->findData(s->booleanMode));

    for (QDoubleSpinBox *box : allBoxes)
        box->blockSignals(false);
    m_booleanMode->blockSignals(false);

    m_updatingProperties = false;

    bool cube = s->type == ShapeNode::Cube;
    bool sphere = s->type == ShapeNode::Sphere;
    bool cylinder = s->type == ShapeNode::Cylinder;

    m_sizeX->setEnabled(cube);
    m_sizeY->setEnabled(cube);
    m_sizeZ->setEnabled(cube);

    m_radius->setEnabled(sphere || cylinder);
    m_height->setEnabled(cylinder);
}

void MainWindow::refreshOpenScadCode()
{
    const QString code = OpenScadGenerator::generate(m_scene);
    m_codeEditor->setPlainText(code);
    writeOpenScadPreview(false);
}

void MainWindow::refreshCsgStatus()
{
    if (!m_csgStatusLabel)
        return;

    const CsgPreview preview = buildCsgPreview(m_scene);
    m_csgStatusLabel->setText(preview.statusText);
}

QString MainWindow::previewScadPath() const
{
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("openscad_preview.scad");
}

bool MainWindow::writeOpenScadPreview(bool notify)
{
    const QString path = previewScadPath();
    const QString directoryPath = QFileInfo(path).absolutePath();
    QDir().mkpath(directoryPath);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (notify) {
            QMessageBox::warning(this, "OpenSCAD preview error",
                                 QString("Cannot write preview file:\n%1").arg(path));
        }
        return false;
    }

    const QByteArray data = m_codeEditor->toPlainText().toUtf8();
    if (file.write(data) != data.size() || !file.commit()) {
        if (notify) {
            QMessageBox::warning(this, "OpenSCAD preview error",
                                 QString("Cannot finish writing preview file:\n%1").arg(path));
        }
        return false;
    }

    if (m_openScadPreviewLabel)
        m_openScadPreviewLabel->setText(QString("Preview file: %1").arg(QDir::toNativeSeparators(path)));

    return true;
}
