#include "scenetreegraphicshelpers.h"
#include "scenetreepalette.h"

#include <QApplication>
#include <QBrush>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QStyleOptionGraphicsItem>
#include <QtGlobal>
#include <QVarLengthArray>
#include <QWidget>
#include <cmath>

namespace SceneTreeGraphics {

const QColor CanvasBackground(31, 41, 55);
const QColor MinorGridColor(96, 106, 121);
const QColor MajorGridColor(139, 150, 166);
constexpr SceneTreePalette::Theme FixedToolbarTheme = SceneTreePalette::Theme::Ocean;

QString toolbarToolTip(const QString &tool)
{
    if (tool == QStringLiteral("module")) {
        return QStringLiteral(
            "Module declaration\n"
            "Drop at the scene level. Add variables to its parameters lane or body, "
            "then drag the module call chip into the scene, groups, loops, or module bodies.");
    }

    if (isVariableToolName(tool)) {
        return QStringLiteral(
            "Variable / parameter\n"
            "Drop at the scene level for a global variable, or into a module's parameters lane or body.");
    }

    if (tool == QStringLiteral("polyhedron")) {
        return QStringLiteral(
            "Polyhedron group\n"
            "A container that builds a polyhedron from its Point Set and Face Set children. "
            "Drop Points3D and Faces3D inside it.");
    }

    if (tool == QStringLiteral("point_3d")) {
        return QStringLiteral(
            "3D Point\n"
            "A single 3D vertex. Drop into a Polyhedron group. "
            "Its index (number) is its position in the group's child list.");
    }

    if (tool == QStringLiteral("face_3d")) {
        return QStringLiteral(
            "3D Face\n"
            "A single face defined by vertex indices. "
            "Drop into a Polyhedron group alongside Point elements. "
            "Edit N for vertex count, V0/V1/... for indices.");
    }

    if (ShapeNode::isPrimitiveTool(tool)) {
        const QString name = tool.left(1).toUpper() + tool.mid(1);
        return QStringLiteral(
            "%1 primitive\n"
            "Drop into the scene, groups, loops, transforms, or module bodies.").arg(name);
    }

    SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
    if (operationForToolName(tool, &operation)) {
        if (isTransformOperation(operation)) {
            return QStringLiteral(
                "%1 transform\n"
                "Drop into the scene, groups, loops, or module bodies; then drop child objects inside it.")
                .arg(tool.left(1).toUpper() + tool.mid(1));
        }
        if (operation == SceneDocument::TreeNode::For) {
            return QStringLiteral(
                "For loop\n"
                "Drop into the scene, groups, transforms, or module bodies; then drop repeated objects inside it.");
        }
        if (operation == SceneDocument::TreeNode::Color) {
            return QStringLiteral(
                "Color group\n"
                "Drop into the scene, groups, loops, transforms, or module bodies; then drop objects inside it.");
        }
        return QStringLiteral(
            "%1 group\n"
            "Drop into the scene, groups, loops, transforms, or module bodies; then drop child objects inside it.")
            .arg(tool.left(1).toUpper() + tool.mid(1));
    }

    return QStringLiteral("Tool\nDrop into a highlighted slot in the scene tree.");
}

QFont sceneTreeGraphicsFont()
{
    QFont font = QApplication::font();
    font.setFamily(QStringLiteral("Segoe UI"));
    font.setPointSizeF(7.25);
    return font;
}

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

void addLabel(QGraphicsScene *scene, const QString &text, const QPointF &position, const QColor &color)
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

void paintPrimitiveIcon(QPainter *painter, ShapeNode::Type type, const QRectF &rect)
{
    const QColor outline(59, 95, 134);
    const QColor face(178, 207, 238);
    const QColor faceLight(221, 235, 248);
    const QColor faceDark(139, 176, 214);

    painter->setPen(QPen(outline, 1));
    if (type == ShapeNode::Sphere) {
        painter->setBrush(face);
        painter->drawEllipse(rect);
        painter->setPen(QPen(QColor(93, 127, 166), 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(rect.adjusted(3.0, 9.0, -3.0, -9.0));
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 255, 255, 165));
        painter->drawEllipse(QRectF(rect.left() + rect.width() * 0.25,
                                    rect.top() + rect.height() * 0.18,
                                    rect.width() * 0.22,
                                    rect.height() * 0.16));
        return;
    }

    if (type == ShapeNode::Circle) {
        painter->setBrush(QColor(178, 207, 238, 170));
        painter->drawEllipse(rect.adjusted(2.0, 2.0, -2.0, -2.0));
        painter->setPen(QPen(QColor(93, 127, 166), 1.2));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(rect.adjusted(5.0, 5.0, -5.0, -5.0));
        return;
    }

    if (type == ShapeNode::Cylinder) {
        const QRectF top(rect.left() + 3.0, rect.top() + 3.0, rect.width() - 6.0, rect.height() * 0.34);
        const QRectF bottom(top.left(), rect.bottom() - top.height() - 3.0, top.width(), top.height());
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

    if (type == ShapeNode::Cone) {
        // Base ellipse at bottom, slant sides meeting at apex
        const qreal bh = rect.height() * 0.30; // base ellipse height
        const QRectF base(rect.left() + 2.0, rect.bottom() - bh - 2.0, rect.width() - 4.0, bh);
        const QPointF apex(rect.center().x(), rect.top() + 3.0);
        painter->setPen(Qt::NoPen);
        painter->setBrush(face);
        // Filled side triangle
        QPolygonF side;
        side << QPointF(base.left(), base.center().y())
             << QPointF(base.right(), base.center().y())
             << apex;
        painter->drawPolygon(side);
        painter->setPen(QPen(outline, 1));
        painter->drawLine(QPointF(base.left(), base.center().y()), apex);
        painter->drawLine(QPointF(base.right(), base.center().y()), apex);
        painter->setBrush(faceDark);
        painter->drawEllipse(base);
        return;
    }

    if (type == ShapeNode::Polyhedron) {
        // Draw a simple irregular polygon (pentagon) to suggest a polyhedron face
        const QPointF c = rect.center();
        const qreal rx = rect.width() * 0.38, ry = rect.height() * 0.38;
        QPolygonF poly;
        poly << QPointF(c.x(),           c.y() - ry)
             << QPointF(c.x() + rx,      c.y() - ry * 0.2)
             << QPointF(c.x() + rx * 0.6, c.y() + ry)
             << QPointF(c.x() - rx * 0.6, c.y() + ry)
             << QPointF(c.x() - rx,      c.y() - ry * 0.2);
        painter->setBrush(face);
        painter->drawPolygon(poly);
        // inner line to suggest facets
        painter->setPen(QPen(outline, 0.8));
        painter->drawLine(poly[0], poly[2]);
        painter->drawLine(poly[0], poly[3]);
        return;
    }

    if (type == ShapeNode::Point3D) {
        // Draw a single dot with coordinate crosshairs
        const QPointF c = rect.center();
        const qreal r = qMin(rect.width(), rect.height()) * 0.22;
        painter->setPen(QPen(outline, 1));
        painter->setBrush(face);
        painter->drawEllipse(c, r, r);
        painter->setPen(QPen(faceDark, 0.8, Qt::DashLine));
        painter->drawLine(QPointF(c.x() - r * 2, c.y()), QPointF(c.x() + r * 2, c.y()));
        painter->drawLine(QPointF(c.x(), c.y() - r * 2), QPointF(c.x(), c.y() + r * 2));
        return;
    }

    if (type == ShapeNode::Face3D) {
        // Draw a single triangle with a small number inside
        const QPointF c = rect.center();
        const qreal rx = rect.width() * 0.38, ry = rect.height() * 0.38;
        QPolygonF tri;
        tri << QPointF(c.x(), c.y() - ry)
            << QPointF(c.x() - rx, c.y() + ry * 0.5)
            << QPointF(c.x() + rx, c.y() + ry * 0.5);
        painter->setPen(QPen(outline, 1));
        painter->setBrush(face);
        painter->drawPolygon(tri);
        painter->setPen(QPen(faceDark, 0.8));
        painter->drawLine(tri[0], c);
        painter->drawLine(tri[1], c);
        painter->drawLine(tri[2], c);
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

    painter->setPen(QPen(outline, 1));
    painter->setBrush(face);
    painter->drawPolygon(leftFace);
    painter->setBrush(faceDark);
    painter->drawPolygon(rightFace);
    painter->setBrush(faceLight);
    painter->drawPolygon(topFace);
}

QRectF paintToolbarIconFrame(QPainter *painter, const QRectF &rect, const QColor &accent, bool selected)
{
    const QRectF frameRect = rect.adjusted(2.0, 2.0, -2.0, -2.0);
    QPainterPath glassPath;
    glassPath.addRoundedRect(frameRect, 7.0, 7.0);

    QLinearGradient glass(frameRect.topLeft(), frameRect.bottomLeft());
    glass.setColorAt(0.0, QColor(32, 42, 58, 224));
    glass.setColorAt(1.0, QColor(7, 11, 18, 205));
    painter->setPen(QPen(selected ? QColor(255, 203, 87) : accent.lighter(120),
                         selected ? 1.8 : 1.15));
    painter->setBrush(glass);
    painter->drawPath(glassPath);

    QPainterPath tintPath;
    tintPath.addRoundedRect(rect.adjusted(5.0, 5.0, -5.0, -5.0), 6.0, 6.0);
    QColor tint = accent;
    tint.setAlpha(34);
    painter->setPen(Qt::NoPen);
    painter->setBrush(tint);
    painter->drawPath(tintPath);

    const qreal side = qMin(rect.width(), rect.height()) * (34.0 / ToolSize);
    const QPointF center = rect.center() + QPointF(0.0, -rect.height() * (1.0 / ToolSize));
    return QRectF(center.x() - side * 0.5,
                  center.y() - side * 0.5,
                  side,
                  side);
}

void paintToolbarPrimitiveIcon(QPainter *painter, ShapeNode::Type type, const QRectF &rect, bool selected)
{
    const QRectF glyphRect = paintToolbarIconFrame(painter, rect, QColor(178, 207, 238), selected);
    paintPrimitiveIcon(painter, type, glyphRect);
}

void paintOperationIcon(QPainter *painter,
                        SceneDocument::TreeNode::Operation operation,
                        const QRectF &rect,
                        const QColor &accent,
                        qreal symbolInset)
{
    painter->setPen(QPen(accent.darker(135), 1));
    painter->setBrush(QColor(255, 255, 255, 135));
    painter->drawRoundedRect(rect, 3.0, 3.0);

    const QPointF center = rect.center();
    const QRectF symbolRect = rect.adjusted(symbolInset, symbolInset, -symbolInset, -symbolInset);
    QPen pen(accent.darker(160), 2);
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    if (operation == SceneDocument::TreeNode::Union) {
        painter->drawLine(symbolRect.left(), center.y(), symbolRect.right(), center.y());
        painter->drawLine(center.x(), symbolRect.top(), center.x(), symbolRect.bottom());
    } else if (operation == SceneDocument::TreeNode::Difference) {
        painter->drawLine(symbolRect.left(), center.y(), symbolRect.right(), center.y());
    } else if (operation == SceneDocument::TreeNode::Intersection) {
        QPen intersectionPen(accent.darker(150), 2.0);
        intersectionPen.setCapStyle(Qt::RoundCap);
        intersectionPen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(intersectionPen);
        painter->setBrush(QColor(255, 255, 255, 60));
        painter->drawEllipse(QRectF(symbolRect.left(), symbolRect.top() + 1.0, symbolRect.width() * 0.62, symbolRect.height() - 2.0));
        painter->drawEllipse(QRectF(symbolRect.center().x() - symbolRect.width() * 0.31,
                                    symbolRect.top() + 1.0,
                                    symbolRect.width() * 0.62,
                                    symbolRect.height() - 2.0));
    } else if (operation == SceneDocument::TreeNode::Translate) {
        painter->drawLine(symbolRect.left(), center.y(), symbolRect.right(), center.y());
        painter->drawLine(symbolRect.right() - 4.0, center.y() - 4.0, symbolRect.right(), center.y());
        painter->drawLine(symbolRect.right() - 4.0, center.y() + 4.0, symbolRect.right(), center.y());
    } else if (operation == SceneDocument::TreeNode::Rotate) {
        painter->drawArc(symbolRect, 35 * 16, 285 * 16);
        painter->drawLine(symbolRect.right() - 2.0, center.y() - 5.0, symbolRect.right(), center.y());
        painter->drawLine(symbolRect.right() - 7.0, center.y() - 1.0, symbolRect.right(), center.y());
    } else if (operation == SceneDocument::TreeNode::Scale) {
        const QRectF small(symbolRect.left(), symbolRect.top() + symbolRect.height() * 0.35,
                           symbolRect.width() * 0.44, symbolRect.height() * 0.44);
        const QRectF large(symbolRect.left() + symbolRect.width() * 0.28,
                           symbolRect.top(),
                           symbolRect.width() * 0.72,
                           symbolRect.height() * 0.72);
        painter->setPen(QPen(accent.darker(160), 1.6));
        painter->drawRect(large);
        painter->drawRect(small);
        painter->drawLine(small.right(), small.top(), large.right(), large.top());
    } else if (operation == SceneDocument::TreeNode::Mirror) {
        // Two small rectangles reflected across a vertical centre line.
        const qreal cx = center.x();
        const qreal gap = 2.5;
        const qreal rw = (symbolRect.width() * 0.5 - gap) * 0.72;
        const qreal rh = symbolRect.height() * 0.55;
        const qreal ry = center.y() - rh * 0.5;
        painter->setPen(QPen(accent.darker(155), 1.4));
        painter->setBrush(QColor(255, 255, 255, 55));
        painter->drawRect(QRectF(cx - gap - rw, ry, rw, rh));
        painter->drawRect(QRectF(cx + gap,       ry, rw, rh));
        painter->setPen(QPen(accent.darker(170), 1.0, Qt::DashLine));
        painter->drawLine(QPointF(cx, symbolRect.top()), QPointF(cx, symbolRect.bottom()));
    } else if (operation == SceneDocument::TreeNode::Hull) {
        // Convex polygon outline (pentagon-ish).
        painter->setPen(QPen(accent.darker(155), 1.5));
        painter->setBrush(QColor(255, 255, 255, 55));
        const qreal r = symbolRect.height() * 0.46;
        const QPointF c = symbolRect.center();
        QPolygonF poly;
        // 5 vertices, first at top-centre, then clockwise
        for (int i = 0; i < 5; ++i) {
            const qreal angle = -M_PI * 0.5 + i * 2.0 * M_PI / 5.0;
            poly << QPointF(c.x() + r * std::cos(angle), c.y() + r * std::sin(angle));
        }
        painter->drawPolygon(poly);
    } else if (operation == SceneDocument::TreeNode::Minkowski) {
        // Two overlapping rounded rectangles (⊕-like Minkowski sum symbol).
        painter->setPen(QPen(accent.darker(155), 1.4));
        painter->setBrush(QColor(255, 255, 255, 55));
        const qreal hw = symbolRect.width() * 0.42;
        const qreal hh = symbolRect.height() * 0.38;
        const qreal offset = symbolRect.width() * 0.14;
        painter->drawRoundedRect(QRectF(symbolRect.left(),          center.y() - hh * 0.5, hw, hh), 2.5, 2.5);
        painter->drawRoundedRect(QRectF(symbolRect.right() - hw - offset, center.y() - hh * 0.5, hw, hh), 2.5, 2.5);
        // Plus sign in the gap between the two shapes
        const qreal gx = symbolRect.left() + hw + (symbolRect.width() - 2 * hw - offset) * 0.3;
        painter->setPen(QPen(accent.darker(170), 1.2));
        painter->drawLine(QPointF(gx, center.y() - 2.5), QPointF(gx, center.y() + 2.5));
        painter->drawLine(QPointF(gx - 2.5, center.y()), QPointF(gx + 2.5, center.y()));
    } else if (operation == SceneDocument::TreeNode::LinearExtrude) {
        painter->setPen(QPen(accent.darker(155), 1.4));
        painter->setBrush(QColor(255, 255, 255, 55));
        const QRectF base(symbolRect.left() + 2.0, symbolRect.bottom() - symbolRect.height() * 0.34,
                          symbolRect.width() - 4.0, symbolRect.height() * 0.24);
        const QRectF top = base.translated(0.0, -symbolRect.height() * 0.42);
        painter->drawEllipse(base);
        painter->drawEllipse(top);
        painter->drawLine(QPointF(base.left(), base.center().y()), QPointF(top.left(), top.center().y()));
        painter->drawLine(QPointF(base.right(), base.center().y()), QPointF(top.right(), top.center().y()));
    } else if (operation == SceneDocument::TreeNode::Polyhedron) {
        painter->setPen(QPen(accent.darker(155), 1.25));
        const QPointF top(symbolRect.center().x(), symbolRect.top() + 1.0);
        const QPointF left(symbolRect.left() + 1.0, symbolRect.center().y() - 0.5);
        const QPointF right(symbolRect.right() - 1.0, symbolRect.center().y() - 0.5);
        const QPointF bottom(symbolRect.center().x(), symbolRect.bottom() - 1.0);
        const QPointF mid(symbolRect.center().x(), symbolRect.center().y() + 1.0);

        painter->setBrush(QColor(255, 255, 255, 78));
        painter->drawPolygon(QPolygonF() << top << left << mid);
        painter->setBrush(QColor(255, 255, 255, 48));
        painter->drawPolygon(QPolygonF() << top << mid << right);
        painter->setBrush(QColor(255, 255, 255, 34));
        painter->drawPolygon(QPolygonF() << left << bottom << mid);
        painter->setBrush(QColor(255, 255, 255, 60));
        painter->drawPolygon(QPolygonF() << mid << bottom << right);

        painter->setBrush(Qt::NoBrush);
        painter->drawPolygon(QPolygonF() << top << left << bottom << right);
        painter->drawLine(top, bottom);
        painter->drawLine(left, right);

        painter->setBrush(accent.darker(150));
        painter->setPen(Qt::NoPen);
        for (const QPointF &p : {top, left, right, bottom})
            painter->drawEllipse(p, 1.25, 1.25);
    } else if (operation == SceneDocument::TreeNode::For) {
        painter->setPen(QPen(accent.darker(160), 1.6));
        painter->drawText(symbolRect.adjusted(-2.0, -1.0, 2.0, 1.0), Qt::AlignCenter, QStringLiteral("for"));
    } else if (operation == SceneDocument::TreeNode::Color) {
        QLinearGradient swatch(symbolRect.topLeft(), symbolRect.bottomRight());
        swatch.setColorAt(0.0, QColor(255, 235, 126));
        swatch.setColorAt(0.45, QColor(79, 163, 255));
        swatch.setColorAt(1.0, QColor(106, 222, 148));
        painter->setPen(QPen(accent.darker(160), 1.4));
        painter->setBrush(swatch);
        painter->drawEllipse(symbolRect.adjusted(1.0, 1.0, -1.0, -1.0));
        painter->setBrush(QColor(255, 255, 255, 150));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(QRectF(symbolRect.left() + symbolRect.width() * 0.18,
                                    symbolRect.top() + symbolRect.height() * 0.18,
                                    symbolRect.width() * 0.22,
                                    symbolRect.height() * 0.22));
    } else if (operation == SceneDocument::TreeNode::Scene) {
        // Draw a small grid of dots to represent the top-level scene container.
        painter->setPen(Qt::NoPen);
        painter->setBrush(accent.darker(150));
        const qreal dotR = 2.0;
        const qreal cx = center.x(), cy = center.y();
        const qreal gap = 4.5;
        for (int row = -1; row <= 1; ++row)
            for (int col = -1; col <= 1; ++col)
                painter->drawEllipse(QPointF(cx + col * gap, cy + row * gap), dotR, dotR);
    } else {
        painter->save();
        QFont moduleFont = painter->font();
        moduleFont.setBold(true);
        moduleFont.setPointSizeF(qMax<qreal>(8.0, moduleFont.pointSizeF() + 1.0));
        painter->setFont(moduleFont);
        painter->setPen(QPen(accent.darker(165), 1.6));
        painter->drawText(rect, Qt::AlignCenter, QStringLiteral("M"));
        painter->restore();
    }
}

QVector<ShapeParameterControl> shapeParameterControls(const ShapeNode &shape)
{
    auto expr = [&](int idx, qreal numericValue) -> QString {
        if (idx < shape.parameterExpressions.size() && !shape.parameterExpressions[idx].isEmpty())
            return shape.parameterExpressions[idx];
        return QString::number(numericValue, 'g');
    };

    if (shape.type == ShapeNode::Sphere || shape.type == ShapeNode::Circle)
        return {{QStringLiteral("R"), shape.radius, expr(0, shape.radius)}};

    if (shape.type == ShapeNode::Cylinder)
        return {{QStringLiteral("R"), shape.radius, expr(0, shape.radius)},
                {QStringLiteral("H"), shape.height, expr(1, shape.height)}};

    if (shape.type == ShapeNode::Cone)
        return {{QStringLiteral("R1"), shape.radius,  expr(0, shape.radius)},
                {QStringLiteral("R2"), shape.radius2, expr(1, shape.radius2)},
                {QStringLiteral("H"),  shape.height,  expr(2, shape.height)}};

    if (shape.type == ShapeNode::Polyhedron)
        return {};

    if (shape.type == ShapeNode::Point3D)
        return {{QStringLiteral("X"), shape.position.x(), expr(0, shape.position.x())},
                {QStringLiteral("Y"), shape.position.y(), expr(1, shape.position.y())},
                {QStringLiteral("Z"), shape.position.z(), expr(2, shape.position.z())}};

    if (shape.type == ShapeNode::Face3D) {
        const int count = shape.polyhedronFaces.isEmpty() ? 0 : shape.polyhedronFaces.first().size();
        QVector<ShapeParameterControl> controls;
        controls.append({QStringLiteral("N"), static_cast<qreal>(count), expr(0, static_cast<qreal>(count))});
        for (int i = 0; i < count; ++i) {
            const QString label = QStringLiteral("V%1").arg(i);
            controls.append({label, static_cast<qreal>(shape.polyhedronFaces.first()[i]),
                            expr(1 + i, static_cast<qreal>(shape.polyhedronFaces.first()[i]))});
        }
        return controls;
    }

    return {{QStringLiteral("X"), shape.size.x(), expr(0, shape.size.x())},
            {QStringLiteral("Y"), shape.size.y(), expr(1, shape.size.y())},
            {QStringLiteral("Z"), shape.size.z(), expr(2, shape.size.z())}};
}

QRectF variableExpressionTextRect(const QRectF &variableRect, qreal nameTextWidth)
{
    // Single-row: badge(6+28+4=38) + nameW + gap(4) + eq(12) + gap(2) = 56+nameW
    const qreal exprLeft = 56.0 + nameTextWidth;
    return QRectF(variableRect.left() + exprLeft,
                  variableRect.top() + (VariableHeight - 16.0) * 0.5,
                  variableRect.width() - exprLeft - 6.0,
                  16.0);
}

QVector<ExpressionTextSpan> expressionSpansInTextRect(const QRectF &textRect, const QString &expression, const QFontMetricsF &metrics)
{
    QVector<ExpressionTextSpan> spans;
    // OpenSCAD expressions already have spaces around operators (e.g. "a + b"),
    // so no extra gap is needed — the natural spaces provide visual separation.
    const qreal operatorGap = 0.0;
    qreal x = textRect.left();

    const QString trimmed = expression.trimmed();
    const bool standaloneSignedNumber = !trimmed.isEmpty()
                                        && (trimmed[0] == QLatin1Char('-') || trimmed[0] == QLatin1Char('+'))
                                        && trimmed.size() > 1;
    if (standaloneSignedNumber) {
        bool ok = false;
        trimmed.toDouble(&ok);
        if (ok) {
            const int start = expression.indexOf(trimmed);
            const qreal width = metrics.horizontalAdvance(trimmed);
            spans.append({trimmed,
                          start,
                          trimmed.size(),
                          QRectF(x - 4.0, textRect.top() + 1.0, width + 8.0, textRect.height() - 2.0),
                          true});
            return spans;
        }
    }

    int index = 0;
    while (index < expression.size()) {
        const QChar ch = expression[index];
        const bool startsWithDigit = ch.isDigit();
        const bool startsWithDecimalPoint = ch == QLatin1Char('.')
                                            && index + 1 < expression.size()
                                            && expression[index + 1].isDigit();

        const bool previousIsIdentifier = index > 0
                                          && (expression[index - 1].isLetterOrNumber()
                                              || expression[index - 1] == QLatin1Char('_'));
        if ((startsWithDigit || startsWithDecimalPoint) && !previousIsIdentifier) {
            const int start = index;
            bool hasDigit = false;
            while (index < expression.size() && expression[index].isDigit()) {
                hasDigit = true;
                ++index;
            }
            if (index < expression.size() && expression[index] == QLatin1Char('.')) {
                ++index;
                while (index < expression.size() && expression[index].isDigit()) {
                    hasDigit = true;
                    ++index;
                }
            }

            const bool nextIsIdentifier = index < expression.size()
                                          && (expression[index].isLetter()
                                              || expression[index] == QLatin1Char('_'));
            if (hasDigit && !nextIsIdentifier) {
                const QString text = expression.mid(start, index - start);
                const qreal width = metrics.horizontalAdvance(text);
                spans.append({text,
                              start,
                              index - start,
                              QRectF(x - 4.0, textRect.top() + 1.0, width + 8.0, textRect.height() - 2.0),
                              true});
                x += width;
                continue;
            }
        }

        if (ch.isLetter() || ch == QLatin1Char('_')) {
            const int start = index++;
            while (index < expression.size() && (expression[index].isLetterOrNumber() || expression[index] == QLatin1Char('_')))
                ++index;

            const QString text = expression.mid(start, index - start);
            const qreal width = metrics.horizontalAdvance(text);
            spans.append({text, start, index - start, QRectF(x, textRect.top(), width, textRect.height()), false});
            x += width;
            continue;
        }

        if (ch.isSpace()) {
            x += metrics.horizontalAdvance(ch);
            ++index;
            continue;
        }

        const QString text(ch);
        const bool spacedOperator = ch == QLatin1Char('+')
                                    || ch == QLatin1Char('-')
                                    || ch == QLatin1Char('*')
                                    || ch == QLatin1Char('/');
        if (spacedOperator)
            x += operatorGap;

        const qreal width = metrics.horizontalAdvance(text);
        spans.append({text, index, 1, QRectF(x, textRect.top(), width, textRect.height()), false});
        x += width + (spacedOperator ? operatorGap : 0.0);
        ++index;
    }

    return spans;
}

QVector<ExpressionTextSpan> expressionTextSpans(const QRectF &variableRect, const QString &expression, const QFontMetricsF &metrics, qreal nameTextWidth)
{
    return expressionSpansInTextRect(variableExpressionTextRect(variableRect, nameTextWidth), expression, metrics);
}

QVector<ExpressionNumberControl> expressionNumberControls(const QRectF &variableRect, const QString &expression, const QFontMetricsF &metrics, qreal nameTextWidth)
{
    QVector<ExpressionNumberControl> controls;
    const QVector<ExpressionTextSpan> spans = expressionTextSpans(variableRect, expression, metrics, nameTextWidth);
    for (const ExpressionTextSpan &span : spans) {
        if (span.number)
            controls.append({span.text, span.start, span.length, span.rect});
    }
    return controls;
}

QString transformAxisExpression(const SceneDocument::TreeNode &node, int axis)
{
    if (axis >= 0 && axis < node.transformExpressions.size() && !node.transformExpressions[axis].isEmpty())
        return node.transformExpressions[axis];
    const QVector3D &v = (node.operation == SceneDocument::TreeNode::Translate
                       || node.operation == SceneDocument::TreeNode::Mirror)  ? node.position
                       : node.operation == SceneDocument::TreeNode::Rotate    ? node.rotation
                                                                              : node.scale;
    const float val = axis == 0 ? v.x() : axis == 1 ? v.y() : v.z();
    const int precision = node.operation == SceneDocument::TreeNode::Scale ? 1 : 0;
    return QString::number(val, 'f', precision);
}

qreal transformHeaderWidthForNode(const SceneDocument::TreeNode &node)
{
    if (!isTransformOperation(node.operation))
        return 0.0;

    const QFontMetricsF metrics(sceneTreeGraphicsFont());
    qreal maxExpressionWidth = 0.0;
    for (int axis = 0; axis < 3; ++axis) {
        const QString expression = transformAxisExpression(node, axis);
        const qreal exprPx = metrics.horizontalAdvance(expression) + 8.0;
        maxExpressionWidth = qMax(maxExpressionWidth, exprPx);
    }

    return qMax<qreal>(TransformHeaderWidth, TransformIconWidth + 4.0 + TransformParamLabelArea + maxExpressionWidth + 8.0);
}

QRectF transformParameterControlRect(const QRectF &groupRect, int axis, qreal headerWidth)
{
    if (axis < 0 || axis > 2)
        return QRectF();
    const qreal left = groupRect.left() + TransformIconWidth + 4.0;
    const qreal width = qMax<qreal>(TransformHeaderWidth, headerWidth) - TransformIconWidth - 8.0;
    const qreal rowHeight = 13.0;
    const qreal rowTop = groupRect.top() + 8.0 + axis * 15.0;
    return QRectF(left, rowTop, width, rowHeight);
}

QVector<ExpressionNumberControl> transformParameterNumberControls(const QRectF &groupRect, int axis, const QString &expression, const QFontMetricsF &metrics, qreal headerWidth)
{
    const QRectF rowRect = transformParameterControlRect(groupRect, axis, headerWidth);
    if (!rowRect.isValid())
        return {};
    const QRectF textRect(rowRect.left() + TransformParamLabelArea,
                          rowRect.top(),
                          rowRect.width() - TransformParamLabelArea,
                          rowRect.height());
    QVector<ExpressionNumberControl> controls;
    for (const ExpressionTextSpan &span : expressionSpansInTextRect(textRect, expression, metrics)) {
        if (span.number)
            controls.append({span.text, span.start, span.length, span.rect});
    }
    return controls;
}

QString forLoopVariableName(const SceneDocument::TreeNode &node)
{
    const QString name = node.loopVariable.trimmed();
    return name.isEmpty() ? QStringLiteral("i") : name;
}

QString forLoopRangeExpression(const SceneDocument::TreeNode &node)
{
    const QString range = node.loopRangeExpression.trimmed();
    return range.isEmpty() ? QStringLiteral("[0 : 1 : 3]") : range;
}

QRectF forLoopRangeTextRect(const QRectF &groupRect, const QString &variableName, const QFontMetricsF &metrics)
{
    const QString prefix = QStringLiteral("for (%1 = ").arg(variableName);
    const qreal left = groupRect.left() + 68.0 + metrics.horizontalAdvance(prefix);
    return QRectF(left,
                  groupRect.top() + 7.0,
                  qMax<qreal>(0.0, groupRect.right() - left - 8.0),
                  16.0);
}

qreal forLoopHeaderMinWidth(const QString &variableName,
                            const QString &rangeExpression,
                            const QFontMetricsF &metrics)
{
    const QString name = variableName.trimmed().isEmpty() ? QStringLiteral("i") : variableName.trimmed();
    const QString range = rangeExpression.trimmed().isEmpty() ? QStringLiteral("[0 : 1 : 3]") : rangeExpression.trimmed();
    const QString prefix = QStringLiteral("for (%1 = ").arg(name);
    const QRectF measureRect(0.0, 0.0, 2048.0, GroupHeaderHeight);

    qreal right = 64.0 + metrics.horizontalAdvance(prefix);
    const QVector<ExpressionTextSpan> spans = forLoopRangeTextSpans(measureRect, name, range, metrics);
    for (const ExpressionTextSpan &span : spans)
        right = qMax(right, span.rect.right());

    // Keep the complete expression clear of the collapse chevron drawn at the
    // right of the horizontal header.
    return right + metrics.horizontalAdvance(QStringLiteral(")")) + 36.0;
}

QVector<ExpressionTextSpan> forLoopRangeTextSpans(const QRectF &groupRect,
                                                  const QString &variableName,
                                                  const QString &rangeExpression,
                                                  const QFontMetricsF &metrics)
{
    QVector<ExpressionTextSpan> spans;
    const QRectF textRect = forLoopRangeTextRect(groupRect, variableName, metrics);
    qreal x = textRect.left();
    int index = 0;

    while (index < rangeExpression.size()) {
        const QChar ch = rangeExpression[index];
        if (ch.isSpace()) {
            x += metrics.horizontalAdvance(ch);
            ++index;
            continue;
        }

        const bool signedNumber = (ch == QLatin1Char('-') || ch == QLatin1Char('+'))
                                  && index + 1 < rangeExpression.size()
                                  && (rangeExpression[index + 1].isDigit() || rangeExpression[index + 1] == QLatin1Char('.'));
        if (ch.isDigit() || ch == QLatin1Char('.') || signedNumber) {
            const int start = index;
            if (signedNumber)
                ++index;

            bool hasDigit = false;
            while (index < rangeExpression.size() && rangeExpression[index].isDigit()) {
                hasDigit = true;
                ++index;
            }

            if (index < rangeExpression.size() && rangeExpression[index] == QLatin1Char('.')) {
                ++index;
                while (index < rangeExpression.size() && rangeExpression[index].isDigit()) {
                    hasDigit = true;
                    ++index;
                }
            }

            if (hasDigit) {
                const QString text = rangeExpression.mid(start, index - start);
                const qreal width = metrics.horizontalAdvance(text);
                spans.append({text,
                              start,
                              index - start,
                              QRectF(x - 4.0, textRect.top() + 1.0, width + 8.0, textRect.height() - 2.0),
                              true});
                x += width;
                continue;
            }

            index = start;
        }

        const QString text(ch);
        const qreal width = metrics.horizontalAdvance(text);
        spans.append({text, index, 1, QRectF(x, textRect.top(), width, textRect.height()), false});
        x += width;
        ++index;
    }

    return spans;
}

QVector<ExpressionNumberControl> forLoopRangeNumberControls(const QRectF &groupRect,
                                                            const QString &variableName,
                                                            const QString &rangeExpression,
                                                            const QFontMetricsF &metrics)
{
    QVector<ExpressionNumberControl> controls;
    for (const ExpressionTextSpan &span : forLoopRangeTextSpans(groupRect, variableName, rangeExpression, metrics)) {
        if (span.number)
            controls.append({span.text, span.start, span.length, span.rect});
    }
    return controls;
}

QString linearExtrudeHeightExpression(const SceneDocument::TreeNode &node)
{
    if (!node.transformExpressions.isEmpty()) {
        const QString expression = node.transformExpressions.first().trimmed();
        if (!expression.isEmpty())
            return expression;
    }
    const qreal height = node.scale.x() > 0.0f ? node.scale.x() : 20.0f;
    return QString::number(height, 'g');
}

QRectF linearExtrudeHeightTextRect(const QRectF &groupRect, const QFontMetricsF &metrics)
{
    const QString prefix = QStringLiteral("linear_extrude(height = ");
    const qreal left = groupRect.left() + 68.0 + metrics.horizontalAdvance(prefix);
    return QRectF(left,
                  groupRect.top() + 7.0,
                  qMax<qreal>(0.0, groupRect.right() - left - 8.0),
                  16.0);
}

qreal linearExtrudeHeaderMinWidth(const QString &heightExpression, const QFontMetricsF &metrics)
{
    const QString expression = heightExpression.trimmed().isEmpty()
        ? QStringLiteral("20")
        : heightExpression.trimmed();
    const QString prefix = QStringLiteral("linear_extrude(height = ");
    const QRectF measureRect(0.0, 0.0, 2048.0, GroupHeaderHeight);

    qreal right = 64.0 + metrics.horizontalAdvance(prefix);
    const QVector<ExpressionTextSpan> spans = linearExtrudeHeightTextSpans(measureRect, expression, metrics);
    for (const ExpressionTextSpan &span : spans)
        right = qMax(right, span.rect.right());

    return right + metrics.horizontalAdvance(QStringLiteral(")")) + 36.0;
}

QVector<ExpressionTextSpan> linearExtrudeHeightTextSpans(const QRectF &groupRect,
                                                         const QString &heightExpression,
                                                         const QFontMetricsF &metrics)
{
    const QString expression = heightExpression.trimmed().isEmpty()
        ? QStringLiteral("20")
        : heightExpression.trimmed();
    return expressionSpansInTextRect(linearExtrudeHeightTextRect(groupRect, metrics),
                                     expression,
                                     metrics);
}

QRectF shapeParameterControlRect(const QRectF &primitiveRect, int index, int count)
{
    if (index < 0 || count <= 0 || index >= count)
        return QRectF();

    // Row is positioned outside the card border (right of PrimitiveCardWidth).
    const qreal left = primitiveRect.left() + PrimitiveCardWidth + 4.0;
    const qreal width = primitiveRect.right() - left - 4.0;
    const qreal gap = 2.0;
    const qreal availableHeight = PrimitiveHeight - 6.0;
    const qreal rowHeight = qMin<qreal>(14.0, (availableHeight - gap * (count - 1)) / count);
    const qreal totalHeight = rowHeight * count + gap * (count - 1);
    const qreal top = primitiveRect.top() + (PrimitiveHeight - totalHeight) * 0.5 + index * (rowHeight + gap);
    return QRectF(left, top, width, rowHeight);
}

QVector<ExpressionNumberControl> shapeParameterNumberControls(const QRectF &primitiveRect, int paramIndex, int paramCount, const QString &expression, const QFontMetricsF &metrics)
{
    const QRectF rowRect = shapeParameterControlRect(primitiveRect, paramIndex, paramCount);
    if (!rowRect.isValid())
        return {};

    const QRectF textRect(rowRect.left() + PrimitiveParamLabelArea,
                          rowRect.top(),
                          rowRect.right() - rowRect.left() - PrimitiveParamLabelArea,
                          rowRect.height());

    QVector<ExpressionNumberControl> controls;
    for (const ExpressionTextSpan &span : expressionSpansInTextRect(textRect, expression, metrics)) {
        if (span.number)
            controls.append({span.text, span.start, span.length, span.rect});
    }
    return controls;
}

QSizeF primitivePreviewSize(const ShapeNode &shape)
{
    const QFontMetricsF metrics(sceneTreeGraphicsFont());
    const QVector<ShapeParameterControl> controls = shapeParameterControls(shape);
    qreal maxExprWidth = 0.0;
    for (const auto &control : controls) {
        const qreal w = metrics.horizontalAdvance(control.expression) + 8.0;
        maxExprWidth = qMax(maxExprWidth, w);
    }
    const qreal width = qMax<qreal>(PrimitiveWidth,
                                    PrimitiveCardWidth + 4.0 + PrimitiveParamLabelArea + maxExprWidth + 4.0);
    return QSizeF(width, PrimitiveHeight);
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

int insertionIndexForY(const QVector<QRectF> &childRects, qreal y, int minimumIndex)
{
    if (childRects.isEmpty())
        return minimumIndex;

    const int minIndex = qBound(0, minimumIndex, childRects.size());
    const qreal hysteresis = qMax<qreal>(ChildGap * 2.0, 20.0);

    if (minIndex == 0 && y <= childRects.first().top() + hysteresis)
        return 0;

    for (int slot = qMax(1, minIndex); slot < childRects.size(); ++slot) {
        const qreal gapTop = childRects[slot - 1].bottom();
        const qreal gapBottom = childRects[slot].top();
        if (y >= gapTop - hysteresis && y <= gapBottom + hysteresis)
            return slot;
    }

    if (y >= childRects.last().bottom() - hysteresis)
        return childRects.size();

    int bestSlot = minIndex;
    qreal bestDistance = std::numeric_limits<qreal>::max();
    for (int slot = minIndex; slot <= childRects.size(); ++slot) {
        qreal slotY = 0.0;
        if (slot == 0) {
            slotY = childRects.first().top();
        } else if (slot == childRects.size()) {
            slotY = childRects.last().bottom();
        } else {
            slotY = (childRects[slot - 1].bottom() + childRects[slot].top()) * 0.5;
        }

        const qreal distance = qAbs(y - slotY);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestSlot = slot;
        }
    }

    return bestSlot;
}

QSizeF defaultPreviewSize()
{
    return QSizeF(PrimitiveWidth, PrimitiveHeight);
}

QSizeF variablePreviewSize(const QString &name, const QString &expression)
{
    const QFontMetricsF metrics(sceneTreeGraphicsFont());
    const QString trimmedName = name.trimmed();
    const QString trimmedExpr = expression.trimmed();

    // Guarantee a minimum name width of ~3 chars so tiny names still look OK
    const qreal nameWidth = qMax(metrics.horizontalAdvance(QStringLiteral("xxx")),
                                 metrics.horizontalAdvance(trimmedName));
    // Expression: actual advance + a little padding (operators add visual space in rendering)
    const qreal exprWidth = qMax(metrics.horizontalAdvance(QStringLiteral("0")),
                                 metrics.horizontalAdvance(trimmedExpr)) + 4.0;

    // Single-row: badge(38) + nameW + gap(4) + eq(12) + gap(2) + exprW + right_pad(6)
    return QSizeF(qMax(PrimitiveWidth, 62.0 + nameWidth + exprWidth), VariableHeight);
}

static qreal moduleCallExprAdvance(const QString &expr, const QFontMetricsF &metrics)
{
    // operatorGap is 0, so the actual advance is just the string width.
    return metrics.horizontalAdvance(expr);
}

QSizeF moduleCallPreviewSize(const QString &moduleName, const QVector<ModuleCallParam> &params)
{
    const QFontMetricsF metrics(sceneTreeGraphicsFont());
    // "moduleName(" + params + ")" — measure each piece with actual font
    qreal textWidth = metrics.horizontalAdvance(moduleName.trimmed() + QStringLiteral("()"));
    for (int i = 0; i < params.size(); ++i) {
        textWidth += metrics.horizontalAdvance(params[i].name + QStringLiteral(" = "));
        textWidth += moduleCallExprAdvance(params[i].expression.trimmed(), metrics);
        if (i < params.size() - 1)
            textWidth += metrics.horizontalAdvance(QStringLiteral(", "));
    }
    textWidth += 8.0; // a little right padding
    return QSizeF(qMax(PrimitiveWidth, 46.0 + textWidth), VariableHeight);
}

QVector<ModuleCallParamControl> moduleCallParamControls(const QRectF &cardRect,
                                                        const QString &moduleName,
                                                        const QVector<ModuleCallParam> &params,
                                                        const QFontMetricsF &metrics)
{
    QVector<ModuleCallParamControl> controls;
    if (params.isEmpty())
        return controls;

    qreal x = cardRect.left() + 46.0 + metrics.horizontalAdvance(moduleName + QStringLiteral("("));
    for (int i = 0; i < params.size(); ++i) {
        x += metrics.horizontalAdvance(params[i].name + QStringLiteral(" = "));
        const QRectF exprRect(x, cardRect.top(), cardRect.right() - x, VariableHeight);
        for (const ExpressionTextSpan &span : expressionSpansInTextRect(exprRect, params[i].expression, metrics)) {
            if (span.number)
                controls.append({params[i].varNodeId, span.start, span.length, span.rect});
        }
        x += moduleCallExprAdvance(params[i].expression, metrics);
        if (i < params.size() - 1)
            x += metrics.horizontalAdvance(QStringLiteral(", "));
    }
    return controls;
}

QSizeF groupPreviewSize()
{
    return QSizeF(GroupMinWidth, GroupHeaderHeight + GroupPadding * 2.0 + PrimitiveHeight);
}

QSizeF transformPreviewSize()
{
    return QSizeF(TransformHeaderWidth + GroupPadding * 2.0 + PrimitiveWidth,
                  GroupPadding * 2.0 + PrimitiveHeight);
}

QSizeF differencePreviewSize()
{
    return QSizeF(GroupWideMinWidth, GroupHeaderHeight + GroupPadding * 2.0 + DifferenceMinContentHeight);
}

QSizeF previewSizeForTool(const QString &tool)
{
    if (isVariableToolName(tool))
        return variablePreviewSize();
    if (tool == "call")
        return moduleCallPreviewSize(QStringLiteral("module"), {});
    if (ShapeNode::isPrimitiveTool(tool))
        return defaultPreviewSize();
    if (tool == "difference")
        return differencePreviewSize();
    if (tool == "intersection")
        return QSizeF(GroupWideMinWidth, GroupHeaderHeight + GroupPadding * 2.0 + PrimitiveHeight);
    if (tool == "module") {
        // Height matches the actual empty-module render: header + param placeholder + separator
        // + call template + body label + one body-slot (+PrimitiveHeight).
        const qreal bodyContent = 16.0 + VariableHeight + ChildGap    // "Parameters" label + empty param row
                                + 16.0 + ChildGap                     // separator / template-label gap
                                + VariableHeight + ChildGap            // call template row
                                + 16.0 + ChildGap                     // "Body" label gap
                                + PrimitiveHeight;                     // visible body drop zone
        return QSizeF(GroupModuleMinWidth,
                      GroupHeaderHeight + GroupPadding * 2.0 + bodyContent);
    }
    if (tool == "for")
        return QSizeF(qMax(GroupWideMinWidth,
                           forLoopHeaderMinWidth(QStringLiteral("i"),
                                                 QStringLiteral("[0 : 1 : 3]"),
                                                 QFontMetricsF(sceneTreeGraphicsFont()))),
                      GroupHeaderHeight + GroupPadding * 2.0 + PrimitiveHeight);
    if (tool == "linear_extrude")
        return QSizeF(qMax(GroupWideMinWidth,
                           linearExtrudeHeaderMinWidth(QStringLiteral("20"),
                                                       QFontMetricsF(sceneTreeGraphicsFont()))),
                      GroupHeaderHeight + GroupPadding * 2.0 + PrimitiveHeight);
    if (tool == "color")
        return QSizeF(GroupWideMinWidth, GroupHeaderHeight + GroupPadding * 2.0 + PrimitiveHeight);
    if (tool == "translate" || tool == "rotate" || tool == "scale")
        return transformPreviewSize();

    return groupPreviewSize();
}

bool isVariableToolName(const QString &tool)
{
    const QString normalized = tool.toLower();
    return normalized == QStringLiteral("var")
           || normalized == QStringLiteral("variable")
           || normalized == QStringLiteral("par")
           || normalized == QStringLiteral("parameter");
}

ShapeNode::Type primitiveTypeForTool(const QString &tool)
{
    const QString normalized = tool.toLower();
    if (normalized.contains("circle"))
        return ShapeNode::Circle;
    if (normalized.contains("sphere"))
        return ShapeNode::Sphere;
    if (normalized.contains("cone"))
        return ShapeNode::Cone;
    if (normalized.contains("cylinder"))
        return ShapeNode::Cylinder;
    if (normalized.contains("polyhedron"))
        return ShapeNode::Polyhedron;
    if (normalized == "point_3d")
        return ShapeNode::Point3D;
    if (normalized == "face_3d")
        return ShapeNode::Face3D;
    return ShapeNode::Cube;
}

QString toolNameForPrimitiveType(ShapeNode::Type type)
{
    if (type == ShapeNode::Circle)
        return QStringLiteral("circle");
    if (type == ShapeNode::Sphere)
        return QStringLiteral("sphere");
    if (type == ShapeNode::Cylinder)
        return QStringLiteral("cylinder");
    if (type == ShapeNode::Cone)
        return QStringLiteral("cone");
    if (type == ShapeNode::Polyhedron)
        return QStringLiteral("polyhedron");
    if (type == ShapeNode::Point3D)
        return QStringLiteral("point_3d");
    if (type == ShapeNode::Face3D)
        return QStringLiteral("face_3d");
    return QStringLiteral("cube");
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
    if (normalized.contains("translate")) {
        *operation = SceneDocument::TreeNode::Translate;
        return true;
    }
    if (normalized.contains("rotate")) {
        *operation = SceneDocument::TreeNode::Rotate;
        return true;
    }
    if (normalized.contains("scale")) {
        *operation = SceneDocument::TreeNode::Scale;
        return true;
    }
    if (normalized.contains("mirror")) {
        *operation = SceneDocument::TreeNode::Mirror;
        return true;
    }
    if (normalized.contains("hull")) {
        *operation = SceneDocument::TreeNode::Hull;
        return true;
    }
    if (normalized.contains("minkowski")) {
        *operation = SceneDocument::TreeNode::Minkowski;
        return true;
    }
    if (normalized == QStringLiteral("polyhedron")) {
        *operation = SceneDocument::TreeNode::Polyhedron;
        return true;
    }
    if (normalized == QStringLiteral("linear_extrude") || normalized == QStringLiteral("extrude")) {
        *operation = SceneDocument::TreeNode::LinearExtrude;
        return true;
    }
    if (normalized == QStringLiteral("for")) {
        *operation = SceneDocument::TreeNode::For;
        return true;
    }
    if (normalized == QStringLiteral("color") || normalized == QStringLiteral("colour")) {
        *operation = SceneDocument::TreeNode::Color;
        return true;
    }
    return false;
}

namespace {

const OperationVisual OperationVisuals[] = {
    {SceneDocument::TreeNode::Union, "union", QColor(216, 237, 226), GroupMinWidth},
    {SceneDocument::TreeNode::Difference, "difference", QColor(247, 224, 204), GroupWideMinWidth},
    {SceneDocument::TreeNode::Intersection, "intersection", QColor(226, 220, 247), GroupWideMinWidth},
    {SceneDocument::TreeNode::Module, "module", QColor(230, 232, 236), GroupModuleMinWidth},
    {SceneDocument::TreeNode::Translate, "translate", QColor(218, 238, 246), TransformHeaderWidth + GroupPadding * 2.0 + PrimitiveWidth},
    {SceneDocument::TreeNode::Rotate, "rotate", QColor(239, 229, 247), TransformHeaderWidth + GroupPadding * 2.0 + PrimitiveWidth},
    {SceneDocument::TreeNode::Scale, "scale", QColor(229, 241, 218), TransformHeaderWidth + GroupPadding * 2.0 + PrimitiveWidth},
    {SceneDocument::TreeNode::Mirror, "mirror", QColor(242, 218, 235), TransformHeaderWidth + GroupPadding * 2.0 + PrimitiveWidth},
    {SceneDocument::TreeNode::Hull, "hull", QColor(218, 240, 218), GroupMinWidth},
    {SceneDocument::TreeNode::Minkowski, "minkowski", QColor(227, 235, 248), GroupMinWidth},
    {SceneDocument::TreeNode::Polyhedron, "polyhedron", QColor(218, 238, 228), 300.0},
    {SceneDocument::TreeNode::LinearExtrude, "linear_extrude", QColor(222, 238, 232), GroupWideMinWidth},
    {SceneDocument::TreeNode::For, "for", QColor(236, 232, 205), GroupWideMinWidth},
    {SceneDocument::TreeNode::Color, "color", QColor(218, 234, 248), TransformHeaderWidth + GroupPadding * 2.0 + PrimitiveWidth},
    {SceneDocument::TreeNode::Scene, "scene", QColor(210, 215, 225), GroupMinWidth},
};

} // namespace

const OperationVisual &operationVisual(SceneDocument::TreeNode::Operation operation)
{
    for (const OperationVisual &visual : OperationVisuals) {
        if (visual.operation == operation)
            return visual;
    }
    return OperationVisuals[0];
}

qreal minimumWidthForOperation(SceneDocument::TreeNode::Operation operation)
{
    const qreal hardMin = operationVisual(operation).minWidth;
    if (isVerticalHeaderOperation(operation))
        return hardMin;

    // For horizontal-header cards ensure the label fits.
    // Header geometry: grip+gap(30) + icon(24) + gap(10) = 64 from left to label;
    // chevron+gap(28) reserved on the right.
    const QFontMetricsF fm(sceneTreeGraphicsFont());
    const qreal labelW = fm.horizontalAdvance(
        QString::fromLatin1(operationVisual(operation).toolName));
    return qMax(hardMin, 64.0 + labelW + 28.0 + 6.0);
}

QString labelForOperation(SceneDocument::TreeNode::Operation operation)
{
    return QString::fromLatin1(operationVisual(operation).toolName);
}

bool isTransformOperation(SceneDocument::TreeNode::Operation operation)
{
    return operation == SceneDocument::TreeNode::Translate
           || operation == SceneDocument::TreeNode::Rotate
           || operation == SceneDocument::TreeNode::Scale
           || operation == SceneDocument::TreeNode::Mirror;
}

bool isVerticalHeaderOperation(SceneDocument::TreeNode::Operation operation)
{
    return isTransformOperation(operation)
           || operation == SceneDocument::TreeNode::Color;
}

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

QRectF slotMarkerRectForInsertIndex(const QRectF &contentRect, const QVector<QRectF> &childRects, int insertIndex)
{
    const qreal markerWidth = childRects.isEmpty()
                                  ? qMax(contentRect.width(), PrimitiveWidth)
                                  : qMax(contentRect.width(), childRects.first().width());
    qreal y = contentRect.top();

    if (childRects.isEmpty()) {
        y = contentRect.top();
    } else if (insertIndex <= 0) {
        y = childRects.first().top() - ChildGap * 0.5;
    } else if (insertIndex >= childRects.size()) {
        y = childRects.last().bottom() + ChildGap * 0.5;
    } else {
        y = (childRects[insertIndex - 1].bottom() + childRects[insertIndex].top()) * 0.5;
    }

    return QRectF(contentRect.left(), y, markerWidth, 1.0);
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
        if (m_label == QStringLiteral("module"))
            glyphRect = QRectF(8.0, 6.0, 38.0, 40.0);
        if (isVariableToolName(m_label)) {
            const QRectF badgeRect = glyphRect.adjusted(0.0, 8.0, 0.0, -8.0);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0, 0, 0, 35));
            painter->drawRoundedRect(badgeRect.translated(1.0, 1.0), 4.0, 4.0);
            QLinearGradient badgeGradient(badgeRect.topLeft(), badgeRect.bottomLeft());
            badgeGradient.setColorAt(0.0, QColor(255, 237, 172));
            badgeGradient.setColorAt(1.0, QColor(193, 143, 48));
            painter->setPen(QPen(QColor(255, 248, 218, 170), 1.0));
            painter->setBrush(QBrush(badgeGradient));
            painter->drawRoundedRect(badgeRect, 4.0, 4.0);
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

QColor fillForTool(const QString &tool)
{
    if (isVariableToolName(tool))
        return QColor(246, 236, 196);

    SceneDocument::TreeNode::Operation operation;
    if (operationForToolName(tool, &operation))
        return operationVisual(operation).fill;
    return QColor(219, 231, 246);
}


QColor fillForOperation(SceneDocument::TreeNode::Operation operation)
{
    return operationVisual(operation).fill;
}

QGraphicsScene *createTreeGraphicsScene(QObject *parent)
{
    return new TreeGraphicsScene(parent);
}

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
