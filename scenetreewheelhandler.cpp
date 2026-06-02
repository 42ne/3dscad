#include "scenetreewheelhandler.h"
#include "scenetreegraphicswidget.h"
#include "scenetreehittestmanager.h"
#include "scenetreehovermanager.h"
#include "scenetreegraphicshelpers.h"
#include "scenetreenoderenderer.h"
#include "shapenode.h"

using namespace SceneTreeGraphics;

SceneTreeWheelHandler::SceneTreeWheelHandler(SceneTreeGraphicsWidget *widget)
    : m_widget(widget)
{}

bool SceneTreeWheelHandler::handleCtrlWheel(const QPointF &scenePos, int wheelSteps)
{
    return handleForLoopRangeWheel    (scenePos, wheelSteps)
        || handleModuleCallParamWheel (scenePos, wheelSteps)
        || handleVariableNumberWheel  (scenePos, wheelSteps)
        || handleShapeParameterWheel  (scenePos, wheelSteps)
        || handlePolygon2DTableWheel  (scenePos, wheelSteps)
        || handleColorChannelWheel    (scenePos, wheelSteps)
        || handleTransformWheel       (scenePos, wheelSteps)
        || handlePolyhedronTableWheel (scenePos, wheelSteps);
}

bool SceneTreeWheelHandler::handleTransformWheel(const QPointF &scenePos, int steps)
{
    if (!m_widget->m_scene)
        return false;

    int groupId = 0;
    int axis = -1;
    int start = -1;
    int length = 0;
    SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
    if (!m_widget->m_hitTest->transformControlAt(scenePos, &groupId, &operation, &axis, &start, &length))
        return false;
    if (start < 0 || length <= 0)
        return false;

    emit m_widget->transformValueAdjusted(groupId, axis, start, length, static_cast<qreal>(steps));
    m_widget->m_hoverManager->updateActiveTransformControl(scenePos, true);
    return true;
}

bool SceneTreeWheelHandler::handleColorChannelWheel(const QPointF &scenePos, int steps)
{
    if (!m_widget->m_scene)
        return false;

    int groupId = 0;
    int channel = -1;
    if (!m_widget->m_hitTest->colorChannelControlAt(scenePos, &groupId, &channel))
        return false;

    emit m_widget->colorChannelAdjusted(groupId, channel, static_cast<qreal>(steps));
    m_widget->m_hoverManager->updateActiveColorChannelControl(scenePos, true);
    return true;
}

bool SceneTreeWheelHandler::handleShapeParameterWheel(const QPointF &scenePos, int steps)
{
    if (!m_widget->m_scene)
        return false;

    int shapeId = -1;
    int nodeId = 0;
    int paramIndex = -1;
    int start = -1;
    int length = 0;
    if (!m_widget->m_hitTest->shapeParameterControlAt(scenePos, &shapeId, &nodeId, &paramIndex, &start, &length))
        return false;

    emit m_widget->shapeParameterAdjusted(nodeId, paramIndex, start, length, static_cast<qreal>(steps));
    m_widget->m_hoverManager->updateActiveShapeParameterControl(scenePos, true);
    Q_UNUSED(shapeId);
    return true;
}

