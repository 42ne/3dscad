#include "mainwindow.h"
#include "openscadgenerator.h"
#include "openscadparser.h"
#include "scenecommands.h"
#include "viewportwidget.h"

#include <QAction>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTextEdit>
#include <QUndoStack>
#include <QVBoxLayout>

// ---------------- MainWindow ----------------

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    refreshOpenScadCode();
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
    m_viewport->setShapes(&m_scene.shapes());

    m_codeEditor = new QTextEdit;
    m_codeEditor->setReadOnly(false);
    m_codeEditor->setMinimumHeight(180);
    m_codeEditor->setFontFamily("Consolas");

    m_applyCodeButton = new QPushButton("Apply code");

    auto *codePanel = new QWidget;
    auto *codeLayout = new QVBoxLayout(codePanel);
    codeLayout->setContentsMargins(0, 0, 0, 0);
    codeLayout->addWidget(m_codeEditor);
    codeLayout->addWidget(m_applyCodeButton);

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

    m_shapeList = new QListWidget;

    leftLayout->addWidget(addCubeButton);
    leftLayout->addWidget(addSphereButton);
    leftLayout->addWidget(addCylinderButton);
    leftLayout->addWidget(m_deleteShapeButton);
    leftLayout->addWidget(new QLabel("Scene tree:"));
    leftLayout->addWidget(m_shapeList);

    leftDock->setWidget(leftPanel);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);

    connect(addCubeButton, &QPushButton::clicked, this, &MainWindow::addCube);
    connect(addSphereButton, &QPushButton::clicked, this, &MainWindow::addSphere);
    connect(addCylinderButton, &QPushButton::clicked, this, &MainWindow::addCylinder);
    connect(m_deleteShapeButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedShape);
    connect(m_applyCodeButton, &QPushButton::clicked, this, &MainWindow::applyOpenScadCode);
    connect(m_shapeList, &QListWidget::currentRowChanged, this, &MainWindow::onSelectionChanged);

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

void MainWindow::onSelectionChanged(int row)
{
    QListWidgetItem *item = m_shapeList->item(row);
    m_scene.setSelectedShapeId(item ? item->data(Qt::UserRole).toInt() : -1);
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

    auto *command = new UpdateShapeCommand(&m_scene, *selectedShape, updatedShape, [this]() {
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
    m_shapeList->blockSignals(true);
    m_shapeList->clear();

    for (const ShapeNode &s : m_scene.shapes()) {
        auto *item = new QListWidgetItem(s.name);
        item->setData(Qt::UserRole, s.id);
        m_shapeList->addItem(item);
    }
    m_shapeList->blockSignals(false);

    refreshOpenScadCode();
    m_viewport->update();
}

void MainWindow::refreshSceneViews()
{
    refreshShapeList();

    m_shapeList->blockSignals(true);
    m_shapeList->setCurrentRow(m_scene.selectedIndex());
    m_shapeList->blockSignals(false);

    m_viewport->setSelectedIndex(m_scene.selectedIndex());
    refreshProperties();
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

    if (!hasSelection)
        return;

    const ShapeNode *s = m_scene.selectedShape();
    if (!s)
        return;

    m_updatingProperties = true;

    QList<QDoubleSpinBox *> allBoxes = boxes;
    for (QDoubleSpinBox *box : allBoxes)
        box->blockSignals(true);

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

    for (QDoubleSpinBox *box : allBoxes)
        box->blockSignals(false);

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
    m_codeEditor->setPlainText(OpenScadGenerator::generate(m_scene));
}
