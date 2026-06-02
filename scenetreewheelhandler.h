#ifndef SCENETREEWHEELHANDLER_H
#define SCENETREEWHEELHANDLER_H

#include <QPointF>

class SceneTreeGraphicsWidget;

// Handles Ctrl+wheel adjustments for all 8 interactive control types
// (transform, color, shape param, variable, for-loop, module-call-param,
// polyhedron table, polygon2D table).
// Extracted from SceneTreeGraphicsWidget to keep the widget lean.
class SceneTreeWheelHandler
{
    friend class SceneTreeGraphicsWidget;
public:
    explicit SceneTreeWheelHandler(SceneTreeGraphicsWidget *widget);

    // Tries all 8 control types in priority order; returns true if consumed.
    bool handleCtrlWheel(const QPointF &scenePos, int wheelSteps);

private:
    bool handleTransformWheel       (const QPointF &scenePos, int steps);
    bool handleColorChannelWheel    (const QPointF &scenePos, int steps);
    bool handleShapeParameterWheel  (const QPointF &scenePos, int steps);
    bool handlePolyhedronTableWheel (const QPointF &scenePos, int steps);
    bool handlePolygon2DTableWheel  (const QPointF &scenePos, int steps);
    bool handleVariableNumberWheel  (const QPointF &scenePos, int steps);
    bool handleForLoopRangeWheel    (const QPointF &scenePos, int steps);
    bool handleModuleCallParamWheel (const QPointF &scenePos, int steps);

    SceneTreeGraphicsWidget *m_widget;
};

#endif // SCENETREEWHEELHANDLER_H
