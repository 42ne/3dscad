#ifndef SCENETREEGRAPHICSWIDGET_H
#define SCENETREEGRAPHICSWIDGET_H

#include "scenedocument.h"

#include <QGraphicsView>
#include <QVector>
#include <functional>

class QGraphicsScene;
class QMouseEvent;

class SceneTreeGraphicsWidget : public QGraphicsView
{
public:
    explicit SceneTreeGraphicsWidget(QWidget *parent = nullptr);

    void setSceneDocument(const SceneDocument *scene);
    void setToolDroppedCallback(std::function<void(const QString &, int)> callback);
    void setTreeNodeDroppedCallback(std::function<void(int, int)> callback);
    void setTreeNodeSelectedCallback(std::function<void(int)> callback);
    void setSelectedTreeNodeId(int nodeId);
    void refresh();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QRectF drawToolbar();
    QRectF drawNode(const SceneDocument::TreeNode &node, const QPointF &topLeft, int depth);
    QRectF drawPrimitive(const SceneDocument::TreeNode &node, const QPointF &topLeft);
    QRectF drawGroup(const SceneDocument::TreeNode &node, const QPointF &topLeft, int depth);
    void handleToolDrop(const QString &toolName, const QPointF &scenePosition);
    void handleTreeNodeDrop(int nodeId, const QPointF &scenePosition);
    void handleTreeNodeSelected(int nodeId);
    int groupIdAt(const QPointF &scenePosition) const;
    QString labelForPrimitive(int shapeId) const;
    QString labelForGroup(SceneDocument::TreeNode::Operation operation) const;
    QColor colorForGroup(SceneDocument::TreeNode::Operation operation) const;

private:
    struct GroupHitArea
    {
        QRectF rect;
        int groupId = 0;
        int depth = 0;
    };

    QGraphicsScene *m_graphicsScene = nullptr;
    const SceneDocument *m_scene = nullptr;
    QVector<GroupHitArea> m_groupHitAreas;
    std::function<void(const QString &, int)> m_toolDroppedCallback;
    std::function<void(int, int)> m_treeNodeDroppedCallback;
    std::function<void(int)> m_treeNodeSelectedCallback;
    int m_selectedTreeNodeId = 0;
    QPoint m_lastPanPoint;
    bool m_panning = false;
};

#endif
