#ifndef SCENETREEHOVERMANAGER_H
#define SCENETREEHOVERMANAGER_H

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QVector>

#include "scenedocument.h"
#include "scenetreegraphicswidget.h"

class QGraphicsItem;
class QGraphicsTextItem;

class SceneTreeHoverManager : public QObject
{
    Q_OBJECT
    friend class SceneTreeGraphicsWidget;
    friend class SceneTreeInlineEditor;

public:
    explicit SceneTreeHoverManager(SceneTreeGraphicsWidget *widget);

    void updateHighlights(const QPointF &scenePosition);
    void updateTooltip(const QPoint &globalPosition, const QPointF &scenePosition, bool controlDown);
    void clearHighlightOverlay();
    void updateHighlightOverlay();
    void drawHintOverlay();
    void updateZoomFpsHintThrottled(bool force = false);
    void updateHoverHint(const QString &key, const QString &text);
    const QString &hoverHintKey() const { return m_hoverHintKey; }
    bool hoverRenameZoneAt(const QPointF &scenePosition, int *nodeId, QRectF *zoneRect) const;
    void updateActiveTransformControl(const QPointF &scenePosition, bool enabled);
    void updateActiveColorChannelControl(const QPointF &scenePosition, bool enabled);
    void updateActiveShapeParameterControl(const QPointF &scenePosition, bool enabled);
    void updateActiveVariableNumberControl(const QPointF &scenePosition, bool enabled);
    void updateActiveForLoopRangeControl(const QPointF &scenePosition, bool enabled);
    void updateActiveModuleCallParamControl(const QPointF &scenePosition, bool enabled);
    bool expressionEditTargetAt(const QPointF &scenePosition, SceneTreeGraphicsWidget::ExpressionEditTarget *target) const;

private:
    void updatePolyhedronElementHover(const QPointF &scenePosition, bool enabled);
    void updatePolygonPointHover(const QPointF &scenePosition, bool enabled);
    QRectF hoverScrollZoneRect(const QPointF &scenePosition) const;
    QString hoverHintTextForPosition(const QPointF &scenePosition, bool controlDown, QString *key) const;
    QString hintOverlayText(const QString &baseHint) const;

    SceneTreeGraphicsWidget *m_widget;

    QVector<QGraphicsItem *> m_hoverHighlightItems;
    int m_hoveredPolyhedronElementNodeId = 0;
    int m_hoveredPolygonPointNodeId = 0;
    int m_hoveredPolygonPointIndex = -1;
    int m_activeTransformControlNodeId = 0;
    int m_activeTransformControlAxis = -1;
    int m_activeTransformControlNumberStart = -1;
    SceneDocument::TreeNode::Operation m_activeTransformControlOperation = SceneDocument::TreeNode::Union;
    int m_activeColorNodeId = 0;
    int m_activeColorChannel = -1;
    int m_activeShapeParameterNodeId = 0;
    int m_activeShapeParameter = -1;
    int m_activeShapeParameterNumberStart = -1;
    int m_activeVariableNodeId = 0;
    int m_activeVariableNumberStart = -1;
    int m_activeForLoopNodeId = 0;
    int m_activeForLoopNumberStart = -1;
    int m_activeModuleCallNodeId = 0;
    int m_activeModuleCallVarNodeId = 0;
    int m_activeModuleCallNumberStart = -1;
    QString m_hoverHintKey;
    QString m_hoverHintText;
    QPointer<QGraphicsTextItem> m_hintTextItem;
    qint64 m_lastZoomFpsHintMs = 0;
    QRectF m_hoveredScrollRect;
    QRectF m_hoveredRenameRect;
    QRectF m_hoveredExpressionRect;
};

#endif
