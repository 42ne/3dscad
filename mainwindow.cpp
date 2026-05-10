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
static constexpr int GroupBooleanModeRole = Qt::UserRole + 1;

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

    std::function<void(int, ShapeNode::BooleanMode)> onShapeDroppedOnGroup;

protected:
    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (dropTarget(event->pos()).shapeId >= 0)
            event->acceptProposedAction();
        else
            event->ignore();
    }

    void dropEvent(QDropEvent *event) override
    {
        const DropTarget target = dropTarget(event->pos());
        if (target.shapeId < 0) {
            event->ignore();
            return;
        }

        if (onShapeDroppedOnGroup)
            onShapeDroppedOnGroup(target.shapeId, target.booleanMode);

        event->acceptProposedAction();
    }

private:
    struct DropTarget
    {
        int shapeId = -1;
        ShapeNode::BooleanMode booleanMode = ShapeNode::Add;
    };

    DropTarget dropTarget(const QPoint &position) const
    {
        DropTarget target;
        const QList<QTreeWidgetItem *> selected = selectedItems();
        if (selected.size() != 1)
            return target;

        const int shapeId = selected.first()->data(0, ShapeIdRole).toInt();
        if (shapeId < 0)
            return target;

        QTreeWidgetItem *targetItem = itemAt(position);
        if (!targetItem)
            return target;

        if (targetItem->data(0, ShapeIdRole).toInt() >= 0)
            targetItem = targetItem->parent();

        if (!targetItem)
            return target;

        const QVariant modeData = targetItem->data(0, GroupBooleanModeRole);
        if (!modeData.isValid())
            return target;

        target.shapeId = shapeId;
        target.booleanMode = static_cast<ShapeNode::BooleanMode>(modeData.toInt());
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

static ShapeNode::BooleanMode booleanModeForGroup(SceneDocument::TreeNode::Operation operation)
{
    if (operation == SceneDocument::TreeNode::Difference)
        return ShapeNode::Subtract;
    if (operation == SceneDocument::TreeNode::Intersection)
        return ShapeNode::Intersect;
    return ShapeNode::Add;
}

static void markGroupItem(QTreeWidgetItem *item, SceneDocument::TreeNode::Operation operation)
{
    QFont font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    item->setData(0, ShapeIdRole, -1);
    item->setData(0, GroupBooleanModeRole, booleanModeForGroup(operation));
    item->setForeground(0, QColor(82, 82, 82));
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDropEnabled);
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
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        return item;
    }

    auto *groupItem = new QTreeWidgetItem(parent);
    groupItem->setText(0, booleanGroupLabel(node.operation));
    markGroupItem(groupItem, node.operation);

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
    m_deleteShapeButton = new QPushButton("Delete selected");
    m_deleteShapeButton->setEnabled(false);

    auto *shapeTree = new SceneTreeWidget;
    shapeTree->onShapeDroppedOnGroup = [this](int shapeId, ShapeNode::BooleanMode booleanMode) {
        changeShapeBooleanMode(shapeId, booleanMode);
    };
    m_shapeTree = shapeTree;
    m_shapeTree->setHeaderHidden(true);

    leftLayout->addWidget(addCubeButton);
    leftLayout->addWidget(addSphereButton);
    leftLayout->addWidget(addCylinderButton);
    leftLayout->addWidget(m_deleteShapeButton);
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
    connect(m_deleteShapeButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedShape);
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

    m_shapeTree->blockSignals(true);
    m_shapeTree->clear();

    appendBooleanTreeItem(m_shapeTree->invisibleRootItem(), m_scene.treeRoot(), m_scene);
    m_shapeTree->expandAll();

    m_shapeTree->blockSignals(false);
    selectShapeInSceneTree(selectedShapeId);

    refreshOpenScadCode();
    m_viewport->update();
    refreshCsgStatus();
}

void MainWindow::refreshSceneViews()
{
    refreshShapeList();
    selectShapeInSceneTree(m_scene.selectedShapeId());

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
