#include "mainwindow.h"

#include <QApplication>
#include <QDockWidget>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QOpenGLFunctions>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QGroupBox>
#include <QSplitter>
#include <QPainter>
#include <QSurfaceFormat>

// ---------------- ViewportWidget ----------------

ViewportWidget::ViewportWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(500, 400);
}

void ViewportWidget::setShapes(const QVector<ShapeNode> *shapes)
{
    m_shapes = shapes;
    update();
}

void ViewportWidget::setSelectedIndex(int index)
{
    m_selectedIndex = index;
    update();
}

void ViewportWidget::initializeGL()
{
    glClearColor(0.12f, 0.13f, 0.15f, 1.0f);
}

void ViewportWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void ViewportWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.fillRect(rect(), QColor(30, 32, 36));

    painter.setPen(QColor(80, 80, 80));
    for (int x = 0; x < width(); x += 40)
        painter.drawLine(x, 0, x, height());

    for (int y = 0; y < height(); y += 40)
        painter.drawLine(0, y, width(), y);

    painter.setPen(Qt::NoPen);

    if (m_shapes) {
        for (int i = 0; i < m_shapes->size(); ++i) {
            const ShapeNode &s = m_shapes->at(i);

            QPointF center(
                width() / 2.0 + s.position.x() * 2.0,
                height() / 2.0 - s.position.y() * 2.0
                );

            QColor color = (i == m_selectedIndex)
                               ? QColor(255, 180, 60)
                               : QColor(80, 160, 255);

            painter.setBrush(color);

            if (s.type == ShapeNode::Cube) {
                QRectF r(
                    center.x() - s.size.x(),
                    center.y() - s.size.y(),
                    s.size.x() * 2.0,
                    s.size.y() * 2.0
                    );
                painter.drawRoundedRect(r, 4, 4);
            } else if (s.type == ShapeNode::Sphere) {
                painter.drawEllipse(center, s.radius * 2.0, s.radius * 2.0);
            } else if (s.type == ShapeNode::Cylinder) {
                QRectF r(
                    center.x() - s.radius * 2.0,
                    center.y() - s.height,
                    s.radius * 4.0,
                    s.height * 2.0
                    );
                painter.drawRoundedRect(r, 18, 18);
            }
        }
    }

    painter.setPen(QColor(220, 220, 220));
    painter.drawText(12, 24, "Viewport placeholder: тут далі буде 3D gizmo / camera / mesh preview");

    painter.end();
}

// ---------------- MainWindow ----------------

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    refreshOpenScadCode();
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

    m_viewport = new ViewportWidget;
    m_viewport->setShapes(&m_shapes);

    m_codeEditor = new QTextEdit;
    m_codeEditor->setReadOnly(false);
    m_codeEditor->setMinimumHeight(180);
    m_codeEditor->setFontFamily("Consolas");

    auto *mainSplitter = new QSplitter(Qt::Vertical);
    mainSplitter->addWidget(m_viewport);
    mainSplitter->addWidget(m_codeEditor);
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

    m_shapeList = new QListWidget;

    leftLayout->addWidget(addCubeButton);
    leftLayout->addWidget(addSphereButton);
    leftLayout->addWidget(addCylinderButton);
    leftLayout->addWidget(new QLabel("Scene tree:"));
    leftLayout->addWidget(m_shapeList);

    leftDock->setWidget(leftPanel);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);

    connect(addCubeButton, &QPushButton::clicked, this, &MainWindow::addCube);
    connect(addSphereButton, &QPushButton::clicked, this, &MainWindow::addSphere);
    connect(addCylinderButton, &QPushButton::clicked, this, &MainWindow::addCylinder);
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
    s.name = QString("Cube %1").arg(m_shapes.size() + 1);
    s.size = QVector3D(20, 20, 20);

    m_shapes.append(s);
    refreshShapeList();
    m_shapeList->setCurrentRow(m_shapes.size() - 1);
}

void MainWindow::addSphere()
{
    ShapeNode s;
    s.type = ShapeNode::Sphere;
    s.name = QString("Sphere %1").arg(m_shapes.size() + 1);
    s.radius = 10;

    m_shapes.append(s);
    refreshShapeList();
    m_shapeList->setCurrentRow(m_shapes.size() - 1);
}

void MainWindow::addCylinder()
{
    ShapeNode s;
    s.type = ShapeNode::Cylinder;
    s.name = QString("Cylinder %1").arg(m_shapes.size() + 1);
    s.radius = 10;
    s.height = 30;

    m_shapes.append(s);
    refreshShapeList();
    m_shapeList->setCurrentRow(m_shapes.size() - 1);
}

