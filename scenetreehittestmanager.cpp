#include "scenetreehittestmanager.h"
#include "scenetreecanvasgraphics.h"
#include "scenetreegraphicswidget.h"
#include "scenetreeexpressionlayout.h"
#include "scenetreepreviewgeometry.h"
#include "scenestringutils.h"

#include <QFontMetricsF>

using namespace SceneTreeGraphics;

SceneTreeHitTestManager::SceneTreeHitTestManager(SceneTreeGraphicsWidget *widget)
    : m_widget(widget)
{
}

// ── Helper alias ──────────────────────────────────────────────────────────────
using GroupHitArea = SceneTreeLayout::GroupHitArea;
using ChildLayout  = SceneTreeLayout::ChildLayout;

// ─────────────────────────────────────────────────────────────────────────────

bool SceneTreeHitTestManager::colorChannelControlAt(const QPointF &scenePosition,
                                                    int *groupId,
                                                    int *channel) const
{
    const GroupHitArea *bestArea = nullptr;
    int bestChannel = -1;
    for (const GroupHitArea &area : m_widget->m_treeLayout.groupHitAreas()) {
        if (area.collapsed || area.operation != SceneDocument::TreeNode::Color
            || !area.rect.contains(scenePosition))
            continue;
        int hitChannel = -1;
        for (int i = 0; i < 3; ++i) {
            if (transformParameterControlRect(area.rect, i, TransformHeaderWidth)
                    .contains(scenePosition)) {
                hitChannel = i;
                break;
            }
        }
        if (hitChannel < 0) continue;
        if (!bestArea || area.depth > bestArea->depth) {
            bestArea    = &area;
            bestChannel = hitChannel;
        }
    }
    if (!bestArea) return false;
    if (groupId) *groupId = bestArea->groupId;
    if (channel) *channel = bestChannel;
    return true;
}

