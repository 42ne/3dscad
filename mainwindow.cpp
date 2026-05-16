#include "mainwindow.h"
#include "csgevaluator.h"
#include "openscadgenerator.h"
#include "openscadparser.h"
#include "scenecommands.h"
#include "scenetreegraphicswidget.h"
#include "viewportwidget.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
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
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QUndoStack>
#include <QVBoxLayout>

#include <functional>
#include <cmath>

// ---------------- MainWindow ----------------

static constexpr int ShapeIdRole = Qt::UserRole;
static constexpr int TreeNodeIdRole = Qt::UserRole + 1;
static constexpr int GroupOperationRole = Qt::UserRole + 2;

static float normalizedRotationDegrees(float value)
{
    while (value > 180.0f)
        value -= 360.0f;
    while (value < -180.0f)
        value += 360.0f;
    return value;
}

static QVector3D normalizedRotation(const QVector3D &rotation)
{
    return QVector3D(normalizedRotationDegrees(rotation.x()),
                     normalizedRotationDegrees(rotation.y()),
                     normalizedRotationDegrees(rotation.z()));
}

static ShapeNode makeShapeForTool(const QString &toolName, int shapeNumber)
{
    ShapeNode shape;
    shape.name = QString("%1 %2").arg(toolName.left(1).toUpper() + toolName.mid(1)).arg(shapeNumber);

    if (toolName == "sphere") {
        shape.type = ShapeNode::Sphere;
        shape.radius = 10.0f;
    } else if (toolName == "cylinder") {
        shape.type = ShapeNode::Cylinder;
        shape.radius = 10.0f;
        shape.height = 30.0f;
    } else {
        shape.type = ShapeNode::Cube;
        shape.size = QVector3D(20, 20, 20);
    }

    return shape;
}

static bool operationForTool(const QString &toolName, SceneDocument::TreeNode::Operation *operation)
{
    if (!operation)
        return false;

    if (toolName == "module") {
        *operation = SceneDocument::TreeNode::Module;
        return true;
    }

    if (toolName == "union") {
        *operation = SceneDocument::TreeNode::Union;
        return true;
    }

    if (toolName == "difference") {
        *operation = SceneDocument::TreeNode::Difference;
        return true;
    }

    if (toolName == "intersection") {
        *operation = SceneDocument::TreeNode::Intersection;
        return true;
    }

    return false;
}

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
        setDefaultDropAction(Qt::CopyAction);
    }

    std::function<void(int, int)> onTreeNodeDroppedOnGroup;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (dropTarget(event->pos()).nodeId > 0) {
            event->setDropAction(Qt::CopyAction);
            event->accept();
        } else {
            event->ignore();
        }
    }

    void dropEvent(QDropEvent *event) override
    {
        const DropTarget target = dropTarget(event->pos());
        if (target.nodeId <= 0) {
            event->ignore();
            return;
        }

        event->setDropAction(Qt::CopyAction);
        event->accept();

        QTimer::singleShot(0, this, [this, target]() {
            if (onTreeNodeDroppedOnGroup)
                onTreeNodeDroppedOnGroup(target.nodeId, target.parentGroupId);
        });
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
    if (operation == SceneDocument::TreeNode::Module)
        return "module scene_model";
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
                                              const SceneDocument &scene,
                                              SceneDocument::TreeNode::Operation parentOperation = SceneDocument::TreeNode::Union,
                                              int childIndex = 0)
{
    QString roleSuffix;
    QColor roleColor;
    if (parentOperation == SceneDocument::TreeNode::Difference) {
        roleSuffix = childIndex == 0 ? " (base)" : " (cut)";
        roleColor = childIndex == 0 ? QColor(45, 90, 145) : QColor(145, 80, 45);
    } else if (parentOperation == SceneDocument::TreeNode::Intersection) {
        roleSuffix = " (mask)";
        roleColor = QColor(85, 95, 145);
    }

    if (node.type == SceneDocument::TreeNode::Primitive) {
        const ShapeNode *shape = scene.shapeById(node.shapeId);
        if (!shape)
            return nullptr;

        auto *item = new QTreeWidgetItem(parent);
        item->setText(0, shape->name + roleSuffix);
        item->setData(0, ShapeIdRole, shape->id);
        item->setData(0, TreeNodeIdRole, node.id);
        if (roleColor.isValid())
            item->setForeground(0, roleColor);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        return item;
    }

    auto *groupItem = new QTreeWidgetItem(parent);
    groupItem->setText(0, booleanGroupLabel(node.operation) + roleSuffix);
    markGroupItem(groupItem, node);
    if (roleColor.isValid())
        groupItem->setForeground(0, roleColor);

    for (int i = 0; i < node.children.size(); ++i)
        appendBooleanTreeItem(groupItem, node.children[i], scene, node.operation, i);

    return groupItem;
}

