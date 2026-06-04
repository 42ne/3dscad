#include "viewportsoftwarerenderer.h"
#include "viewporthelpers.h"
#include "viewportwidget.h" // ViewportSelectionEdgeCandidate, DragMode

#include <QPainter>
#include <QPolygonF>
#include <QtMath>

using namespace ViewportHelpers;

struct Line2D
{
    QPointF a;
    QPointF b;
    QColor color;
};

QString ViewportSoftwareRenderer::renderScene(QPainter &painter, const Context &ctx, bool drawSceneMeshes)
{
    if (drawSceneMeshes)
        painter.fillRect(QRect(QPoint(0, 0), ctx.viewportSize), viewportBackgroundColor(ctx.darkViewportTheme,
                         ctx.hasCustomAppearanceTheme ? &ctx.customAppearanceTheme : nullptr));

    const QVector<SceneLight> lights = viewportLightsForPreset(ctx.lightingPreset);

    const QSize &vpSize = ctx.viewportSize;
    auto project = [&](const QVector3D &world) {
        return ctx.camera.project(world, vpSize);
    };

    auto drawMinorGrid = [&]() {
        painter.setPen(viewportMinorGridColor(ctx.darkViewportTheme,
                       ctx.hasCustomAppearanceTheme ? &ctx.customAppearanceTheme : nullptr));

        for (int i = -ViewportConstants::kGridExtent; i <= ViewportConstants::kGridExtent; i += ViewportConstants::kGridStep) {
            painter.drawLine(project(QVector3D(-120, i, 0)).point, project(QVector3D(120, i, 0)).point);
            painter.drawLine(project(QVector3D(i, -120, 0)).point, project(QVector3D(i, 120, 0)).point);
        }
    };

    auto drawGridAxes = [&]() {
        const QPointF origin2D = project(QVector3D(0, 0, 0)).point;
        const float originDepth = qMax(8.0f, ctx.camera.toCameraPoint(QVector3D(0, 0, 0)).z());
        const float axisLen = 130.0f * 420.0f / originDepth;

        auto screenEnd = [&](const QVector3D &worldDir, float len) {
            const QVector3D camDir = ctx.camera.toCameraDirection(worldDir);
            const QVector2D sd(camDir.x(), -camDir.y());
            return sd.isNull() ? origin2D : origin2D + (sd.normalized() * len).toPointF();
        };

        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(ViewportConstants::kGridAxisXColor, 2));
        painter.drawLine(screenEnd(QVector3D(-1, 0, 0), axisLen), screenEnd(QVector3D(1, 0, 0), axisLen));
        painter.setPen(QPen(ViewportConstants::kGridAxisYColor, 2));
        painter.drawLine(screenEnd(QVector3D(0, -1, 0), axisLen), screenEnd(QVector3D(0, 1, 0), axisLen));
        painter.setPen(QPen(ViewportConstants::kGridAxisZColor, 2));
        painter.drawLine(origin2D, screenEnd(QVector3D(0, 0, 1), axisLen));
        painter.setRenderHint(QPainter::Antialiasing, false);
    };

    auto drawShadow = [&](const QVector<QVector3D> &points) {
        if (points.isEmpty())
            return;

        QVector<QVector3D> groundPoints;

        for (const QVector3D &point : points) {
            groundPoints.append(QVector3D(point.x(), point.y(), 0.0f));
        }

        const QVector<QVector3D> hull = convexHullXY(groundPoints);
        if (hull.size() < 3)
            return;

        QPolygonF shadow;
        float averageHeight = 0.0f;

        for (const QVector3D &point : points)
            averageHeight += qMax(0.0f, point.z());

        averageHeight /= points.size();

        for (const QVector3D &point : hull)
            shadow.append(project(point).point);

        const QPointF screenOffset(5.0, 6.0);
        const int alpha = qBound(20, 42 + static_cast<int>(averageHeight * 0.08f), 54);
        painter.setBrush(QColor(0, 0, 0, alpha));
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(shadow.translated(screenOffset));
    };

    auto appendMesh = [&](QVector<Triangle2D> &triangles,
                          const SceneMesh &mesh,
                          const QColor &baseColor,
                          int shapeIndex,
                          bool drawMeshShadow = true,
                          bool hatched = false,
                          bool lit = true) {
        if (drawSceneMeshes && drawMeshShadow)
            drawShadow(mesh.shadowPoints);

        for (const MeshTriangle &meshTriangle : mesh.triangles) {
            const ProjectedPoint a = project(meshTriangle.a);
            const ProjectedPoint b = project(meshTriangle.b);
            const ProjectedPoint c = project(meshTriangle.c);

            Triangle2D triangle;
            triangle.a = a.point;
            triangle.b = b.point;
            triangle.c = c.point;
            triangle.depthA = a.depth;
            triangle.depthB = b.depth;
            triangle.depthC = c.depth;
            triangle.color = lit ? litColor(baseColor.lighter(meshTriangle.shade), meshTriangle.normal, lights) : baseColor;
            triangle.shapeIndex = shapeIndex;
            triangle.hatched = hatched;
            triangles.append(triangle);
        }
    };

    auto appendProjectedHull = [&](QVector<QPair<QPolygonF, QColor>> &outlines,
                                   const SceneMesh &mesh,
                                   const QColor &color) {
        QVector<QPointF> points;
        points.reserve(mesh.triangles.size() * 3);
        for (const MeshTriangle &meshTriangle : mesh.triangles) {
            points.append(project(meshTriangle.a).point);
            points.append(project(meshTriangle.b).point);
            points.append(project(meshTriangle.c).point);
        }

        const QPolygonF hull = convexHull2D(points);
        if (hull.size() >= 3)
            outlines.append({hull, color});
    };

    auto appendCutFeatureEdges = [&](QVector<Line2D> &lines,
                                     const SceneMesh &mesh,
                                     const QColor &color,
                                     bool visibleFacesOnly = false) {
        const auto edges = characteristicMeshEdges(mesh, ctx.camera.yaw, ctx.camera.pitch,
                                                   visibleFacesOnly);
        for (const auto &edge : edges) {
            Line2D line;
            line.a = project(edge.first).point;
            line.b = project(edge.second).point;
            line.color = color;
            lines.append(line);
        }
    };

    if (drawSceneMeshes)
        drawMinorGrid();

    QString csgStatus = "CSG preview: plain mesh";

    if (ctx.shapes) {
        const QSet<int> selectedShapeIds = selectedViewportShapeIds(ctx.scene,
                                                                     ctx.shapes,
                                                                     ctx.selectedIndex,
                                                                     ctx.selectedGroupId);
        const int selectedTreeNodeId = selectionHasTreeNodeId(ctx.scene, ctx.selectedGroupId) ? ctx.selectedGroupId : 0;
        const bool hasViewportSelection = (!selectedShapeIds.isEmpty() || selectedTreeNodeId > 0)
                                          && !ctx.draggingGroup;
        const QColor selectionFeatureColor(255, 211, 112, 240);
        const QColor selectionGlowColor(255, 183, 64, 86);
        const qreal selectionEdgeWidth = 1.65;
        QVector<Triangle2D> triangles;
        QVector<Triangle2D> translucentHelperTriangles;
        QVector<Line2D> backgroundHelperLines;
        QVector<Line2D> foregroundHelperLines;
        QVector<Line2D> selectedFeatureLines;
        QVector<QPair<QPolygonF, QColor>> cutHelperOutlines;

        if (ctx.draggingShape || ctx.draggingGroup) {
            csgStatus = "CSG preview: interaction preview";

            if (drawSceneMeshes) {
                for (int i = 0; i < ctx.shapes->size(); ++i) {
                    const ShapeNode &shape = ctx.shapes->at(i);
                    QColor color = ViewportConstants::kDefaultMeshColor;
                    if (shape.booleanMode == ShapeNode::Subtract)
                        color = ViewportConstants::kSubtractColor;
                    else if (shape.booleanMode == ShapeNode::Intersect)
                        color = ViewportConstants::kIntersectColor;

                    const bool selected = selectedShapeIds.contains(shape.id);
                    if (hasViewportSelection)
                        color = selectionHighlightColor(color, selected);

                    const SceneMesh mesh = interactionMeshForShape(ctx.scene, shape);
                    appendMesh(triangles, mesh, color, i);
                    if (selected) {
                        appendCutFeatureEdges(selectedFeatureLines, mesh, selectionFeatureColor, false);
                    }
                }
            }
        } else {
            const CsgPreview &preview = *ctx.cachedCsgPreview;
            csgStatus = ctx.csgComputing ? QStringLiteral("CSG preview: computing…") : preview.statusText;
            // Check if a per-group 3D selection mesh is available (takes priority
            // over flat 2D helpers which show wrong geometry for LinearExtrude).
            bool hasSelectionGroupMatch = false;
            for (const CsgRenderItem &item : preview.items) {
                if (item.isSelectionGroup
                        && itemBelongsToSelection(item, ctx.shapes, selectedShapeIds, selectedTreeNodeId)) {
                    hasSelectionGroupMatch = true;
                    break;
                }
            }

            for (const CsgRenderItem &item : preview.items) {
                const bool selected = !ctx.draggingGroup
                                      && itemBelongsToSelection(item, ctx.shapes, selectedShapeIds, selectedTreeNodeId);
                QColor color = item.computed
                    ? (item.color.isValid() ? item.color : viewportComputedSolidColor(ctx.darkViewportTheme, ctx.viewportColorVariant, ctx.hasCustomAppearanceTheme ? &ctx.customAppearanceTheme : nullptr))
                    : (item.color.isValid() ? item.color : viewportPlainSolidColor(ctx.darkViewportTheme, ctx.viewportColorVariant, ctx.hasCustomAppearanceTheme ? &ctx.customAppearanceTheme : nullptr));
                if (!item.computed && item.booleanMode == ShapeNode::Subtract)
                    color = ViewportConstants::kSubtractColor;
                else if (!item.computed && item.booleanMode == ShapeNode::Intersect)
                    color = ViewportConstants::kIntersectColor;
                if (!item.helper && hasViewportSelection)
                    color = selectionHighlightColor(color, selected);

                if (item.helper) {
                    if (!drawSceneMeshes)
                        continue;

                    if (item.isSelectionGroup && selected) {
                        // Per-group 3D mesh: draw its edges as selection outline
                        appendCutFeatureEdges(selectedFeatureLines, item.mesh, selectionFeatureColor, false);
                    } else if (!item.isSelectionGroup && item.booleanMode == ShapeNode::Subtract) {
                        const bool selectedCut = selected;
                        QColor cutColor = selectedCut
                                              ? (ctx.darkViewportTheme ? QColor(188, 210, 218, 58) : QColor(190, 205, 212, 82))
                                              : (ctx.darkViewportTheme ? QColor(188, 210, 218, 14) : QColor(190, 205, 212, 22));
                        appendMesh(translucentHelperTriangles, item.mesh, cutColor, -1, false, true, false);
                        QColor cutEdgeColor = selectedCut
                                                  ? selectionFeatureColor
                                                  : (ctx.darkViewportTheme ? QColor(176, 216, 232, 76) : QColor(42, 68, 84, 44));
                        appendProjectedHull(cutHelperOutlines, item.mesh, cutEdgeColor);
                        if (selectedCut)
                            appendCutFeatureEdges(foregroundHelperLines, item.mesh, cutEdgeColor);
                    } else if (!item.isSelectionGroup && selected && !hasSelectionGroupMatch) {
                        // Flat helper fallback — only when no 3D selection mesh exists
                        appendCutFeatureEdges(selectedFeatureLines, item.mesh, selectionFeatureColor, true);
                    }
                } else if (drawSceneMeshes) {
                    appendMesh(triangles, item.mesh, color, item.shapeIndex);
                }
                if (!item.helper && selected && drawSceneMeshes) {
                    appendCutFeatureEdges(selectedFeatureLines, item.mesh, selectionFeatureColor, false);
                }
            }
        }

        for (const Line2D &line : backgroundHelperLines) {
            painter.setPen(QPen(line.color, 0.7, Qt::DashLine, Qt::RoundCap));
            painter.drawLine(line.a, line.b);
        }

        if (drawSceneMeshes) {
            *ctx.pickBufferSize = vpSize;
            drawTrianglesWithDepth(&painter, triangles, vpSize, ctx.pickBuffer, ctx.depthBuffer, ctx.renderImage);
        }

        if (drawSceneMeshes)
            drawGridAxes();

        drawTransparentTriangles(&painter, translucentHelperTriangles);

        for (const auto &outline : cutHelperOutlines) {
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(QPen(outline.second, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPolygon(outline.first);
        }

        for (const Line2D &line : foregroundHelperLines) {
            painter.setPen(QPen(line.color, 1.15, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(line.a, line.b);
        }

        for (const Line2D &line : selectedFeatureLines) {
            painter.setPen(QPen(selectionGlowColor, selectionEdgeWidth + 2.3,
                                Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(line.a, line.b);
            painter.setPen(QPen(line.color, selectionEdgeWidth, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(line.a, line.b);
        }

        const SceneDocument::TreeNode *selectedGroup = ctx.scene && ctx.selectedGroupId > 0
                                                            ? ctx.scene->treeNodeById(ctx.selectedGroupId)
                                                            : nullptr;
        const bool editableTransformGroup = selectedGroup
                                            && (selectedGroup->operation == SceneDocument::TreeNode::Translate
                                                || selectedGroup->operation == SceneDocument::TreeNode::Rotate);
        if (editableTransformGroup) {
            const bool showMoveAxes = selectedGroup->operation == SceneDocument::TreeNode::Translate;
            const bool showRotationRings = selectedGroup->operation == SceneDocument::TreeNode::Rotate;
            const QVector3D origin = transformOriginForGroup(ctx);
            const QVector<QPair<QVector3D, QColor>> axes = {
                {QVector3D(38.0f, 0.0f, 0.0f), ViewportConstants::kAxisXColor},
                {QVector3D(0.0f, 38.0f, 0.0f), ViewportConstants::kAxisYColor},
                {QVector3D(0.0f, 0.0f, 38.0f), ViewportConstants::kAxisZColor}
            };

            if (showMoveAxes) {
                for (const auto &axis : axes) {
                    const QPointF start = project(origin).point;
                    const QPointF end = project(origin + worldAxisVectorForGroup(ctx, axis.first)).point;
                    drawHaloLine(&painter, start, end, axis.second, 4.5);
                    drawArrowHead(&painter, start, end, axis.second);
                }
            }

            const QVector<QPair<ViewportWidget::DragMode, QColor>> rings = {
                {ViewportWidget::RotateXDrag, ViewportConstants::kRotateXColor},
                {ViewportWidget::RotateYDrag, ViewportConstants::kRotateYColor},
                {ViewportWidget::RotateZDrag, ViewportConstants::kRotateZColor}
            };

            if (showRotationRings) {
                for (const auto &ring : rings) {
                    QPolygonF ringPath;
                    for (int step = 0; step <= ViewportConstants::kRingSegments; ++step) {
                        const QVector3D worldPoint = rotationRingPoint(origin, ring.first, ViewportConstants::kRotationRingRadius, step * ViewportConstants::kRingStepDegrees);
                        ringPath << project(worldPoint).point;
                    }

                    painter.setBrush(Qt::NoBrush);
                    drawHaloPolyline(&painter, ringPath, ring.second, 2.2);
                }
            }
        }
    }

    return csgStatus;
}

QVector3D ViewportSoftwareRenderer::transformOriginForGroup(const Context &ctx) const
{
    if (!ctx.scene || ctx.selectedGroupId <= 0)
        return QVector3D();

    const SceneDocument::TreeNode *group = ctx.scene->treeNodeById(ctx.selectedGroupId);
    if (!group)
        return QVector3D();

    QVector<SceneDocument::TreeNode> groupStack;
    if (ctx.selectedGroupId > 0)
        collectParentGroupStackForGroup(ctx.scene->treeRoot(), ctx.selectedGroupId, &groupStack);

    return transformPointByGroupStack(group->position, groupStack);
}

QVector3D ViewportSoftwareRenderer::worldAxisVectorForGroup(const Context &ctx, const QVector3D &localAxis) const
{
    QVector<SceneDocument::TreeNode> groupStack;
    if (ctx.selectedGroupId > 0)
        collectParentGroupStackForGroup(ctx.scene->treeRoot(), ctx.selectedGroupId, &groupStack);

    return transformVectorByGroupStack(localAxis, groupStack);
}
