#include "scenetreegraphicswidget.h"

#include <QBrush>
#include <QGraphicsPixmapItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QScrollBar>
#include <QVarLengthArray>
#include <QWheelEvent>
#include <functional>
#include <cmath>

namespace {
constexpr qreal ToolbarX = 12.0;
constexpr qreal ToolbarY = 12.0;
constexpr qreal ToolSize = 54.0;
constexpr qreal ToolGap = 8.0;
constexpr qreal TreeX = 12.0;
constexpr qreal TreeY = 92.0;
constexpr qreal PrimitiveWidth = 88.0;
constexpr qreal PrimitiveHeight = 42.0;
constexpr qreal PrimitiveIconSize = 34.0;
constexpr qreal GroupMinWidth = 180.0;
constexpr qreal GroupHeaderHeight = 28.0;
constexpr qreal GroupPadding = 12.0;
constexpr qreal ChildGap = 10.0;
constexpr qreal CornerRadius = 5.0;
constexpr qreal DifferenceMinContentHeight = PrimitiveHeight * 2.0 + ChildGap;
constexpr qreal CanvasMargin = 2000.0;
const QColor CanvasBackground(31, 41, 55);
const QColor MinorGridColor(96, 106, 121);
const QColor MajorGridColor(139, 150, 166);

void drawCanvasGrid(QPainter *painter, const QRectF &rect, qreal gridSize, const QColor &color, int width)
{
    QVarLengthArray<QLineF, 128> lines;

    const qreal left = std::floor(rect.left() / gridSize) * gridSize;
    const qreal top = std::floor(rect.top() / gridSize) * gridSize;

    for (qreal x = left; x < rect.right(); x += gridSize)
        lines.append(QLineF(x, rect.top(), x, rect.bottom()));

    for (qreal y = top; y < rect.bottom(); y += gridSize)
        lines.append(QLineF(rect.left(), y, rect.right(), y));

    QPen gridPen(color);
    gridPen.setWidth(width);
    gridPen.setCosmetic(true);
    painter->setPen(gridPen);
    painter->drawLines(lines.constData(), lines.size());
}

class TreeGraphicsScene : public QGraphicsScene
{
public:
    explicit TreeGraphicsScene(QObject *parent = nullptr)
        : QGraphicsScene(parent)
    {
        setBackgroundBrush(CanvasBackground);
    }

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override
    {
        QGraphicsScene::drawBackground(painter, rect);
        drawCanvasGrid(painter, rect, 24.0, MinorGridColor, 1);
        drawCanvasGrid(painter, rect, 96.0, MajorGridColor, 1);
    }
};

void addLabel(QGraphicsScene *scene, const QString &text, const QPointF &position, const QColor &color = QColor(35, 35, 35))
{
    auto *label = scene->addSimpleText(text);
    label->setBrush(color);
    label->setPos(position);
}

QGraphicsRectItem *addSoftShadow(QGraphicsScene *scene, const QRectF &rect, qreal zValue)
{
    auto *shadow = scene->addRect(rect.translated(2.0, 2.0), Qt::NoPen, QBrush(QColor(0, 0, 0, 38)));
    shadow->setZValue(zValue);
    return shadow;
}

QGraphicsPathItem *addRoundedPanel(QGraphicsScene *scene,
                                   const QRectF &rect,
                                   qreal radius,
                                   const QPen &pen,
                                   const QBrush &brush,
                                   qreal zValue)
{
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    auto *panel = scene->addPath(path, pen, brush);
    panel->setZValue(zValue);
    return panel;
}

void addCenteredLabel(QGraphicsScene *scene, const QString &text, const QRectF &rect, const QColor &color)
{
    auto *label = scene->addSimpleText(text);
    label->setBrush(color);
    const QRectF labelRect = label->boundingRect();
    label->setPos(rect.center() - QPointF(labelRect.width() * 0.5, labelRect.height() * 0.5 + 1.0));
    label->setZValue(12.0);
}

void addPillLabel(QGraphicsScene *scene, const QString &text, const QPointF &position, const QColor &accent)
{
    auto *label = scene->addSimpleText(text);
    label->setBrush(accent.darker(135));
    const QRectF textRect = label->boundingRect();
    const QRectF pillRect(position, QSizeF(textRect.width() + 12.0, textRect.height() + 4.0));
    auto *pill = addRoundedPanel(scene, pillRect, 6.0, QPen(accent, 1), QBrush(QColor(255, 255, 255, 115)), 8.0);
    pill->setZValue(8.0);
    label->setPos(pillRect.left() + 6.0, pillRect.top() + 1.0);
    label->setZValue(9.0);
}

void addPrimitiveIcon(QGraphicsScene *scene, ShapeNode::Type type, const QRectF &rect)
{
    const QColor outline(59, 95, 134);
    const QColor face(178, 207, 238);
    const QColor faceLight(221, 235, 248);
    const QColor faceDark(139, 176, 214);

    if (type == ShapeNode::Sphere) {
        auto *sphere = scene->addEllipse(rect, QPen(outline, 1), QBrush(face));
        auto *latitude = scene->addEllipse(rect.adjusted(3.0, 9.0, -3.0, -9.0), QPen(QColor(93, 127, 166), 1), Qt::NoBrush);
        auto *highlight = scene->addEllipse(QRectF(rect.left() + rect.width() * 0.25,
                                                   rect.top() + rect.height() * 0.18,
                                                   rect.width() * 0.22,
                                                   rect.height() * 0.16),
                                            Qt::NoPen,
                                            QBrush(QColor(255, 255, 255, 165)));
        sphere->setZValue(5.0);
        latitude->setZValue(6.0);
        highlight->setZValue(7.0);
        return;
    }

    if (type == ShapeNode::Cylinder) {
        const QRectF top(rect.left() + 3.0, rect.top() + 3.0, rect.width() - 6.0, rect.height() * 0.34);
        const QRectF bottom(top.left(), rect.bottom() - top.height() - 3.0, top.width(), top.height());
        auto *body = scene->addRect(QRectF(top.left(), top.center().y(), top.width(), bottom.center().y() - top.center().y()),
                                    Qt::NoPen,
                                    QBrush(face));
        auto *left = scene->addLine(top.left(), top.center().y(), bottom.left(), bottom.center().y(), QPen(outline, 1));
        auto *right = scene->addLine(top.right(), top.center().y(), bottom.right(), bottom.center().y(), QPen(outline, 1));
        auto *bottomEllipse = scene->addEllipse(bottom, QPen(outline, 1), QBrush(faceDark));
        auto *topEllipse = scene->addEllipse(top, QPen(outline, 1), QBrush(faceLight));
        body->setZValue(5.0);
        left->setZValue(6.0);
        right->setZValue(6.0);
        bottomEllipse->setZValue(7.0);
        topEllipse->setZValue(8.0);
        return;
    }

    QPolygonF topFace;
    topFace << QPointF(rect.left() + rect.width() * 0.22, rect.top() + rect.height() * 0.34)
            << QPointF(rect.left() + rect.width() * 0.48, rect.top() + rect.height() * 0.12)
            << QPointF(rect.left() + rect.width() * 0.82, rect.top() + rect.height() * 0.28)
            << QPointF(rect.left() + rect.width() * 0.56, rect.top() + rect.height() * 0.50);

    QPolygonF leftFace;
    leftFace << topFace[0]
             << topFace[3]
             << QPointF(rect.left() + rect.width() * 0.56, rect.top() + rect.height() * 0.86)
             << QPointF(rect.left() + rect.width() * 0.22, rect.top() + rect.height() * 0.70);

    QPolygonF rightFace;
    rightFace << topFace[3]
              << topFace[2]
              << QPointF(rect.left() + rect.width() * 0.82, rect.top() + rect.height() * 0.64)
              << QPointF(rect.left() + rect.width() * 0.56, rect.top() + rect.height() * 0.86);

    auto *leftItem = scene->addPolygon(leftFace, QPen(outline, 1), QBrush(face));
    auto *rightItem = scene->addPolygon(rightFace, QPen(outline, 1), QBrush(faceDark));
    auto *topItem = scene->addPolygon(topFace, QPen(outline, 1), QBrush(faceLight));
    leftItem->setZValue(5.0);
    rightItem->setZValue(6.0);
    topItem->setZValue(7.0);
}

QString primitiveNumberText(const QString &label, int fallbackId)
{
    int end = label.size() - 1;
    while (end >= 0 && label[end].isSpace())
        --end;

    int start = end;
    while (start >= 0 && label[start].isDigit())
        --start;

    if (start < end)
        return label.mid(start + 1, end - start);

    return fallbackId > 0 ? QString::number(fallbackId) : QStringLiteral("?");
}

void addPrimitiveNumberBadge(QGraphicsScene *scene, const QString &number, const QRectF &rect)
{
    const QRectF badgeRect(rect.center().x() + 12.0, rect.top() + 5.0, 18.0, 18.0);
    auto *badge = scene->addEllipse(badgeRect, QPen(QColor(82, 111, 146), 1), QBrush(QColor(244, 248, 252)));
    badge->setZValue(9.0);

    auto *text = scene->addSimpleText(number);
    text->setBrush(QColor(30, 58, 90));
    const QRectF textRect = text->boundingRect();
    text->setPos(badgeRect.center() - QPointF(textRect.width() * 0.5, textRect.height() * 0.5 + 1.0));
    text->setZValue(10.0);
}

void addPrimitiveSelectionHalo(QGraphicsScene *scene, const QRectF &iconRect)
{
    auto *halo = scene->addEllipse(iconRect.adjusted(-5.0, -5.0, 5.0, 5.0),
                                   QPen(QColor(255, 203, 87), 2, Qt::DashLine),
                                   QBrush(QColor(255, 203, 87, 32)));
    halo->setZValue(4.0);
}

void addOperationIcon(QGraphicsScene *scene,
                      SceneDocument::TreeNode::Operation operation,
                      const QRectF &rect,
                      const QColor &accent)
{
    auto *frame = addRoundedPanel(scene, rect, 3.0, QPen(accent.darker(135), 1), QBrush(QColor(255, 255, 255, 135)), 9.0);
    frame->setZValue(9.0);

    const QPointF center = rect.center();
    const QRectF symbolRect = rect.adjusted(4.0, 4.0, -4.0, -4.0);
    QPen pen(accent.darker(160), 2);
    pen.setCapStyle(Qt::RoundCap);

    if (operation == SceneDocument::TreeNode::Union) {
        auto *h = scene->addLine(symbolRect.left(), center.y(), symbolRect.right(), center.y(), pen);
        auto *v = scene->addLine(center.x(), symbolRect.top(), center.x(), symbolRect.bottom(), pen);
        h->setZValue(10.0);
        v->setZValue(10.0);
        return;
    }

    if (operation == SceneDocument::TreeNode::Difference) {
        auto *minus = scene->addLine(symbolRect.left(), center.y(), symbolRect.right(), center.y(), pen);
        minus->setZValue(10.0);
        return;
    }

    if (operation == SceneDocument::TreeNode::Intersection) {
        auto *left = scene->addEllipse(QRectF(symbolRect.left(), symbolRect.top() + 1.0, symbolRect.width() * 0.62, symbolRect.height() - 2.0),
                                       QPen(accent.darker(150), 1),
                                       QBrush(QColor(255, 255, 255, 60)));
        auto *right = scene->addEllipse(QRectF(symbolRect.center().x() - symbolRect.width() * 0.31,
                                               symbolRect.top() + 1.0,
                                               symbolRect.width() * 0.62,
                                               symbolRect.height() - 2.0),
                                        QPen(accent.darker(150), 1),
                                        QBrush(QColor(255, 255, 255, 60)));
        left->setZValue(10.0);
        right->setZValue(10.0);
        return;
    }

    addCenteredLabel(scene, "M", rect, accent.darker(160));
}

int insertionIndexForY(const QVector<QRectF> &childRects, qreal y, int minimumIndex = 0)
{
    int insertIndex = childRects.size();
    for (int i = minimumIndex; i < childRects.size(); ++i) {
        if (y < childRects[i].center().y()) {
            insertIndex = i;
            break;
        }
    }

    return qMax(minimumIndex, insertIndex);
}

QSizeF defaultPreviewSize()
{
    return QSizeF(PrimitiveWidth, PrimitiveHeight);
}

QSizeF groupPreviewSize()
{
    return QSizeF(GroupMinWidth, GroupHeaderHeight + GroupPadding * 2.0 + PrimitiveHeight);
}

QSizeF differencePreviewSize()
{
    return QSizeF(GroupMinWidth, GroupHeaderHeight + GroupPadding * 2.0 + DifferenceMinContentHeight);
}

QSizeF previewSizeForTool(const QString &tool)
{
    if (tool == "cube" || tool == "sphere" || tool == "cylinder")
        return defaultPreviewSize();
    if (tool == "difference")
        return differencePreviewSize();

    return groupPreviewSize();
}

ShapeNode::Type primitiveTypeForTool(const QString &tool)
{
    const QString normalized = tool.toLower();
    if (normalized.contains("sphere"))
        return ShapeNode::Sphere;
    if (normalized.contains("cylinder"))
        return ShapeNode::Cylinder;
    return ShapeNode::Cube;
}

bool operationForToolName(const QString &tool, SceneDocument::TreeNode::Operation *operation)
{
    if (!operation)
        return false;

    const QString normalized = tool.toLower();
    if (normalized.contains("union")) {
        *operation = SceneDocument::TreeNode::Union;
        return true;
    }
    if (normalized.contains("difference")) {
        *operation = SceneDocument::TreeNode::Difference;
        return true;
    }
    if (normalized.contains("intersection")) {
        *operation = SceneDocument::TreeNode::Intersection;
        return true;
    }
    if (normalized.contains("module")) {
        *operation = SceneDocument::TreeNode::Module;
        return true;
    }
    return false;
}

QString labelForOperation(SceneDocument::TreeNode::Operation operation)
{
    if (operation == SceneDocument::TreeNode::Module)
        return "module";
    if (operation == SceneDocument::TreeNode::Difference)
        return "difference";
    if (operation == SceneDocument::TreeNode::Intersection)
        return "intersection";
    return "union";
}

QColor fillForTool(const QString &tool);

QRectF placeholderRectForInsertIndex(const QRectF &contentRect, const QVector<QRectF> &childRects, int insertIndex, const QSizeF &previewSize)
{
    if (childRects.isEmpty())
        return QRectF(contentRect.left(), contentRect.top(), previewSize.width(), previewSize.height());

    if (insertIndex <= 0)
        return QRectF(contentRect.left(), contentRect.top(), previewSize.width(), previewSize.height());

    if (insertIndex >= childRects.size())
        return QRectF(contentRect.left(), childRects.last().bottom() + ChildGap, previewSize.width(), previewSize.height());

    return QRectF(contentRect.left(), childRects[insertIndex].top(), previewSize.width(), previewSize.height());
}

QRectF expandedGroupRectForPreview(const QRectF &groupRect, const QRectF &placeholderRect, const QVector<QRectF> &childRects, int insertIndex, const QSizeF &previewSize)
{
    QRectF expanded = groupRect;
    QRectF futureContent = placeholderRect;
    const qreal shift = previewSize.height() + ChildGap;

    for (int i = 0; i < childRects.size(); ++i) {
        const QRectF childRect = i >= insertIndex ? childRects[i].translated(0.0, shift) : childRects[i];
        futureContent = futureContent.united(childRect);
    }

    expanded.setRight(qMax(expanded.right(), futureContent.right() + GroupPadding));
    expanded.setBottom(qMax(expanded.bottom(), futureContent.bottom() + GroupPadding));
    return expanded;
}

QRectF expandedGroupRectForChangedChild(const QRectF &groupRect, const QVector<QRectF> &childRects, const QRectF &oldChildRect, const QRectF &newChildRect)
{
    QRectF futureContent = newChildRect;
    const qreal shift = qMax<qreal>(0.0, newChildRect.height() - oldChildRect.height());
    bool passedChangedChild = false;

    for (const QRectF &childRect : childRects) {
        if (childRect == oldChildRect) {
            passedChangedChild = true;
            continue;
        }

        futureContent = futureContent.united(passedChangedChild ? childRect.translated(0.0, shift) : childRect);
    }

    QRectF expanded = groupRect;
    expanded.setRight(qMax(expanded.right(), futureContent.right() + GroupPadding));
    expanded.setBottom(qMax(expanded.bottom(), futureContent.bottom() + GroupPadding));
    return expanded;
}

class ToolGlyphItem : public QGraphicsItem
{
public:
    ToolGlyphItem(const QString &tool, const QRectF &rect, QGraphicsItem *parent = nullptr)
        : QGraphicsItem(parent)
        , m_tool(tool)
        , m_rect(rect)
    {
    }

