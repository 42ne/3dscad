#include "scenetreenoderenderer.h"
#include "scenetreegraphicshelpers.h"

#include <QBrush>
#include <QGraphicsScene>
#include <QPen>
#include <utility>

using namespace SceneTreeGraphics;

SceneTreeNodeRenderer::SceneTreeNodeRenderer(QGraphicsScene *scene,
                                             int selectedNodeId,
                                             NodeSelectedCallback onSelected)
    : m_scene(scene)
    , m_selectedNodeId(selectedNodeId)
    , m_onSelected(std::move(onSelected))
{
}

void SceneTreeNodeRenderer::renderPrimitive(const SceneDocument::TreeNode &node,
                                            const QRectF &rect,
                                            const QString &label,
                                            ShapeNode::Type type)
{
    const QRectF iconRect(rect.left() + 20.0,
                          rect.top() + (PrimitiveHeight - PrimitiveIconSize) * 0.5,
                          PrimitiveIconSize,
                          PrimitiveIconSize);

    if (node.id == m_selectedNodeId)
        addPrimitiveSelectionHalo(m_scene, iconRect);

    addPrimitiveIcon(m_scene, type, iconRect);
    addPrimitiveNumberBadge(m_scene, primitiveNumberText(label, node.shapeId), rect);
}

void SceneTreeNodeRenderer::renderGroup(const SceneDocument::TreeNode &node,
                                        const QRectF &rect,
                                        int depth,
                                        qreal cutSeparatorY)
{
    const QColor fill = fillForOperation(node.operation);
    const bool selected = node.id == m_selectedNodeId;

    addSoftShadow(m_scene, rect, zForDepth(depth, -101.0));
    auto *groupItem = addRoundedPanel(m_scene,
                                      rect,
                                      CornerRadius,
                                      QPen(selected ? QColor(255, 203, 87) : fill.darker(145), selected ? 3 : 2),
                                      QBrush(fill),
                                      zForDepth(depth, -100.0));
    groupItem->setZValue(zForDepth(depth, -100.0));

    const QRectF headerRect(rect.left() + 1.5,
                            rect.top() + 1.5,
                            rect.width() - 3.0,
                            GroupHeaderHeight - 2.0);
    auto *header = addRoundedPanel(m_scene,
                                   headerRect,
                                   CornerRadius - 1.0,
                                   Qt::NoPen,
                                   QBrush(fill.lighter(112)),
                                   zForDepth(depth, -95.0));
    header->setZValue(zForDepth(depth, -95.0));

    m_scene->addItem(createTreeNodeSelectionItem(node.id,
                                                 rect,
                                                 zForDepth(depth, -80.0),
                                                 m_onSelected));

    const QRectF iconRect(rect.left() + 8.0, rect.top() + 6.0, 18.0, 18.0);
    addOperationIcon(m_scene, node.operation, iconRect, fill.darker(125));
    addLabel(m_scene, labelForOperation(node.operation), rect.topLeft() + QPointF(32.0, 7.0), QColor(24, 34, 44));

    if (node.children.isEmpty()) {
        addLabel(m_scene,
                 QStringLiteral("empty"),
                 QPointF(rect.left() + GroupPadding, rect.top() + GroupHeaderHeight + GroupPadding + 10.0),
                 QColor(95, 98, 105));
    }

    if (node.operation != SceneDocument::TreeNode::Difference)
        return;

    auto *separator = m_scene->addLine(rect.left() + GroupPadding,
                                       cutSeparatorY,
                                       rect.right() - GroupPadding,
                                       cutSeparatorY,
                                       QPen(QColor(130, 92, 70), 1, Qt::DashLine));
    separator->setZValue(zForDepth(depth, -70.0));

    addPillLabel(m_scene, QStringLiteral("base"), QPointF(rect.right() - 61.0, rect.top() + GroupHeaderHeight + 7.0), QColor(128, 99, 73));
    addPillLabel(m_scene, QStringLiteral("cut"), QPointF(rect.right() - 51.0, cutSeparatorY + 5.0), QColor(153, 85, 56));
}

qreal SceneTreeNodeRenderer::zForDepth(int depth, qreal offset) const
{
    return depth * 10.0 + offset;
}