static bool findEffectiveBooleanMode(const SceneDocument::TreeNode &node,
                                     int shapeId,
                                     ShapeNode::BooleanMode inheritedMode,
                                     ShapeNode::BooleanMode *mode)
{
    if (node.type == SceneDocument::TreeNode::Primitive) {
        if (node.shapeId == shapeId) {
            *mode = inheritedMode;
            return true;
        }

        return false;
    }

    for (int i = 0; i < node.children.size(); ++i) {
        ShapeNode::BooleanMode childMode = inheritedMode;
        if (node.operation == SceneDocument::TreeNode::Difference && i > 0)
            childMode = ShapeNode::Subtract;
        else if (node.operation == SceneDocument::TreeNode::Intersection)
            childMode = ShapeNode::Intersect;

        if (findEffectiveBooleanMode(node.children[i], shapeId, childMode, mode))
            return true;
    }

    return false;
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

    m_deleteShapeButton = new QPushButton("Delete selected");
    m_deleteGroupButton = new QPushButton("Delete group");
    m_useOpenGLCheckBox = new QCheckBox("Use OpenGL");
    m_deleteShapeButton->setEnabled(false);
    m_deleteGroupButton->setEnabled(false);
    m_useOpenGLCheckBox->setChecked(m_viewport->renderBackend() == ViewportWidget::OpenGLRenderBackend);
    m_useOpenGLCheckBox->setEnabled(m_viewport->isOpenGLRenderBackendAvailable());
    m_useOpenGLCheckBox->setToolTip(m_useOpenGLCheckBox->isEnabled()
                                        ? "Use the experimental OpenGL viewport backend."
                                        : "The experimental OpenGL backend is not available in this build.");

    auto *shapeTree = new SceneTreeWidget;
    shapeTree->onTreeNodeDroppedOnGroup = [this](int nodeId, int parentGroupId) {
        moveTreeNodeToGroup(nodeId, parentGroupId);
    };
    m_shapeTree = shapeTree;
    m_shapeTree->setHeaderHidden(true);
    m_shapeTree->setContextMenuPolicy(Qt::CustomContextMenu);

    m_sceneTreeGraphics = new SceneTreeGraphicsWidget;
    m_sceneTreeGraphics->setSceneDocument(&m_scene);
    m_sceneTreeGraphics->setToolDroppedCallback([this](const QString &toolName, int parentGroupId, int insertIndex) {
        onGraphicsTreeToolDropped(toolName, parentGroupId, insertIndex);
    });
    m_sceneTreeGraphics->setTreeNodeDroppedCallback([this](int nodeId, int parentGroupId, int insertIndex) {
        moveTreeNodeToGroup(nodeId, parentGroupId, insertIndex);
    });
    m_sceneTreeGraphics->setTreeNodeSelectedCallback([this](int nodeId) {
        onGraphicsTreeNodeSelected(nodeId);
    });
    m_sceneTreeGraphics->setTreeNodeDeleteRequestedCallback([this](int nodeId) {
        onGraphicsTreeNodeDeleteRequested(nodeId);
    });

    auto *legacyTreePanel = new QWidget;
    auto *legacyTreeLayout = new QVBoxLayout(legacyTreePanel);
    legacyTreeLayout->setContentsMargins(0, 0, 0, 0);
    legacyTreeLayout->addWidget(new QLabel("Scene tree:"));
    legacyTreeLayout->addWidget(m_shapeTree);

    auto *graphicsTreePanel = new QWidget;
    auto *graphicsTreeLayout = new QVBoxLayout(graphicsTreePanel);
    graphicsTreeLayout->setContentsMargins(0, 0, 0, 0);
    graphicsTreeLayout->addWidget(new QLabel("Graphics tree preview:"));
    graphicsTreeLayout->addWidget(m_sceneTreeGraphics);

    auto *treeSplitter = new QSplitter(Qt::Vertical);
    treeSplitter->addWidget(legacyTreePanel);
    treeSplitter->addWidget(graphicsTreePanel);
    treeSplitter->setStretchFactor(0, 1);
    treeSplitter->setStretchFactor(1, 5);
    treeSplitter->setSizes({115, 520});

    leftLayout->addWidget(m_deleteShapeButton);
    leftLayout->addWidget(m_deleteGroupButton);
    leftLayout->addWidget(m_useOpenGLCheckBox);
    leftLayout->addWidget(treeSplitter, 1);
    m_csgStatusLabel = new QLabel;
    m_csgStatusLabel->setWordWrap(true);
    leftLayout->addWidget(m_csgStatusLabel);

    leftDock->setWidget(leftPanel);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);

    connect(m_deleteShapeButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedShape);
    connect(m_deleteGroupButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedGroup);
    connect(m_useOpenGLCheckBox, &QCheckBox::toggled, this, &MainWindow::onUseOpenGLToggled);
    connect(m_applyCodeButton, &QPushButton::clicked, this, &MainWindow::applyOpenScadCode);
    connect(m_sendToOpenScadButton, &QPushButton::clicked, this, &MainWindow::sendToOpenScad);
    connect(m_viewport, &ViewportWidget::shapeClicked, this, [this](int index) {
        const ShapeNode *shape = m_scene.shapeAt(index);
        m_scene.setSelectedIndex(index);
        selectShapeInSceneTree(shape ? shape->id : -1);
        m_viewport->setSelectedIndex(m_scene.selectedIndex());
        m_viewport->setSelectedGroupId(0);
        refreshProperties();
    });
    connect(m_viewport, &ViewportWidget::shapeDragStarted, this, &MainWindow::onViewportShapeDragStarted);
    connect(m_viewport, &ViewportWidget::shapeDragged, this, &MainWindow::onViewportShapeDragged);
    connect(m_viewport, &ViewportWidget::shapeDragFinished, this, &MainWindow::onViewportShapeDragFinished);
    connect(m_viewport, &ViewportWidget::shapeRotationDragStarted, this, &MainWindow::onViewportShapeRotationDragStarted);
    connect(m_viewport, &ViewportWidget::shapeRotated, this, &MainWindow::onViewportShapeRotated);
    connect(m_viewport, &ViewportWidget::shapeRotationDragFinished, this, &MainWindow::onViewportShapeRotationDragFinished);
    connect(m_viewport, &ViewportWidget::groupDragStarted, this, &MainWindow::onViewportGroupDragStarted);
    connect(m_viewport, &ViewportWidget::groupDragged, this, &MainWindow::onViewportGroupDragged);
    connect(m_viewport, &ViewportWidget::groupDragFinished, this, &MainWindow::onViewportGroupDragFinished);
    connect(m_viewport, &ViewportWidget::groupRotationDragStarted, this, &MainWindow::onViewportGroupRotationDragStarted);
    connect(m_viewport, &ViewportWidget::groupRotated, this, &MainWindow::onViewportGroupRotated);
    connect(m_viewport, &ViewportWidget::groupRotationDragFinished, this, &MainWindow::onViewportGroupRotationDragFinished);
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
    shapeLayout->addRow("Tree role", m_booleanMode);

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
            this, &MainWindow::onBooleanModeChanged);
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
    const int groupId = current && current->data(0, GroupOperationRole).isValid()
                            ? current->data(0, TreeNodeIdRole).toInt()
                            : 0;
    const int treeNodeId = current ? current->data(0, TreeNodeIdRole).toInt() : 0;
    m_scene.setSelectedShapeId(shapeId);
    m_viewport->setSelectedIndex(m_scene.selectedIndex());
    m_viewport->setSelectedGroupId(groupId);
    if (m_sceneTreeGraphics)
        m_sceneTreeGraphics->setSelectedTreeNodeId(treeNodeId);
    highlightOpenScadSelection();
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
    if (!selectedShape) {
        const int groupId = selectedDirectGroupId();
        if (groupId <= 0)
            return;

        auto *command = new UpdateGroupTransformCommand(
            &m_scene,
            groupId,
            QVector3D(m_posX->value(), m_posY->value(), m_posZ->value()),
            QVector3D(m_rotX->value(), m_rotY->value(), m_rotZ->value()),
            [this]() {
                refreshSceneViews();
            });

        if (!command->isValid()) {
            delete command;
            return;
        }

        m_undoStack->push(command);
        return;
    }

    ShapeNode updatedShape = *selectedShape;
    updatedShape.position = QVector3D(m_posX->value(), m_posY->value(), m_posZ->value());
    updatedShape.rotation = QVector3D(m_rotX->value(), m_rotY->value(), m_rotZ->value());
    updatedShape.size = QVector3D(m_sizeX->value(), m_sizeY->value(), m_sizeZ->value());

    updatedShape.radius = m_radius->value();
    updatedShape.height = m_height->value();

    auto *command = new UpdateShapeCommand(&m_scene, *selectedShape, updatedShape, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::onBooleanModeChanged(int index)
{
    if (m_updatingProperties || index < 0)
        return;

    const ShapeNode *selectedShape = m_scene.selectedShape();
    if (!selectedShape)
        return;

    changeShapeBooleanMode(selectedShape->id, static_cast<ShapeNode::BooleanMode>(m_booleanMode->itemData(index).toInt()));
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

void MainWindow::onViewportShapeRotationDragStarted(int index)
{
    onViewportShapeDragStarted(index);
}

void MainWindow::onViewportShapeRotated(int index, const QVector3D &deltaDegrees)
{
    if (!m_viewportDragActive || m_scene.selectedIndex() != index)
        return;

    ShapeNode *shape = m_scene.selectedShape();
    if (!shape)
        return;

    *shape = m_viewportDragStartShape;
    shape->rotation = normalizedRotation(m_viewportDragStartShape.rotation + deltaDegrees);

    m_viewport->invalidateCsgPreview();
    m_viewport->update();
    refreshProperties();
}

void MainWindow::onViewportShapeRotationDragFinished(int index)
{
    onViewportShapeDragFinished(index);
}

void MainWindow::onViewportGroupDragStarted(int groupId)
{
    selectTreeNodeInSceneTree(groupId);
    m_viewport->setSelectedGroupId(groupId);

    const SceneDocument::TreeNode *group = m_scene.treeNodeById(groupId);
    if (!group || group->type != SceneDocument::TreeNode::Group)
        return;

    m_viewportDragGroupId = groupId;
    m_viewportDragStartGroupPosition = group->position;
    m_viewportDragStartGroupRotation = group->rotation;
    m_viewportGroupDragActive = true;
}

void MainWindow::onViewportGroupDragged(int groupId, const QVector3D &delta)
{
    if (!m_viewportGroupDragActive || m_viewportDragGroupId != groupId)
        return;

    m_scene.updateGroupTransform(groupId, m_viewportDragStartGroupPosition + delta, m_viewportDragStartGroupRotation);
    m_viewport->invalidateCsgPreview();
    m_viewport->update();
}

void MainWindow::onViewportGroupDragFinished(int groupId)
{
    if (!m_viewportGroupDragActive || m_viewportDragGroupId != groupId)
        return;

    m_viewportGroupDragActive = false;

    const SceneDocument::TreeNode *group = m_scene.treeNodeById(groupId);
    if (!group || group->type != SceneDocument::TreeNode::Group)
        return;

    const QVector3D finalPosition = group->position;
    const QVector3D finalRotation = group->rotation;
    if (finalPosition == m_viewportDragStartGroupPosition && finalRotation == m_viewportDragStartGroupRotation) {
        refreshProperties();
        return;
    }

    const SceneDocument::Snapshot newSnapshot = m_scene.snapshot();
    m_scene.updateGroupTransform(groupId, m_viewportDragStartGroupPosition, m_viewportDragStartGroupRotation);
    const SceneDocument::Snapshot oldSnapshot = m_scene.snapshot();
    m_scene.restoreSnapshot(newSnapshot);

    auto *command = new UpdateGroupTransformCommand(&m_scene, oldSnapshot, newSnapshot, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        refreshProperties();
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::onViewportGroupRotationDragStarted(int groupId)
{
    onViewportGroupDragStarted(groupId);
}

void MainWindow::onViewportGroupRotated(int groupId, const QVector3D &deltaDegrees)
{
    if (!m_viewportGroupDragActive || m_viewportDragGroupId != groupId)
        return;

    m_scene.updateGroupTransform(groupId,
                                 m_viewportDragStartGroupPosition,
                                 normalizedRotation(m_viewportDragStartGroupRotation + deltaDegrees));
    m_viewport->invalidateCsgPreview();
    m_viewport->update();
    refreshProperties();
}

void MainWindow::onViewportGroupRotationDragFinished(int groupId)
{
    onViewportGroupDragFinished(groupId);
}

void MainWindow::onUseOpenGLToggled(bool checked)
{
    m_viewport->setRenderBackend(checked
                                     ? ViewportWidget::OpenGLRenderBackend
                                     : ViewportWidget::SoftwareRenderBackend);

    const bool usingOpenGL = m_viewport->renderBackend() == ViewportWidget::OpenGLRenderBackend;
    if (m_useOpenGLCheckBox->isChecked() != usingOpenGL) {
        m_useOpenGLCheckBox->blockSignals(true);
        m_useOpenGLCheckBox->setChecked(usingOpenGL);
        m_useOpenGLCheckBox->blockSignals(false);
    }

    m_viewport->update();
}

void MainWindow::onGraphicsTreeToolDropped(const QString &toolName, int parentGroupId, int insertIndex)
{
    SceneDocument::TreeNode::Operation operation;
    if (operationForTool(toolName, &operation)) {
        if (operation == SceneDocument::TreeNode::Module && parentGroupId == 0)
            return;

        auto *command = new AddGroupCommand(&m_scene, operation, parentGroupId, insertIndex, [this]() {
            refreshSceneViews();
        });

        if (!command->isValid()) {
            delete command;
            return;
        }

        m_undoStack->push(command);
        return;
    }

    if (toolName != "cube" && toolName != "sphere" && toolName != "cylinder")
        return;

    ShapeNode shape = makeShapeForTool(toolName, m_scene.shapeCount() + 1);
    auto *command = new AddShapeCommand(&m_scene, shape, parentGroupId, insertIndex, [this]() {
        refreshSceneViews();
    });

    m_undoStack->push(command);
}

void MainWindow::onGraphicsTreeNodeSelected(int nodeId)
{
    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node)
        return;

    if (node->type == SceneDocument::TreeNode::Primitive) {
        m_scene.setSelectedShapeId(node->shapeId);
        selectShapeInSceneTree(node->shapeId);
        m_viewport->setSelectedIndex(m_scene.selectedIndex());
        m_viewport->setSelectedGroupId(0);
    } else {
        m_scene.setSelectedShapeId(-1);
        selectTreeNodeInSceneTree(node->id);
        m_viewport->setSelectedGroupId(node->id);
    }

    if (m_sceneTreeGraphics)
        m_sceneTreeGraphics->setSelectedTreeNodeId(nodeId);

    refreshProperties();
    m_viewport->update();
}

void MainWindow::onGraphicsTreeNodeDeleteRequested(int nodeId)
{
    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node)
        return;

    if (node->type == SceneDocument::TreeNode::Primitive) {
        auto *command = new DeleteShapeCommand(&m_scene, node->shapeId, [this]() {
            refreshSceneViews();
        });

        if (!command->isValid()) {
            delete command;
            return;
        }

        m_undoStack->push(command);
        return;
    }

    if (node->id == m_scene.treeRoot().id)
        return;

    auto *command = new RemoveGroupCommand(&m_scene, node->id, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
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
    if (m_sceneTreeGraphics)
        m_sceneTreeGraphics->refresh();

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
    m_viewport->setSelectedGroupId(selectedDirectGroupId());
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

    if (m_sceneTreeGraphics)
        m_sceneTreeGraphics->setSelectedTreeNodeId(selectedItem ? selectedItem->data(0, TreeNodeIdRole).toInt() : 0);
    highlightOpenScadSelection();
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

    if (m_sceneTreeGraphics)
        m_sceneTreeGraphics->setSelectedTreeNodeId(selectedItem ? selectedItem->data(0, TreeNodeIdRole).toInt() : 0);
    highlightOpenScadSelection();
}

int MainWindow::selectedTreeNodeIdForCodeHighlight() const
{
    if (!m_shapeTree || !m_shapeTree->currentItem())
        return 0;

    return m_shapeTree->currentItem()->data(0, TreeNodeIdRole).toInt();
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

int MainWindow::selectedDirectGroupId() const
{
    if (!m_shapeTree || !m_shapeTree->currentItem())
        return 0;

    QTreeWidgetItem *item = m_shapeTree->currentItem();
    if (!item->data(0, GroupOperationRole).isValid())
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

void MainWindow::moveTreeNodeToGroup(int nodeId, int parentGroupId, int insertIndex)
{
    auto *command = new MoveTreeNodeCommand(&m_scene, nodeId, parentGroupId, insertIndex, [this]() {
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
    if (!shape)
        return;

    ShapeNode::BooleanMode effectiveMode = shape->booleanMode;
    findEffectiveBooleanMode(m_scene.treeRoot(), shapeId, ShapeNode::Add, &effectiveMode);
    if (effectiveMode == booleanMode && shape->booleanMode == booleanMode)
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
    const bool hasShapeSelection = m_scene.hasSelection();
    const int selectedGroupId = selectedDirectGroupId();
    const SceneDocument::TreeNode *selectedGroup = selectedGroupId > 0 ? m_scene.treeNodeById(selectedGroupId) : nullptr;
    const bool hasGroupSelection = selectedGroup && selectedGroup->type == SceneDocument::TreeNode::Group;
    const bool hasTransformSelection = hasShapeSelection || hasGroupSelection;

    QList<QDoubleSpinBox *> transformBoxes = {
        m_posX, m_posY, m_posZ,
        m_rotX, m_rotY, m_rotZ
    };
    QList<QDoubleSpinBox *> shapeBoxes = {
        m_sizeX, m_sizeY, m_sizeZ,
        m_radius, m_height
    };

    for (QDoubleSpinBox *box : transformBoxes)
        box->setEnabled(hasTransformSelection);

    for (QDoubleSpinBox *box : shapeBoxes)
        box->setEnabled(hasShapeSelection);

    m_deleteShapeButton->setEnabled(hasShapeSelection);
    m_deleteGroupButton->setEnabled(selectedGroupId > 0 && selectedGroupId != m_scene.treeRoot().id);
    m_booleanMode->setEnabled(hasShapeSelection);

    if (!hasTransformSelection)
        return;

    m_updatingProperties = true;

    QList<QDoubleSpinBox *> allBoxes = transformBoxes + shapeBoxes;
    for (QDoubleSpinBox *box : allBoxes)
        box->blockSignals(true);
    m_booleanMode->blockSignals(true);

    if (hasGroupSelection) {
        m_posX->setValue(selectedGroup->position.x());
        m_posY->setValue(selectedGroup->position.y());
        m_posZ->setValue(selectedGroup->position.z());

        m_rotX->setValue(selectedGroup->rotation.x());
        m_rotY->setValue(selectedGroup->rotation.y());
        m_rotZ->setValue(selectedGroup->rotation.z());

        m_sizeX->setValue(0.0);
        m_sizeY->setValue(0.0);
        m_sizeZ->setValue(0.0);
        m_radius->setValue(0.1);
        m_height->setValue(0.1);
        m_booleanMode->setCurrentIndex(-1);

        for (QDoubleSpinBox *box : allBoxes)
            box->blockSignals(false);
        m_booleanMode->blockSignals(false);

        m_updatingProperties = false;
        return;
    }

    const ShapeNode *s = m_scene.selectedShape();
    if (!s) {
        for (QDoubleSpinBox *box : allBoxes)
            box->blockSignals(false);
        m_booleanMode->blockSignals(false);
        m_updatingProperties = false;
        return;
    }

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
    ShapeNode::BooleanMode effectiveMode = s->booleanMode;
    findEffectiveBooleanMode(m_scene.treeRoot(), s->id, ShapeNode::Add, &effectiveMode);
    m_booleanMode->setCurrentIndex(m_booleanMode->findData(effectiveMode));

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
    const QString code = OpenScadGenerator::generateWithSourceMap(m_scene, &m_openScadSourceRanges);
    m_codeEditor->setPlainText(code);
    highlightOpenScadSelection();
    writeOpenScadPreview(false);
}

void MainWindow::highlightOpenScadSelection()
{
    if (!m_codeEditor)
        return;

    const int selectedTreeNodeId = selectedTreeNodeIdForCodeHighlight();
    QTextEdit::ExtraSelection selection;
    bool hasSelection = false;

    for (const OpenScadGenerator::SourceRange &range : m_openScadSourceRanges) {
        if (range.treeNodeId != selectedTreeNodeId || range.length <= 0)
            continue;

        QTextCursor cursor(m_codeEditor->document());
        cursor.setPosition(range.start);
        cursor.setPosition(range.start + range.length, QTextCursor::KeepAnchor);

        QTextCharFormat format;
        format.setBackground(QColor(255, 203, 87, 95));

        selection.cursor = cursor;
        selection.format = format;
        hasSelection = true;
        break;
    }

    m_codeEditor->setExtraSelections(hasSelection ? QList<QTextEdit::ExtraSelection>{selection}
                                                  : QList<QTextEdit::ExtraSelection>{});
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