    QRectF boundingRect() const override
    {
        return m_rect;
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);

        SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
        if (operationForToolName(m_tool, &operation)) {
            const QColor accent = fillForTool(m_tool).darker(125);
            painter->setPen(QPen(accent.darker(135), 1));
            painter->setBrush(QColor(255, 255, 255, 150));
            painter->drawRoundedRect(m_rect, 3.0, 3.0);

            const QPointF center = m_rect.center();
            const QRectF symbolRect = m_rect.adjusted(7.0, 7.0, -7.0, -7.0);
            QPen pen(accent.darker(160), 2);
            pen.setCapStyle(Qt::RoundCap);
            painter->setPen(pen);
            painter->setBrush(QColor(255, 255, 255, 70));

            if (operation == SceneDocument::TreeNode::Union) {
                painter->drawLine(QPointF(symbolRect.left(), center.y()), QPointF(symbolRect.right(), center.y()));
                painter->drawLine(QPointF(center.x(), symbolRect.top()), QPointF(center.x(), symbolRect.bottom()));
            } else if (operation == SceneDocument::TreeNode::Difference) {
                painter->drawLine(QPointF(symbolRect.left(), center.y()), QPointF(symbolRect.right(), center.y()));
            } else if (operation == SceneDocument::TreeNode::Intersection) {
                painter->drawEllipse(QRectF(symbolRect.left(), symbolRect.top() + 2.0, symbolRect.width() * 0.62, symbolRect.height() - 4.0));
                painter->drawEllipse(QRectF(symbolRect.center().x() - symbolRect.width() * 0.31,
                                            symbolRect.top() + 2.0,
                                            symbolRect.width() * 0.62,
                                            symbolRect.height() - 4.0));
            } else {
                painter->drawText(m_rect, Qt::AlignCenter, "M");
            }
            return;
        }

