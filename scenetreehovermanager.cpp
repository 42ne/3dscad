#include "scenetreehovermanager.h"
#include "scenetreegraphicswidget.h"
#include "scenetreeinlineeditor.h"
#include "scenecanvasdraghandler.h"
#include "scenetreegraphicshelpers.h"
#include "scenestringutils.h"
#include "scenetreepalette.h"
#include "scenetreelayout.h"
#include "scenetreenoderenderer.h"

#include <QApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QPainterPath>
#include <QTextDocument>
#include <QLinearGradient>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QtMath>
#include <cmath>

using namespace SceneTreeGraphics;

namespace {

bool isPolyhedronEditableNumberCell(PolyhedronTableItem::Cell::Type type)
{
    return type == PolyhedronTableItem::Cell::PtX
           || type == PolyhedronTableItem::Cell::PtY
           || type == PolyhedronTableItem::Cell::PtZ
           || type == PolyhedronTableItem::Cell::FaceParticipate;
}

bool isPolyhedronButtonCell(PolyhedronTableItem::Cell::Type type)
{
    return type == PolyhedronTableItem::Cell::RemovePt
           || type == PolyhedronTableItem::Cell::RemoveFace
           || type == PolyhedronTableItem::Cell::AddPt
           || type == PolyhedronTableItem::Cell::AddFace
           || type == PolyhedronTableItem::Cell::AutoFace
           || type == PolyhedronTableItem::Cell::TemplateButton
           || type == PolyhedronTableItem::Cell::ClearPolyhedron;
}

bool isPolyhedronSelectableLabelCell(PolyhedronTableItem::Cell::Type type)
{
    return type == PolyhedronTableItem::Cell::PtLabel
           || type == PolyhedronTableItem::Cell::FaceLabel;
}

bool isPolygonEditableNumberCell(Polygon2DTableItem::Cell::Type type)
{
    return type == Polygon2DTableItem::Cell::PtX
           || type == Polygon2DTableItem::Cell::PtY;
}

bool isPolygonButtonCell(Polygon2DTableItem::Cell::Type type)
{
    return type == Polygon2DTableItem::Cell::RemovePt
           || type == Polygon2DTableItem::Cell::AddPt;
}

bool isPolygonSelectableLabelCell(Polygon2DTableItem::Cell::Type type)
{
    return type == Polygon2DTableItem::Cell::PtLabel;
}

bool usesDarkOverlayGlass(int backgroundTheme)
{
    return backgroundTheme == 2 || backgroundTheme == 3 || backgroundTheme == 5 || backgroundTheme == 7;
}

}

SceneTreeHoverManager::SceneTreeHoverManager(SceneTreeGraphicsWidget *widget)
    : QObject(widget)
    , m_widget(widget)
{
}

void SceneTreeHoverManager::clearHighlightOverlay()
{
    for (QGraphicsItem *item : m_hoverHighlightItems) {
        if (!item)
            continue;
        m_widget->m_graphicsScene->removeItem(item);
        delete item;
    }
    m_hoverHighlightItems.clear();
}

void SceneTreeHoverManager::updateHighlightOverlay()
{
    if (!m_widget->m_graphicsScene)
        return;

    clearHighlightOverlay();

    const bool hasActiveScrollControl = m_activeTransformControlNodeId > 0
                                        || m_activeColorNodeId > 0
                                        || m_activeShapeParameterNodeId > 0
                                        || m_activeVariableNodeId > 0
                                        || m_activeForLoopNodeId > 0
                                        || m_activeModuleCallNodeId > 0;

    auto addHoverOverlay = [this](const QRectF &rect,
                                  const QColor &fill,
                                  const QColor &border,
                                  qreal inflate,
                                  qreal radius) {
        if (!rect.isValid() || rect.isEmpty())
            return;
        QPainterPath path;
        path.addRoundedRect(rect.adjusted(-inflate, -inflate, inflate, inflate), radius, radius);
        auto *item = m_widget->m_graphicsScene->addPath(path, QPen(border, 1.5), QBrush(fill));
        item->setAcceptedMouseButtons(Qt::NoButton);
        item->setZValue(180.0);
        m_hoverHighlightItems.append(item);
    };

    if (!hasActiveScrollControl) {
        addHoverOverlay(m_hoveredScrollRect,
                        QColor(100, 215, 240, 45), QColor(65, 180, 210, 155), 2.5, 5.0);
    }
    addHoverOverlay(m_hoveredRenameRect,
                    QColor(190, 160, 245, 40), QColor(145, 108, 215, 150), 2.0, 4.0);
    addHoverOverlay(m_hoveredExpressionRect,
                    QColor(255, 214, 110, 42), QColor(216, 154, 54, 165), 2.0, 4.0);
}

