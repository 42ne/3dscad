#include "mainwindow.h"
#include "csgevaluator.h"
#include "expression.h"
#include "openscadgenerator.h"
#include "openscadparser.h"
#include "scenecommands.h"
#include "scenetreegraphicshelpers.h"
#include "scenetreegraphicswidget.h"
#include "viewportwidget.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPoint>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QStringList>
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
static constexpr int ModuleParameterInsertSentinel = -100000;

static bool decodeModuleParameterInsertIndex(int *insertIndex)
{
    if (!insertIndex || *insertIndex > ModuleParameterInsertSentinel)
        return false;

    *insertIndex = -ModuleParameterInsertSentinel - *insertIndex;
    return true;
}

static bool isStandaloneNumericToken(const QString &expression, int start, int length)
{
    if (start < 0 || length <= 0 || start + length > expression.size())
        return false;

    return expression.mid(start, length) == expression.trimmed();
}

static QString adjustedNumericToken(const QString &expression,
                                    int start,
                                    int length,
                                    qreal delta,
                                    qreal step,
                                    qreal minimumValue,
                                    bool clampMagnitude)
{
    const QString numberText = expression.mid(start, length);
    bool ok = false;
    const qreal value = numberText.toDouble(&ok);
    if (!ok)
        return QString();

    const int decimalPoint = numberText.indexOf(QLatin1Char('.'));
    const int precision = decimalPoint >= 0 ? qMin(3, numberText.size() - decimalPoint - 1) : 0;
    const qreal adjusted = value + delta * step;
    const qreal newValue = clampMagnitude ? qMax(minimumValue, adjusted) : adjusted;
    QString replacement = QString::number(newValue, 'f', precision);
    if (precision == 0 && replacement == QStringLiteral("-0"))
        replacement = QStringLiteral("0");
    return replacement;
}

static QStringList splitAtTopLevelCommas(const QString &text)
{
    QStringList result;
    int depth = 0;
    int start = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text[i];
        if (ch == QLatin1Char('(') || ch == QLatin1Char('['))
            ++depth;
        else if (ch == QLatin1Char(')') || ch == QLatin1Char(']'))
            --depth;
        else if (ch == QLatin1Char(',') && depth == 0) {
            const QString part = text.mid(start, i - start).trimmed();
            if (!part.isEmpty())
                result.append(part);
            start = i + 1;
        }
    }
    const QString tail = text.mid(start).trimmed();
    if (!tail.isEmpty())
        result.append(tail);
    return result;
}

static QHash<QString, QString> parseNamedArgumentExpressions(const QString &arguments)
{
    QHash<QString, QString> result;
    for (const QString &part : splitAtTopLevelCommas(arguments)) {
        const int equal = part.indexOf(QLatin1Char('='));
        if (equal <= 0)
            continue;

        const QString name = part.left(equal).trimmed();
        const QString expression = part.mid(equal + 1).trimmed();
        if (!name.isEmpty() && !expression.isEmpty())
            result[name] = expression;
    }
    return result;
}

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
    if (toolName == "translate") {
        *operation = SceneDocument::TreeNode::Translate;
        return true;
    }
    if (toolName == "rotate") {
        *operation = SceneDocument::TreeNode::Rotate;
        return true;
    }
    if (toolName == "scale") {
        *operation = SceneDocument::TreeNode::Scale;
        return true;
    }
    if (toolName == "for") {
        *operation = SceneDocument::TreeNode::For;
        return true;
    }

    return false;
}