bool SceneTreeHitTestManager::transformControlAt(const QPointF &scenePosition,
                                                 int *groupId,
                                                 SceneDocument::TreeNode::Operation *operation,
                                                 int *axisOut,
                                                 int *numberStart,
                                                 int *numberLength) const
{
    const GroupHitArea *bestArea = nullptr;
    enum { Standard, Linear, Rotate } paramGroupKind = Standard;
    for (const GroupHitArea &area : m_widget->m_treeLayout.groupHitAreas()) {
        if (area.collapsed) continue;
        if (area.operation == SceneDocument::TreeNode::LinearExtrude
            || area.operation == SceneDocument::TreeNode::RotateExtrude) {
            if (!area.rect.contains(scenePosition)) continue;
            if (!bestArea || area.depth > bestArea->depth) {
                bestArea = &area;
                paramGroupKind = (area.operation == SceneDocument::TreeNode::LinearExtrude)
                    ? Linear : Rotate;
            }
            continue;
        }
        if (area.operation != SceneDocument::TreeNode::Translate
            && area.operation != SceneDocument::TreeNode::Rotate
            && area.operation != SceneDocument::TreeNode::Scale
            && area.operation != SceneDocument::TreeNode::Mirror
            && area.operation != SceneDocument::TreeNode::Resize)
            continue;
        if (!area.rect.contains(scenePosition)) continue;
        if (!bestArea || area.depth > bestArea->depth) {
            bestArea = &area;
            paramGroupKind = Standard;
        }
    }
    if (!bestArea) return false;

    if (paramGroupKind != Standard) {
        // Check each param pill
        if (!m_widget->m_scene) return false;
        const SceneDocument::TreeNode *node = m_widget->m_scene->treeNodeById(bestArea->groupId);
        if (!node) return false;

        if (paramGroupKind == Linear) {
            const QString heightExpr = SceneTreeGraphics::linearExtrudeHeightExpression(*node);
            const QString centerExpr = node->linearExtrudeCenter
                ? QStringLiteral("true") : QStringLiteral("false");
            const QString twistExpr = SceneTreeGraphics::linearExtrudeParam(*node, 1,
                QString::number(node->linearExtrudeTwist, 'g'));
            const QString slicesExpr = SceneTreeGraphics::linearExtrudeParam(*node, 2,
                QString::number(node->linearExtrudeSlices));
            const QString scaleExpr = SceneTreeGraphics::linearExtrudeParam(*node, 3,
                QString::number(node->linearExtrudeScaleVal, 'g'));

            const QFontMetricsF metrics(sceneTreeGraphicsFont());
            const auto paramInfos = SceneTreeGraphics::linearExtrudeParamInfos(
                bestArea->rect,
                heightExpr, centerExpr, twistExpr, slicesExpr, scaleExpr, metrics);

            for (const LinearExtrudeParamInfo &pInfo : paramInfos) {
                if (!pInfo.textRect.contains(scenePosition))
                    continue;
                if (groupId)   *groupId   = bestArea->groupId;
                if (operation) *operation = bestArea->operation;
                if (axisOut)   *axisOut   = pInfo.paramIndex;
                if (numberStart) *numberStart = 0;
                if (numberLength) *numberLength = pInfo.expression.size();
                return true;
            }
        } else {
            // RotateExtrude
            const QString angleExpr = SceneTreeGraphics::rotateExtrudeAngleExpression(*node);
            const QFontMetricsF metrics(sceneTreeGraphicsFont());

            const auto paramInfos = SceneTreeGraphics::rotateExtrudeParamInfos(
                bestArea->rect, angleExpr, metrics);

            for (const RotateExtrudeParamInfo &pInfo : paramInfos) {
                if (!pInfo.textRect.contains(scenePosition))
                    continue;
                if (groupId)   *groupId   = bestArea->groupId;
                if (operation) *operation = bestArea->operation;
                if (axisOut)   *axisOut   = pInfo.paramIndex;
                if (numberStart) *numberStart = 0;
                if (numberLength) *numberLength = pInfo.expression.size();
                return true;
            }
        }
        return false;
    }

    qreal headerWidth = TransformHeaderWidth;
    if (!bestArea->children.isEmpty())
        headerWidth = qMax<qreal>(TransformHeaderWidth,
                                  bestArea->children.first().rect.left()
                                  - bestArea->rect.left() - GroupPadding);

    int hitAxis = -1;
    for (int i = 0; i < 3; ++i) {
        if (transformParameterControlRect(bestArea->rect, i, headerWidth).contains(scenePosition)) {
            hitAxis = i;
            break;
        }
    }
    if (hitAxis < 0) return false;

    if (m_widget->m_scene && (numberStart || numberLength)) {
        const SceneDocument::TreeNode *node = m_widget->m_scene->treeNodeById(bestArea->groupId);
        if (node) {
            const QString expr = transformAxisExpression(*node, hitAxis);
            const QFontMetricsF metrics(sceneTreeGraphicsFont());
            const auto numCtrls = transformParameterNumberControls(
                bestArea->rect, hitAxis, expr, metrics, headerWidth);
            for (const ExpressionNumberControl &nc : numCtrls) {
                if (nc.rect.contains(scenePosition)) {
                    if (numberStart)  *numberStart  = nc.start;
                    if (numberLength) *numberLength = nc.length;
                    break;
                }
            }
        }
    }
    if (groupId)   *groupId   = bestArea->groupId;
    if (operation) *operation = bestArea->operation;
    if (axisOut)   *axisOut   = hitAxis;
    return true;
}