void SceneTreeHoverManager::drawHintOverlay()
{
    if (!m_widget->m_graphicsScene || !m_widget->viewport())
        return;

    const QString hint = m_hoverHintText.trimmed().isEmpty()
                             ? QStringLiteral("Scene tree\nHover blocks, values, gaps, or handles to see available actions.")
                             : m_hoverHintText;

    const QPointF viewportTopLeft = m_widget->mapToScene(QPoint(0, 0));
    const qreal viewportWidth = m_widget->viewport()->width();
    const qreal viewportHeight = m_widget->viewport()->height();
    const qreal viewportScale = m_widget->transform().m11();
    const qreal safeScale = qMax<qreal>(0.001, std::abs(viewportScale));

    const auto scenePoint = [&](qreal x, qreal y) {
        return viewportTopLeft + QPointF(x / safeScale, y / safeScale);
    };

    constexpr qreal OverlayMargin = 12.0;
    constexpr qreal ThemePanelWidth = 146.0;
    constexpr qreal ThemePanelHeight = 26.0;
    constexpr qreal BackgroundPanelHeight = 26.0;
    constexpr qreal SwitcherRowGap = 8.0;
    constexpr qreal Gap = 14.0;
    constexpr qreal PadH = 12.0;
    constexpr qreal PadV = 9.0;
    constexpr qreal BottomGap = 12.0;
    constexpr qreal LocalOverlayZ = 10020.0;

    qreal panelX = OverlayMargin + ThemePanelWidth + Gap;
    qreal availableW = viewportWidth - panelX - OverlayMargin;
    if (availableW < 260.0) {
        panelX = OverlayMargin;
        availableW = viewportWidth - OverlayMargin * 2.0;
    }
    availableW = qBound<qreal>(220.0, availableW, 640.0);

    QFont font = sceneTreeGraphicsFont();
    font.setPointSizeF(qMax<qreal>(8.0, font.pointSizeF() - 0.2));
    const qreal textW = availableW - PadH * 2.0;
    QTextDocument textMeasure;
    textMeasure.setDefaultFont(font);
    textMeasure.setDocumentMargin(0.0);
    textMeasure.setTextWidth(textW);
    textMeasure.setPlainText(hint);
    const qreal maxPanelH = qMax<qreal>(84.0, viewportHeight * 0.38);
    const qreal panelH = qBound<qreal>(42.0,
                                       textMeasure.size().height() + PadV * 2.0 + 4.0,
                                       maxPanelH);
    qreal panelY = viewportHeight - BottomGap - panelH;
    if (panelX <= OverlayMargin + 0.5)
        panelY -= ThemePanelHeight + BackgroundPanelHeight + SwitcherRowGap + Gap;

    const QRectF panelLocal(0.0, 0.0, availableW, panelH);
    const QPointF panelTopLeft = scenePoint(panelX, qMax<qreal>(OverlayMargin, panelY));
    const bool darkGlass = usesDarkOverlayGlass(m_widget->m_canvasBackgroundTheme);
    const bool customGlass = SceneTreePalette::hasCustomTheme();
    const TreeAppearanceTheme customTheme = SceneTreePalette::customTheme();

    auto *shadow = m_widget->m_graphicsScene->addRect(panelLocal.translated(3.0, 4.0),
                                            Qt::NoPen,
                                            QBrush(QColor(0, 0, 0, darkGlass ? 115 : 38)));
    shadow->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    shadow->setAcceptedMouseButtons(Qt::NoButton);
    shadow->setPos(panelTopLeft);
    shadow->setZValue(LocalOverlayZ - 2.0);
    shadow->setOpacity(darkGlass ? 0.72 : 0.42);
    shadow->setData(0, QStringLiteral("shadow"));
    m_widget->m_toolbarItems.append(shadow);

    QPainterPath path;
    path.addRoundedRect(panelLocal, 8.0, 8.0);
    QLinearGradient glass(QPointF(0.0, 0.0), QPointF(0.0, panelH));
    glass.setColorAt(0.0, customGlass ? customTheme.glassTop
                                      : darkGlass ? QColor(24, 34, 50, 218) : QColor(255, 255, 255, 116));
    glass.setColorAt(1.0, customGlass ? customTheme.glassBottom
                                      : darkGlass ? QColor(8, 13, 22, 196) : QColor(237, 244, 249, 74));
    auto *panel = m_widget->m_graphicsScene->addPath(path,
                                           QPen(customGlass ? customTheme.glassBorder
                                                            : darkGlass ? QColor(142, 178, 215, 120)
                                                                        : QColor(116, 141, 166, 70), 1.0),
                                           QBrush(glass));
    panel->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    panel->setAcceptedMouseButtons(Qt::NoButton);
    panel->setPos(panelTopLeft);
    panel->setZValue(LocalOverlayZ - 1.0);
    panel->setData(0, QStringLiteral("glass_hint"));
    m_widget->m_toolbarItems.append(panel);

    auto *text = m_widget->m_graphicsScene->addText(hint, font);
    text->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    text->setAcceptedMouseButtons(Qt::NoButton);
    text->document()->setDocumentMargin(0.0);
    text->setTextWidth(textW);
    text->setDefaultTextColor(customGlass ? customTheme.text
                                          : darkGlass ? QColor(232, 242, 255) : QColor(39, 51, 66));
    text->setPos(scenePoint(panelX + PadH, qMax<qreal>(OverlayMargin, panelY) + PadV));
    text->setZValue(LocalOverlayZ);
    text->setData(0, QStringLiteral("glass_hint"));
    m_widget->m_toolbarItems.append(text);
}

void SceneTreeHoverManager::updateTooltip(const QPoint &globalPosition,
                                           const QPointF &scenePosition,
                                           bool controlDown)
{
    Q_UNUSED(globalPosition);
    if (m_widget->m_dragActive)
        return;

    QString key;
    const QString message = hoverHintTextForPosition(scenePosition, controlDown, &key);
    updateHoverHint(key, message);
}

void SceneTreeHoverManager::updateHoverHint(const QString &key, const QString &text)
{
    if (key == m_hoverHintKey && text == m_hoverHintText)
        return;

    m_hoverHintKey = key;
    m_hoverHintText = text;
    if (m_widget->m_dragActive)
        return;

    m_widget->updateToolbarOverlay();
}