void MainWindow::onSelectionChanged(int row)
{
    m_selectedIndex = row;
    m_viewport->setSelectedIndex(row);
    refreshProperties();
}

void MainWindow::onPropertyChanged()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= m_shapes.size())
        return;

    ShapeNode &s = m_shapes[m_selectedIndex];

    s.position = QVector3D(m_posX->value(), m_posY->value(), m_posZ->value());
    s.rotation = QVector3D(m_rotX->value(), m_rotY->value(), m_rotZ->value());
    s.size = QVector3D(m_sizeX->value(), m_sizeY->value(), m_sizeZ->value());

    s.radius = m_radius->value();
    s.height = m_height->value();

    refreshOpenScadCode();
    m_viewport->update();
}

void MainWindow::refreshShapeList()
{
    m_shapeList->clear();

    for (const ShapeNode &s : m_shapes) {
        m_shapeList->addItem(s.name);
    }

    refreshOpenScadCode();
    m_viewport->update();
}

void MainWindow::refreshProperties()
{
    bool hasSelection = m_selectedIndex >= 0 && m_selectedIndex < m_shapes.size();

    QList<QDoubleSpinBox *> boxes = {
        m_posX, m_posY, m_posZ,
        m_rotX, m_rotY, m_rotZ,
        m_sizeX, m_sizeY, m_sizeZ,
        m_radius, m_height
    };

    for (QDoubleSpinBox *box : boxes)
        box->setEnabled(hasSelection);

    if (!hasSelection)
        return;

    const ShapeNode &s = m_shapes[m_selectedIndex];

    QList<QDoubleSpinBox *> allBoxes = boxes;
    for (QDoubleSpinBox *box : allBoxes)
        box->blockSignals(true);

    m_posX->setValue(s.position.x());
    m_posY->setValue(s.position.y());
    m_posZ->setValue(s.position.z());

    m_rotX->setValue(s.rotation.x());
    m_rotY->setValue(s.rotation.y());
    m_rotZ->setValue(s.rotation.z());

    m_sizeX->setValue(s.size.x());
    m_sizeY->setValue(s.size.y());
    m_sizeZ->setValue(s.size.z());

    m_radius->setValue(s.radius);
    m_height->setValue(s.height);

    for (QDoubleSpinBox *box : allBoxes)
        box->blockSignals(false);

    bool cube = s.type == ShapeNode::Cube;
    bool sphere = s.type == ShapeNode::Sphere;
    bool cylinder = s.type == ShapeNode::Cylinder;

    m_sizeX->setEnabled(cube);
    m_sizeY->setEnabled(cube);
    m_sizeZ->setEnabled(cube);

    m_radius->setEnabled(sphere || cylinder);
    m_height->setEnabled(cylinder);
}

void MainWindow::refreshOpenScadCode()
{
    m_codeEditor->setPlainText(generateOpenScadCode());
}

QString MainWindow::generateOpenScadCode() const
{
    QString code;

    code += "// Generated by OpenSCAD Visual Editor Prototype\n";
    code += "// UI -> OpenSCAD code\n\n";

    if (m_shapes.isEmpty()) {
        code += "// Add shapes from the left panel.\n";
        return code;
    }

    code += "union() {\n";

    for (const ShapeNode &s : m_shapes) {
        QString shapeCode = shapeToOpenScad(s);
        const QStringList lines = shapeCode.split('\n');

        for (const QString &line : lines) {
            if (!line.trimmed().isEmpty())
                code += "    " + line + "\n";
        }
    }

    code += "}\n";

    return code;
}

QString MainWindow::shapeToOpenScad(const ShapeNode &s) const
{
    QString code;

    code += QString("translate([%1, %2, %3])\n")
                .arg(s.position.x())
                .arg(s.position.y())
                .arg(s.position.z());

    code += QString("rotate([%1, %2, %3])\n")
                .arg(s.rotation.x())
                .arg(s.rotation.y())
                .arg(s.rotation.z());

    if (s.type == ShapeNode::Cube) {
        code += QString("cube([%1, %2, %3], center=true);\n")
        .arg(s.size.x())
            .arg(s.size.y())
            .arg(s.size.z());
    } else if (s.type == ShapeNode::Sphere) {
        code += QString("sphere(r=%1);\n")
        .arg(s.radius);
    } else if (s.type == ShapeNode::Cylinder) {
        code += QString("cylinder(h=%1, r=%2, center=true);\n")
        .arg(s.height)
            .arg(s.radius);
    }

    return code;
}