bool SceneTreeHitTestManager::shapeParameterControlAt(const QPointF &scenePosition,
                                                      int *shapeId, int *nodeId,
                                                      int *parameter,
                                                      int *numberStart,
                                                      int *numberLength) const
{
    if (!m_widget->m_scene) return false;

    const SceneDocument::TreeNode *bestNode = nullptr;
    QRectF bestRect;
    int bestDepth = -1;
    for (const GroupHitArea &area : m_widget->m_treeLayout.groupHitAreas()) {
        for (const ChildLayout &child : area.children) {
            if (!child.rect.contains(scenePosition)) continue;
            const SceneDocument::TreeNode *node = m_widget->m_scene->treeNodeById(child.nodeId);
            if (!node || node->type != SceneDocument::TreeNode::Primitive) continue;
            if (area.depth > bestDepth) {
                bestNode = node; bestRect = child.rect; bestDepth = area.depth;
            }
        }
    }
    if (!bestNode) return false;

    const ShapeNode *shape = m_widget->m_scene->shapeById(bestNode->shapeId);
    if (!shape) return false;

    const QFontMetricsF metrics(sceneTreeValueFont());
    const auto controls = shapeParameterControls(*shape);
    const bool isCube = (shape->type == ShapeNode::Cube);
    for (int i = 0; i < controls.size(); ++i) {
        const QRectF rowRect = shapeParameterControlRect(bestRect, i, controls.size());
        if (!rowRect.contains(scenePosition))
            continue;
        if (isCube) {
            const QRectF fieldRect = cubeShapeParameterFieldRect(rowRect);
            if (!fieldRect.contains(scenePosition))
                continue;
            const QVector<ExpressionTextSpan> spans =
                expressionSpansInTextRect(fieldRect, controls[i].expression, metrics);
            const bool simple = (spans.size() == 1 && spans.first().number);
            for (const ExpressionTextSpan &span : spans) {
                if (!span.number) continue;
                const QRectF pillRect = cubeShapeParameterPillRect(fieldRect, span);
                if (!pillRect.intersects(fieldRect))
                    continue;
                if (!simple && !pillRect.adjusted(-2,-1,2,1).contains(scenePosition))
                    continue;
                if (shapeId)     *shapeId     = bestNode->shapeId;
                if (nodeId)      *nodeId      = bestNode->id;
                if (parameter)   *parameter   = i;
                if (numberStart) *numberStart = span.start;
                if (numberLength)*numberLength= span.length;
                return true;
            }
            continue;
        }
        const auto numCtrls = shapeParameterNumberControls(
            bestRect, i, controls.size(), controls[i].expression, metrics);
        for (const ExpressionNumberControl &nc : numCtrls) {
            if (!nc.rect.contains(scenePosition)) continue;
            if (shapeId)    *shapeId    = bestNode->shapeId;
            if (nodeId)     *nodeId     = bestNode->id;
            if (parameter)  *parameter  = i;
            if (numberStart)  *numberStart  = nc.start;
            if (numberLength) *numberLength = nc.length;
            return true;
        }
    }
    return false;
}

bool SceneTreeHitTestManager::polyhedronTableControlAt(const QPointF &scenePosition,
                                                       PolyhedronTableItem::Cell *cell) const
{
    if (!cell) return false;
    for (QGraphicsItem *item : m_widget->m_treeItems) {
        auto *t = dynamic_cast<PolyhedronTableItem *>(item);
        if (!t || !t->boundingRect().contains(t->mapFromScene(scenePosition))) continue;
        *cell = t->cellAt(t->mapFromScene(scenePosition));
        return cell->type != PolyhedronTableItem::Cell::None;
    }
    return false;
}

int SceneTreeHitTestManager::polyhedronGroupIdForCell(const QPointF &scenePosition) const
{
    for (QGraphicsItem *item : m_widget->m_treeItems) {
        auto *t = dynamic_cast<PolyhedronTableItem *>(item);
        if (!t || !t->boundingRect().contains(t->mapFromScene(scenePosition))) continue;
        return t->groupNodeId();
    }
    return 0;
}

bool SceneTreeHitTestManager::polygon2DTableControlAt(const QPointF &scenePosition,
                                                      Polygon2DTableItem::Cell *cell) const
{
    if (!cell) return false;
    for (QGraphicsItem *item : m_widget->m_treeItems) {
        auto *t = dynamic_cast<Polygon2DTableItem *>(item);
        if (!t) continue;
        const QPointF local = t->mapFromScene(scenePosition);
        if (!t->boundingRect().contains(local)) continue;
        *cell = t->cellAt(local);
        return cell->type != Polygon2DTableItem::Cell::None;
    }
    return false;
}

bool SceneTreeHitTestManager::variableNumberControlAt(const QPointF &scenePosition,
                                                      int *nodeId, int *start, int *length) const
{
    if (!m_widget->m_scene) return false;

    const SceneDocument::TreeNode *bestNode = nullptr;
    QRectF bestRect;
    int bestDepth = -1;
    for (const GroupHitArea &area : m_widget->m_treeLayout.groupHitAreas()) {
        for (const ChildLayout &child : area.children) {
            if (!child.rect.contains(scenePosition)) continue;
            const SceneDocument::TreeNode *node = m_widget->m_scene->treeNodeById(child.nodeId);
            if (!node || node->type != SceneDocument::TreeNode::Variable) continue;
            if (area.depth > bestDepth) {
                bestNode = node; bestRect = child.rect; bestDepth = area.depth;
            }
        }
    }
    if (!bestNode) return false;

    const QFontMetricsF metrics(sceneTreeGraphicsFont());
    const qreal nameW = metrics.horizontalAdvance(bestNode->variableName);
    const auto controls = expressionNumberControls(bestRect, bestNode->variableExpression,
                                                   metrics, nameW);
    for (const ExpressionNumberControl &ctrl : controls) {
        if (!ctrl.rect.contains(scenePosition)) continue;
        if (nodeId) *nodeId = bestNode->id;
        if (start)  *start  = ctrl.start;
        if (length) *length = ctrl.length;
        return true;
    }
    return false;
}