QString SceneTreeHoverManager::hoverHintTextForPosition(const QPointF &scenePosition,
                                                         bool controlDown,
                                                         QString *key) const
{
    auto setKey = [key](const QString &value) {
        if (key)
            *key = value;
    };

    int collapseGroupId = 0;
    if (m_widget->m_canvasDragHandler->groupCollapseControlAt(scenePosition, &collapseGroupId)) {
        const bool collapsed = m_widget->m_collapsedGroupIds.contains(collapseGroupId);
        setKey(QStringLiteral("group-collapse:%1:%2").arg(collapseGroupId).arg(collapsed));
        return collapsed
            ? QStringLiteral("Group contents hidden\nClick to expand this group")
            : QStringLiteral("Group contents visible\nClick to collapse this group");
    }

    SceneTreeGraphicsWidget::ExpressionEditTarget expressionTarget;
    if (!m_widget->colorEditMode() && expressionEditTargetAt(scenePosition, &expressionTarget)) {
        setKey(QStringLiteral("expression:%1:%2:%3")
                   .arg(int(expressionTarget.kind))
                   .arg(expressionTarget.nodeId)
                   .arg(expressionTarget.secondaryId));
        if (controlDown) {
            return QStringLiteral("%1\nMouse wheel: change this value\nClick: edit the full expression")
                .arg(expressionTarget.label);
        }
        if (expressionTarget.kind == SceneTreeGraphicsWidget::ExpressionEditTarget::PolyhedronParticipation) {
            return QStringLiteral("%1\nClick: type participation position (-1 excludes)\nHold Ctrl + mouse wheel: cycle point participation")
                .arg(expressionTarget.label);
        }
        if (expressionTarget.kind == SceneTreeGraphicsWidget::ExpressionEditTarget::Polygon2DPoint) {
            return QStringLiteral("%1 coordinate\nClick: edit value\nHold Ctrl + mouse wheel: move this polygon point")
                .arg(expressionTarget.label);
        }
        if (expressionTarget.kind == SceneTreeGraphicsWidget::ExpressionEditTarget::ShapeParameter) {
            return QStringLiteral("%1\nClick: edit value\nHold Ctrl + mouse wheel: change this value")
                .arg(expressionTarget.label);
        }
        return QStringLiteral("%1 expression\nClick: edit the full value after =\nEnter: apply; Esc: cancel")
            .arg(expressionTarget.label);
    }

    int forNodeId = 0;
    int forNumberStart = -1;
    int forNumberLength = 0;
    if (m_widget->forLoopRangeControlAt(scenePosition, &forNodeId, &forNumberStart, &forNumberLength)) {
        setKey(QStringLiteral("for:%1:%2:%3").arg(forNodeId).arg(forNumberStart).arg(controlDown));
        return controlDown
            ? QStringLiteral("For range number\nMouse wheel: change this range value\nDouble-click module/variable labels to rename")
            : QStringLiteral("For range number\nHold Ctrl + mouse wheel: change this value\nDrag child blocks into the loop body");
    }

    int groupId = 0;
    int axis = -1;
    SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
    int transformNumberStart = -1;
    int transformNumberLength = 0;
    if (m_widget->transformControlAt(scenePosition, &groupId, &operation, &axis, &transformNumberStart, &transformNumberLength)
        && transformNumberStart >= 0
        && transformNumberLength > 0) {
        static const char *AxisNames[] = {"X", "Y", "Z"};
        const QString axisName = QString::fromLatin1(AxisNames[qBound(0, axis, 2)]);
        setKey(QStringLiteral("transform:%1:%2:%3").arg(groupId).arg(transformNumberStart).arg(controlDown));
        return controlDown
            ? QStringLiteral("%1 %2 value\nMouse wheel: change value\nDrag selected viewport axes: move/rotate when supported")
                  .arg(labelForOperation(operation), axisName)
            : QStringLiteral("%1 %2 value\nHold Ctrl + mouse wheel: change value\nClick block: select the transform group")
                  .arg(labelForOperation(operation), axisName);
    }

    int colorGroupId = 0;
    int colorChannel = -1;
    if (m_widget->colorChannelControlAt(scenePosition, &colorGroupId, &colorChannel)) {
        static const char *ChannelNames[] = {"R", "G", "B"};
        const QString channelName = QString::fromLatin1(ChannelNames[qBound(0, colorChannel, 2)]);
        setKey(QStringLiteral("color:%1:%2:%3").arg(colorGroupId).arg(colorChannel).arg(controlDown));
        return controlDown
            ? QStringLiteral("Color %1 channel\nMouse wheel: change channel\nShift makes bigger steps").arg(channelName)
            : QStringLiteral("Color %1 channel\nHold Ctrl + mouse wheel: change channel\nDrop blocks into the color body").arg(channelName);
    }

    int variableNodeId = 0;
    int numberStart = -1;
    int numberLength = 0;
    if (m_widget->variableNumberControlAt(scenePosition, &variableNodeId, &numberStart, &numberLength)) {
        setKey(QStringLiteral("variable:%1:%2:%3").arg(variableNodeId).arg(numberStart).arg(controlDown));
        return controlDown
            ? QStringLiteral("Variable value\nMouse wheel: change this number\nDouble-click variable name: rename")
            : QStringLiteral("Variable value\nHold Ctrl + mouse wheel: change this number\nDrag VAR into module parameters/body");
    }

    int moduleCallNodeId = 0;
    int moduleCallVarNodeId = 0;
    int moduleCallStart = -1;
    int moduleCallLength = 0;
    if (m_widget->moduleCallParamControlAt(scenePosition, &moduleCallNodeId, &moduleCallVarNodeId, &moduleCallStart, &moduleCallLength)) {
        setKey(QStringLiteral("modulecall:%1:%2:%3").arg(moduleCallNodeId).arg(moduleCallStart).arg(controlDown));
        return controlDown
            ? QStringLiteral("Module call argument\nMouse wheel: change this argument\nDrop the call into groups like any block")
            : QStringLiteral("Module call argument\nHold Ctrl + mouse wheel: change this argument\nDrag module call handles from module blocks");
    }

    int shapeId = -1;
    int nodeId = 0;
    int parameter = -1;
    int shapeNumberStart = -1;
    int shapeNumberLength = 0;
    if (m_widget->shapeParameterControlAt(scenePosition, &shapeId, &nodeId, &parameter, &shapeNumberStart, &shapeNumberLength)) {
        const ShapeNode *shape = m_widget->m_scene ? m_widget->m_scene->shapeById(shapeId) : nullptr;
        const QVector<ShapeParameterControl> controls = shape ? shapeParameterControls(*shape) : QVector<ShapeParameterControl>();
        const QString label = parameter >= 0 && parameter < controls.size() ? controls[parameter].label : QStringLiteral("parameter");
        setKey(QStringLiteral("shape:%1:%2:%3").arg(nodeId).arg(parameter).arg(controlDown));
        return controlDown
            ? QStringLiteral("Shape %1\nMouse wheel: change value\nDrag card: reorder or move into a group").arg(label)
            : QStringLiteral("Shape %1\nHold Ctrl + mouse wheel: change value\nClick card: select object").arg(label);
    }

    int renameNodeId = 0;
    QRectF renameRect;
    if (hoverRenameZoneAt(scenePosition, &renameNodeId, &renameRect)) {
        const SceneDocument::TreeNode *node = m_widget->m_scene ? m_widget->m_scene->treeNodeById(renameNodeId) : nullptr;
        const bool isModule = node && node->type == SceneDocument::TreeNode::Group
                              && node->operation == SceneDocument::TreeNode::Module;
        setKey(QStringLiteral("rename:%1").arg(renameNodeId));
        return isModule
            ? QStringLiteral("Module name\nDouble-click: rename module\nModule calls update through the tree")
            : QStringLiteral("Variable name\nDouble-click: rename variable\nCtrl + wheel works on numeric values");
    }

    const QRectF scrollRect = hoverScrollZoneRect(scenePosition);
    if (scrollRect.isValid()) {
        setKey(QStringLiteral("scrollzone:%1:%2").arg(qRound(scrollRect.x())).arg(qRound(scrollRect.y())));
        return QStringLiteral("Editable number\nHold Ctrl + mouse wheel: change value\nThe highlighted field shows the active target");
    }

    {
        PolyhedronTableItem::Cell cell;
        if (m_widget->polyhedronTableControlAt(scenePosition, &cell)) {
            if (isPolyhedronButtonCell(cell.type)) {
                setKey(QStringLiteral("poly-button:%1:%2:%3").arg(cell.nodeId).arg(cell.index).arg(int(cell.type)));
                switch (cell.type) {
                case PolyhedronTableItem::Cell::AddPt:
                    return QStringLiteral("Add polyhedron point\nClick: append a new 3D point");
                case PolyhedronTableItem::Cell::AddFace:
                    return QStringLiteral("Add polyhedron face\nClick: append a new face row");
                case PolyhedronTableItem::Cell::AutoFace:
                    return QStringLiteral("AutoFace\nClick: rebuild faces from points; repeated clicks cycle available variants");
                case PolyhedronTableItem::Cell::ClearPolyhedron:
                    return QStringLiteral("Clear polyhedron\nClick: remove all point and face rows");
                case PolyhedronTableItem::Cell::RemovePt:
                    return QStringLiteral("Remove point\nClick: delete this polyhedron point");
                case PolyhedronTableItem::Cell::RemoveFace:
                    return QStringLiteral("Remove face\nClick: delete this polyhedron face");
                case PolyhedronTableItem::Cell::TemplateButton:
                    return QStringLiteral("Polyhedron template\nClick: replace contents with this template");
                default:
                    break;
                }
            }
            if (cell.type == PolyhedronTableItem::Cell::PtLabel) {
                setKey(QStringLiteral("poly-pt-label:%1").arg(cell.nodeId));
                return QStringLiteral("Polyhedron point label\nClick: highlight this point in viewport; click again to clear\nShift + click: add/remove from selection");
            }
            if (cell.type == PolyhedronTableItem::Cell::FaceLabel) {
                setKey(QStringLiteral("poly-face-label:%1").arg(cell.nodeId));
                return QStringLiteral("Polyhedron face label\nClick: highlight this face in viewport; click again to clear\nShift + click: add/remove from selection");
            }
            if (cell.type != PolyhedronTableItem::Cell::None) {
                setKey(QStringLiteral("poly-passive:%1:%2").arg(cell.nodeId).arg(int(cell.type)));
                return QStringLiteral("Polyhedron table\nPoint coordinates and face membership cells are editable; labels select elements");
            }
        }
    }

    {
        Polygon2DTableItem::Cell cell;
        if (m_widget->polygon2DTableControlAt(scenePosition, &cell)) {
            if (isPolygonButtonCell(cell.type)) {
                setKey(QStringLiteral("polygon-button:%1:%2:%3").arg(cell.nodeId).arg(cell.index).arg(int(cell.type)));
                return cell.type == Polygon2DTableItem::Cell::AddPt
                    ? QStringLiteral("Add polygon point\nClick: append a new 2D point")
                    : QStringLiteral("Remove polygon point\nClick: delete this 2D point");
            }
            if (cell.type == Polygon2DTableItem::Cell::PtLabel) {
                setKey(QStringLiteral("polygon-point-label:%1:%2").arg(cell.nodeId).arg(cell.index));
                return QStringLiteral("Polygon point label\nClick: select this point; click again to clear\nShift + click: add/remove from selection");
            }
            if (cell.type == Polygon2DTableItem::Cell::HeaderLabel) {
                setKey(QStringLiteral("polygon-header:%1").arg(cell.nodeId));
                return QStringLiteral("Polygon point table\nEdit X/Y fields or use Ctrl + wheel over a coordinate\nClick Pt labels to select points");
            }
        }
    }

    for (const SceneTreeGraphicsWidget::CanvasMoveHandle &handle : m_widget->m_canvasMoveHandles) {
        if (handle.gripRect.contains(scenePosition)) {
            setKey(QStringLiteral("grip:%1").arg(handle.nodeId));
            return QStringLiteral("Canvas block handle\nDrag: move block on the tree canvas\nSlow drag moves touching blocks; fast drag detaches");
        }
    }

    for (const SceneTreeLayout::GroupHitArea &area : m_widget->m_treeLayout.groupHitAreas()) {
        for (const SceneTreeLayout::ChildLayout &child : area.children) {
            if (child.rect.contains(scenePosition)) {
                setKey(QStringLiteral("node:%1").arg(child.nodeId));
                return QStringLiteral("Tree block\nClick: select\nDrag: move between groups; Delete removes selected");
            }
        }
    }

    for (const SceneTreeLayout::GroupHitArea &area : m_widget->m_treeLayout.groupHitAreas()) {
        if (area.rect.contains(scenePosition)) {
            setKey(QStringLiteral("group:%1").arg(area.groupId));
            return QStringLiteral("%1 group\nDrop blocks into gaps to reorder or nest\nRight-click/left-click: select; Delete: remove selected")
                .arg(labelForOperation(area.operation));
        }
    }

    setKey(QStringLiteral("canvas"));
    return QStringLiteral("Scene tree canvas\nWheel: zoom tree view\nDrag empty space: pan; drag toolbar icons to create blocks");
}

