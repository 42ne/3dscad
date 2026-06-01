#include "viewportoverlaypreview.h"
#include "viewportwidget.h"
#include "viewporthelpers.h"

#include <QPainter>
#include <QtMath>

using namespace ViewportHelpers;

void ViewportOverlayPreview::drawTransformControlPreview(QPainter &painter, ViewportWidget &w) const
{
    if (!w.m_scene || w.m_treeTransformPreviewGroupId <= 0 || w.m_treeTransformPreviewAxis < 0)
        return;

    const SceneDocument::TreeNode *group = w.m_scene->treeNodeById(w.m_treeTransformPreviewGroupId);
    if (!group)
        return;

    const bool translatePreview = w.m_treeTransformPreviewOperation == SceneDocument::TreeNode::Translate;
    const bool rotatePreview = w.m_treeTransformPreviewOperation == SceneDocument::TreeNode::Rotate;
    const bool scalePreview = w.m_treeTransformPreviewOperation == SceneDocument::TreeNode::Scale;
    if (!translatePreview && !rotatePreview && !scalePreview)
        return;

    const QVector3D origin = w.transformOriginForGroup(w.m_treeTransformPreviewGroupId);
    QVector3D localAxis;
    QColor accent;
    if (w.m_treeTransformPreviewAxis == 0) {
        localAxis = QVector3D(1.0f, 0.0f, 0.0f);
        accent = ViewportConstants::kAxisXColor;
    } else if (w.m_treeTransformPreviewAxis == 1) {
        localAxis = QVector3D(0.0f, 1.0f, 0.0f);
        accent = ViewportConstants::kAxisYColor;
    } else {
        localAxis = QVector3D(0.0f, 0.0f, 1.0f);
        accent = ViewportConstants::kAxisZColor;
    }

    auto project = [&](const QVector3D &world) {
        return w.m_camera.project(world, w.size()).point;
    };
    auto axisName = [&]() {
        if (w.m_treeTransformPreviewAxis == 0)
            return QStringLiteral("X");
        if (w.m_treeTransformPreviewAxis == 1)
            return QStringLiteral("Y");
        return QStringLiteral("Z");
    };
    auto axisValue = [&]() {
        const QVector3D values = translatePreview
                                     ? group->position
                                     : rotatePreview
                                           ? group->rotation
                                           : group->scale;
        if (w.m_treeTransformPreviewAxis == 0)
            return values.x();
        if (w.m_treeTransformPreviewAxis == 1)
            return values.y();
        return values.z();
    };
    const QString valueLabel = QStringLiteral("%1%2 %3")
                                   .arg(scalePreview ? QStringLiteral("S") : rotatePreview ? QStringLiteral("R") : QStringLiteral("T"),
                                        axisName(),
                                        formatPreviewValue(axisValue(), scalePreview ? 1 : -1));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (translatePreview || scalePreview) {
        QVector3D worldAxis = w.worldAxisVectorForGroup(w.m_treeTransformPreviewGroupId, localAxis);
        if (worldAxis.lengthSquared() <= 0.0001f)
            worldAxis = localAxis;
        worldAxis.normalize();

        const QPointF center = project(origin);
        const float axisLength = scalePreview ? 42.0f : 34.0f;
        const QPointF negative = project(origin - worldAxis * axisLength);
        const QPointF positive = project(origin + worldAxis * axisLength);
        drawHaloLine(&painter, negative, positive, accent, 4.0);
        drawArrowHead(&painter, center, positive, accent);
        drawArrowHead(&painter, center, negative, accent);
        drawDirectionLabel(&painter, positive, center, "+");
        drawDirectionLabel(&painter, negative, center, "-");
        drawValueLabel(&painter, center + QPointF(0.0, -28.0), valueLabel);
    } else {
        QVector<QPointF> arcPoints;
        const float radius = ViewportConstants::kRotationRingRadius;
        const QVector<SceneDocument::TreeNode> stack = w.parentGroupStackForGroup(w.m_treeTransformPreviewGroupId);
        for (int step = -7; step <= 7; ++step) {
            const float angle = qDegreesToRadians(step * 8.0f);
            const float c = qCos(angle) * radius;
            const float s = qSin(angle) * radius;
            QVector3D localPoint;
            if (w.m_treeTransformPreviewAxis == 0)
                localPoint = QVector3D(0.0f, c, s);
            else if (w.m_treeTransformPreviewAxis == 1)
                localPoint = QVector3D(c, 0.0f, s);
            else
                localPoint = QVector3D(c, s, 0.0f);

            arcPoints.append(project(origin + transformVectorByGroupStack(localPoint, stack)));
        }

        if (arcPoints.size() >= 2) {
            drawHaloPolyline(&painter, QPolygonF(arcPoints), accent, 3.0);
            drawArrowHead(&painter, arcPoints[1], arcPoints.first(), accent);
            drawArrowHead(&painter, arcPoints[arcPoints.size() - 2], arcPoints.last(), accent);
            const QPointF center = project(origin);
            drawDirectionLabel(&painter, arcPoints.last(), center, "+");
            drawDirectionLabel(&painter, arcPoints.first(), center, "-");
            drawValueLabel(&painter, center + QPointF(0.0, -62.0), valueLabel);
        }
    }

    painter.restore();
}

