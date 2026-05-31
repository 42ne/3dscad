#ifndef SCENETREENODERENDERER_H
#define SCENETREENODERENDERER_H

#include "scenedocument.h"
#include "scenetreegraphicshelpers.h"
#include "shapenode.h"

#include <QGraphicsItem>
#include <QImage>
#include <QRectF>
#include <QVector>
#include <functional>

class QGraphicsScene;
class QGraphicsItem;

// ── Polyhedron table item ────────────────────────────────────────────────────
class PolyhedronTableItem final : public QGraphicsItem
{
public:
    struct Cell {
        QRectF rect;
        enum Type { None, PtX, PtY, PtZ, RemovePt, RemoveFace, AddPt, AddFace, AutoFace, PtLabel, FaceLabel, FaceParticipate, TemplateButton, ClearPolyhedron };
        Type type = None;
        int index = -1;
        int sub   = -1;
        int nodeId = 0;
    };

    using CellCallback = std::function<void(const Cell &cell, int wheelSteps, bool clicked)>;

    PolyhedronTableItem(const QRectF &rect,
                        int groupNodeId,
                        const SceneDocument *scene,
                        CellCallback onCellEdited = nullptr,
                        int theme = 0,
                        int activeShapeNodeId = 0,
                        int activeParamIndex = -1,
                        int activeNumberStart = -1);

    QRectF boundingRect() const override { return m_rect; }

    Cell cellAt(const QPointF &pos) const;
    const QVector<Cell> &cells() const { return m_cells; }
    int groupNodeId() const { return m_groupNodeId; }
    int pointCount() const { return m_points.size(); }
    int faceCount() const { return m_faces.size(); }
    int pointNodeIdForIndex(int pointIndex) const { return (pointIndex >= 0 && pointIndex < m_points.size()) ? m_points[pointIndex].nodeId : 0; }
    int faceNodeIdForIndex(int faceIndex) const { return (faceIndex >= 0 && faceIndex < m_faces.size()) ? m_faces[faceIndex].nodeId : 0; }
    qreal tableWidth() const { return m_tableWidth; }
    qreal tableHeight() const { return m_tableHeight; }

    static QSizeF estimateSize(int groupNodeId, const SceneDocument *scene);

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override;

private:
    void gatherData();
    void computeLayout();

    QRectF m_rect;
    int m_groupNodeId;
    const SceneDocument *m_scene;
    CellCallback m_onCellEdited;
    int m_theme = 0;
    int m_activeShapeNodeId = 0;
    int m_activeParamIndex = -1;
    int m_activeNumberStart = -1;

    struct PointData { int nodeId; QVector3D position; };
    struct FaceData  { int nodeId; QVector<int> indices; };
    QVector<PointData> m_points;
    QVector<FaceData>  m_faces;
    QVector<Cell> m_cells;
    qreal m_tableWidth  = 0;
    qreal m_tableHeight = 0;
};

class SceneTreeNodeRenderer
{
public:
    using NodeSelectedCallback = std::function<void(int)>;

    SceneTreeNodeRenderer(QGraphicsScene *scene,
                          int selectedNodeId,
                          NodeSelectedCallback onSelected,
                          int activeTransformNodeId = 0,
                          int activeTransformAxis = -1,
                          int activeTransformNumberStart = -1,
                          int activeShapeNodeId = 0,
                          int activeShapeParameter = -1,
                          int activeShapeParamNumberStart = -1,
                          int activeVariableNodeId = 0,
                          int activeVariableNumberStart = -1,
                          int activeForLoopNodeId = 0,
                          int activeForLoopNumberStart = -1,
                          int activeModuleCallNodeId = 0,
                          int activeModuleCallVarNodeId = 0,
                          int activeModuleCallNumberStart = -1);

    void renderPrimitive(const SceneDocument::TreeNode &node,
                         const QRectF &rect,
                         const QString &label,
                         const ShapeNode *shape,
                         const QImage &thumbnail = QImage());

    void renderVariable(const SceneDocument::TreeNode &node,
                        const QRectF &rect);

    void renderModuleCall(const SceneDocument::TreeNode &node,
                          const QRectF &rect,
                          const QVector<SceneTreeGraphics::ModuleCallParam> &params = {},
                          const QImage &thumbnail = QImage());

    void renderGroup(const SceneDocument::TreeNode &node,
                     const QRectF &rect,
                     int depth,
                     qreal cutSeparatorY,
                     const QImage &thumbnail = QImage(),
                     bool collapsed = false);

    // Set the active visual theme (0 = Frost, 1 = Glass, 2 = Embers, 3 = Deep).
    // Returns *this so it can be chained: SceneTreeNodeRenderer(...).setTheme(t).renderXxx(...)
    SceneTreeNodeRenderer &setTheme(int theme) { m_theme = theme; return *this; }

    static void renderPreviewTool(QGraphicsScene *scene,
                                  QVector<QGraphicsItem *> *items,
                                  const QString &tool,
                                  const QRectF &rect,
                                  int theme = 0);
    static void renderPreviewPrimitive(QGraphicsScene *scene,
                                       QVector<QGraphicsItem *> *items,
                                       const ShapeNode *shape,
                                       int shapeId,
                                       const QRectF &rect);
    static void renderPreviewVariable(QGraphicsScene *scene,
                                      QVector<QGraphicsItem *> *items,
                                      const QString &name,
                                      const QString &expression,
                                      bool isParameter,
                                      const QRectF &rect,
                                      int theme = 0);
    static void renderPreviewGroup(QGraphicsScene *scene,
                                   QVector<QGraphicsItem *> *items,
                                   SceneDocument::TreeNode::Operation operation,
                                   const QRectF &rect,
                                   qreal cutSeparatorY = 0.0,
                                   int theme = 0,
                                   int depth = 0,
                                   const QColor &color = QColor());

private:
    qreal zForDepth(int depth, qreal offset) const;

private:
    QGraphicsScene *m_scene = nullptr;
    int m_selectedNodeId = 0;
    int m_activeTransformNodeId = 0;
    int m_activeTransformAxis = -1;
    int m_activeTransformNumberStart = -1;
    int m_activeShapeNodeId = 0;
    int m_activeShapeParameter = -1;
    int m_activeShapeParamNumberStart = -1;
    int m_activeVariableNodeId = 0;
    int m_activeVariableNumberStart = -1;
    int m_activeForLoopNodeId = 0;
    int m_activeForLoopNumberStart = -1;
    int m_activeModuleCallNodeId = 0;
    int m_activeModuleCallVarNodeId = 0;
    int m_activeModuleCallNumberStart = -1;
    int m_theme = 0;  // SceneTreePalette::Theme cast to int; 0 = Frost
    NodeSelectedCallback m_onSelected;
};

#endif