void SceneTreeHoverManager::updateActiveTransformControl(const QPointF &scenePosition, bool enabled)
{
    int groupId = 0;
    int axis = -1;
    int numberStart = -1;
    int numberLength = 0;
    SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
    if (!enabled
        || !m_widget->transformControlAt(scenePosition, &groupId, &operation, &axis, &numberStart, &numberLength)
        || numberStart < 0
        || numberLength <= 0) {
        groupId = 0;
        axis = -1;
        numberStart = -1;
        operation = SceneDocument::TreeNode::Union;
    }

    if (m_activeTransformControlNodeId == groupId
        && m_activeTransformControlAxis == axis
        && m_activeTransformControlNumberStart == numberStart
        && m_activeTransformControlOperation == operation) {
        return;
    }

    m_activeTransformControlNodeId = groupId;
    m_activeTransformControlAxis = axis;
    m_activeTransformControlNumberStart = numberStart;
    m_activeTransformControlOperation = operation;
    if (!m_widget->m_dragActive)
        m_widget->refresh();

    emit m_widget->transformControlHovered(groupId, operation, axis);
}

void SceneTreeHoverManager::updateActiveColorChannelControl(const QPointF &scenePosition, bool enabled)
{
    int groupId = 0;
    int channel = -1;
    if (!enabled || !m_widget->colorChannelControlAt(scenePosition, &groupId, &channel)) {
        groupId = 0;
        channel = -1;
    }

    if (m_activeColorNodeId == groupId && m_activeColorChannel == channel)
        return;

    m_activeColorNodeId = groupId;
    m_activeColorChannel = channel;
    if (!m_widget->m_dragActive)
        m_widget->refresh();
}

void SceneTreeHoverManager::updateActiveShapeParameterControl(const QPointF &scenePosition, bool enabled)
{
    int shapeId = -1;
    int nodeId = 0;
    int paramIndex = -1;
    int numberStart = -1;
    int numberLength = 0;
    if (!enabled) {
        // all cleared — fall through to update
    } else if (m_widget->shapeParameterControlAt(scenePosition, &shapeId, &nodeId, &paramIndex, &numberStart, &numberLength)) {
        // regular primitive card pill — already set above
    } else {
        // check polyhedron table PtX/PtY/PtZ cells
        PolyhedronTableItem::Cell cell;
        if (m_widget->polyhedronTableControlAt(scenePosition, &cell)) {
            switch (cell.type) {
            case PolyhedronTableItem::Cell::PtX: paramIndex = 0; break;
            case PolyhedronTableItem::Cell::PtY: paramIndex = 1; break;
            case PolyhedronTableItem::Cell::PtZ: paramIndex = 2; break;
            default: break;
            }
            if (paramIndex >= 0) {
                nodeId = cell.nodeId;
                numberStart = 0;
            }
        }
        if (paramIndex < 0) {
            Polygon2DTableItem::Cell polygonCell;
            if (m_widget->polygon2DTableControlAt(scenePosition, &polygonCell)) {
                if (polygonCell.type == Polygon2DTableItem::Cell::PtX)
                    paramIndex = polygonCell.index * 2;
                else if (polygonCell.type == Polygon2DTableItem::Cell::PtY)
                    paramIndex = polygonCell.index * 2 + 1;
                if (paramIndex >= 0) {
                    shapeId = polygonCell.nodeId;
                    nodeId = polygonCell.nodeId;
                    numberStart = 0;
                }
            }
        }
    }

    if (m_activeShapeParameterNodeId == nodeId
        && m_activeShapeParameter == paramIndex
        && m_activeShapeParameterNumberStart == numberStart) {
        return;
    }

    m_activeShapeParameterNodeId = nodeId;
    m_activeShapeParameter = paramIndex;
    m_activeShapeParameterNumberStart = numberStart;
    if (!m_widget->m_dragActive)
        m_widget->refresh();

    emit m_widget->shapeParameterHovered(shapeId, paramIndex);
}

void SceneTreeHoverManager::updateActiveVariableNumberControl(const QPointF &scenePosition, bool enabled)
{
    int nodeId = 0;
    int start = -1;
    int length = 0;
    if (!enabled || !m_widget->variableNumberControlAt(scenePosition, &nodeId, &start, &length)) {
        nodeId = 0;
        start = -1;
    }

    Q_UNUSED(length);

    if (m_activeVariableNodeId == nodeId
        && m_activeVariableNumberStart == start) {
        return;
    }

    m_activeVariableNodeId = nodeId;
    m_activeVariableNumberStart = start;
    if (!m_widget->m_dragActive)
        m_widget->refresh();
    emit m_widget->variableNumberHovered(nodeId, start);
}

void SceneTreeHoverManager::updateActiveForLoopRangeControl(const QPointF &scenePosition, bool enabled)
{
    int nodeId = 0;
    int start = -1;
    int length = 0;
    if (!enabled || !m_widget->forLoopRangeControlAt(scenePosition, &nodeId, &start, &length)) {
        nodeId = 0;
        start = -1;
    }

    Q_UNUSED(length);

    if (m_activeForLoopNodeId == nodeId
        && m_activeForLoopNumberStart == start) {
        return;
    }

    m_activeForLoopNodeId = nodeId;
    m_activeForLoopNumberStart = start;
    if (!m_widget->m_dragActive)
        m_widget->refresh();
    emit m_widget->forLoopRangeHovered(nodeId, start);
}

