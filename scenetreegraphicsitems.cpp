#include "scenetreegraphicsitems.h"
#include "scenetreeiconpainter.h"
#include "scenetreepreviewgeometry.h"
#include "scenetreetoolmetadata.h"
#include "scenetreepalette.h"

#include <QBrush>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QStyleOptionGraphicsItem>
#include <QtGlobal>
#include <QWidget>

namespace SceneTreeGraphics {

constexpr SceneTreePalette::Theme FixedToolbarTheme = SceneTreePalette::Theme::Ocean;

void appendPreviewItem(QVector<QGraphicsItem *> *items, QGraphicsItem *item)
{
    if (!items || !item)
        return;
    items->append(item);
}

QPainterPath dragFocusOutlinePath(const QString &tool, const QRectF &rect)
{
    Q_UNUSED(tool);

    QPainterPath path;
    path.addRoundedRect(rect.adjusted(-4.0, -4.0, 4.0, 4.0), CornerRadius + 2.0, CornerRadius + 2.0);
    return path;
}

QGraphicsPathItem *addDragFocusOutline(QGraphicsScene *scene,
                                       QVector<QGraphicsItem *> *items,
                                       const QString &tool,
                                       const QRectF &rect,
                                       qreal zValue)
{
    auto *outline = scene->addPath(dragFocusOutlinePath(tool, rect),
                                   QPen(QColor(74, 190, 116), 2, Qt::DashLine),
                                   Qt::NoBrush);
    outline->setZValue(zValue);
    appendPreviewItem(items, outline);
    return outline;
}

QGraphicsPathItem *addDropSlotMarker(QGraphicsScene *scene,
                                     QVector<QGraphicsItem *> *items,
                                     const QRectF &rect,
                                     qreal zValue)
{
    const qreal centerY = rect.isValid() ? rect.center().y() : rect.top();
    const qreal tipX = rect.left() - 7.0;
    const qreal baseX = tipX - 12.0;
    const qreal halfHeight = 7.0;

    QPainterPath path;
    path.moveTo(tipX, centerY);
    path.lineTo(baseX, centerY - halfHeight);
    path.lineTo(baseX, centerY + halfHeight);
    path.closeSubpath();

    auto *marker = scene->addPath(path,
                                  QPen(QColor(38, 145, 82, 210), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin),
                                  QBrush(QColor(74, 190, 116, 205)));
    marker->setZValue(zValue);
    appendPreviewItem(items, marker);

    auto *shadow = scene->addPath(path,
                                  QPen(QColor(0, 0, 0, 55), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin),
                                  QBrush(QColor(0, 0, 0, 35)));
    shadow->setZValue(zValue - 0.1);
    appendPreviewItem(items, shadow);
    return marker;
}

class TreeNodeDragHandleItem : public QGraphicsRectItem
{
public:
    TreeNodeDragHandleItem(int nodeId,
                           const QString &label,
                           const QRectF &rect,
                           const QRectF &sourceRect,
                           std::function<void(int)> onSelected,
                           const QSizeF &previewSize,
                           std::function<void(const QPointF &, const QSizeF &, const QString &)> onPreviewMoved,
                           std::function<void()> onPreviewFinished,
                           std::function<void(int, const QPointF &)> onDropped)
        : QGraphicsRectItem(rect)
        , m_nodeId(nodeId)
        , m_label(label)
        , m_sourceRect(sourceRect)
        , m_previewSize(previewSize)
        , m_onSelected(onSelected)
        , m_onPreviewMoved(onPreviewMoved)
        , m_onPreviewFinished(onPreviewFinished)
        , m_onDropped(onDropped)
    {
        setPen(Qt::NoPen);
        setBrush(QColor(0, 0, 0, 1));
        setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
        setZValue(70.0);
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (event->button() == Qt::RightButton) {
            if (m_onSelected)
                m_onSelected(m_nodeId);
            event->accept();
            return;
        }

        if (event->button() != Qt::LeftButton) {
            event->ignore();
            return;
        }

        m_pressScenePos = event->scenePos();
        m_previewActive = false;
        createDragSnapshot(m_pressScenePos);
        event->accept();
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override
    {
        moveDragSnapshot(event->scenePos());
        if (!m_previewActive) {
            const QPointF delta = event->scenePos() - m_pressScenePos;
            if (delta.x() * delta.x() + delta.y() * delta.y() < DragPreviewStartDistance * DragPreviewStartDistance) {
                event->accept();
                return;
            }
            m_previewActive = true;
        }
        if (m_onPreviewMoved)
            m_onPreviewMoved(draggedRectCenter(event->scenePos()), m_previewSize, m_label);
        event->accept();
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override
    {
        const QPointF dropPosition = event->scenePos();
        const QPointF dropCenter = draggedRectCenter(dropPosition);
        const bool wasPreviewActive = m_previewActive;
        const int nodeId = m_nodeId;
        const auto onPreviewFinished = m_onPreviewFinished;
        const auto onDropped = m_onDropped;

        moveDragSnapshot(dropPosition);
        delete m_dragSnapshot;
        m_dragSnapshot = nullptr;
        delete m_dragOutline;
        m_dragOutline = nullptr;
        m_previewActive = false;
        event->accept();

        if (wasPreviewActive && onPreviewFinished)
            onPreviewFinished();
        if (wasPreviewActive && onDropped)
            onDropped(nodeId, dropCenter);
    }

private:
    void createDragSnapshot(const QPointF &scenePosition)
    {
        if (!scene() || !m_sourceRect.isValid())
            return;

        QPixmap pixmap(m_sourceRect.size().toSize());
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        scene()->render(&painter, QRectF(QPointF(0.0, 0.0), m_sourceRect.size()), m_sourceRect);

        m_dragOffset = scenePosition - m_sourceRect.topLeft();
        m_dragSnapshot = scene()->addPixmap(pixmap);
        m_dragSnapshot->setOpacity(0.88);
        m_dragSnapshot->setZValue(120.0);
        m_dragOutline = scene()->addPath(dragFocusOutlinePath(m_label, QRectF(QPointF(0.0, 0.0), m_sourceRect.size())),
                                         QPen(QColor(74, 190, 116), 2, Qt::DashLine),
                                         Qt::NoBrush);
        m_dragOutline->setZValue(121.0);
        moveDragSnapshot(scenePosition);
    }

    void moveDragSnapshot(const QPointF &scenePosition)
    {
        const QPointF dragPos = scenePosition - m_dragOffset;
        if (m_dragSnapshot)
            m_dragSnapshot->setPos(dragPos);
        if (m_dragOutline)
            m_dragOutline->setPos(dragPos);
    }

    QPointF draggedRectCenter(const QPointF &scenePosition) const
    {
        return scenePosition - m_dragOffset + QPointF(m_previewSize.width() * 0.5,
                                                      m_previewSize.height() * 0.5);
    }

    int m_nodeId = 0;
    QString m_label;
    QRectF m_sourceRect;
    QPointF m_pressScenePos;
    QPointF m_dragOffset;
    QSizeF m_previewSize;
    bool m_previewActive = false;
    std::function<void(int)> m_onSelected;
    std::function<void(const QPointF &, const QSizeF &, const QString &)> m_onPreviewMoved;
    std::function<void()> m_onPreviewFinished;
    std::function<void(int, const QPointF &)> m_onDropped;
    QGraphicsPixmapItem *m_dragSnapshot = nullptr;
    QGraphicsPathItem *m_dragOutline = nullptr;
};

class TreeNodeSelectionItem : public QGraphicsRectItem
{
public:
    TreeNodeSelectionItem(int nodeId, const QRectF &rect, qreal zValue, std::function<void(int)> onSelected)
        : QGraphicsRectItem(rect)
        , m_nodeId(nodeId)
        , m_onSelected(onSelected)
    {
        setPen(Qt::NoPen);
        setBrush(QColor(0, 0, 0, 1));
        setAcceptedMouseButtons(Qt::RightButton);
        setZValue(zValue);
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (event->button() != Qt::RightButton) {
            event->ignore();
            return;
        }

        if (m_onSelected)
            m_onSelected(m_nodeId);
        event->accept();
    }

private:
    int m_nodeId = 0;
    std::function<void(int)> m_onSelected;
};

class PaletteToolItem : public QGraphicsItem
{
public:
    PaletteToolItem(const QString &label,
                    const QColor &fill,
                    int theme,
                    std::function<void(const QPointF &, const QSizeF &, const QString &)> onPreviewMoved,
                    std::function<void()> onPreviewFinished,
                    std::function<void(const QString &, const QPointF &)> onDropped,
                    std::function<void(const QString &, bool)> onHoverChanged)
        : m_label(label)
        , m_fill(fill)
        , m_theme(theme)
        , m_onPreviewMoved(onPreviewMoved)
        , m_onPreviewFinished(onPreviewFinished)
        , m_onDropped(onDropped)
        , m_onHoverChanged(onHoverChanged)
    {
        setAcceptedMouseButtons(Qt::LeftButton);
        setAcceptHoverEvents(true);
        setZValue(100.0);
    }

    QRectF boundingRect() const override { return QRectF(0.0, 0.0, ToolSize, ToolSize); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        const auto theme = FixedToolbarTheme;
        QColor accent = m_fill;
        if (isVariableToolName(m_label))
            accent = QColor(226, 185, 88);
        SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
        const bool operationTool = operationForToolName(m_label, &operation);
        if (operationTool)
            accent = SceneTreePalette::groupFill(operation, 0, theme);

        QColor iconAccent = accent;
        if (operationTool) {
            int h = 0;
            int s = 0;
            int v = 0;
            iconAccent.getHsv(&h, &s, &v);
            if (s < 55) {
                if (operation == SceneDocument::TreeNode::Module)
                    iconAccent = QColor(182, 205, 230);
                else if (operation == SceneDocument::TreeNode::Minkowski)
                    iconAccent = QColor(64, 86, 116);
                else if (operation == SceneDocument::TreeNode::For)
                    iconAccent = QColor(82, 104, 132);
                else
                    iconAccent.setHsv(h < 0 ? 210 : h, 82, qMax(v, 210));
            } else {
                iconAccent.setHsv(h, qMax(s, 95), qMax(v, 214));
            }
        }

        QRectF glyphRect = paintToolbarIconFrame(painter, boundingRect(), iconAccent);
        const qreal s = ToolSize / 54.0;
        if (m_label == QStringLiteral("module"))
            glyphRect = QRectF(8.0*s, 6.0*s, 38.0*s, 40.0*s);
        if (isVariableToolName(m_label)) {
            const QRectF badgeRect = glyphRect.adjusted(0.0, 8.0*s, 0.0, -8.0*s);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0, 0, 0, 35));
            painter->drawRoundedRect(badgeRect.translated(1.0, 1.0), 4.0*s, 4.0*s);
            QLinearGradient badgeGradient(badgeRect.topLeft(), badgeRect.bottomLeft());
            badgeGradient.setColorAt(0.0, QColor(255, 237, 172));
            badgeGradient.setColorAt(1.0, QColor(193, 143, 48));
            painter->setPen(QPen(QColor(255, 248, 218, 170), 1.0));
            painter->setBrush(QBrush(badgeGradient));
            painter->drawRoundedRect(badgeRect, 4.0*s, 4.0*s);
            QFont font = painter->font();
            font.setBold(true);
            font.setPointSizeF(qMax<qreal>(7.0, font.pointSizeF() - 1.0));
            painter->setFont(font);
            painter->setPen(QColor(61, 48, 24));
            painter->drawText(badgeRect, Qt::AlignCenter, QStringLiteral("VAR"));
        } else if (operationTool) {
            const QColor operationIconAccent = operation == SceneDocument::TreeNode::For
                                                   || operation == SceneDocument::TreeNode::Minkowski
                                                   ? iconAccent
                                                   : iconAccent.lighter(130);
            paintOperationIcon(painter, operation, glyphRect, operationIconAccent);
        } else {
            paintPrimitiveIcon(painter, primitiveTypeForTool(m_label), glyphRect);
        }
    }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override
    {
        if (m_onHoverChanged)
            m_onHoverChanged(m_label, true);
        event->accept();
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override
    {
        if (m_onHoverChanged)
            m_onHoverChanged(m_label, false);
        event->accept();
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *event) override { updatePreview(event); }
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override { updatePreview(event); }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override
    {
        const QPointF dropPosition = event->scenePos();
        const QString label = m_label;
        const auto onPreviewFinished = m_onPreviewFinished;
        const auto onDropped = m_onDropped;

        event->accept();

        if (onPreviewFinished)
            onPreviewFinished();
        if (onDropped)
            onDropped(label, dropPosition);
    }

private:
    void updatePreview(QGraphicsSceneMouseEvent *event)
    {
        if (m_onPreviewMoved)
            m_onPreviewMoved(event->scenePos(), previewSizeForTool(m_label), m_label);
        event->accept();
    }

    QString m_label;
    QColor m_fill;
    int m_theme = 0;
    std::function<void(const QPointF &, const QSizeF &, const QString &)> m_onPreviewMoved;
    std::function<void()> m_onPreviewFinished;
    std::function<void(const QString &, const QPointF &)> m_onDropped;
    std::function<void(const QString &, bool)> m_onHoverChanged;
};

QGraphicsItem *createTreeNodeDragHandleItem(int nodeId, const QString &label, const QRectF &rect, const QRectF &sourceRect, std::function<void(int)> onSelected, const QSizeF &previewSize, std::function<void(const QPointF &, const QSizeF &, const QString &)> onPreviewMoved, std::function<void()> onPreviewFinished, std::function<void(int, const QPointF &)> onDropped)
{
    return new TreeNodeDragHandleItem(nodeId, label, rect, sourceRect, onSelected, previewSize, onPreviewMoved, onPreviewFinished, onDropped);
}

QGraphicsItem *createTreeNodeSelectionItem(int nodeId, const QRectF &rect, qreal zValue, std::function<void(int)> onSelected)
{
    return new TreeNodeSelectionItem(nodeId, rect, zValue, onSelected);
}

QGraphicsItem *createPaletteToolItem(const QString &label, const QColor &fill, int theme, std::function<void(const QPointF &, const QSizeF &, const QString &)> onPreviewMoved, std::function<void()> onPreviewFinished, std::function<void(const QString &, const QPointF &)> onDropped, std::function<void(const QString &, bool)> onHoverChanged)
{
    return new PaletteToolItem(label, fill, theme, onPreviewMoved, onPreviewFinished, onDropped, onHoverChanged);
}

} // namespace SceneTreeGraphics