        const QColor outline(59, 95, 134);
        const QColor face(178, 207, 238);
        const QColor faceLight(221, 235, 248);
        const QColor faceDark(139, 176, 214);
        const ShapeNode::Type type = primitiveTypeForTool(m_tool);

        painter->setPen(QPen(outline, 1));
        if (type == ShapeNode::Sphere) {
            painter->setBrush(face);
            painter->drawEllipse(m_rect.adjusted(3.0, 3.0, -3.0, -3.0));
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(QColor(93, 127, 166), 1));
            painter->drawEllipse(m_rect.adjusted(6.0, 13.0, -6.0, -13.0));
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(255, 255, 255, 165));
            painter->drawEllipse(QRectF(m_rect.left() + m_rect.width() * 0.28,
                                        m_rect.top() + m_rect.height() * 0.22,
                                        m_rect.width() * 0.18,
                                        m_rect.height() * 0.14));
            return;
        }

        if (type == ShapeNode::Cylinder) {
            const QRectF top(m_rect.left() + 5.0, m_rect.top() + 5.0, m_rect.width() - 10.0, m_rect.height() * 0.28);
            const QRectF bottom(top.left(), m_rect.bottom() - top.height() - 5.0, top.width(), top.height());
            painter->setPen(Qt::NoPen);
            painter->setBrush(face);
            painter->drawRect(QRectF(top.left(), top.center().y(), top.width(), bottom.center().y() - top.center().y()));
            painter->setPen(QPen(outline, 1));
            painter->drawLine(top.left(), top.center().y(), bottom.left(), bottom.center().y());
            painter->drawLine(top.right(), top.center().y(), bottom.right(), bottom.center().y());
            painter->setBrush(faceDark);
            painter->drawEllipse(bottom);
            painter->setBrush(faceLight);
            painter->drawEllipse(top);
            return;
        }

        QPolygonF topFace;
        topFace << QPointF(m_rect.left() + m_rect.width() * 0.22, m_rect.top() + m_rect.height() * 0.34)
                << QPointF(m_rect.left() + m_rect.width() * 0.48, m_rect.top() + m_rect.height() * 0.12)
                << QPointF(m_rect.left() + m_rect.width() * 0.82, m_rect.top() + m_rect.height() * 0.28)
                << QPointF(m_rect.left() + m_rect.width() * 0.56, m_rect.top() + m_rect.height() * 0.50);
        QPolygonF leftFace;
        leftFace << topFace[0] << topFace[3]
                 << QPointF(m_rect.left() + m_rect.width() * 0.56, m_rect.top() + m_rect.height() * 0.86)
                 << QPointF(m_rect.left() + m_rect.width() * 0.22, m_rect.top() + m_rect.height() * 0.70);
        QPolygonF rightFace;
        rightFace << topFace[3] << topFace[2]
                  << QPointF(m_rect.left() + m_rect.width() * 0.82, m_rect.top() + m_rect.height() * 0.64)
                  << QPointF(m_rect.left() + m_rect.width() * 0.56, m_rect.top() + m_rect.height() * 0.86);
        painter->setPen(QPen(outline, 1));
        painter->setBrush(face);
        painter->drawPolygon(leftFace);
        painter->setBrush(faceDark);
        painter->drawPolygon(rightFace);
        painter->setBrush(faceLight);
        painter->drawPolygon(topFace);
    }