void SceneTreeHoverManager::updateActiveModuleCallParamControl(const QPointF &scenePosition, bool enabled)
{
    int moduleCallNodeId = 0;
    int varNodeId = 0;
    int start = -1;
    int length = 0;
    if (!enabled || !m_widget->moduleCallParamControlAt(scenePosition, &moduleCallNodeId, &varNodeId, &start, &length)) {
        moduleCallNodeId = 0;
        varNodeId = 0;
        start = -1;
    }

    Q_UNUSED(length);

    if (m_activeModuleCallNodeId == moduleCallNodeId
        && m_activeModuleCallVarNodeId == varNodeId
        && m_activeModuleCallNumberStart == start) {
        return;
    }

    m_activeModuleCallNodeId = moduleCallNodeId;
    m_activeModuleCallVarNodeId = varNodeId;
    m_activeModuleCallNumberStart = start;
    if (!m_widget->m_dragActive)
        m_widget->refresh();
    emit m_widget->moduleCallParamHovered(moduleCallNodeId, varNodeId, start);
}

void SceneTreeHoverManager::updateHighlights(const QPointF &scenePosition)
{
    if (m_widget->m_dragActive || m_widget->m_inlineEditor->isEditing())
        return;

    const bool controlDown = QApplication::keyboardModifiers() & Qt::ControlModifier;

    // --- Scroll zone hover (teal) ---
    QRectF newScrollRect;
    if (controlDown) {
        newScrollRect = QRectF();
    } else {
        newScrollRect = hoverScrollZoneRect(scenePosition);
    }

    // --- Rename zone hover (lavender) ---
    int renameNodeId = 0;
    QRectF renameRect;
    hoverRenameZoneAt(scenePosition, &renameNodeId, &renameRect);
    const QRectF newRenameRect = renameNodeId > 0 ? renameRect : QRectF();

    SceneTreeGraphicsWidget::ExpressionEditTarget expressionTarget;
    const bool onExpression = !m_widget->colorEditMode() && expressionEditTargetAt(scenePosition, &expressionTarget);
    const QRectF newExpressionRect = onExpression ? expressionTarget.hoverRect : QRectF();

    int collapseGroupId = 0;
    const bool onCollapseControl = m_widget->m_canvasDragHandler->groupCollapseControlAt(scenePosition, &collapseGroupId);
    bool onCanvasGrip = false;
    for (const SceneTreeGraphicsWidget::CanvasMoveHandle &handle : m_widget->m_canvasMoveHandles) {
        if (handle.gripRect.contains(scenePosition)) {
            onCanvasGrip = true;
            break;
        }
    }
    PolyhedronTableItem::Cell polyCell;
    const bool onPolyhedronCell = m_widget->polyhedronTableControlAt(scenePosition, &polyCell);
    const bool onPolyhedronElementLabel = onPolyhedronCell
        && isPolyhedronSelectableLabelCell(polyCell.type)
        && polyCell.nodeId > 0;
    updatePolyhedronElementHover(scenePosition, onPolyhedronElementLabel);

    Polygon2DTableItem::Cell polygonCell;
    const bool onPolygonCell = m_widget->polygon2DTableControlAt(scenePosition, &polygonCell);
    const bool onPolygonPointLabel = onPolygonCell
        && isPolygonSelectableLabelCell(polygonCell.type)
        && polygonCell.nodeId > 0
        && polygonCell.index >= 0;
    updatePolygonPointHover(scenePosition, onPolygonPointLabel);

    // Update cursor.
    if (controlDown && newExpressionRect.isValid())
        m_widget->setCursor(Qt::SizeVerCursor);
    else if (controlDown
             && ((onPolyhedronCell && isPolyhedronEditableNumberCell(polyCell.type))
                 || (onPolygonCell && isPolygonEditableNumberCell(polygonCell.type))))
        m_widget->setCursor(Qt::SizeVerCursor);
    else if (newExpressionRect.isValid()
             || newRenameRect.isValid()
             || (onPolyhedronCell && isPolyhedronEditableNumberCell(polyCell.type))
             || (onPolygonCell && isPolygonEditableNumberCell(polygonCell.type)))
        m_widget->setCursor(Qt::IBeamCursor);
    else if (newScrollRect.isValid())
        m_widget->setCursor(Qt::SizeVerCursor);
    else if (onCollapseControl
             || onPolyhedronElementLabel
             || onPolygonPointLabel
             || (onPolyhedronCell && isPolyhedronButtonCell(polyCell.type))
             || (onPolygonCell && isPolygonButtonCell(polygonCell.type)))
        m_widget->setCursor(Qt::PointingHandCursor);
    else if (onCanvasGrip)
        m_widget->setCursor(Qt::SizeAllCursor);
    else if (onPolyhedronCell || onPolygonCell)
        m_widget->setCursor(Qt::ArrowCursor);
    else
        m_widget->setCursor(Qt::OpenHandCursor);

    if (newScrollRect == m_hoveredScrollRect
        && newRenameRect == m_hoveredRenameRect
        && newExpressionRect == m_hoveredExpressionRect)
        return;

    m_hoveredScrollRect = newScrollRect;
    m_hoveredRenameRect = newRenameRect;
    m_hoveredExpressionRect = newExpressionRect;
    updateHighlightOverlay();
    emit m_widget->hoverScrollZoneChanged(m_hoveredScrollRect);
}

void SceneTreeHoverManager::updatePolyhedronElementHover(const QPointF &scenePosition, bool enabled)
{
    int nodeId = 0;
    if (enabled) {
        PolyhedronTableItem::Cell cell;
        if (m_widget->polyhedronTableControlAt(scenePosition, &cell)
            && (cell.type == PolyhedronTableItem::Cell::PtLabel
                || cell.type == PolyhedronTableItem::Cell::FaceLabel)) {
            nodeId = cell.nodeId;
        }
    }

    if (m_hoveredPolyhedronElementNodeId == nodeId)
        return;

    m_hoveredPolyhedronElementNodeId = nodeId;
    if (!m_widget->m_dragActive)
        m_widget->refresh();
    emit m_widget->polyhedronElementHoverChanged(nodeId);
}

void SceneTreeHoverManager::updatePolygonPointHover(const QPointF &scenePosition, bool enabled)
{
    int nodeId = 0;
    int pointIndex = -1;
    if (enabled) {
        Polygon2DTableItem::Cell cell;
        if (m_widget->polygon2DTableControlAt(scenePosition, &cell)
            && cell.type == Polygon2DTableItem::Cell::PtLabel
            && cell.nodeId > 0
            && cell.index >= 0) {
            nodeId = cell.nodeId;
            pointIndex = cell.index;
        }
    }

    if (m_hoveredPolygonPointNodeId == nodeId
        && m_hoveredPolygonPointIndex == pointIndex) {
        return;
    }

    m_hoveredPolygonPointNodeId = nodeId;
    m_hoveredPolygonPointIndex = pointIndex;
    if (!m_widget->m_dragActive)
        m_widget->refresh();
}

