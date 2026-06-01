#ifndef VIEWPORTHELPERS_H
#define VIEWPORTHELPERS_H

#include "viewportwidget.h"
#include "scenedocument.h"
#include "scenemesh.h"
#include "csgevaluator.h"
#include "appearancethemes.h"
#include "shapenode.h"

#include <QColor>
#include <QImage>
#include <QMatrix4x4>
#include <QPainter>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSet>
#include <QSize>
#include <QString>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

struct ProjectedPoint
{
    QPointF point;
    float depth = 0.0f;
    bool visible = true;
};

struct Triangle2D
{
    QPointF a;
    QPointF b;
    QPointF c;
    float depthA = 0.0f;
    float depthB = 0.0f;
    float depthC = 0.0f;
    QColor color;
    int shapeIndex = -1;
    bool hatched = false;
};

struct SceneLight
{
    QVector3D direction;
    QColor color;
    float intensity = 1.0f;
};

namespace ViewportHelpers {

int clampColorChannel(float value);
float normalizedDegrees(float value);
QVector3D toCameraPoint(const QVector3D &world,
                        float yawDegrees,
                        float pitchDegrees,
                        float cameraDistance,
                        const QVector3D &cameraTarget = QVector3D());
QVector3D toCameraDirection(const QVector3D &world, float yawDegrees, float pitchDegrees);
QVector3D cameraRightVector(float yawDegrees);
QVector3D cameraUpVector(float yawDegrees, float pitchDegrees);
QVector3D rotatePoint(const QVector3D &point, const QVector3D &degrees);
QVector3D inverseRotatePoint(const QVector3D &point, const QVector3D &degrees);
QVector3D reflectAcrossPlane(const QVector3D &vector, const QVector3D &normal);
QVector3D transformPointForGroup(const QVector3D &point, const SceneDocument::TreeNode &group);
QVector3D transformPointByGroupStack(QVector3D point, const QVector<SceneDocument::TreeNode> &groupStack);
QVector3D transformVectorByGroupStack(QVector3D vector, const QVector<SceneDocument::TreeNode> &groupStack);
QVector3D transformNormalByGroupStack(QVector3D normal,
                                      const QVector<SceneDocument::TreeNode> &groupStack);
QVector3D inverseTransformVectorByGroupStack(QVector3D vector, const QVector<SceneDocument::TreeNode> &groupStack);
bool collectParentGroupStackForShape(const SceneDocument::TreeNode &node,
                                     int shapeId,
                                     QVector<SceneDocument::TreeNode> *groupStack);
bool collectParentGroupStackForGroup(const SceneDocument::TreeNode &node,
                                     int groupId,
                                     QVector<SceneDocument::TreeNode> *groupStack);
bool collectTreeNodePath(const SceneDocument::TreeNode &node,
                         int nodeId,
                         QVector<const SceneDocument::TreeNode *> *path);
int primitiveTreeNodeIdForShape(const SceneDocument::TreeNode &node, int shapeId);
const ShapeNode *shapeForPrimitiveNode(const SceneDocument *scene, const SceneDocument::TreeNode *node);
SceneMesh interactionMeshForShape(const SceneDocument *scene, const ShapeNode &shape);

ProjectedPoint projectWorldPoint(const QVector3D &world,
                                 const QSize &viewportSize,
                                 float yawDegrees,
                                 float pitchDegrees,
                                 float cameraDistance,
                                 const QVector3D &cameraTarget = QVector3D(),
                                 bool orthographic = false);
float distanceToSegment(const QPointF &point, const QPointF &a, const QPointF &b);
float cross2D(const QVector3D &origin, const QVector3D &a, const QVector3D &b);
QVector<QVector3D> convexHullXY(QVector<QVector3D> points);
qreal cross2D(const QPointF &origin, const QPointF &a, const QPointF &b);
QPolygonF convexHull2D(QVector<QPointF> points);

void drawArrowHead(QPainter *painter,
                   const QPointF &start,
                   const QPointF &end,
                   const QColor &color,
                   float length = 18.0f,
                   float width = 7.5f,
                   qreal outlineWidth = 3.0);
void drawHaloLine(QPainter *painter,
                  const QPointF &start,
                  const QPointF &end,
                  const QColor &color,
                  qreal width,
                  Qt::PenJoinStyle joinStyle = Qt::RoundJoin);
void drawVolumetricGizmoAxis(QPainter *painter, const QPointF &start, const QPointF &end, const QColor &color);
void drawHaloPolyline(QPainter *painter, const QPolygonF &points, const QColor &color, qreal width);
void drawValueLabel(QPainter *painter, const QPointF &anchor, const QString &text);
void drawGlassPanel(QPainter *painter, const QRectF &rect, bool darkTheme = true,
                    const ViewportAppearanceTheme *theme = nullptr);
QString formatPreviewValue(float value, int precision = -1);
void drawDirectionLabel(QPainter *painter, const QPointF &anchor, const QPointF &awayFrom, const QString &label);
QVector3D rotationRingPoint(const QVector3D &origin, ViewportWidget::DragMode dragMode, float radius, float degrees);
QVector3D rotationVectorForMode(ViewportWidget::DragMode dragMode, float degrees);

QColor litColor(const QColor &baseColor, const QVector3D &normal,
                const QVector<SceneLight> &lights, float ambient = 0.30f);
QColor thumbnailLitColor(const QColor &baseColor, const QVector3D &normal,
                         const QVector<SceneLight> &lights);
QVector<SceneLight> thumbnailLights();
QVector<SceneLight> viewportLightsForPreset(int preset);
float viewportAmbientForLightingPreset(int preset);
float viewportSpecularForLightingPreset(int preset);
QVector3D colorToVector(const QColor &color);
QVector4D colorToVector4(const QColor &color);
QColor viewportBackgroundColor(bool darkTheme, const ViewportAppearanceTheme *theme = nullptr);
QColor viewportMinorGridColor(bool darkTheme, const ViewportAppearanceTheme *theme = nullptr);
QColor viewportComputedSolidColor(bool darkTheme, int variant, const ViewportAppearanceTheme *theme = nullptr);
QColor viewportPlainSolidColor(bool darkTheme, int variant, const ViewportAppearanceTheme *theme = nullptr);
QColor subduedViewportColor(QColor color, float factor);
QColor selectionHighlightColor(QColor color, bool selected);

void collectPrimitiveShapeIds(const SceneDocument::TreeNode &node, QSet<int> *shapeIds);
bool selectionHasTreeNodeId(const SceneDocument *scene, int selectedGroupId);
QSet<int> selectedViewportShapeIds(const SceneDocument *scene,
                                   const QVector<ShapeNode> *shapes,
                                   int selectedIndex,
                                   int selectedGroupId);
bool itemBelongsToSelection(const CsgRenderItem &item,
                            const QVector<ShapeNode> *shapes,
                            const QSet<int> &selectedShapeIds,
                            int selectedTreeNodeId);

float edgeValue(const QPointF &a, const QPointF &b, const QPointF &point);
void rasterizeTriangle(QImage *image,
                       QVector<float> *depthBuffer,
                       QVector<int> *pickBuffer,
                       const QSize &viewportSize,
                       const QPointF &a,
                       const QPointF &b,
                       const QPointF &c,
                       float depthA,
                       float depthB,
                       float depthC,
                       const QColor &color,
                       int shapeIndex);
void drawTrianglesWithDepth(QPainter *painter,
                            const QVector<Triangle2D> &triangles,
                            const QSize &viewportSize,
                            QVector<int> *pickBuffer,
                            QVector<float> *depthBuffer,
                            QImage *image);
void drawTransparentTriangles(QPainter *painter, QVector<Triangle2D> triangles);

QVector<QPair<QVector3D, QVector3D>> meshEdges(const SceneMesh &mesh);
QVector<QPair<QVector3D, QVector3D>> characteristicMeshEdges(const SceneMesh &mesh,
                                                              float cameraYaw,
                                                              float cameraPitch,
                                                              bool visibleFacesOnly = true,
                                                              bool includeViewSilhouette = true,
                                                              bool includeStructural = true);
QVector<ViewportSelectionEdgeCandidate> selectionEdgeTopology(const SceneMesh &mesh);

uint shapeFingerprint(const ShapeNode &shape, uint seed);
uint shapesFingerprint(const QVector<ShapeNode> &shapes);
uint treeFingerprint(const SceneDocument::TreeNode &node, uint seed = 0);
uint sceneFingerprint(const SceneDocument &scene);

QMatrix4x4 buildViewMatrix(float yawDeg, float pitchDeg, float dist, const QVector3D &target);
QMatrix4x4 buildProjectionMatrix(float viewW, float viewH, float dist = 200.0f, bool ortho = false);

} // namespace ViewportHelpers

#endif