void ViewportOverlayPreview::drawShapeParameterPreview(QPainter &painter, ViewportWidget &w) const
{
    if (!w.m_shapes || w.m_treeShapePreviewShapeId <= 0 || w.m_treeShapePreviewParameter < 0)
        return;

    const ShapeNode *shape = nullptr;
    for (const ShapeNode &candidate : *w.m_shapes) {
        if (candidate.id == w.m_treeShapePreviewShapeId) {
            shape = &candidate;
            break;
        }
    }

    if (!shape)
        return;

    auto project = [&](const QVector3D &world) {
        return w.m_camera.project(world, w.size()).point;
    };

    QVector<SceneDocument::TreeNode> parentGroups;
    if (w.m_scene)
        collectParentGroupStackForShape(w.m_scene->treeRoot(), shape->id, &parentGroups);

    auto transformed = [&](const QVector3D &local) {
        const QVector3D shapeSpace = rotatePoint(local, shape->rotation) + shape->position;
        return transformPointByGroupStack(shapeSpace, parentGroups);
    };

    auto drawDimension = [&](const QVector3D &localAxis,
                             float halfLength,
                             float sideOffset,
                             const QColor &accent,
                             const QString &label,
                             float value) {
        if (halfLength <= 0.001f)
            return;

        QVector3D localSide;
        if (qAbs(localAxis.z()) > 0.5f)
            localSide = QVector3D(1.0f, 0.0f, 0.0f);
        else
            localSide = QVector3D(0.0f, 0.0f, 1.0f);

        const QVector3D negative = transformed(-localAxis * halfLength + localSide * sideOffset);
        const QVector3D positive = transformed(localAxis * halfLength + localSide * sideOffset);
        const QVector3D center = transformed(localSide * sideOffset);
        const QPointF negativePoint = project(negative);
        const QPointF positivePoint = project(positive);
        const QPointF centerPoint = project(center);

        drawHaloLine(&painter, negativePoint, positivePoint, accent, 2.6);
        drawArrowHead(&painter, centerPoint, positivePoint, accent, 12.0f, 5.0f, 2.0);
        drawArrowHead(&painter, centerPoint, negativePoint, accent, 12.0f, 5.0f, 2.0);
        drawDirectionLabel(&painter, positivePoint, centerPoint, "+");
        drawDirectionLabel(&painter, negativePoint, centerPoint, "-");
        drawValueLabel(&painter, centerPoint + QPointF(0.0, -24.0), QStringLiteral("%1 %2").arg(label, formatPreviewValue(value)));
    };

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (shape->type == ShapeNode::Cube) {
        QVector3D localAxis;
        QColor accent;
        float halfLength = 0.0f;
        if (w.m_treeShapePreviewParameter == 0) {
            localAxis = QVector3D(1.0f, 0.0f, 0.0f);
            accent = ViewportConstants::kAxisXColor;
            halfLength = shape->size.x() * 0.5f;
            drawDimension(localAxis, halfLength, 9.0f, accent, QStringLiteral("X"), shape->size.x());
        } else if (w.m_treeShapePreviewParameter == 1) {
            localAxis = QVector3D(0.0f, 1.0f, 0.0f);
            accent = ViewportConstants::kAxisYColor;
            halfLength = shape->size.y() * 0.5f;
            drawDimension(localAxis, halfLength, 9.0f, accent, QStringLiteral("Y"), shape->size.y());
        } else if (w.m_treeShapePreviewParameter == 2) {
            localAxis = QVector3D(0.0f, 0.0f, 1.0f);
            accent = ViewportConstants::kAxisZColor;
            halfLength = shape->size.z() * 0.5f;
            drawDimension(localAxis, halfLength, 9.0f, accent, QStringLiteral("Z"), shape->size.z());
        }
    } else if (shape->type == ShapeNode::Cylinder && w.m_treeShapePreviewParameter == 1) {
        drawDimension(QVector3D(0.0f, 0.0f, 1.0f), shape->height * 0.5f, shape->radius + 8.0f, ViewportConstants::kAxisZColor, QStringLiteral("H"), shape->height);
    } else {
        const float radius = shape->radius;
        if (radius > 0.001f) {
            const QColor accent(255, 190, 85);
            QVector<QPointF> circlePoints;
            for (int step = 0; step <= 48; ++step) {
                const float angle = qDegreesToRadians(step * 360.0f / 48.0f);
                circlePoints.append(project(transformed(QVector3D(qCos(angle) * radius, qSin(angle) * radius, 0.0f))));
            }

            drawHaloPolyline(&painter, QPolygonF(circlePoints), accent, 2.4);

            const QVector3D center = transformed(QVector3D());
            const QVector3D edge = transformed(QVector3D(radius, 0.0f, 0.0f));
            const QVector3D inward = transformed(QVector3D(radius * 0.45f, 0.0f, 0.0f));
            const QPointF centerPoint = project(center);
            const QPointF edgePoint = project(edge);
            const QPointF inwardPoint = project(inward);
            drawHaloLine(&painter, centerPoint, edgePoint, accent, 2.6);
            drawArrowHead(&painter, inwardPoint, edgePoint, accent, 12.0f, 5.0f, 2.0);
            drawArrowHead(&painter, inwardPoint, centerPoint, accent, 12.0f, 5.0f, 2.0);
            drawDirectionLabel(&painter, edgePoint, inwardPoint, "+");
            drawDirectionLabel(&painter, centerPoint, inwardPoint, "-");
            drawValueLabel(&painter, inwardPoint + QPointF(0.0, -24.0), QStringLiteral("R %1").arg(formatPreviewValue(radius)));
        }
    }

    painter.restore();
}
