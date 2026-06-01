#include "viewportaxisgizmo.h"
#include "viewporthelpers.h"

#include <QPainter>
#include <QPainterPath>

using namespace ViewportHelpers;

void ViewportAxisGizmo::drawAxisGizmo(QPainter &painter, ViewportWidget &w) const
{
    const QRectF panelRect(w.width() - 94.0, 14.0, 76.0, 76.0);
    const QPointF center = panelRect.center();
    const float axisLength = 27.0f;

    QVector<AxisGizmoAxis> axes = {
        {QStringLiteral("X"), QVector3D(1.0f, 0.0f, 0.0f), QColor(235, 80, 80), QPointF(), 0.0f},
        {QStringLiteral("Y"), QVector3D(0.0f, 1.0f, 0.0f), QColor(80, 210, 120), QPointF(), 0.0f},
        {QStringLiteral("Z"), QVector3D(0.0f, 0.0f, 1.0f), QColor(90, 155, 245), QPointF(), 0.0f}
    };

    for (AxisGizmoAxis &axis : axes) {
        const QVector3D cameraDirection = toCameraDirection(axis.direction, w.m_cameraYaw, w.m_cameraPitch);
        axis.end = center + QPointF(cameraDirection.x() * axisLength, -cameraDirection.y() * axisLength);
        axis.cameraDepth = cameraDirection.z();
    }

    std::sort(axes.begin(), axes.end(), [](const AxisGizmoAxis &left, const AxisGizmoAxis &right) {
        return left.cameraDepth < right.cameraDepth;
    });

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(255, 255, 255, 42), 1));
    painter.setBrush(QColor(10, 14, 20, 105));
    painter.drawRoundedRect(panelRect, 8.0, 8.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(235, 240, 245, 180));
    painter.drawEllipse(center, 3.0, 3.0);

    QFont labelFont = painter.font();
    labelFont.setBold(true);
    labelFont.setPointSize(qMax(7, labelFont.pointSize()));
    painter.setFont(labelFont);

    for (const AxisGizmoAxis &axis : axes) {
        QColor lineColor = axis.color;
        lineColor.setAlpha(axis.cameraDepth < 0.0f ? 120 : 235);
        painter.setPen(QPen(lineColor, axis.cameraDepth < 0.0f ? 2 : 3, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(center, axis.end);

        QColor dotColor = axis.color.lighter(axis.cameraDepth < 0.0f ? 105 : 118);
        dotColor.setAlpha(axis.cameraDepth < 0.0f ? 155 : 245);
        painter.setPen(QPen(axis.color.darker(135), 1));
        painter.setBrush(dotColor);
        painter.drawEllipse(axis.end, 8.0, 8.0);

        painter.setPen(QColor(255, 255, 255, axis.cameraDepth < 0.0f ? 170 : 245));
        painter.drawText(QRectF(axis.end.x() - 8.0, axis.end.y() - 8.0, 16.0, 16.0),
                         Qt::AlignCenter,
                         axis.label);
    }

    painter.restore();
}

void ViewportAxisGizmo::drawPolyhedronElementSelectionOverlay(QPainter &painter, ViewportWidget &w) const
{
    if (!w.m_scene
        || (w.m_selectedPolyhedronElementNodeIds.isEmpty()
            && w.m_hoveredPolyhedronElementNodeId <= 0))
        return;

    QVector<int> overlayNodeIds = w.m_selectedPolyhedronElementNodeIds;
    if (w.m_hoveredPolyhedronElementNodeId > 0
        && !overlayNodeIds.contains(w.m_hoveredPolyhedronElementNodeId)) {
        overlayNodeIds.append(w.m_hoveredPolyhedronElementNodeId);
    }

    auto project = [&](const QVector3D &world) {
        return projectWorldPoint(world, w.size(), w.m_cameraYaw, w.m_cameraPitch, w.m_cameraDistance, w.m_cameraTarget, w.m_orthographicProjection);
    };
    auto pointWorldPosition = [&](int nodeId, QVector3D *world) {
        const SceneDocument::TreeNode *node = w.m_scene->treeNodeById(nodeId);
        if (!node || node->type != SceneDocument::TreeNode::Primitive)
            return false;
        const ShapeNode *shape = w.m_scene->shapeById(node->shapeId);
        if (!shape || shape->type != ShapeNode::Point3D)
            return false;

        QVector<SceneDocument::TreeNode> parentGroups;
        collectParentGroupStackForShape(w.m_scene->treeRoot(), shape->id, &parentGroups);
        *world = transformPointByGroupStack(shape->position, parentGroups);
        return true;
    };

    for (int nodeId : overlayNodeIds) {
        const bool hoveredOnly = nodeId == w.m_hoveredPolyhedronElementNodeId
                                 && !w.m_selectedPolyhedronElementNodeIds.contains(nodeId);
        const QColor faceFill = hoveredOnly ? QColor(255, 210, 90, 26)
                                            : QColor(255, 199, 64, 58);
        const QColor faceEdge = hoveredOnly ? QColor(255, 226, 132, 150)
                                            : QColor(255, 220, 112, 235);
        const SceneDocument::TreeNode *node = w.m_scene->treeNodeById(nodeId);
        if (!node || node->type != SceneDocument::TreeNode::Primitive)
            continue;
        const ShapeNode *faceShape = w.m_scene->shapeById(node->shapeId);
        if (!faceShape || faceShape->type != ShapeNode::Face3D || faceShape->polyhedronFaces.isEmpty())
            continue;

        int parentGroupId = 0;
        if (!SceneDocument::findChildParent(w.m_scene->treeRoot(), nodeId, &parentGroupId, nullptr))
            continue;
        const SceneDocument::TreeNode *group = w.m_scene->treeNodeById(parentGroupId);
        if (!group || group->operation != SceneDocument::TreeNode::Polyhedron)
            continue;

        QVector<SceneDocument::TreeNode> parentGroups;
        collectParentGroupStackForGroup(w.m_scene->treeRoot(), parentGroupId, &parentGroups);

        QVector<QVector3D> points;
        for (const SceneDocument::TreeNode &child : group->children) {
            if (child.type != SceneDocument::TreeNode::Primitive)
                continue;
            const ShapeNode *pointShape = w.m_scene->shapeById(child.shapeId);
            if (pointShape && pointShape->type == ShapeNode::Point3D)
                points.append(transformPointByGroupStack(pointShape->position, parentGroups));
        }

        QPolygonF polygon;
        for (int pointIndex : faceShape->polyhedronFaces.first()) {
            if (pointIndex < 0 || pointIndex >= points.size())
                continue;
            const ProjectedPoint pp = project(points[pointIndex]);
            if (pp.visible)
                polygon << pp.point;
        }
        if (polygon.size() < 2)
            continue;
        QPolygonF outline = polygon;
        if (outline.size() >= 3)
            outline << outline.first();

        painter.save();
        painter.setBrush(polygon.size() >= 3 ? QBrush(faceFill) : Qt::NoBrush);
        painter.setPen(QPen(faceEdge, hoveredOnly ? 1.5 : 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (polygon.size() >= 3)
            painter.drawPolygon(polygon);
        else
            painter.drawPolyline(polygon);
        painter.setBrush(Qt::NoBrush);
        drawHaloPolyline(&painter, outline, faceEdge, hoveredOnly ? 1.2 : 2.0);
        painter.restore();
    }

    for (int nodeId : overlayNodeIds) {
        const bool hoveredOnly = nodeId == w.m_hoveredPolyhedronElementNodeId
                                 && !w.m_selectedPolyhedronElementNodeIds.contains(nodeId);
        const QColor pointFill = hoveredOnly ? QColor(255, 232, 145, 160)
                                             : QColor(255, 231, 130, 235);
        const QColor pointEdge = hoveredOnly ? QColor(80, 54, 10, 170)
                                             : QColor(42, 25, 0, 230);
        QVector3D world;
        if (!pointWorldPosition(nodeId, &world))
            continue;
        const ProjectedPoint pp = project(world);
        if (!pp.visible)
            continue;

        const qreal radius = hoveredOnly ? 4.4 : 5.5;
        const QRectF dot(pp.point.x() - radius, pp.point.y() - radius, radius * 2.0, radius * 2.0);
        painter.save();
        painter.setPen(QPen(QColor(255, 255, 255, hoveredOnly ? 105 : 190), hoveredOnly ? 3.0 : 5.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(dot.adjusted(-1.2, -1.2, 1.2, 1.2));
        painter.setPen(QPen(pointEdge, hoveredOnly ? 1.3 : 2.0));
        painter.setBrush(pointFill);
        painter.drawEllipse(dot);
        painter.restore();
    }
}

void ViewportAxisGizmo::drawPolyhedronSelectionMoveTool(QPainter &painter, ViewportWidget &w) const
{
    if (w.selectedPolyhedronPointNodeIds().isEmpty())
        return;

    const QVector3D origin = w.polyhedronSelectionOrigin();
    const QPointF center = projectWorldPoint(origin, w.size(), w.m_cameraYaw, w.m_cameraPitch, w.m_cameraDistance, w.m_cameraTarget, w.m_orthographicProjection).point;
    const float axisLength = w.polyhedronSelectionGizmoAxisLength();
    struct AxisDraw {
        QVector3D axis;
        QColor color;
        float depth = 0.0f;
        QPointF end;
    };
    QVector<AxisDraw> axes = {
        {QVector3D(axisLength, 0.0f, 0.0f), QColor(255, 95, 120, 205), 0.0f, QPointF()},
        {QVector3D(0.0f, axisLength, 0.0f), QColor(105, 245, 145, 205), 0.0f, QPointF()},
        {QVector3D(0.0f, 0.0f, axisLength), QColor(105, 180, 255, 205), 0.0f, QPointF()}
    };
    for (AxisDraw &axis : axes) {
        const ProjectedPoint projected = projectWorldPoint(origin + w.polyhedronSelectionWorldAxisVector(axis.axis),
                                                           w.size(), w.m_cameraYaw, w.m_cameraPitch, w.m_cameraDistance, w.m_cameraTarget, w.m_orthographicProjection);
        axis.depth = projected.depth;
        axis.end = projected.point;
    }
    std::sort(axes.begin(), axes.end(), [](const AxisDraw &a, const AxisDraw &b) {
        return a.depth > b.depth;
    });

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (const auto &axis : axes) {
        drawVolumetricGizmoAxis(&painter, center, axis.end, axis.color);
    }

    QPolygonF hub;
    hub << QPointF(center.x(), center.y() - 10.0)
        << QPointF(center.x() + 10.0, center.y())
        << QPointF(center.x(), center.y() + 10.0)
        << QPointF(center.x() - 10.0, center.y());
    QPolygonF hubShadow = hub.translated(2.7, 3.4);

    QLinearGradient hubGradient(center - QPointF(5.0, 7.0), center + QPointF(7.0, 8.0));
    hubGradient.setColorAt(0.0, QColor(255, 249, 178, 176));
    hubGradient.setColorAt(0.45, QColor(255, 203, 72, 142));
    hubGradient.setColorAt(1.0, QColor(103, 61, 4, 168));
    painter.setPen(QPen(QColor(10, 8, 2, 148), 1.55));
    painter.setBrush(QColor(0, 0, 0, 38));
    painter.drawPolygon(hubShadow);
    painter.setBrush(hubGradient);
    painter.drawPolygon(hub);
    painter.setPen(QPen(QColor(255, 255, 255, 96), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(center.x() - 4.2, center.y() - 1.5),
                     QPointF(center.x(), center.y() - 5.8));
    painter.drawLine(QPointF(center.x(), center.y() - 5.8),
                     QPointF(center.x() + 4.2, center.y() - 1.5));
    painter.restore();
}

bool ViewportAxisGizmo::pickSelectedTransformAxis(const QPoint &position, ViewportWidget::DragMode *dragMode, ViewportWidget &w) const
{
    const SceneDocument::TreeNode *selectedGroup = w.m_scene && w.m_selectedGroupId > 0
                                                       ? w.m_scene->treeNodeById(w.m_selectedGroupId)
                                                       : nullptr;
    if (!selectedGroup
        || (selectedGroup->operation != SceneDocument::TreeNode::Translate
            && selectedGroup->operation != SceneDocument::TreeNode::Rotate)) {
        return false;
    }

    const bool allowMoveAxes = selectedGroup->operation == SceneDocument::TreeNode::Translate;
    const bool allowRotationRings = selectedGroup->operation == SceneDocument::TreeNode::Rotate;
    const QVector3D origin = w.selectedTransformOrigin();
    float bestDistance = 9.0f;
    ViewportWidget::DragMode pickedAxis = ViewportWidget::NoDrag;
    const QPointF start = projectWorldPoint(origin, w.size(), w.m_cameraYaw, w.m_cameraPitch, w.m_cameraDistance, w.m_cameraTarget, w.m_orthographicProjection).point;

    const QVector<QPair<ViewportWidget::DragMode, QVector3D>> axes = {
        {ViewportWidget::AxisXDrag, w.selectedWorldAxisVector(QVector3D(36.0f, 0.0f, 0.0f))},
        {ViewportWidget::AxisYDrag, w.selectedWorldAxisVector(QVector3D(0.0f, 36.0f, 0.0f))},
        {ViewportWidget::AxisZDrag, w.selectedWorldAxisVector(QVector3D(0.0f, 0.0f, 36.0f))}
    };

    if (allowMoveAxes) {
        for (const auto &axis : axes) {
            const QPointF end = projectWorldPoint(origin + axis.second, w.size(), w.m_cameraYaw, w.m_cameraPitch, w.m_cameraDistance, w.m_cameraTarget, w.m_orthographicProjection).point;
            const float distance = distanceToSegment(position, start, end);
            if (distance < bestDistance) {
                bestDistance = distance;
                pickedAxis = axis.first;
            }
        }
    }

    const QVector<ViewportWidget::DragMode> rings = {ViewportWidget::RotateXDrag, ViewportWidget::RotateYDrag, ViewportWidget::RotateZDrag};
    if (allowRotationRings) {
        for (ViewportWidget::DragMode ring : rings) {
            QPointF previous;
            bool hasPrevious = false;

            for (int step = 0; step <= 72; ++step) {
                const QVector3D worldPoint = rotationRingPoint(origin, ring, 48.0f, step * 5.0f);
                const QPointF current = projectWorldPoint(worldPoint, w.size(), w.m_cameraYaw, w.m_cameraPitch, w.m_cameraDistance, w.m_cameraTarget, w.m_orthographicProjection).point;
                if (hasPrevious) {
                    const float distance = distanceToSegment(position, previous, current);
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        pickedAxis = ring;
                    }
                }

                previous = current;
                hasPrevious = true;
            }
        }
    }

    if (pickedAxis == ViewportWidget::NoDrag)
        return false;

    *dragMode = pickedAxis;
    return true;
}

bool ViewportAxisGizmo::pickPolyhedronSelectionAxis(const QPoint &position, ViewportWidget::DragMode *dragMode, ViewportWidget &w) const
{
    if (w.selectedPolyhedronPointNodeIds().isEmpty())
        return false;

    const QVector3D origin = w.polyhedronSelectionOrigin();
    const QPointF start = projectWorldPoint(origin, w.size(), w.m_cameraYaw, w.m_cameraPitch, w.m_cameraDistance, w.m_cameraTarget, w.m_orthographicProjection).point;
    float bestDistance = 10.0f;
    ViewportWidget::DragMode picked = ViewportWidget::NoDrag;

    if (QVector2D(QPointF(position) - start).length() <= 9.0f) {
        bestDistance = 0.0f;
        picked = ViewportWidget::PlaneDrag;
    }

    const float axisLength = w.polyhedronSelectionGizmoAxisLength();
    const QVector<QPair<ViewportWidget::DragMode, QVector3D>> axes = {
        {ViewportWidget::AxisXDrag, QVector3D(axisLength, 0.0f, 0.0f)},
        {ViewportWidget::AxisYDrag, QVector3D(0.0f, axisLength, 0.0f)},
        {ViewportWidget::AxisZDrag, QVector3D(0.0f, 0.0f, axisLength)}
    };
    for (const auto &axis : axes) {
        const QPointF end = projectWorldPoint(origin + w.polyhedronSelectionWorldAxisVector(axis.second),
                                              w.size(), w.m_cameraYaw, w.m_cameraPitch, w.m_cameraDistance, w.m_cameraTarget, w.m_orthographicProjection).point;
        const float distance = distanceToSegment(position, start, end);
        if (distance < bestDistance) {
            bestDistance = distance;
            picked = axis.first;
        }
    }

    if (picked == ViewportWidget::NoDrag)
        return false;
    if (dragMode)
        *dragMode = picked;
    return true;
}

bool ViewportAxisGizmo::hitTestPolyhedronSelection(const QPoint &position, ViewportWidget &w) const
{
    if (!w.m_scene || w.m_selectedPolyhedronElementNodeIds.isEmpty())
        return false;

    const QVector<SceneDocument::TreeNode> parentGroups = w.polyhedronSelectionParentGroupStack();
    for (int nodeId : w.m_selectedPolyhedronElementNodeIds) {
        const SceneDocument::TreeNode *node = w.m_scene->treeNodeById(nodeId);
        const ShapeNode *shape = shapeForPrimitiveNode(w.m_scene, node);
        if (!shape)
            continue;

        if (shape->type == ShapeNode::Point3D) {
            const QPointF screen = projectWorldPoint(transformPointByGroupStack(shape->position, parentGroups),
                                                     w.size(), w.m_cameraYaw, w.m_cameraPitch, w.m_cameraDistance, w.m_cameraTarget, w.m_orthographicProjection).point;
            if (QVector2D(QPointF(position) - screen).length() <= 10.0f)
                return true;
            continue;
        }

        if (shape->type != ShapeNode::Face3D || shape->polyhedronFaces.isEmpty())
            continue;

        const int groupId = w.polyhedronGroupIdForElementNode(nodeId);
        const SceneDocument::TreeNode *group = groupId > 0 ? w.m_scene->treeNodeById(groupId) : nullptr;
        if (!group)
            continue;

        QVector<QVector3D> points;
        for (const SceneDocument::TreeNode &child : group->children) {
            const ShapeNode *pointShape = shapeForPrimitiveNode(w.m_scene, &child);
            if (pointShape && pointShape->type == ShapeNode::Point3D)
                points.append(transformPointByGroupStack(pointShape->position, parentGroups));
        }

        QPolygonF polygon;
        for (int pointIndex : shape->polyhedronFaces.first()) {
            if (pointIndex < 0 || pointIndex >= points.size())
                continue;
            polygon << projectWorldPoint(points[pointIndex], w.size(), w.m_cameraYaw, w.m_cameraPitch, w.m_cameraDistance, w.m_cameraTarget, w.m_orthographicProjection).point;
        }
        if (polygon.size() < 2)
            continue;

        QPainterPath path;
        if (polygon.size() >= 3) {
            path.addPolygon(polygon);
            if (path.contains(position))
                return true;
        }
        for (int i = 0; i < polygon.size(); ++i) {
            if (distanceToSegment(position, polygon[i], polygon[(i + 1) % polygon.size()]) <= 8.0f)
                return true;
        }
    }

    return false;
}