static bool isVariableTool(const QString &toolName)
{
    return toolName == QStringLiteral("var") || toolName == QStringLiteral("variable");
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

static QString booleanGroupLabel(SceneDocument::TreeNode::Operation operation,
                                 const QString &moduleName = QString())
{
    if (operation == SceneDocument::TreeNode::Module)
        return "module " + (moduleName.trimmed().isEmpty() ? QStringLiteral("scene_model") : moduleName.trimmed());
    if (operation == SceneDocument::TreeNode::Difference)
        return "difference()";
    if (operation == SceneDocument::TreeNode::Intersection)
        return "intersection()";
    if (operation == SceneDocument::TreeNode::Translate)
        return "translate";
    if (operation == SceneDocument::TreeNode::Rotate)
        return "rotate";
    if (operation == SceneDocument::TreeNode::Scale)
        return "scale";
    if (operation == SceneDocument::TreeNode::For)
        return "for";
    if (operation == SceneDocument::TreeNode::Scene)
        return "scene";
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

    if (node.type == SceneDocument::TreeNode::Variable) {
        auto *item = new QTreeWidgetItem(parent);
        const QString prefix = node.isParameter ? QStringLiteral("param ") : QString();
        item->setText(0, prefix + QStringLiteral("%1 = %2").arg(node.variableName, node.variableExpression));
        item->setData(0, ShapeIdRole, -1);
        item->setData(0, TreeNodeIdRole, node.id);
        item->setForeground(0, node.isParameter ? QColor(60, 110, 170) : QColor(122, 92, 36));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        return item;
    }

    auto *groupItem = new QTreeWidgetItem(parent);
    groupItem->setText(0, booleanGroupLabel(node.operation, node.moduleName) + roleSuffix);
    markGroupItem(groupItem, node);
    if (roleColor.isValid())
        groupItem->setForeground(0, roleColor);

    for (int i = 0; i < node.children.size(); ++i)
        appendBooleanTreeItem(groupItem, node.children[i], scene, node.operation, i);

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

    auto *fileMenu = menuBar()->addMenu("File");
    auto *examplesMenu = fileMenu->addMenu("Open Example");
    populateExamplesMenu(examplesMenu);

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

    m_parseErrorLabel = new QLabel;
    m_parseErrorLabel->setWordWrap(true);
    m_parseErrorLabel->setContentsMargins(4, 2, 4, 2);
    m_parseErrorLabel->hide();

    auto *codePanel = new QWidget;
    auto *codeLayout = new QVBoxLayout(codePanel);
    codeLayout->setContentsMargins(0, 0, 0, 0);
    codeLayout->addWidget(m_codeEditor);
    codeLayout->addWidget(m_applyCodeButton);
    codeLayout->addWidget(m_parseErrorLabel);
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
    m_sceneTreeGraphics->setModuleCallDroppedCallback([this](int moduleGroupId, int parentGroupId, int insertIndex) {
        onGraphicsTreeModuleCallDropped(moduleGroupId, parentGroupId, insertIndex);
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
    m_sceneTreeGraphics->setTransformValueAdjustedCallback([this](int groupId, int axis, int numberStart, int numberLength, qreal delta) {
        onGraphicsTreeTransformValueAdjusted(groupId, axis, numberStart, numberLength, delta);
    });
    m_sceneTreeGraphics->setTransformControlHoveredCallback([this](int groupId, SceneDocument::TreeNode::Operation operation, int axis) {
        onGraphicsTreeTransformControlHovered(groupId, operation, axis);
    });
    m_sceneTreeGraphics->setShapeParameterAdjustedCallback([this](int nodeId, int paramIndex, int numberStart, int numberLength, qreal delta) {
        onGraphicsTreeShapeParameterAdjusted(nodeId, paramIndex, numberStart, numberLength, delta);
    });
    m_sceneTreeGraphics->setShapeParameterHoveredCallback([this](int shapeId, int parameter) {
        onGraphicsTreeShapeParameterHovered(shapeId, parameter);
    });
    m_sceneTreeGraphics->setVariableNumberAdjustedCallback([this](int nodeId, int start, int length, qreal delta) {
        onGraphicsTreeVariableNumberAdjusted(nodeId, start, length, delta);
    });
    m_sceneTreeGraphics->setModuleCallArgumentAdjustedCallback([this](int moduleCallId, int parameterVariableId, int start, int length, qreal delta) {
        onGraphicsTreeModuleCallArgumentAdjusted(moduleCallId, parameterVariableId, start, length, delta);
    });
    m_sceneTreeGraphics->setForLoopRangeAdjustedCallback([this](int nodeId, int start, int length, qreal delta) {
        onGraphicsTreeForLoopRangeAdjusted(nodeId, start, length, delta);
    });
    m_sceneTreeGraphics->setCtrlReleasedCallback([this]() {
        m_ctrlHighlight.active = false;
        highlightOpenScadSelection();
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

    leftLayout->addWidget(treeSplitter, 1);
    m_csgStatusLabel = new QLabel;
    m_csgStatusLabel->setWordWrap(true);
    leftLayout->addWidget(m_csgStatusLabel);

    leftDock->setWidget(leftPanel);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);

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
    connect(m_viewport, &ViewportWidget::emptyClicked, this, &MainWindow::clearSelection);
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
    if (m_shapeTree && m_shapeTree->currentItem()) {
        const int nodeId = m_shapeTree->currentItem()->data(0, TreeNodeIdRole).toInt();
        const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
        if (node && node->type == SceneDocument::TreeNode::Variable) {
            auto *command = new RemoveVariableCommand(&m_scene, nodeId, [this]() {
                refreshSceneViews();
            });

            if (!command->isValid()) {
                delete command;
                return;
            }

            m_undoStack->push(command);
            return;
        }
    }

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
    SceneDocument::Snapshot snapshot;
    QString errorMessage;
    int errorLine = -1;

    if (!OpenScadParser::parseScene(m_codeEditor->toPlainText(), &snapshot, &errorMessage, &errorLine)) {
        m_parseErrorLabel->setText(QString("<span style='color:#d04040;'>%1</span>")
                                       .arg(errorMessage.toHtmlEscaped()));
        m_parseErrorLabel->show();

        if (errorLine > 0) {
            QTextCursor cursor(m_codeEditor->document());
            cursor.movePosition(QTextCursor::Start);
            cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, errorLine - 1);
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            m_codeEditor->setTextCursor(cursor);
            m_codeEditor->ensureCursorVisible();
        }
        return;
    }

    m_parseErrorLabel->hide();

    auto *command = new ReplaceSceneCommand(&m_scene, snapshot, [this]() {
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
    else
        clearSelection();

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
    m_viewportDragStartGroupScale = group->scale;
    m_viewportGroupDragActive = true;
}

void MainWindow::onViewportGroupDragged(int groupId, const QVector3D &delta)
{
    if (!m_viewportGroupDragActive || m_viewportDragGroupId != groupId)
        return;

    m_scene.updateGroupTransform(groupId,
                                 m_viewportDragStartGroupPosition + delta,
                                 m_viewportDragStartGroupRotation,
                                 m_viewportDragStartGroupScale);
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
    const QVector3D finalScale = group->scale;
    if (finalPosition == m_viewportDragStartGroupPosition
        && finalRotation == m_viewportDragStartGroupRotation
        && finalScale == m_viewportDragStartGroupScale) {
        refreshProperties();
        return;
    }

    const SceneDocument::Snapshot newSnapshot = m_scene.snapshot();
    m_scene.updateGroupTransform(groupId,
                                 m_viewportDragStartGroupPosition,
                                 m_viewportDragStartGroupRotation,
                                 m_viewportDragStartGroupScale);
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
                                 normalizedRotation(m_viewportDragStartGroupRotation + deltaDegrees),
                                 m_viewportDragStartGroupScale);
    m_viewport->invalidateCsgPreview();
    m_viewport->update();
    refreshProperties();
}

void MainWindow::onViewportGroupRotationDragFinished(int groupId)
{
    onViewportGroupDragFinished(groupId);
}

void MainWindow::onGraphicsTreeToolDropped(const QString &toolName, int parentGroupId, int insertIndex)
{
    const bool moduleParameterZone = decodeModuleParameterInsertIndex(&insertIndex);

    if (isVariableTool(toolName)) {
        const int rootId = m_scene.treeRoot().id;
        const int sceneId = m_scene.sceneNodeId();
        // Variable dropped inside a Module node → becomes a module parameter.
        const SceneDocument::TreeNode *parentNode =
            parentGroupId > 0 ? m_scene.treeNodeById(parentGroupId) : nullptr;
        const bool inModule = parentNode
                              && parentNode->type == SceneDocument::TreeNode::Group
                              && parentNode->operation == SceneDocument::TreeNode::Module;
        if (parentGroupId > 0 && !inModule && parentGroupId != rootId && parentGroupId != sceneId)
            return;

        if (inModule) {
            // Add into the module parameter strip or regular module body.
            struct AddModuleParamCommand : public QUndoCommand {
                SceneDocument *scene; int moduleId; int insertIdx; bool parameter; std::function<void()> refresh;
                int addedId = 0;
                AddModuleParamCommand(SceneDocument *s, int mid, int idx, bool isParameter, std::function<void()> r)
                    : scene(s), moduleId(mid), insertIdx(idx), parameter(isParameter), refresh(r) {}
                void redo() override { addedId = scene->addVariableToModule(moduleId, parameter, insertIdx); if (refresh) refresh(); }
                void undo() override { if (addedId > 0) { scene->removeVariableById(addedId); if (refresh) refresh(); } }
                bool isValid() const { return scene && moduleId > 0; }
            };
            auto *cmd = new AddModuleParamCommand(&m_scene, parentGroupId, insertIndex, moduleParameterZone, [this]() { refreshSceneViews(); });
            if (!cmd->isValid()) { delete cmd; return; }
            m_undoStack->push(cmd);
            return;
        }

        auto *command = new AddVariableCommand(&m_scene, insertIndex, [this]() {
            refreshSceneViews();
        });

        if (!command->isValid()) {
            delete command;
            return;
        }

        m_undoStack->push(command);
        return;
    }

    SceneDocument::TreeNode::Operation operation;
    if (operationForTool(toolName, &operation)) {
        if (operation == SceneDocument::TreeNode::Module) {
            if (parentGroupId > 0 && parentGroupId != m_scene.treeRoot().id)
                return;
            parentGroupId = 0;
        } else if (parentGroupId <= 0 || parentGroupId == m_scene.treeRoot().id) {
            parentGroupId = m_scene.sceneNodeId();
        }

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

void MainWindow::onGraphicsTreeModuleCallDropped(int moduleGroupId, int parentGroupId, int insertIndex)
{
    auto *command = new AddModuleCallCommand(&m_scene, moduleGroupId, parentGroupId, insertIndex, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::onGraphicsTreeNodeSelected(int nodeId)
{
    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node) {
        clearSelection();
        return;
    }

    if (node->type == SceneDocument::TreeNode::Primitive) {
        m_scene.setSelectedShapeId(node->shapeId);
        selectShapeInSceneTree(node->shapeId);
        m_viewport->setSelectedIndex(m_scene.selectedIndex());
        m_viewport->setSelectedGroupId(0);
    } else if (node->type == SceneDocument::TreeNode::ModuleCall) {
        m_scene.setSelectedShapeId(-1);
        selectTreeNodeInSceneTree(node->id);
        m_viewport->setSelectedIndex(-1);
        m_viewport->setSelectedGroupId(node->id);
    } else if (node->type == SceneDocument::TreeNode::Variable) {
        m_scene.setSelectedShapeId(-1);
        selectTreeNodeInSceneTree(node->id);
        m_viewport->setSelectedIndex(-1);
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

    if (node->type == SceneDocument::TreeNode::ModuleCall) {
        auto *command = new RemoveModuleCallCommand(&m_scene, node->id, [this]() {
            refreshSceneViews();
        });

        if (!command->isValid()) {
            delete command;
            return;
        }

        m_undoStack->push(command);
        return;
    }

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

    if (node->type == SceneDocument::TreeNode::Variable) {
        auto *command = new RemoveVariableCommand(&m_scene, node->id, [this]() {
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

void MainWindow::onGraphicsTreeTransformValueAdjusted(int groupId, int axis, int numberStart, int numberLength, qreal delta)
{
    if (axis < 0 || axis > 2 || qFuzzyIsNull(delta))
        return;

    const SceneDocument::TreeNode *group = m_scene.treeNodeById(groupId);
    if (!group || group->type != SceneDocument::TreeNode::Group)
        return;

    // Get effective expression for this axis
    const QString currentExpr = SceneTreeGraphics::transformAxisExpression(*group, axis);

    QStringList newExpressions = group->transformExpressions;
    while (newExpressions.size() < 3)
        newExpressions.append(QString());

    QVector3D position = group->position;
    QVector3D rotation = group->rotation;
    QVector3D scale = group->scale;

    if (numberStart >= 0 && numberLength > 0 && numberStart + numberLength <= currentExpr.size()) {
        const bool isScale = group->operation == SceneDocument::TreeNode::Scale;
        const qreal minVal = isScale ? 0.01 : -1e9;
        const QString numberText = currentExpr.mid(numberStart, numberLength);
        const int decimalPoint = numberText.indexOf(QLatin1Char('.'));
        const int precision = decimalPoint >= 0 ? qMin(3, numberText.size() - decimalPoint - 1) : 0;
        const qreal step = precision > 0 ? 0.1 : 1.0;
        const bool standaloneNumber = isStandaloneNumericToken(currentExpr, numberStart, numberLength);
        const qreal tokenMin = isScale ? 0.01 : (standaloneNumber ? -1e9 : 0.0);
        QString replacement = adjustedNumericToken(currentExpr, numberStart, numberLength, delta, step, tokenMin, isScale || !standaloneNumber);
        if (replacement.isEmpty())
            return;
        if (isScale && precision == 0 && !replacement.contains(QLatin1Char('.')))
            replacement = QString::number(replacement.toDouble(), 'f', 1);
        const QString newExpr = currentExpr.left(numberStart) + replacement + currentExpr.mid(numberStart + numberLength);
        newExpressions[axis] = newExpr;

        if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
            // Generated: translate([X, Y, Z]) — build prefix up to the Nth value so
            // the search is unique even when all three axes have the same numeric value.
            QString contextPrefix = QStringLiteral("[");
            for (int i = 0; i < axis; ++i)
                contextPrefix += SceneTreeGraphics::transformAxisExpression(*group, i) + QStringLiteral(", ");
            m_ctrlHighlight.active        = true;
            m_ctrlHighlight.nodeId        = groupId;
            m_ctrlHighlight.contextPrefix = contextPrefix;
            m_ctrlHighlight.expression    = newExpr;
            m_ctrlHighlight.numberStart   = numberStart;
            m_ctrlHighlight.numberLength  = int(replacement.size());
        } else {
            m_ctrlHighlight.active = false;
        }

        // Evaluate new expression to get numeric value
        QHash<QString, qreal> varValues;
        for (const SceneDocument::TreeNode &child : m_scene.treeRoot().children) {
            if (child.type == SceneDocument::TreeNode::Variable)
                varValues[child.variableName] = child.variableValue;
        }
        qreal newNumeric = replacement.toDouble();
        ExpressionSyntax::evaluate(newExpr, varValues, &newNumeric);
        newNumeric = qMax(minVal, newNumeric);

        if (axis == 0) {
            if (group->operation == SceneDocument::TreeNode::Translate) position.setX(static_cast<float>(newNumeric));
            else if (group->operation == SceneDocument::TreeNode::Rotate) rotation.setX(static_cast<float>(newNumeric));
            else scale.setX(static_cast<float>(newNumeric));
        } else if (axis == 1) {
            if (group->operation == SceneDocument::TreeNode::Translate) position.setY(static_cast<float>(newNumeric));
            else if (group->operation == SceneDocument::TreeNode::Rotate) rotation.setY(static_cast<float>(newNumeric));
            else scale.setY(static_cast<float>(newNumeric));
        } else {
            if (group->operation == SceneDocument::TreeNode::Translate) position.setZ(static_cast<float>(newNumeric));
            else if (group->operation == SceneDocument::TreeNode::Rotate) rotation.setZ(static_cast<float>(newNumeric));
            else scale.setZ(static_cast<float>(newNumeric));
        }
    } else {
        // Plain numeric adjustment (no expression or no number found)
        const bool isScale = group->operation == SceneDocument::TreeNode::Scale;
        const qreal step = group->operation == SceneDocument::TreeNode::Rotate ? 5.0
                         : isScale ? 0.1 : 1.0;
        QVector3D *targetVector = group->operation == SceneDocument::TreeNode::Translate ? &position
                                : group->operation == SceneDocument::TreeNode::Rotate    ? &rotation
                                                                                         : &scale;
        auto adjustAxis = [&](float current) -> float {
            return static_cast<float>(isScale ? qMax(0.01, static_cast<qreal>(current) + delta * step)
                                              : static_cast<qreal>(current) + delta * step);
        };
        if (axis == 0)      targetVector->setX(adjustAxis(targetVector->x()));
        else if (axis == 1) targetVector->setY(adjustAxis(targetVector->y()));
        else                targetVector->setZ(adjustAxis(targetVector->z()));
        // Clear expression for this axis so numeric value takes over
        newExpressions[axis].clear();
        m_ctrlHighlight.active = false;
    }

    const SceneDocument::Snapshot oldSnapshot = m_scene.snapshot();
    if (!m_scene.updateGroupTransform(groupId, position, rotation, scale, newExpressions))
        return;
    const SceneDocument::Snapshot newSnapshot = m_scene.snapshot();
    m_scene.restoreSnapshot(oldSnapshot);

    auto *command = new UpdateGroupTransformCommand(&m_scene, oldSnapshot, newSnapshot, [this]() {
        refreshSceneViews();
    });
    m_undoStack->push(command);
}

void MainWindow::onGraphicsTreeTransformControlHovered(int groupId, SceneDocument::TreeNode::Operation operation, int axis)
{
    if (m_viewport)
        m_viewport->setTreeTransformControlPreview(groupId, operation, axis);
}

void MainWindow::onGraphicsTreeShapeParameterAdjusted(int nodeId, int paramIndex, int numberStart, int numberLength, qreal delta)
{
    if (nodeId <= 0 || paramIndex < 0 || numberStart < 0 || numberLength <= 0 || qFuzzyIsNull(delta))
        return;

    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node || node->type != SceneDocument::TreeNode::Primitive)
        return;

    const ShapeNode *shape = m_scene.shapeById(node->shapeId);
    if (!shape)
        return;

    const QVector<SceneTreeGraphics::ShapeParameterControl> controls =
        SceneTreeGraphics::shapeParameterControls(*shape);
    if (paramIndex >= controls.size())
        return;

    const QString &currentExpr = controls[paramIndex].expression;
    if (numberStart + numberLength > currentExpr.size())
        return;

    const QString numberText = currentExpr.mid(numberStart, numberLength);
    const int decimalPoint = numberText.indexOf(QLatin1Char('.'));
    const int precision = decimalPoint >= 0 ? qMin(3, numberText.size() - decimalPoint - 1) : 0;
    const qreal step = precision > 0 ? 0.1 : 1.0;
    const bool standaloneNumber = isStandaloneNumericToken(currentExpr, numberStart, numberLength);
    const QString replacement = adjustedNumericToken(currentExpr, numberStart, numberLength, delta, step, 0.1, !standaloneNumber);
    if (replacement.isEmpty())
        return;

    const QString newExpr = currentExpr.left(numberStart) + replacement + currentExpr.mid(numberStart + numberLength);

    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        // Build a contextPrefix that uniquely identifies this parameter in the generated
        // code so the highlight does not land on the wrong token when multiple parameters
        // share the same numeric value (e.g. cube([20,20,20]) or translate([10,10,10])).
        QString contextPrefix;
        if (shape->type == ShapeNode::Cylinder) {
            // Generated: cylinder(h=H, r=R, center=true)
            contextPrefix = (paramIndex == 0) ? QStringLiteral(", r=") : QStringLiteral("h=");
        } else if (shape->type == ShapeNode::Sphere) {
            contextPrefix = QStringLiteral("r=");
        } else {
            // Cube: cube([X, Y, Z], center=true) — prefix accumulates preceding values
            contextPrefix = QStringLiteral("[");
            for (int i = 0; i < paramIndex && i < controls.size(); ++i)
                contextPrefix += controls[i].expression + QStringLiteral(", ");
        }
        m_ctrlHighlight.active        = true;
        m_ctrlHighlight.nodeId        = nodeId;
        m_ctrlHighlight.contextPrefix = contextPrefix;
        m_ctrlHighlight.expression    = newExpr;
        m_ctrlHighlight.numberStart   = numberStart;
        m_ctrlHighlight.numberLength  = int(replacement.size());
    } else {
        m_ctrlHighlight.active = false;
    }

    // Build variable context for re-evaluation.
    QHash<QString, qreal> varValues;
    for (const SceneDocument::TreeNode &child : m_scene.treeRoot().children) {
        if (child.type == SceneDocument::TreeNode::Variable)
            varValues[child.variableName] = child.variableValue;
    }

    qreal newNumericValue = replacement.toDouble();
    ExpressionSyntax::evaluate(newExpr, varValues, &newNumericValue);
    newNumericValue = qMax(0.1, newNumericValue);

    ShapeNode updatedShape = *shape;
    while (updatedShape.parameterExpressions.size() < controls.size())
        updatedShape.parameterExpressions.append(QString());
    updatedShape.parameterExpressions[paramIndex] = newExpr;

    if (updatedShape.type == ShapeNode::Cube) {
        QVector3D size = updatedShape.size;
        if (paramIndex == 0)      size.setX(newNumericValue);
        else if (paramIndex == 1) size.setY(newNumericValue);
        else if (paramIndex == 2) size.setZ(newNumericValue);
        updatedShape.size = size;
    } else if (updatedShape.type == ShapeNode::Sphere) {
        if (paramIndex == 0) updatedShape.radius = newNumericValue;
    } else if (updatedShape.type == ShapeNode::Cylinder) {
        if (paramIndex == 0)      updatedShape.radius = newNumericValue;
        else if (paramIndex == 1) updatedShape.height = newNumericValue;
    }

    auto *command = new UpdateShapeCommand(&m_scene, *shape, updatedShape, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::onGraphicsTreeShapeParameterHovered(int shapeId, int parameter)
{
    if (m_viewport)
        m_viewport->setTreeShapeParameterPreview(shapeId, parameter);
}

void MainWindow::onGraphicsTreeVariableNumberAdjusted(int nodeId, int start, int length, qreal delta)
{
    if (nodeId <= 0 || start < 0 || length <= 0 || qFuzzyIsNull(delta))
        return;

    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node || node->type != SceneDocument::TreeNode::Variable)
        return;

    QString expression = node->variableExpression;
    if (start + length > expression.size())
        return;

    const QString numberText = expression.mid(start, length);
    const int decimalPoint = numberText.indexOf(QLatin1Char('.'));
    const int precision = decimalPoint >= 0 ? qMin(3, numberText.size() - decimalPoint - 1) : 0;
    const qreal step = precision > 0 ? 0.1 : 1.0;
    const bool standaloneNumber = isStandaloneNumericToken(expression, start, length);
    QString replacement = adjustedNumericToken(expression, start, length, delta, step, 0.0, !standaloneNumber);
    if (replacement.isEmpty())
        return;

    expression.replace(start, length, replacement);

    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        m_ctrlHighlight.active        = true;
        m_ctrlHighlight.nodeId        = nodeId;
        m_ctrlHighlight.contextPrefix = QString(); // variable expression is unique in its range
        m_ctrlHighlight.expression    = expression;
        m_ctrlHighlight.numberStart   = start;
        m_ctrlHighlight.numberLength  = int(replacement.size());
    } else {
        m_ctrlHighlight.active = false;
    }

    auto *command = new UpdateVariableExpressionCommand(&m_scene, nodeId, expression, [this]() {
        refreshSceneViews();
    });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::onGraphicsTreeModuleCallArgumentAdjusted(int moduleCallId,
                                                          int parameterVariableId,
                                                          int start,
                                                          int length,
                                                          qreal delta)
{
    if (moduleCallId <= 0 || parameterVariableId <= 0 || start < 0 || length <= 0 || qFuzzyIsNull(delta))
        return;

    const SceneDocument::TreeNode *callNode = m_scene.treeNodeById(moduleCallId);
    const SceneDocument::TreeNode *parameterNode = m_scene.treeNodeById(parameterVariableId);
    if (!callNode || callNode->type != SceneDocument::TreeNode::ModuleCall
        || !parameterNode || parameterNode->type != SceneDocument::TreeNode::Variable
        || !parameterNode->isParameter) {
        return;
    }

    const QHash<QString, QString> overrides = parseNamedArgumentExpressions(callNode->moduleCallArguments);
    QString expression = overrides.value(parameterNode->variableName,
                                         parameterNode->variableExpression.trimmed().isEmpty()
                                             ? QString::number(parameterNode->variableValue)
                                             : parameterNode->variableExpression.trimmed());
    if (start + length > expression.size())
        return;

    const QString numberText = expression.mid(start, length);
    const int decimalPoint = numberText.indexOf(QLatin1Char('.'));
    const int precision = decimalPoint >= 0 ? qMin(3, numberText.size() - decimalPoint - 1) : 0;
    const qreal step = precision > 0 ? 0.1 : 1.0;
    const bool standaloneNumber = isStandaloneNumericToken(expression, start, length);
    QString replacement = adjustedNumericToken(expression, start, length, delta, step, 0.0, !standaloneNumber);
    if (replacement.isEmpty())
        return;

    expression.replace(start, length, replacement);

    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        m_ctrlHighlight.active        = true;
        m_ctrlHighlight.nodeId        = moduleCallId;
        m_ctrlHighlight.contextPrefix = QString();
        m_ctrlHighlight.expression    = expression;
        m_ctrlHighlight.numberStart   = start;
        m_ctrlHighlight.numberLength  = int(replacement.size());
    } else {
        m_ctrlHighlight.active = false;
    }

    auto *command = new UpdateModuleCallArgumentCommand(&m_scene,
                                                        moduleCallId,
                                                        parameterNode->variableName,
                                                        expression,
                                                        [this]() {
                                                            refreshSceneViews();
                                                        });

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::onGraphicsTreeForLoopRangeAdjusted(int nodeId, int start, int length, qreal delta)
{
    if (nodeId <= 0 || start < 0 || length <= 0 || qFuzzyIsNull(delta))
        return;

    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node || node->type != SceneDocument::TreeNode::Group || node->operation != SceneDocument::TreeNode::For)
        return;

    QString expression = SceneTreeGraphics::forLoopRangeExpression(*node);
    if (start + length > expression.size())
        return;

    const QString numberText = expression.mid(start, length);
    const int decimalPoint = numberText.indexOf(QLatin1Char('.'));
    const int precision = decimalPoint >= 0 ? qMin(3, numberText.size() - decimalPoint - 1) : 0;
    const qreal step = precision > 0 ? 0.1 : 1.0;
    const bool standaloneNumber = isStandaloneNumericToken(expression, start, length);
    const QString replacement = adjustedNumericToken(expression, start, length, delta, step, 0.0, !standaloneNumber);
    if (replacement.isEmpty())
        return;

    expression.replace(start, length, replacement);

    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        m_ctrlHighlight.active        = true;
        m_ctrlHighlight.nodeId        = nodeId;
        m_ctrlHighlight.contextPrefix = QString(); // full range expression is unique in for-loop range
        m_ctrlHighlight.expression    = expression;
        m_ctrlHighlight.numberStart   = start;
        m_ctrlHighlight.numberLength  = int(replacement.size());
    } else {
        m_ctrlHighlight.active = false;
    }

    auto *command = new UpdateForLoopCommand(&m_scene, nodeId, SceneTreeGraphics::forLoopVariableName(*node), expression, [this]() {
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

void MainWindow::clearSelection()
{
    m_scene.setSelectedShapeId(-1);
    m_ctrlHighlight = CtrlParamHighlight();

    if (m_shapeTree) {
        m_shapeTree->blockSignals(true);
        m_shapeTree->setCurrentItem(nullptr);
        m_shapeTree->clearSelection();
        m_shapeTree->blockSignals(false);
    }

    if (m_sceneTreeGraphics)
        m_sceneTreeGraphics->setSelectedTreeNodeId(0);

    if (m_viewport) {
        m_viewport->setSelectedIndex(-1);
        m_viewport->setSelectedGroupId(0);
        m_viewport->setTreeTransformControlPreview(0, SceneDocument::TreeNode::Union, -1);
        m_viewport->setTreeShapeParameterPreview(-1, -1);
        m_viewport->update();
    }

    highlightOpenScadSelection();
    refreshProperties();
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
    const bool moduleParameterZone = decodeModuleParameterInsertIndex(&insertIndex);
    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (node && node->type == SceneDocument::TreeNode::Variable && parentGroupId > 0) {
        const SceneDocument::TreeNode *parentNode = m_scene.treeNodeById(parentGroupId);
        const bool targetIsRoot = parentGroupId == m_scene.treeRoot().id;
        const bool targetIsModule = parentNode
                                    && parentNode->type == SceneDocument::TreeNode::Group
                                    && parentNode->operation == SceneDocument::TreeNode::Module;
        if (!targetIsRoot && !targetIsModule)
            return;
    }
    if (node && node->type == SceneDocument::TreeNode::ModuleCall) {
        const SceneDocument::TreeNode *parentNode = m_scene.treeNodeById(parentGroupId);
        if (!parentNode || parentNode->type != SceneDocument::TreeNode::Group)
            return;
    }

    auto *command = new MoveTreeNodeCommand(&m_scene, nodeId, parentGroupId, insertIndex, [this]() {
        refreshSceneViews();
    }, moduleParameterZone);

    if (!command->isValid()) {
        delete command;
        return;
    }

    m_undoStack->push(command);
}

void MainWindow::refreshProperties()
{
}

void MainWindow::refreshOpenScadCode()
{
    const QString code = OpenScadGenerator::generateWithSourceMap(m_scene, &m_openScadSourceRanges);
    m_codeEditor->setPlainText(code);
    highlightOpenScadSelection();
    writeOpenScadPreview(false);
}

void MainWindow::applyCtrlParamHighlight()
{
    const QString code = m_codeEditor->toPlainText();

    for (const OpenScadGenerator::SourceRange &range : m_openScadSourceRanges) {
        if (range.treeNodeId != m_ctrlHighlight.nodeId || range.length <= 0)
            continue;

        // Search within the node's range (capped to avoid matching children's code).
        // Prefix + expression together uniquely identify the token even when the same
        // numeric value appears in multiple parameters (e.g. cube([20,20,20])).
        const int searchCap = qMin(range.length, 300);
        const QString needle = m_ctrlHighlight.contextPrefix + m_ctrlHighlight.expression;
        const int hitPos = code.indexOf(needle, range.start);
        if (hitPos < 0 || hitPos >= range.start + searchCap)
            break;

        const int exprPos = hitPos + m_ctrlHighlight.contextPrefix.size();
        const int numStart = exprPos + m_ctrlHighlight.numberStart;
        const int numLen   = m_ctrlHighlight.numberLength;
        if (numStart + numLen > code.size())
            break;

        QTextCursor cursor(m_codeEditor->document());
        cursor.setPosition(numStart);
        cursor.setPosition(numStart + numLen, QTextCursor::KeepAnchor);

        QTextCharFormat fmt;
        fmt.setBackground(QColor(80, 180, 255, 140));
        fmt.setFontUnderline(true);

        QTextEdit::ExtraSelection sel;
        sel.cursor = cursor;
        sel.format = fmt;
        m_codeEditor->setExtraSelections({sel});
        return;
    }

    // Expression not found — code may not have been refreshed yet (selectShapeInSceneTree
    // is called before refreshOpenScadCode inside refreshShapeList). Keep active so the
    // next call from refreshOpenScadCode, which runs with the updated code, can apply it.
}

void MainWindow::highlightOpenScadSelection()
{
    if (!m_codeEditor)
        return;

    if (m_ctrlHighlight.active) {
        applyCtrlParamHighlight();
        return;
    }

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

    if (m_viewport) {
        m_csgStatusLabel->setText(m_viewport->csgStatusText());
        return;
    }

    m_csgStatusLabel->setText(buildCsgPreview(m_scene).statusText);
}

QString MainWindow::previewScadPath() const
{
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("openscad_preview.scad");
}

QString MainWindow::examplesPath() const
{
    // Walk up from the exe until we find a directory that contains docs/sample_codes
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        QDir candidate(dir.absoluteFilePath("docs/sample_codes"));
        if (candidate.exists())
            return candidate.absolutePath();
        if (!dir.cdUp())
            break;
    }
    return QString();
}

void MainWindow::populateExamplesMenu(QMenu *menu)
{
    const QString path = examplesPath();
    if (path.isEmpty()) {
        menu->addAction("(no examples found)")->setEnabled(false);
        return;
    }

    const QStringList files = QDir(path).entryList({"*.scad"}, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        menu->addAction("(no .scad files)")->setEnabled(false);
        return;
    }

    for (const QString &fileName : files) {
        const QString filePath = QDir(path).absoluteFilePath(fileName);
        QAction *action = menu->addAction(QFileInfo(fileName).completeBaseName());
        connect(action, &QAction::triggered, this, [this, filePath]() {
            loadExample(filePath);
        });
    }
}

void MainWindow::loadExample(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Open Example", QString("Cannot open:\n%1").arg(filePath));
        return;
    }
    m_codeEditor->setPlainText(QString::fromUtf8(file.readAll()));
    applyOpenScadCode();
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