QRectF SceneTreeHoverManager::hoverScrollZoneRect(const QPointF &scenePosition) const
{
    // Check each type of scroll control and return the first rect that contains scenePosition.
    {
        int groupId = 0;
        SceneDocument::TreeNode::Operation op = SceneDocument::TreeNode::Union;
        int axis = -1, numStart = -1, numLen = -1;
        if (m_widget->transformControlAt(scenePosition, &groupId, &op, &axis, &numStart, &numLen) && numStart >= 0) {
            const SceneDocument::TreeNode *node = m_widget->m_scene ? m_widget->m_scene->treeNodeById(groupId) : nullptr;
            if (node) {
                const QString expr = transformAxisExpression(*node, axis);
                const qreal hw = transformHeaderWidthForNode(*node);
                const QFontMetricsF metrics(sceneTreeGraphicsFont());
                const auto controls = transformParameterNumberControls(
                    m_widget->groupRectForNode(groupId), axis, expr, metrics, hw);
                for (const auto &ctl : controls) {
                    if (ctl.start == numStart)
                        return ctl.rect;
                }
            }
        }
    }
    {
        int groupId = 0, channel = -1;
        if (m_widget->colorChannelControlAt(scenePosition, &groupId, &channel) && channel >= 0) {
            const SceneDocument::TreeNode *node = m_widget->m_scene ? m_widget->m_scene->treeNodeById(groupId) : nullptr;
            if (node) {
                const QColor color = node->color.isValid() ? node->color : QColor(79, 163, 255);
                const int channelValues[3] = {color.red(), color.green(), color.blue()};
                const QString expr = QString::number(channelValues[qBound(0, channel, 2)]);
                const QFontMetricsF metrics(sceneTreeGraphicsFont());
                const auto controls = transformParameterNumberControls(
                    m_widget->groupRectForNode(groupId), channel, expr, metrics, TransformHeaderWidth);
                if (!controls.isEmpty())
                    return controls.first().rect;
            }
        }
    }
    {
        int shapeId = 0, nodeId2 = 0, param = -1, numStart = -1, numLen = -1;
        if (m_widget->shapeParameterControlAt(scenePosition, &shapeId, &nodeId2, &param, &numStart, &numLen) && numStart >= 0) {
            const ShapeNode *shape = (m_widget->m_scene && shapeId > 0) ? m_widget->m_scene->shapeById(shapeId) : nullptr;
            if (shape && param >= 0) {
                for (const SceneTreeLayout::GroupHitArea &area : m_widget->m_treeLayout.groupHitAreas()) {
                    for (const SceneTreeLayout::ChildLayout &child : area.children) {
                        if (child.nodeId != nodeId2)
                            continue;
                        const auto paramControls = shapeParameterControls(*shape);
                        if (param < paramControls.size()) {
                            const QFontMetricsF metrics(sceneTreeGraphicsFont());
                            const auto numCtrls = shapeParameterNumberControls(
                                child.rect, param, paramControls.size(),
                                paramControls[param].expression, metrics);
                            for (const auto &ctl : numCtrls) {
                                if (ctl.start == numStart)
                                    return ctl.rect;
                            }
                        }
                        break;
                    }
                }
            }
        }
    }
    {
        int nodeId2 = 0, start = -1, length = 0;
        if (m_widget->variableNumberControlAt(scenePosition, &nodeId2, &start, &length) && start >= 0) {
            const SceneDocument::TreeNode *node = m_widget->m_scene ? m_widget->m_scene->treeNodeById(nodeId2) : nullptr;
            if (node) {
                for (const SceneTreeLayout::GroupHitArea &area : m_widget->m_treeLayout.groupHitAreas()) {
                    for (const SceneTreeLayout::ChildLayout &child : area.children) {
                        if (child.nodeId != nodeId2)
                            continue;
                        const QFontMetricsF metrics(sceneTreeGraphicsFont());
                        const qreal nameW = metrics.horizontalAdvance(node->variableName);
                        const auto controls = expressionNumberControls(
                            child.rect, node->variableExpression, metrics, nameW);
                        for (const auto &ctl : controls) {
                            if (ctl.start == start)
                                return ctl.rect;
                        }
                        break;
                    }
                }
            }
        }
    }
    {
        int nodeId2 = 0, start = -1, length = 0;
        if (m_widget->forLoopRangeControlAt(scenePosition, &nodeId2, &start, &length) && start >= 0) {
            const SceneDocument::TreeNode *node = m_widget->m_scene ? m_widget->m_scene->treeNodeById(nodeId2) : nullptr;
            if (node) {
                const QFontMetricsF metrics(sceneTreeGraphicsFont());
                for (const SceneTreeLayout::GroupHitArea &area : m_widget->m_treeLayout.groupHitAreas()) {
                    if (area.groupId == nodeId2) {
                        const QString varName = forLoopVariableName(*node);
                        const QString rangeExpr = forLoopRangeExpression(*node);
                        const auto controls = forLoopRangeNumberControls(area.rect, varName, rangeExpr, metrics);
                        for (const auto &ctl : controls) {
                            if (ctl.start == start)
                                return ctl.rect;
                        }
                        break;
                    }
                }
            }
        }
    }
    {
        int moduleCallId = 0, varId = 0, start = -1, length = 0;
        if (m_widget->moduleCallParamControlAt(scenePosition, &moduleCallId, &varId, &start, &length) && start >= 0) {
            const SceneDocument::TreeNode *callNode = m_widget->m_scene ? m_widget->m_scene->treeNodeById(moduleCallId) : nullptr;
            const SceneDocument::TreeNode *moduleNode = (callNode && m_widget->m_scene)
                                                            ? m_widget->m_scene->treeNodeById(callNode->shapeId)
                                                            : nullptr;
            if (callNode && moduleNode) {
                QVector<ModuleCallParam> params;
                for (const SceneDocument::TreeNode &child : moduleNode->children) {
                    if (child.type == SceneDocument::TreeNode::Variable && child.isParameter) {
                        params.append({child.id, child.variableName,
                                       child.variableExpression.trimmed().isEmpty()
                                           ? QString::number(child.variableValue)
                                           : child.variableExpression.trimmed()});
                    }
                }
                for (const SceneTreeLayout::GroupHitArea &area : m_widget->m_treeLayout.groupHitAreas()) {
                    for (const SceneTreeLayout::ChildLayout &child : area.children) {
                        if (child.nodeId == moduleCallId) {
                            const QFontMetricsF metrics(sceneTreeGraphicsFont());
                            const auto controls = moduleCallParamControls(
                                child.rect, moduleNode->moduleName, params, metrics);
                            for (const auto &ctl : controls) {
                                if (ctl.paramVarNodeId == varId && ctl.numberStart == start)
                                    return ctl.rect;
                            }
                        }
                    }
                }
            }
        }
    }
    // ── Polyhedron table pill (PtX/PtY/PtZ/FaceParticipate) ──
    {
        PolyhedronTableItem::Cell cell;
        if (m_widget->polyhedronTableControlAt(scenePosition, &cell)) {
            switch (cell.type) {
            case PolyhedronTableItem::Cell::PtX:
            case PolyhedronTableItem::Cell::PtY:
            case PolyhedronTableItem::Cell::PtZ:
            case PolyhedronTableItem::Cell::FaceParticipate: {
                for (QGraphicsItem *item : m_widget->m_treeItems) {
                    auto *tableItem = dynamic_cast<PolyhedronTableItem *>(item);
                    if (!tableItem) continue;
                    if (!tableItem->boundingRect().contains(tableItem->mapFromScene(scenePosition)))
                        continue;
                    return QRectF(tableItem->mapToScene(cell.rect.topLeft()),
                                  tableItem->mapToScene(cell.rect.bottomRight()));
                }
                break;
            }
            default:
                break;
            }
        }
    }
    return QRectF();
}