bool SceneTreeHitTestManager::forLoopRangeControlAt(const QPointF &scenePosition,
                                                    int *nodeId, int *start, int *length) const
{
    if (!m_widget->m_scene) return false;

    const GroupHitArea *bestArea = nullptr;
    for (const GroupHitArea &area : m_widget->m_treeLayout.groupHitAreas()) {
        if (area.collapsed || area.operation != SceneDocument::TreeNode::For
            || !area.rect.contains(scenePosition))
            continue;
        if (!bestArea || area.depth > bestArea->depth) bestArea = &area;
    }
    if (!bestArea) return false;

    const SceneDocument::TreeNode *node = m_widget->m_scene->treeNodeById(bestArea->groupId);
    if (!node || node->type != SceneDocument::TreeNode::Group
        || node->operation != SceneDocument::TreeNode::For)
        return false;

    const QFontMetricsF metrics(sceneTreeGraphicsFont());
    const auto controls = forLoopRangeNumberControls(
        bestArea->rect, forLoopVariableName(*node), forLoopRangeExpression(*node), metrics);
    for (const ExpressionNumberControl &ctrl : controls) {
        if (!ctrl.rect.contains(scenePosition)) continue;
        if (nodeId) *nodeId = node->id;
        if (start)  *start  = ctrl.start;
        if (length) *length = ctrl.length;
        return true;
    }
    return false;
}

bool SceneTreeHitTestManager::moduleCallParamControlAt(const QPointF &scenePosition,
                                                        int *moduleCallNodeId,
                                                        int *paramVarNodeId,
                                                        int *start,
                                                        int *length) const
{
    if (!m_widget->m_scene) return false;

    for (const GroupHitArea &area : m_widget->m_treeLayout.groupHitAreas()) {
        for (const ChildLayout &child : area.children) {
            if (!child.rect.contains(scenePosition)) continue;
            const SceneDocument::TreeNode *node = m_widget->m_scene->treeNodeById(child.nodeId);
            if (!node || node->type != SceneDocument::TreeNode::ModuleCall) continue;
            const SceneDocument::TreeNode *modGroup = m_widget->m_scene->treeNodeById(node->shapeId);
            if (!modGroup || modGroup->operation != SceneDocument::TreeNode::Module) continue;

            QVector<ModuleCallParam> params;
            const QHash<QString, QString> overrides =
                resolveModuleArguments(node->moduleCallArguments, *modGroup);
            for (const SceneDocument::TreeNode &pChild : modGroup->children) {
                if (pChild.type != SceneDocument::TreeNode::Variable || !pChild.isParameter)
                    continue;
                const QString expr = overrides.value(
                    pChild.variableName,
                    pChild.variableExpression.trimmed().isEmpty()
                        ? QString::number(pChild.variableValue)
                        : pChild.variableExpression.trimmed());
                params.append({pChild.id, pChild.variableName, expr});
            }
            if (params.isEmpty()) continue;

            const QFontMetricsF metrics(sceneTreeGraphicsFont());
            const auto controls = moduleCallParamControls(
                child.rect, node->moduleName, params, metrics);
            for (const ModuleCallParamControl &ctrl : controls) {
                if (!ctrl.rect.contains(scenePosition)) continue;
                if (moduleCallNodeId) *moduleCallNodeId = node->id;
                if (paramVarNodeId)   *paramVarNodeId   = ctrl.paramVarNodeId;
                if (start)            *start            = ctrl.numberStart;
                if (length)           *length           = ctrl.numberLength;
                return true;
            }
        }
    }
    return false;
}