bool SceneTreeWheelHandler::handlePolyhedronTableWheel(const QPointF &scenePos, int steps)
{
    PolyhedronTableItem::Cell cell;
    if (!m_widget->m_hitTest->polyhedronTableControlAt(scenePos, &cell))
        return false;

    if (cell.type == PolyhedronTableItem::Cell::FaceParticipate) {
        if (!m_widget->m_scene) return false;
        const SceneDocument::TreeNode *faceNode = m_widget->m_scene->treeNodeById(cell.nodeId);
        if (!faceNode || faceNode->type != SceneDocument::TreeNode::Primitive) return false;
        const ShapeNode *shape = m_widget->m_scene->shapeById(faceNode->shapeId);
        if (!shape || shape->type != ShapeNode::Face3D) return false;

        const QVector<int> face = shape->polyhedronFaces.isEmpty()
                                      ? QVector<int>()
                                      : shape->polyhedronFaces.first();
        const int oldPos = face.indexOf(cell.sub);

        int newPos = oldPos;
        if (steps > 0) {
            if (oldPos < 0)
                newPos = 0;
            else if (oldPos >= face.size() - 1)
                newPos = -1;
            else
                newPos = oldPos + 1;
        } else {
            if (oldPos < 0)
                newPos = face.size();
            else if (oldPos == 0)
                newPos = -1;
            else
                newPos = oldPos - 1;
        }

        int ptNodeId = 0;
        for (QGraphicsItem *item : m_widget->m_treeItems) {
            auto *tableItem = dynamic_cast<PolyhedronTableItem *>(item);
            if (!tableItem) continue;
            ptNodeId = tableItem->pointNodeIdForIndex(cell.sub);
            break;
        }
        if (ptNodeId <= 0) return false;

        emit m_widget->polyhedronFaceParticipationAdjusted(cell.nodeId, ptNodeId, newPos);
        return true;
    }

    int paramIndex = -1;
    switch (cell.type) {
    case PolyhedronTableItem::Cell::PtX: paramIndex = 0; break;
    case PolyhedronTableItem::Cell::PtY: paramIndex = 1; break;
    case PolyhedronTableItem::Cell::PtZ: paramIndex = 2; break;
    default: return false;
    }

    const SceneDocument::TreeNode *node = m_widget->m_scene ? m_widget->m_scene->treeNodeById(cell.nodeId) : nullptr;
    if (!node || node->type != SceneDocument::TreeNode::Primitive) return false;
    const ShapeNode *shape = m_widget->m_scene->shapeById(node->shapeId);
    if (!shape) return false;

    const QVector<ShapeParameterControl> controls = shapeParameterControls(*shape);
    if (paramIndex >= controls.size()) return false;

    const QString &expr = controls[paramIndex].expression;
    emit m_widget->shapeParameterAdjusted(cell.nodeId, paramIndex, 0, expr.size(), static_cast<qreal>(steps));
    m_widget->m_hoverManager->updateActiveShapeParameterControl(scenePos, true);
    return true;
}

bool SceneTreeWheelHandler::handlePolygon2DTableWheel(const QPointF &scenePos, int steps)
{
    Polygon2DTableItem::Cell cell;
    if (!m_widget->m_hitTest->polygon2DTableControlAt(scenePos, &cell))
        return false;

    int coord = -1;
    if (cell.type == Polygon2DTableItem::Cell::PtX)
        coord = 0;
    else if (cell.type == Polygon2DTableItem::Cell::PtY)
        coord = 1;
    if (coord < 0 || cell.index < 0)
        return false;

    emit m_widget->polygon2DPointAdjusted(cell.nodeId, cell.index, coord, steps);
    m_widget->m_hoverManager->updateActiveShapeParameterControl(scenePos, true);
    return true;
}

bool SceneTreeWheelHandler::handleVariableNumberWheel(const QPointF &scenePos, int steps)
{
    if (!m_widget->m_scene)
        return false;

    int nodeId = 0;
    int start = -1;
    int length = 0;
    if (!m_widget->m_hitTest->variableNumberControlAt(scenePos, &nodeId, &start, &length))
        return false;

    emit m_widget->variableNumberAdjusted(nodeId, start, length, steps);
    m_widget->m_hoverManager->updateActiveVariableNumberControl(scenePos, true);
    return true;
}

bool SceneTreeWheelHandler::handleForLoopRangeWheel(const QPointF &scenePos, int steps)
{
    if (!m_widget->m_scene)
        return false;

    int nodeId = 0;
    int start = -1;
    int length = 0;
    if (!m_widget->m_hitTest->forLoopRangeControlAt(scenePos, &nodeId, &start, &length))
        return false;

    emit m_widget->forLoopRangeAdjusted(nodeId, start, length, steps);
    m_widget->m_hoverManager->updateActiveForLoopRangeControl(scenePos, true);
    return true;
}

bool SceneTreeWheelHandler::handleModuleCallParamWheel(const QPointF &scenePos, int steps)
{
    if (!m_widget->m_scene)
        return false;

    int moduleCallNodeId = 0;
    int paramVarNodeId = 0;
    int start = -1;
    int length = 0;
    if (!m_widget->m_hitTest->moduleCallParamControlAt(scenePos, &moduleCallNodeId, &paramVarNodeId, &start, &length))
        return false;

    emit m_widget->moduleCallArgumentAdjusted(moduleCallNodeId, paramVarNodeId, start, length, steps);
    m_widget->m_hoverManager->updateActiveModuleCallParamControl(scenePos, true);
    return true;
}