bool SceneTreeHoverManager::hoverRenameZoneAt(const QPointF &scenePosition, int *nodeId, QRectF *zoneRect) const
{
    for (const SceneTreeGraphicsWidget::RenameZone &zone : m_widget->m_renameZones) {
        if (zone.rect.adjusted(-2, -2, 2, 2).contains(scenePosition)) {
            if (nodeId)   *nodeId   = zone.nodeId;
            if (zoneRect) *zoneRect = zone.rect;
            return true;
        }
    }
    if (nodeId)   *nodeId   = 0;
    if (zoneRect) *zoneRect = QRectF();
    return false;
}

bool SceneTreeHoverManager::expressionEditTargetAt(const QPointF &scenePosition,
                                                     SceneTreeGraphicsWidget::ExpressionEditTarget *target) const
{
    if (!m_widget->m_scene || m_widget->colorEditMode())
        return false;

    const QFontMetricsF metrics(sceneTreeGraphicsFont());

    const SceneTreeLayout::GroupHitArea *bestTransform = nullptr;
    for (const SceneTreeLayout::GroupHitArea &area : m_widget->m_treeLayout.groupHitAreas()) {
        if (area.collapsed || !area.rect.contains(scenePosition))
            continue;
        if (area.operation != SceneDocument::TreeNode::Translate
            && area.operation != SceneDocument::TreeNode::Rotate
            && area.operation != SceneDocument::TreeNode::Scale
            && area.operation != SceneDocument::TreeNode::Mirror) {
            continue;
        }
        if (!bestTransform || area.depth > bestTransform->depth)
            bestTransform = &area;
    }
    if (bestTransform) {
        const SceneDocument::TreeNode *node = m_widget->m_scene->treeNodeById(bestTransform->groupId);
        if (node) {
            const qreal headerWidth = transformHeaderWidthForNode(*node);
            for (int axis = 0; axis < 3; ++axis) {
                const QRectF rowRect = transformParameterControlRect(bestTransform->rect, axis, headerWidth);
                if (!rowRect.adjusted(-2.0, -1.5, 2.0, 1.5).contains(scenePosition))
                    continue;
                static const char *AxisNames[] = {"X", "Y", "Z"};
                const QRectF editRect(rowRect.left() + TransformParamLabelArea,
                                      rowRect.top(),
                                      rowRect.width() - TransformParamLabelArea,
                                      rowRect.height());
                if (target) {
                    target->kind = SceneTreeGraphicsWidget::ExpressionEditTarget::Transform;
                    target->hoverRect = rowRect;
                    target->editRect = editRect;
                    target->label = QStringLiteral("%1 %2")
                                        .arg(labelForOperation(bestTransform->operation),
                                             QString::fromLatin1(AxisNames[axis]));
                    target->expression = transformAxisExpression(*node, axis);
                    target->nodeId = bestTransform->groupId;
                    target->secondaryId = axis;
                }
                return true;
            }
        }
    }

    const SceneTreeLayout::GroupHitArea *bestLinearExtrude = nullptr;
    for (const SceneTreeLayout::GroupHitArea &area : m_widget->m_treeLayout.groupHitAreas()) {
        if (area.operation != SceneDocument::TreeNode::LinearExtrude
            || !area.rect.contains(scenePosition)) {
            continue;
        }
        if (!bestLinearExtrude || area.depth > bestLinearExtrude->depth)
            bestLinearExtrude = &area;
    }
    if (bestLinearExtrude) {
        const SceneDocument::TreeNode *node = m_widget->m_scene->treeNodeById(bestLinearExtrude->groupId);
        if (node) {
            const QString expression = linearExtrudeHeightExpression(*node);
            const QRectF textRect = linearExtrudeHeightTextRect(bestLinearExtrude->rect, metrics);
            const qreal prefixLeft = bestLinearExtrude->rect.left() + 68.0;
            const qreal expressionRight = textRect.left()
                                          + qMax<qreal>(40.0, metrics.horizontalAdvance(expression) + 8.0);
            const QRectF hoverRect(prefixLeft,
                                   textRect.top() - 1.0,
                                   qMin(bestLinearExtrude->rect.right() - prefixLeft - 28.0,
                                        expressionRight - prefixLeft),
                                   textRect.height() + 2.0);
            if (hoverRect.adjusted(-2.0, -1.5, 2.0, 1.5).contains(scenePosition)) {
                if (target) {
                    target->kind = SceneTreeGraphicsWidget::ExpressionEditTarget::Transform;
                    target->hoverRect = hoverRect;
                    target->editRect = textRect;
                    target->label = QStringLiteral("Linear extrude height");
                    target->expression = expression;
                    target->nodeId = bestLinearExtrude->groupId;
                    target->secondaryId = 0;
                }
                return true;
            }
        }
    }

    const SceneDocument::TreeNode *bestNode = nullptr;
    QRectF bestRect;
    int bestDepth = -1;
    for (const SceneTreeLayout::GroupHitArea &area : m_widget->m_treeLayout.groupHitAreas()) {
        for (const SceneTreeLayout::ChildLayout &child : area.children) {
            if (!child.rect.contains(scenePosition))
                continue;
            const SceneDocument::TreeNode *node = m_widget->m_scene->treeNodeById(child.nodeId);
            if (!node)
                continue;
            if (node->type != SceneDocument::TreeNode::Primitive
                && node->type != SceneDocument::TreeNode::Variable
                && node->type != SceneDocument::TreeNode::ModuleCall) {
                continue;
            }
            if (area.depth > bestDepth) {
                bestNode = node;
                bestRect = child.rect;
                bestDepth = area.depth;
            }
        }
    }

    // ── Polyhedron table pills (inline edit) ────────────────
    if (!bestNode) {
        for (QGraphicsItem *item : m_widget->m_treeItems) {
            if (auto *polygonTable = dynamic_cast<Polygon2DTableItem *>(item)) {
                const QPointF localPos = polygonTable->mapFromScene(scenePosition);
                if (!polygonTable->boundingRect().contains(localPos))
                    continue;
                const Polygon2DTableItem::Cell cell = polygonTable->cellAt(localPos);
                if (cell.type != Polygon2DTableItem::Cell::PtX
                    && cell.type != Polygon2DTableItem::Cell::PtY)
                    continue;

                const SceneDocument::TreeNode *node = m_widget->m_scene->treeNodeById(cell.nodeId);
                const ShapeNode *shape = (node && node->type == SceneDocument::TreeNode::Primitive)
                    ? m_widget->m_scene->shapeById(node->shapeId) : nullptr;
                if (!shape || cell.index < 0 || cell.index >= shape->polyhedronPoints.size())
                    continue;

                const bool isX = cell.type == Polygon2DTableItem::Cell::PtX;
                const QRectF sceneRect(polygonTable->mapToScene(cell.rect.topLeft()),
                                       polygonTable->mapToScene(cell.rect.bottomRight()));
                if (target) {
                    target->kind = SceneTreeGraphicsWidget::ExpressionEditTarget::Polygon2DPoint;
                    target->hoverRect = sceneRect;
                    target->editRect = sceneRect;
                    target->label = QStringLiteral("Pt %1 %2").arg(cell.index).arg(isX ? QStringLiteral("X") : QStringLiteral("Y"));
                    target->expression = QString::number(isX ? shape->polyhedronPoints[cell.index].x()
                                                             : shape->polyhedronPoints[cell.index].y(), 'g');
                    target->nodeId = cell.nodeId;
                    target->secondaryId = cell.index * 2 + (isX ? 0 : 1);
                }
                return true;
            }

            auto *tableItem = dynamic_cast<PolyhedronTableItem *>(item);
            if (!tableItem) continue;
            const QPointF localPos = tableItem->mapFromScene(scenePosition);
            if (!tableItem->boundingRect().contains(localPos))
                continue;
            const PolyhedronTableItem::Cell cell = tableItem->cellAt(localPos);
            if (cell.type == PolyhedronTableItem::Cell::None)
                continue;

            const QRectF sceneRect(tableItem->mapToScene(cell.rect.topLeft()),
                                   tableItem->mapToScene(cell.rect.bottomRight()));

            // ── Face participation (custom editing) ──
            if (cell.type == PolyhedronTableItem::Cell::FaceParticipate) {
                const SceneDocument::TreeNode *faceNode = m_widget->m_scene->treeNodeById(cell.nodeId);
                if (!faceNode || faceNode->type != SceneDocument::TreeNode::Primitive) continue;
                const ShapeNode *faceShape = m_widget->m_scene->shapeById(faceNode->shapeId);
                if (!faceShape || faceShape->type != ShapeNode::Face3D) continue;

                int pos = -1;
                if (!faceShape->polyhedronFaces.isEmpty())
                    pos = faceShape->polyhedronFaces.first().indexOf(cell.sub);

                const int ptNodeId = tableItem->pointNodeIdForIndex(cell.sub);
                if (target) {
                    target->kind = SceneTreeGraphicsWidget::ExpressionEditTarget::PolyhedronParticipation;
                    target->hoverRect  = sceneRect;
                    target->editRect   = sceneRect;
                    target->label      = QStringLiteral("F%1 vertex").arg(cell.index);
                    target->expression = QString::number(pos);
                    target->nodeId     = cell.nodeId;   // face nodeId
                    target->secondaryId = ptNodeId;     // point nodeId
                }
                return true;
            }

            // ── PtX/PtY/PtZ (standard shape parameter) ──
            int paramIndex = -1;
            switch (cell.type) {
            case PolyhedronTableItem::Cell::PtX: paramIndex = 0; break;
            case PolyhedronTableItem::Cell::PtY: paramIndex = 1; break;
            case PolyhedronTableItem::Cell::PtZ: paramIndex = 2; break;
            default: continue;
            }

            const SceneDocument::TreeNode *node = m_widget->m_scene->treeNodeById(cell.nodeId);
            if (!node || node->type != SceneDocument::TreeNode::Primitive) continue;
            const ShapeNode *shape = m_widget->m_scene->shapeById(node->shapeId);
            if (!shape) continue;

            const QVector<ShapeParameterControl> controls = shapeParameterControls(*shape);
            if (paramIndex >= controls.size()) continue;

            if (target) {
                target->kind = SceneTreeGraphicsWidget::ExpressionEditTarget::ShapeParameter;
                target->hoverRect  = sceneRect;
                target->editRect   = sceneRect;
                target->label      = controls[paramIndex].label;
                target->expression = controls[paramIndex].expression;
                target->nodeId     = cell.nodeId;
                target->secondaryId = paramIndex;
            }
            return true;
        }
    }

    if (!bestNode)
        return false;

    if (bestNode->type == SceneDocument::TreeNode::Primitive) {
        const ShapeNode *shape = m_widget->m_scene->shapeById(bestNode->shapeId);
        if (!shape)
            return false;
        const QVector<ShapeParameterControl> controls = shapeParameterControls(*shape);
        for (int i = 0; i < controls.size(); ++i) {
            const QRectF rowRect = shapeParameterControlRect(bestRect, i, controls.size());
            if (!rowRect.adjusted(-2.0, -1.5, 2.0, 1.5).contains(scenePosition))
                continue;
            const QRectF editRect(rowRect.left() + PrimitiveParamLabelArea,
                                  rowRect.top(),
                                  rowRect.width() - PrimitiveParamLabelArea,
                                  rowRect.height());
            if (target) {
                target->kind = SceneTreeGraphicsWidget::ExpressionEditTarget::ShapeParameter;
                target->hoverRect = rowRect;
                target->editRect = editRect;
                target->label = QStringLiteral("Shape %1").arg(controls[i].label);
                target->expression = controls[i].expression;
                target->nodeId = bestNode->id;
                target->secondaryId = i;
            }
            return true;
        }
        return false;
    }

    if (bestNode->type == SceneDocument::TreeNode::Variable) {
        const qreal nameW = metrics.horizontalAdvance(bestNode->variableName);
        const QRectF editRect = variableExpressionTextRect(bestRect, nameW);
        const QRectF hoverRect(bestRect.left() + 38.0 + nameW,
                               bestRect.top() + (VariableHeight - 18.0) * 0.5,
                               bestRect.right() - (bestRect.left() + 38.0 + nameW) - 4.0,
                               18.0);
        if (!hoverRect.adjusted(-2.0, -2.0, 2.0, 2.0).contains(scenePosition))
            return false;
        if (target) {
            target->kind = SceneTreeGraphicsWidget::ExpressionEditTarget::Variable;
            target->hoverRect = hoverRect;
            target->editRect = editRect;
            target->label = QStringLiteral("Variable %1").arg(bestNode->variableName);
            target->expression = bestNode->variableExpression;
            target->nodeId = bestNode->id;
        }
        return true;
    }

    if (bestNode->type == SceneDocument::TreeNode::ModuleCall) {
        const SceneDocument::TreeNode *moduleNode = m_widget->m_scene->treeNodeById(bestNode->shapeId);
        if (!moduleNode || moduleNode->operation != SceneDocument::TreeNode::Module)
            return false;

        QVector<ModuleCallParam> params;
        const QHash<QString, QString> overrides = resolveModuleArguments(bestNode->moduleCallArguments, *moduleNode);
        for (const SceneDocument::TreeNode &child : moduleNode->children) {
            if (child.type == SceneDocument::TreeNode::Variable && child.isParameter) {
                const QString expr = overrides.value(child.variableName,
                                                     child.variableExpression.trimmed().isEmpty()
                                                         ? QString::number(child.variableValue)
                                                         : child.variableExpression.trimmed());
                params.append({child.id, child.variableName, expr});
            }
        }

        qreal x = bestRect.left() + 46.0 + metrics.horizontalAdvance(bestNode->moduleName + QStringLiteral("("));
        for (int i = 0; i < params.size(); ++i) {
            const qreal labelLeft = x;
            x += metrics.horizontalAdvance(params[i].name + QStringLiteral(" = "));
            const qreal exprWidth = qMax<qreal>(40.0, metrics.horizontalAdvance(params[i].expression) + 10.0);
            const QRectF editRect(x, bestRect.top(), qMin(exprWidth, bestRect.right() - x), VariableHeight);
            const QRectF hoverRect(labelLeft, bestRect.top(), qMin(x + exprWidth - labelLeft, bestRect.right() - labelLeft), VariableHeight);
            if (hoverRect.adjusted(-2.0, -1.5, 2.0, 1.5).contains(scenePosition)) {
                if (target) {
                    target->kind = SceneTreeGraphicsWidget::ExpressionEditTarget::ModuleCallArgument;
                    target->hoverRect = hoverRect;
                    target->editRect = editRect;
                    target->label = QStringLiteral("Argument %1").arg(params[i].name);
                    target->expression = params[i].expression;
                    target->nodeId = bestNode->id;
                    target->secondaryId = params[i].varNodeId;
                }
                return true;
            }
            x += metrics.horizontalAdvance(params[i].expression);
            if (i < params.size() - 1)
                x += metrics.horizontalAdvance(QStringLiteral(", "));
        }
    }

    return false;
}
