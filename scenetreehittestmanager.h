#ifndef SCENETREEHITTESTMANAGER_H
#define SCENETREEHITTESTMANAGER_H

#include "scenetreelayout.h"
#include "scenetreenoderenderer.h"
#include "scenedocument.h"

#include <QPointF>

class SceneTreeGraphicsWidget;

// Stateless hit-testing cluster for the scene-tree canvas.
// All 9 *ControlAt() methods moved here from SceneTreeGraphicsWidget,
// leaving thin wrappers on the widget for backward-compatible call-sites.
class SceneTreeHitTestManager
{
    friend class SceneTreeGraphicsWidget;
    friend class SceneTreeHoverManager;

public:
    explicit SceneTreeHitTestManager(SceneTreeGraphicsWidget *widget);

    bool colorChannelControlAt(const QPointF &scenePosition,
                               int *groupId, int *channel) const;

    bool transformControlAt(const QPointF &scenePosition,
                            int *groupId,
                            SceneDocument::TreeNode::Operation *operation,
                            int *axis,
                            int *numberStart = nullptr,
                            int *numberLength = nullptr) const;

    bool shapeParameterControlAt(const QPointF &scenePosition,
                                 int *shapeId, int *nodeId,
                                 int *parameter,
                                 int *numberStart, int *numberLength) const;

    bool polyhedronTableControlAt(const QPointF &scenePosition,
                                  PolyhedronTableItem::Cell *cell) const;

    int  polyhedronGroupIdForCell(const QPointF &scenePosition) const;

    bool polygon2DTableControlAt(const QPointF &scenePosition,
                                 Polygon2DTableItem::Cell *cell) const;

    bool variableNumberControlAt(const QPointF &scenePosition,
                                 int *nodeId, int *start, int *length) const;

    bool forLoopRangeControlAt(const QPointF &scenePosition,
                               int *nodeId, int *start, int *length) const;

    bool moduleCallParamControlAt(const QPointF &scenePosition,
                                  int *moduleCallNodeId, int *paramVarNodeId,
                                  int *start, int *length) const;

private:
    SceneTreeGraphicsWidget *m_widget;
};

#endif // SCENETREEHITTESTMANAGER_H