private:
    QString m_tool;
    QRectF m_rect;
};

void appendPreviewItem(QVector<QGraphicsItem *> *items, QGraphicsItem *item)
{
    if (!items || !item)
        return;
    items->append(item);
}

void addPreviewGlyph(QGraphicsScene *scene, QVector<QGraphicsItem *> *items, const QString &tool, const QRectF &rect)
{
    auto *glyph = new ToolGlyphItem(tool, rect);
    glyph->setZValue(58.0);
    scene->addItem(glyph);
    appendPreviewItem(items, glyph);
}

void addPreviewBlock(QGraphicsScene *scene,
                     QVector<QGraphicsItem *> *items,
                     const QString &previewTool,
                     const QRectF &rect,
                     const QColor &fill)
{
    SceneDocument::TreeNode::Operation operation;
    if (operationForToolName(previewTool, &operation)) {
        auto *panel = addRoundedPanel(scene,
                                      rect,
                                      CornerRadius,
                                      QPen(fill.darker(145), 2),
                                      QBrush(QColor(fill.red(), fill.green(), fill.blue(), 205)),
                                      56.0);
        appendPreviewItem(items, panel);

        const QRectF headerRect(rect.left() + 1.5, rect.top() + 1.5, rect.width() - 3.0, GroupHeaderHeight - 2.0);
        auto *header = addRoundedPanel(scene,
                                       headerRect,
                                       CornerRadius - 1.0,
                                       Qt::NoPen,
                                       QBrush(QColor(fill.lighter(112).red(), fill.lighter(112).green(), fill.lighter(112).blue(), 210)),
                                       57.0);
        appendPreviewItem(items, header);

        addPreviewGlyph(scene, items, previewTool, QRectF(rect.left() + 8.0, rect.top() + 6.0, 18.0, 18.0));

        auto *label = scene->addSimpleText(labelForOperation(operation));
        label->setBrush(QColor(24, 34, 44));
        label->setPos(rect.topLeft() + QPointF(32.0, 7.0));
        label->setZValue(58.0);
        appendPreviewItem(items, label);
        return;
    }

    const QRectF iconRect(rect.left() + 20.0,
                          rect.top() + (PrimitiveHeight - PrimitiveIconSize) * 0.5,
                          PrimitiveIconSize,
                          PrimitiveIconSize);
    addPreviewGlyph(scene, items, previewTool, iconRect);
}

void addPreviewGroupFrame(QGraphicsScene *scene,
                          QVector<QGraphicsItem *> *items,
                          const QRectF &rect,
                          SceneDocument::TreeNode::Operation operation,
                          qreal cutSeparatorY,
                          const QColor &fill)
{
    auto *panel = addRoundedPanel(scene,
                                  rect,
                                  CornerRadius,
                                  QPen(fill.darker(145), 2),
                                  QBrush(QColor(fill.red(), fill.green(), fill.blue(), 245)),
                                  52.0);
    appendPreviewItem(items, panel);

    const QRectF headerRect(rect.left() + 1.5, rect.top() + 1.5, rect.width() - 3.0, GroupHeaderHeight - 2.0);
    auto *header = addRoundedPanel(scene,
                                   headerRect,
                                   CornerRadius - 1.0,
                                   Qt::NoPen,
                                   QBrush(QColor(fill.lighter(112).red(), fill.lighter(112).green(), fill.lighter(112).blue(), 245)),
                                   53.0);
    appendPreviewItem(items, header);

    addPreviewGlyph(scene, items, labelForOperation(operation), QRectF(rect.left() + 8.0, rect.top() + 6.0, 18.0, 18.0));

    auto *label = scene->addSimpleText(labelForOperation(operation));
    label->setBrush(QColor(24, 34, 44));
    label->setPos(rect.topLeft() + QPointF(32.0, 7.0));
    label->setZValue(54.0);
    appendPreviewItem(items, label);

    if (operation == SceneDocument::TreeNode::Difference && cutSeparatorY > 0.0) {
        auto *separator = scene->addLine(rect.left() + GroupPadding,
                                         cutSeparatorY,
                                         rect.right() - GroupPadding,
                                         cutSeparatorY,
                                         QPen(QColor(130, 92, 70), 1, Qt::DashLine));
        separator->setZValue(54.0);
        appendPreviewItem(items, separator);
    }
}

void addReservedPreviewSlot(QGraphicsScene *scene, QVector<QGraphicsItem *> *items, const QRectF &rect)
{
    QPainterPath path;
    path.addRoundedRect(rect.adjusted(1.0, 1.0, -1.0, -1.0), CornerRadius, CornerRadius);
    auto *slot = scene->addPath(path,
                                QPen(QColor(90, 104, 118, 170), 1, Qt::DashLine),
                                QBrush(QColor(255, 255, 255, 42)));
    slot->setZValue(56.0);
    appendPreviewItem(items, slot);
}

QPainterPath dragFocusOutlinePath(const QString &tool, const QRectF &rect)
{
    QPainterPath path;
    SceneDocument::TreeNode::Operation operation;
    if (operationForToolName(tool, &operation)) {
        path.addRoundedRect(rect.adjusted(-4.0, -4.0, 4.0, 4.0), CornerRadius + 2.0, CornerRadius + 2.0);
        return path;
    }

    const QRectF iconRect(rect.left() + 20.0,
                          rect.top() + (PrimitiveHeight - PrimitiveIconSize) * 0.5,
                          PrimitiveIconSize,
                          PrimitiveIconSize);
    path.addEllipse(iconRect.adjusted(-6.0, -6.0, 6.0, 6.0));
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

        createDragSnapshot(event->scenePos());
        if (m_onPreviewMoved)
            m_onPreviewMoved(event->scenePos(), m_previewSize, m_label);
        event->accept();
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override
    {
        moveDragSnapshot(event->scenePos());
        if (m_onPreviewMoved)
            m_onPreviewMoved(event->scenePos(), m_previewSize, m_label);
        event->accept();
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override
    {
        const QPointF dropPosition = event->scenePos();
        moveDragSnapshot(dropPosition);
        delete m_dragSnapshot;
        m_dragSnapshot = nullptr;
        delete m_dragOutline;
        m_dragOutline = nullptr;
        if (m_onPreviewFinished)
            m_onPreviewFinished();
        if (m_onDropped)
            m_onDropped(m_nodeId, dropPosition);
        event->accept();
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

    int m_nodeId = 0;
    QString m_label;
    QRectF m_sourceRect;
    QPointF m_dragOffset;
    QSizeF m_previewSize;
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

class PaletteToolItem : public QGraphicsRectItem
{
public:
    PaletteToolItem(const QString &label,
                    const QColor &fill,
                    std::function<void(const QPointF &, const QSizeF &, const QString &)> onPreviewMoved,
                    std::function<void()> onPreviewFinished,
                    std::function<void(const QString &, const QPointF &)> onDropped)
        : QGraphicsRectItem(QRectF(0.0, 0.0, ToolSize, ToolSize))
        , m_label(label)
        , m_onPreviewMoved(onPreviewMoved)
        , m_onPreviewFinished(onPreviewFinished)
        , m_onDropped(onDropped)
    {
        setPen(QPen(fill.darker(145)));
        setBrush(QColor(255, 255, 255));
        setAcceptedMouseButtons(Qt::LeftButton);
        setToolTip(label);
        setZValue(100.0);

        auto *glyph = new ToolGlyphItem(label, QRectF(10.0, 8.0, 34.0, 34.0), this);
        glyph->setZValue(1.0);
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (m_onPreviewMoved)
            m_onPreviewMoved(event->scenePos(), previewSizeForTool(m_label), m_label);
        event->accept();
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (m_onPreviewMoved)
            m_onPreviewMoved(event->scenePos(), previewSizeForTool(m_label), m_label);
        event->accept();
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override
    {
        const QPointF dropPosition = event->scenePos();
        if (m_onPreviewFinished)
            m_onPreviewFinished();
        if (m_onDropped)
            m_onDropped(m_label, dropPosition);
        event->accept();
    }

    QString m_label;
    std::function<void(const QPointF &, const QSizeF &, const QString &)> m_onPreviewMoved;
    std::function<void()> m_onPreviewFinished;
    std::function<void(const QString &, const QPointF &)> m_onDropped;
};

QColor fillForTool(const QString &tool)
{
    if (tool == "difference")
        return QColor(247, 224, 204);
    if (tool == "intersection")
        return QColor(226, 220, 247);
    if (tool == "union")
        return QColor(216, 237, 226);
    if (tool == "module")
        return QColor(230, 232, 236);
    return QColor(219, 231, 246);
}
}

SceneTreeGraphicsWidget::SceneTreeGraphicsWidget(QWidget *parent)
    : QGraphicsView(parent)
    , m_graphicsScene(new TreeGraphicsScene(this))
{
    setScene(m_graphicsScene);
    setMinimumHeight(280);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(CanvasBackground);
    setCacheMode(QGraphicsView::CacheNone);
    setFocusPolicy(Qt::StrongFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setCursor(Qt::OpenHandCursor);
}

void SceneTreeGraphicsWidget::setSceneDocument(const SceneDocument *scene)
{
    m_scene = scene;
    refresh();
}

void SceneTreeGraphicsWidget::setToolDroppedCallback(std::function<void(const QString &, int, int)> callback)
{
    m_toolDroppedCallback = callback;
}

void SceneTreeGraphicsWidget::setTreeNodeDroppedCallback(std::function<void(int, int, int)> callback)
{
    m_treeNodeDroppedCallback = callback;
}

void SceneTreeGraphicsWidget::setTreeNodeSelectedCallback(std::function<void(int)> callback)
{
    m_treeNodeSelectedCallback = callback;
}

void SceneTreeGraphicsWidget::setTreeNodeDeleteRequestedCallback(std::function<void(int)> callback)
{
    m_treeNodeDeleteRequestedCallback = callback;
}

void SceneTreeGraphicsWidget::setSelectedTreeNodeId(int nodeId)
{
    if (m_selectedTreeNodeId == nodeId)
        return;

    m_selectedTreeNodeId = nodeId;
    refresh();
}

void SceneTreeGraphicsWidget::refresh()
{
    clearDropPreview();
    m_graphicsScene->clear();
    m_groupHitAreas.clear();

    const QRectF toolbarRect = drawToolbar();

    if (m_scene && !m_scene->treeRoot().children.isEmpty()) {
        drawNode(m_scene->treeRoot(), QPointF(TreeX, TreeY), 0);
    } else {
        addLabel(m_graphicsScene, "Drop tree components here", QPointF(TreeX + 8.0, TreeY + 8.0), QColor(105, 105, 105));
    }

    QRectF bounds = m_graphicsScene->itemsBoundingRect().united(toolbarRect).adjusted(-CanvasMargin,
                                                                                       -CanvasMargin,
                                                                                       CanvasMargin,
                                                                                       CanvasMargin);
    if (bounds.width() < 420.0)
        bounds.setWidth(420.0);
    if (bounds.height() < 260.0)
        bounds.setHeight(260.0);
    m_graphicsScene->setSceneRect(bounds);
}

void SceneTreeGraphicsWidget::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->fillRect(rect, CanvasBackground);
    drawCanvasGrid(painter, rect, 24.0, MinorGridColor, 1);
    drawCanvasGrid(painter, rect, 96.0, MajorGridColor, 1);
}

void SceneTreeGraphicsWidget::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && m_selectedTreeNodeId > 0) {
        if (m_treeNodeDeleteRequestedCallback)
            m_treeNodeDeleteRequestedCallback(m_selectedTreeNodeId);
        event->accept();
        return;
    }

    QGraphicsView::keyPressEvent(event);
}

void SceneTreeGraphicsWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus();

    if (event->button() == Qt::LeftButton && itemAt(event->pos()) == nullptr) {
        m_panning = true;
        m_lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void SceneTreeGraphicsWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastPanPoint;
        m_lastPanPoint = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void SceneTreeGraphicsWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_panning) {
        m_panning = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void SceneTreeGraphicsWidget::wheelEvent(QWheelEvent *event)
{
    const qreal factor = event->angleDelta().y() > 0 ? 1.12 : 1.0 / 1.12;
    scale(factor, factor);
    event->accept();
}

QRectF SceneTreeGraphicsWidget::drawToolbar()
{
    const QStringList tools = {
        "cube",
        "sphere",
        "cylinder",
        "union",
        "difference",
        "intersection",
        "module"
    };

    const QRectF toolbarRect(ToolbarX - 6.0,
                             ToolbarY - 6.0,
                             tools.size() * ToolSize + (tools.size() - 1) * ToolGap + 12.0,
                             ToolSize + 12.0);
    addSoftShadow(m_graphicsScene, toolbarRect, -4.0);
    addRoundedPanel(m_graphicsScene, toolbarRect, CornerRadius, QPen(QColor(166, 174, 186)), QBrush(QColor(232, 235, 239)), -3.0);

    for (int i = 0; i < tools.size(); ++i) {
        auto *tool = new PaletteToolItem(
            tools[i],
            fillForTool(tools[i]),
            [this](const QPointF &position, const QSizeF &previewSize, const QString &previewTool) {
                showDropPreview(position, previewSize, previewTool);
            },
            [this]() {
                clearDropPreview();
            },
            [this](const QString &toolName, const QPointF &position) {
                handleToolDrop(toolName, position);
            });
        tool->setPos(ToolbarX + i * (ToolSize + ToolGap), ToolbarY);
        m_graphicsScene->addItem(tool);
    }

    return toolbarRect;
}

QRectF SceneTreeGraphicsWidget::drawNode(const SceneDocument::TreeNode &node, const QPointF &topLeft, int depth)
{
    if (node.type == SceneDocument::TreeNode::Primitive)
        return drawPrimitive(node, topLeft);

    return drawGroup(node, topLeft, depth);
}

QRectF SceneTreeGraphicsWidget::drawPrimitive(const SceneDocument::TreeNode &node, const QPointF &topLeft)
{
    const QRectF rect(topLeft, QSizeF(PrimitiveWidth, PrimitiveHeight));
    const QString label = labelForPrimitive(node.shapeId);
    const bool selected = node.id == m_selectedTreeNodeId;
    const QRectF iconRect(rect.left() + 20.0,
                          rect.top() + (PrimitiveHeight - PrimitiveIconSize) * 0.5,
                          PrimitiveIconSize,
                          PrimitiveIconSize);
    if (selected)
        addPrimitiveSelectionHalo(m_graphicsScene, iconRect);
    addPrimitiveIcon(m_graphicsScene, typeForPrimitive(node.shapeId), iconRect);
    addPrimitiveNumberBadge(m_graphicsScene, primitiveNumberText(label, node.shapeId), rect);
    auto *handle = new TreeNodeDragHandleItem(
        node.id,
        label,
        rect,
        rect,
        [this](int nodeId) {
            handleTreeNodeSelected(nodeId);
        },
        rect.size(),
        [this, nodeId = node.id](const QPointF &position, const QSizeF &previewSize, const QString &previewTool) {
            showDropPreview(position, previewSize, previewTool, nodeId);
        },
        [this]() {
            clearDropPreview();
        },
        [this](int nodeId, const QPointF &position) {
            handleTreeNodeDrop(nodeId, position);
        });
    handle->setToolTip(label);
    m_graphicsScene->addItem(handle);
    return rect;
}

QRectF SceneTreeGraphicsWidget::drawGroup(const SceneDocument::TreeNode &node, const QPointF &topLeft, int depth)
{
    QVector<QRectF> childRects;
    QVector<QString> childPreviewTools;
    QVector<int> childNodeIds;
    QPointF childTopLeft(topLeft.x() + GroupPadding, topLeft.y() + GroupHeaderHeight + GroupPadding);
    qreal maxChildWidth = 0.0;

    for (const SceneDocument::TreeNode &child : node.children) {
        const QRectF childRect = drawNode(child, childTopLeft, depth + 1);
        childRects.append(childRect);
        childPreviewTools.append(previewToolForNode(child));
        childNodeIds.append(child.id);
        maxChildWidth = qMax(maxChildWidth, childRect.width());
        childTopLeft.ry() += childRect.height() + ChildGap;
    }

    qreal childrenHeight = childRects.isEmpty()
                               ? PrimitiveHeight
                               : childTopLeft.y() - topLeft.y() - GroupHeaderHeight - GroupPadding - ChildGap;
    if (node.operation == SceneDocument::TreeNode::Difference)
        childrenHeight = qMax(childrenHeight, DifferenceMinContentHeight);
    const QSizeF size(qMax(GroupMinWidth, maxChildWidth + GroupPadding * 2.0),
                      GroupHeaderHeight + GroupPadding * 2.0 + childrenHeight);
    const QRectF rect(topLeft, size);
    const QColor fill = colorForGroup(node.operation);
    const bool selected = node.id == m_selectedTreeNodeId;
    qreal cutSeparatorY = 0.0;
    if (node.operation == SceneDocument::TreeNode::Difference) {
        cutSeparatorY = rect.top() + GroupHeaderHeight + GroupPadding + PrimitiveHeight + ChildGap * 0.5;
        if (!childRects.isEmpty())
            cutSeparatorY = childRects.first().bottom() + ChildGap * 0.5;
    }
    m_groupHitAreas.append({rect, node.id, depth, node.operation, cutSeparatorY, childRects, childPreviewTools, childNodeIds});

    addSoftShadow(m_graphicsScene, rect, depth * 10.0 - 101.0);
    auto *groupItem = addRoundedPanel(m_graphicsScene,
                                      rect,
                                      CornerRadius,
                                      QPen(selected ? QColor(255, 203, 87) : fill.darker(145), selected ? 3 : 2),
                                      QBrush(fill),
                                      depth * 10.0 - 100.0);
    groupItem->setZValue(depth * 10.0 - 100.0);

    const QRectF headerRect(rect.left() + 1.5, rect.top() + 1.5, rect.width() - 3.0, GroupHeaderHeight - 2.0);
    auto *header = addRoundedPanel(m_graphicsScene,
                                   headerRect,
                                   CornerRadius - 1.0,
                                   Qt::NoPen,
                                   QBrush(fill.lighter(112)),
                                   depth * 10.0 - 95.0);
    header->setZValue(depth * 10.0 - 95.0);
    m_graphicsScene->addItem(new TreeNodeSelectionItem(
        node.id,
        rect,
        depth * 10.0 - 80.0,
        [this](int nodeId) {
            handleTreeNodeSelected(nodeId);
        }));

    const QRectF iconRect(rect.left() + 8.0, rect.top() + 6.0, 18.0, 18.0);
    addOperationIcon(m_graphicsScene, node.operation, iconRect, fill.darker(125));
    const QString groupLabel = labelForGroup(node.operation);
    addLabel(m_graphicsScene, groupLabel, rect.topLeft() + QPointF(32.0, 7.0), QColor(24, 34, 44));
    m_graphicsScene->addItem(new TreeNodeDragHandleItem(
        node.id,
        groupLabel,
        QRectF(rect.topLeft(), QSizeF(rect.width(), GroupHeaderHeight)),
        rect,
        [this](int nodeId) {
            handleTreeNodeSelected(nodeId);
        },
        rect.size(),
        [this, nodeId = node.id](const QPointF &position, const QSizeF &previewSize, const QString &previewTool) {
            showDropPreview(position, previewSize, previewTool, nodeId);
        },
        [this]() {
            clearDropPreview();
        },
        [this](int nodeId, const QPointF &position) {
            handleTreeNodeDrop(nodeId, position);
        }));

    if (node.children.isEmpty())
        addLabel(m_graphicsScene, "empty", QPointF(rect.left() + GroupPadding, rect.top() + GroupHeaderHeight + GroupPadding + 10.0), QColor(95, 98, 105));

    if (node.operation == SceneDocument::TreeNode::Difference) {
        auto *separator = m_graphicsScene->addLine(rect.left() + GroupPadding,
                                                   cutSeparatorY,
                                                   rect.right() - GroupPadding,
                                                   cutSeparatorY,
                                                   QPen(QColor(130, 92, 70), 1, Qt::DashLine));
        separator->setZValue(depth * 10.0 - 70.0);

        addPillLabel(m_graphicsScene, "base", QPointF(rect.right() - 61.0, rect.top() + GroupHeaderHeight + 7.0), QColor(128, 99, 73));
        addPillLabel(m_graphicsScene, "cut", QPointF(rect.right() - 51.0, cutSeparatorY + 5.0), QColor(153, 85, 56));
    }

    return rect;
}

void SceneTreeGraphicsWidget::handleToolDrop(const QString &toolName, const QPointF &scenePosition)
{
    if (m_toolDroppedCallback) {
        const DropTarget target = dropTargetAt(scenePosition);
        m_toolDroppedCallback(toolName, target.parentGroupId, target.insertIndex);
    }
}

QString SceneTreeGraphicsWidget::previewToolForNode(const SceneDocument::TreeNode &node) const
{
    if (node.type != SceneDocument::TreeNode::Primitive)
        return labelForOperation(node.operation);

    const ShapeNode *shape = m_scene ? m_scene->shapeById(node.shapeId) : nullptr;
    const ShapeNode::Type type = shape ? shape->type : ShapeNode::Cube;
    if (type == ShapeNode::Sphere)
        return "sphere";
    if (type == ShapeNode::Cylinder)
        return "cylinder";
    return "cube";
}

void SceneTreeGraphicsWidget::handleTreeNodeDrop(int nodeId, const QPointF &scenePosition)
{
    if (m_treeNodeDroppedCallback) {
        const DropTarget target = dropTargetAt(scenePosition);
        m_treeNodeDroppedCallback(nodeId, target.parentGroupId, target.insertIndex);
    }
}

void SceneTreeGraphicsWidget::handleTreeNodeSelected(int nodeId)
{
    setFocus();
    m_selectedTreeNodeId = nodeId;
    refresh();
    if (m_treeNodeSelectedCallback)
        m_treeNodeSelectedCallback(nodeId);
}

void SceneTreeGraphicsWidget::showDropPreview(const QPointF &scenePosition, const QSizeF &previewSize, const QString &previewTool, int movingNodeId)
{
    clearDropPreview();

    const QSizeF effectivePreviewSize = previewSize.isValid() ? previewSize : defaultPreviewSize();
    DropTarget target = dropTargetAt(scenePosition, effectivePreviewSize, movingNodeId);
    if (!target.zoneRect.isValid()) {
        target.zoneRect = QRectF(scenePosition - QPointF(effectivePreviewSize.width() * 0.5, effectivePreviewSize.height() * 0.5),
                                 effectivePreviewSize);
        if (movingNodeId <= 0) {
            target.placeholderRect = target.zoneRect;
            target.hasTarget = true;
        }
    }

    for (int i = 0; i < target.expandedGroupRects.size(); ++i) {
        const QRectF expandedRect = target.expandedGroupRects[i];
        const SceneDocument::TreeNode::Operation operation = i < target.expandedGroupOperations.size()
                                                                 ? target.expandedGroupOperations[i]
                                                                 : SceneDocument::TreeNode::Union;
        const QColor fill = colorForGroup(operation);
        QPainterPath expandedPath;
        expandedPath.addRoundedRect(expandedRect, CornerRadius, CornerRadius);
        auto *expanded = m_graphicsScene->addPath(expandedPath,
                                                  QPen(fill.darker(145), 2, Qt::DashLine),
                                                  QBrush(QColor(fill.red(), fill.green(), fill.blue(), 46)));
        expanded->setZValue(54.0);
        m_dropPreviewItems.append(expanded);
    }

    const bool sourceGroupCoveredByTarget = target.sourceGroupRect.isValid()
                                            && target.previewGroupRect.isValid()
                                            && target.previewGroupRect.contains(target.sourceGroupRect.center());
    if (target.sourceGroupRect.isValid() && !sourceGroupCoveredByTarget) {
        addPreviewGroupFrame(m_graphicsScene,
                             &m_dropPreviewItems,
                             target.sourceGroupRect,
                             target.sourceGroupOperation,
                             target.sourceCutSeparatorY,
                             colorForGroup(target.sourceGroupOperation));
        for (int i = 0; i < target.sourceChildRects.size(); ++i) {
            const QString childTool = i < target.sourceChildTools.size()
                                          ? target.sourceChildTools[i]
                                          : QStringLiteral("cube");
            addPreviewBlock(m_graphicsScene,
                            &m_dropPreviewItems,
                            childTool,
                            target.sourceChildRects[i],
                            fillForTool(childTool));
        }
    }

    if (target.previewGroupRect.isValid()) {
        addPreviewGroupFrame(m_graphicsScene,
                             &m_dropPreviewItems,
                             target.previewGroupRect,
                             target.previewGroupOperation,
                             target.previewCutSeparatorY,
                             colorForGroup(target.previewGroupOperation));
    }

    for (int i = 0; i < target.previewChildRects.size(); ++i) {
        const QString childTool = i < target.previewChildTools.size()
                                      ? target.previewChildTools[i]
                                      : QStringLiteral("cube");
        addPreviewBlock(m_graphicsScene,
                        &m_dropPreviewItems,
                        childTool,
                        target.previewChildRects[i],
                        fillForTool(childTool));
    }

    const bool sourceSlotCoveredByTarget = target.hasTarget
                                           && target.sourceRect.isValid()
                                           && target.placeholderRect.intersects(target.sourceRect);
    if (movingNodeId > 0 && target.sourceRect.isValid() && !sourceSlotCoveredByTarget)
        addReservedPreviewSlot(m_graphicsScene, &m_dropPreviewItems, target.sourceRect);

    if (target.hasTarget) {
        if (movingNodeId > 0) {
            addReservedPreviewSlot(m_graphicsScene, &m_dropPreviewItems, target.placeholderRect);
        } else {
            addPreviewBlock(m_graphicsScene,
                            &m_dropPreviewItems,
                            previewTool,
                            target.placeholderRect,
                            fillForTool(previewTool));
            addDragFocusOutline(m_graphicsScene,
                                &m_dropPreviewItems,
                                previewTool,
                                target.placeholderRect,
                                90.0);
        }
    }
}

void SceneTreeGraphicsWidget::clearDropPreview()
{
    for (QGraphicsItem *item : m_dropPreviewItems)
        delete item;
    m_dropPreviewItems.clear();
}

SceneTreeGraphicsWidget::DropTarget SceneTreeGraphicsWidget::dropTargetAt(const QPointF &scenePosition, const QSizeF &previewSize, int movingNodeId) const
{
    DropTarget target;
    const QSizeF effectivePreviewSize = previewSize.isValid() ? previewSize : defaultPreviewSize();
    const GroupHitArea *sourceArea = nullptr;
    if (movingNodeId > 0) {
        for (const GroupHitArea &area : m_groupHitAreas) {
            for (int i = 0; i < area.childNodeIds.size() && i < area.childRects.size(); ++i) {
                if (area.childNodeIds[i] == movingNodeId) {
                    target.sourceRect = area.childRects[i];
                    sourceArea = &area;
                    break;
                }
            }
            if (target.sourceRect.isValid())
                break;
        }
    }

    int bestDepth = -1;
    const GroupHitArea *bestArea = nullptr;

    for (const GroupHitArea &area : m_groupHitAreas) {
        if (area.depth <= bestDepth || !area.rect.contains(scenePosition))
            continue;
        if (movingNodeId > 0 && area.groupId == movingNodeId)
            continue;
        if (movingNodeId > 0 && sourceArea && target.sourceRect.isValid()
            && area.depth > sourceArea->depth
            && target.sourceRect.contains(area.rect.center())) {
            continue;
        }

        bestArea = &area;
        bestDepth = area.depth;
    }

    if (sourceArea && sourceArea != bestArea) {
        target.sourceGroupRect = sourceArea->rect;
        target.sourceGroupOperation = sourceArea->operation;
        target.sourceCutSeparatorY = sourceArea->cutSeparatorY;
        for (int i = 0; i < sourceArea->childRects.size(); ++i) {
            const int childNodeId = i < sourceArea->childNodeIds.size() ? sourceArea->childNodeIds[i] : 0;
            if (childNodeId == movingNodeId)
                continue;

            target.sourceChildRects.append(sourceArea->childRects[i]);
            target.sourceChildTools.append(i < sourceArea->childPreviewTools.size()
                                               ? sourceArea->childPreviewTools[i]
                                               : QStringLiteral("cube"));
        }
    }

    if (!bestArea)
        return target;

    target.hasTarget = true;
    target.parentGroupId = bestArea->groupId;
    target.previewGroupOperation = bestArea->operation;
    target.previewCutSeparatorY = bestArea->cutSeparatorY;
    const QRectF contentRect = bestArea->rect.adjusted(GroupPadding,
                                                       GroupHeaderHeight + GroupPadding,
                                                       -GroupPadding,
                                                       -GroupPadding);
    QVector<QRectF> candidateChildRects;
    QVector<QString> candidateChildTools;
    for (int i = 0; i < bestArea->childRects.size(); ++i) {
        const int childNodeId = i < bestArea->childNodeIds.size() ? bestArea->childNodeIds[i] : 0;
        if (movingNodeId > 0 && childNodeId == movingNodeId)
            continue;

        candidateChildRects.append(bestArea->childRects[i]);
        candidateChildTools.append(i < bestArea->childPreviewTools.size()
                                       ? bestArea->childPreviewTools[i]
                                       : QStringLiteral("cube"));
    }

    auto setPreviewChildren = [&target, &candidateChildRects, &candidateChildTools](qreal shift) {
        target.previewChildRects.clear();
        target.previewChildTools.clear();
        const int startIndex = qBound(0, target.insertIndex, candidateChildRects.size());
        for (int i = 0; i < candidateChildRects.size(); ++i) {
            target.previewChildRects.append(i >= startIndex ? candidateChildRects[i].translated(0.0, shift)
                                                            : candidateChildRects[i]);
            target.previewChildTools.append(i < candidateChildTools.size()
                                                ? candidateChildTools[i]
                                                : QStringLiteral("cube"));
        }
    };
    target.zoneRect = contentRect;
    target.insertIndex = insertionIndexForY(candidateChildRects, scenePosition.y());
    target.placeholderRect = placeholderRectForInsertIndex(contentRect, candidateChildRects, target.insertIndex, effectivePreviewSize);
    setPreviewChildren(effectivePreviewSize.height() + ChildGap);

    if (bestArea->operation == SceneDocument::TreeNode::Difference && bestArea->cutSeparatorY > 0.0) {
        const bool baseZone = scenePosition.y() < bestArea->cutSeparatorY;
        target.insertIndex = baseZone
                                 ? 0
                                 : insertionIndexForY(candidateChildRects, scenePosition.y(), 1);
        target.zoneRect = baseZone
                              ? QRectF(contentRect.left(),
                                       contentRect.top(),
                                       contentRect.width(),
                                       qMax<qreal>(PrimitiveHeight, bestArea->cutSeparatorY - contentRect.top()))
                              : QRectF(contentRect.left(),
                                       bestArea->cutSeparatorY,
                                       contentRect.width(),
                                       qMax<qreal>(PrimitiveHeight, contentRect.bottom() - bestArea->cutSeparatorY));
        target.placeholderRect = placeholderRectForInsertIndex(contentRect, candidateChildRects, target.insertIndex, effectivePreviewSize);
        if (!baseZone && target.placeholderRect.top() < bestArea->cutSeparatorY)
            target.placeholderRect.moveTop(bestArea->cutSeparatorY + ChildGap * 0.5);
        setPreviewChildren(effectivePreviewSize.height() + ChildGap);
        if (baseZone)
            target.previewCutSeparatorY = target.placeholderRect.bottom() + ChildGap * 0.5;
    }

    QRectF changedOldRect = bestArea->rect;
    QRectF changedNewRect = expandedGroupRectForPreview(bestArea->rect,
                                                        target.placeholderRect,
                                                        candidateChildRects,
                                                        target.insertIndex,
                                                        effectivePreviewSize);
    target.previewGroupRect = changedNewRect;

    QVector<const GroupHitArea *> containingAreas;
    for (const GroupHitArea &area : m_groupHitAreas) {
        if (area.depth <= bestDepth && area.rect.contains(bestArea->rect.center()))
            containingAreas.append(&area);
    }

    for (int i = containingAreas.size() - 1; i >= 0; --i) {
        const GroupHitArea *area = containingAreas[i];
        QRectF expandedRect;
        if (area == bestArea) {
            expandedRect = changedNewRect;
        } else {
            QRectF oldChildRect;
            for (const QRectF &childRect : area->childRects) {
                if (childRect.contains(changedOldRect.center())) {
                    oldChildRect = childRect;
                    break;
                }
            }

            if (!oldChildRect.isValid())
                continue;

            expandedRect = expandedGroupRectForChangedChild(area->rect, area->childRects, oldChildRect, changedNewRect);
        }

        if (expandedRect != area->rect) {
            target.expandedGroupRects.prepend(expandedRect);
            target.expandedGroupOperations.prepend(area->operation);
        }

        changedOldRect = area->rect;
        changedNewRect = expandedRect;
    }

    return target;
}

QString SceneTreeGraphicsWidget::labelForPrimitive(int shapeId) const
{
    if (!m_scene)
        return "shape";

    const ShapeNode *shape = m_scene->shapeById(shapeId);
    if (!shape)
        return "shape";

    if (!shape->name.isEmpty())
        return shape->name;

    if (shape->type == ShapeNode::Sphere)
        return "sphere";
    if (shape->type == ShapeNode::Cylinder)
        return "cylinder";
    return "cube";
}

ShapeNode::Type SceneTreeGraphicsWidget::typeForPrimitive(int shapeId) const
{
    if (!m_scene)
        return ShapeNode::Cube;

    const ShapeNode *shape = m_scene->shapeById(shapeId);
    return shape ? shape->type : ShapeNode::Cube;
}

QString SceneTreeGraphicsWidget::labelForGroup(SceneDocument::TreeNode::Operation operation) const
{
    return labelForOperation(operation);
}

QColor SceneTreeGraphicsWidget::colorForGroup(SceneDocument::TreeNode::Operation operation) const
{
    if (operation == SceneDocument::TreeNode::Module)
        return QColor(230, 232, 236);
    if (operation == SceneDocument::TreeNode::Difference)
        return QColor(247, 224, 204);
    if (operation == SceneDocument::TreeNode::Intersection)
        return QColor(226, 220, 247);
    return QColor(216, 237, 226);
}
