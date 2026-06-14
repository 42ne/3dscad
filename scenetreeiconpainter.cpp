#include "scenetreeiconpainter.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <cmath>

namespace SceneTreeGraphics {

void paintPrimitiveIcon(QPainter *painter, ShapeNode::Type type, const QRectF &rect)
{
    const QColor outline(59, 95, 134);
    const QColor face(178, 207, 238);
    const QColor faceLight(221, 235, 248);
    const QColor faceDark(139, 176, 214);

    // ── 2D spline icons: flat outline + square control-point nodes ───────────
    auto drawNode = [&](QPainter *p, const QPointF &pt) {
        p->drawRect(QRectF(pt.x() - 2.0, pt.y() - 2.0, 4.0, 4.0));
    };
    const QColor splineStroke(72, 110, 155);
    const QColor splineFill(185, 213, 240, 38);
    const QColor nodeStroke(60, 95, 140);
    const QColor nodeFill(220, 235, 252);

    painter->setPen(QPen(outline, 1));
    if (type == ShapeNode::Square) {
        const QRectF sr = rect.adjusted(6.0, 6.0, -6.0, -6.0);
        painter->setPen(QPen(splineStroke, 1.5));
        painter->setBrush(splineFill);
        painter->drawRect(sr);
        painter->setPen(QPen(nodeStroke, 1.0));
        painter->setBrush(nodeFill);
        drawNode(painter, sr.topLeft());
        drawNode(painter, sr.topRight());
        drawNode(painter, sr.bottomRight());
        drawNode(painter, sr.bottomLeft());
        return;
    }
    if (type == ShapeNode::Polygon2D) {
        const qreal cx = rect.center().x(), cy = rect.center().y();
        const qreal rx = rect.width() * 0.33, ry = rect.height() * 0.33;
        const struct { qreal a, s; } verts[] = {
            { 90.0, 1.00}, {20.0, 0.88}, {310.0, 0.92},
            {220.0, 0.88}, {150.0, 0.95}
        };
        QPolygonF poly;
        for (const auto &v : verts) {
            const qreal rad = v.a * M_PI / 180.0;
            poly << QPointF(cx + std::cos(rad) * rx * v.s,
                            cy - std::sin(rad) * ry * v.s);
        }
        painter->setPen(QPen(splineStroke, 1.5));
        painter->setBrush(splineFill);
        painter->drawPolygon(poly);
        // Vertex nodes
        painter->setPen(QPen(nodeStroke, 1.0));
        painter->setBrush(nodeFill);
        for (const QPointF &pt : poly) drawNode(painter, pt);
        return;
    }
    if (type == ShapeNode::Sphere) {
        const QRectF sr = rect.adjusted(3.0, 3.0, -3.0, -3.0);
        painter->setBrush(face);
        painter->drawEllipse(sr);
        painter->setPen(QPen(QColor(93, 127, 166), 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(sr.adjusted(3.0, sr.height() * 0.28, -3.0, -sr.height() * 0.28));
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 255, 255, 165));
        painter->drawEllipse(QRectF(sr.left() + sr.width() * 0.25,
                                    sr.top()  + sr.height() * 0.18,
                                    sr.width() * 0.22,
                                    sr.height() * 0.16));
        return;
    }

    if (type == ShapeNode::Circle) {
        const QRectF cr = rect.adjusted(6.0, 6.0, -6.0, -6.0);
        // Flat circle outline
        painter->setPen(QPen(splineStroke, 1.5));
        painter->setBrush(splineFill);
        painter->drawEllipse(cr);
        // 4 control nodes at cardinal positions
        const QPointF nodes[] = {
            {cr.center().x(), cr.top()},    // N
            {cr.right(),  cr.center().y()}, // E
            {cr.center().x(), cr.bottom()}, // S
            {cr.left(),   cr.center().y()}, // W
        };
        painter->setPen(QPen(nodeStroke, 1.0));
        painter->setBrush(nodeFill);
        for (const QPointF &pt : nodes) drawNode(painter, pt);
        // Tangent handle stub at East node (shows it's a curve, not a circle primitive)
        painter->setPen(QPen(splineStroke, 0.9));
        painter->drawLine(nodes[1] + QPointF(0, -4.5), nodes[1] + QPointF(0, 4.5));
        return;
    }

    if (type == ShapeNode::Cylinder) {
        const QRectF top(rect.left() + 5.0, rect.top() + 4.0, rect.width() - 10.0, rect.height() * 0.30);
        const QRectF bottom(top.left(), rect.bottom() - top.height() - 4.0, top.width(), top.height());
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
        const qreal bh = rect.height() * 0.28;
        const QRectF base(rect.left() + 5.0, rect.bottom() - bh - 4.0, rect.width() - 10.0, bh);
        const QPointF apex(rect.center().x(), rect.top() + 5.0);
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

namespace {

// ── Future-tool glyphs (not yet backed by an Operation/primitive) ───────────
// Coordinates are relative to the glyph rect, so these scale to any icon size.

void paintFutureText(QPainter *p, const QRectF &g)
{
    // Bold "A" letterform on a baseline — text/font marker. Stroked (not
    // drawText) so it stays crisp at small sizes and renders without fonts.
    p->save();
    QPen pen(QColor(221, 235, 248), g.width() * 0.10);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p->setPen(pen);
    const qreal cx = g.center().x();
    const QPointF apex(cx, g.top() + g.height() * 0.10);
    const QPointF bl(g.left() + g.width() * 0.20, g.bottom() - g.height() * 0.22);
    const QPointF br(g.right() - g.width() * 0.20, g.bottom() - g.height() * 0.22);
    p->drawLine(apex, bl);
    p->drawLine(apex, br);
    const qreal t = 0.58;
    p->drawLine(apex + (bl - apex) * t, apex + (br - apex) * t);
    QPen blp(QColor(150, 185, 224), g.height() * 0.045);
    blp.setCapStyle(Qt::RoundCap);
    p->setPen(blp);
    const qreal by = g.bottom() - g.height() * 0.06;
    p->drawLine(QPointF(g.left() + g.width() * 0.14, by),
                QPointF(g.right() - g.width() * 0.14, by));
    p->restore();
}

void paintFutureOffset(QPainter *p, const QRectF &g)
{
    // Inner solid rounded shape + outer dashed offset outline + distance tick.
    p->save();
    const QColor stroke(205, 220, 240);
    const qreal r = g.width() * 0.16;
    const QRectF inner = g.adjusted(g.width() * 0.26, g.height() * 0.26,
                                    -g.width() * 0.26, -g.height() * 0.26);
    const QRectF outer = g.adjusted(g.width() * 0.08, g.height() * 0.08,
                                    -g.width() * 0.08, -g.height() * 0.08);
    p->setBrush(QColor(255, 255, 255, 60));
    p->setPen(QPen(stroke, g.width() * 0.035));
    p->drawRoundedRect(inner, r * 0.7, r * 0.7);
    QPen dashed(QColor(160, 195, 232), g.width() * 0.030);
    dashed.setStyle(Qt::DashLine);
    p->setBrush(Qt::NoBrush);
    p->setPen(dashed);
    p->drawRoundedRect(outer, r, r);
    p->setPen(QPen(stroke, g.width() * 0.030));
    const qreal ax = g.center().x();
    p->drawLine(QPointF(ax, outer.top()), QPointF(ax, inner.top()));
    p->restore();
}

void paintFutureProjection(QPainter *p, const QRectF &g)
{
    // Iso cube up top casting a flat footprint outline below, dotted droplines.
    p->save();
    const QColor stroke(214, 224, 244);
    const qreal w = g.width(), h = g.height();
    const QPointF c(g.center().x(), g.top() + h * 0.26);
    const qreal s = w * 0.28;
    QPolygonF top;
    top << QPointF(c.x(), c.y() - s * 0.55)
        << QPointF(c.x() + s, c.y())
        << QPointF(c.x(), c.y() + s * 0.55)
        << QPointF(c.x() - s, c.y());
    p->setPen(QPen(stroke, w * 0.030));
    p->setBrush(QColor(255, 255, 255, 70));
    p->drawPolygon(top);
    p->drawLine(top[3], top[3] + QPointF(0, s * 0.7));
    p->drawLine(top[1], top[1] + QPointF(0, s * 0.7));
    p->drawLine(top[2], top[2] + QPointF(0, s * 0.7));
    p->drawLine(top[3] + QPointF(0, s * 0.7), top[2] + QPointF(0, s * 0.7));
    p->drawLine(top[1] + QPointF(0, s * 0.7), top[2] + QPointF(0, s * 0.7));
    QPolygonF foot;
    const qreal fy = g.bottom() - h * 0.16;
    foot << QPointF(c.x(), fy - s * 0.42)
         << QPointF(c.x() + s, fy)
         << QPointF(c.x(), fy + s * 0.42)
         << QPointF(c.x() - s, fy);
    p->setBrush(QColor(150, 195, 240, 90));
    p->setPen(QPen(QColor(170, 205, 240), w * 0.028));
    p->drawPolygon(foot);
    QPen dp(QColor(150, 180, 220), w * 0.022);
    dp.setStyle(Qt::DotLine);
    p->setPen(dp);
    p->drawLine(top[0] + QPointF(0, s * 0.7), foot[0]);
    p->drawLine(top[3] + QPointF(0, s * 0.7), foot[3]);
    p->drawLine(top[1] + QPointF(0, s * 0.7), foot[1]);
    p->restore();
}

void paintFutureImport(QPainter *p, const QRectF &g)
{
    // Open tray with an arrow descending into it = import a file.
    p->save();
    const QColor stroke(214, 224, 244);
    const qreal w = g.width(), h = g.height();
    QPen ap(stroke, w * 0.075);
    ap.setCapStyle(Qt::RoundCap);
    ap.setJoinStyle(Qt::RoundJoin);
    p->setPen(ap);
    const qreal ax = g.center().x();
    const qreal aTop = g.top() + h * 0.10;
    const qreal aBot = g.center().y() + h * 0.10;
    p->drawLine(QPointF(ax, aTop), QPointF(ax, aBot));
    p->drawLine(QPointF(ax - w * 0.16, aBot - h * 0.16), QPointF(ax, aBot));
    p->drawLine(QPointF(ax + w * 0.16, aBot - h * 0.16), QPointF(ax, aBot));
    QPen tp(QColor(170, 200, 234), w * 0.055);
    tp.setCapStyle(Qt::RoundCap);
    tp.setJoinStyle(Qt::RoundJoin);
    p->setPen(tp);
    const qreal tl = g.left() + w * 0.16, tr = g.right() - w * 0.16;
    const qreal ty = g.bottom() - h * 0.20, tb = g.bottom() - h * 0.06;
    p->drawLine(QPointF(tl, ty), QPointF(tl, tb));
    p->drawLine(QPointF(tr, ty), QPointF(tr, tb));
    p->drawLine(QPointF(tl, tb), QPointF(tr, tb));
    p->restore();
}

} // namespace

bool paintFutureToolIcon(QPainter *painter, const QString &toolName, const QRectF &rect)
{
    // offset is now a real Operation (see paintOperationIcon); the remaining
    // entries stay here until their features are wired.
    const QString n = toolName.toLower();
    if (n == QStringLiteral("text"))       { paintFutureText(painter, rect);       return true; }
    if (n == QStringLiteral("projection")) { paintFutureProjection(painter, rect); return true; }
    if (n == QStringLiteral("import"))     { paintFutureImport(painter, rect);     return true; }
    return false;
}

QRectF paintToolbarIconFrame(QPainter *painter, const QRectF &rect, const QColor &accent, bool selected)
{
    constexpr qreal kOuterRadius = 8.0;   // corner radius of the dark mat
    constexpr qreal kGap         = 4.0;   // gap between outer edge and glass panel
    constexpr qreal kGlassRadius = 5.5;   // corner radius of the glass panel

    // Dark semi-transparent mat that fills the icon cell — this is the visible gap
    QPainterPath outerPath;
    outerPath.addRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), kOuterRadius, kOuterRadius);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 88));
    painter->drawPath(outerPath);

    // Glass panel inset inside the dark mat
    const QRectF frameRect = rect.adjusted(kGap, kGap, -kGap, -kGap);
    QPainterPath glassPath;
    glassPath.addRoundedRect(frameRect, kGlassRadius, kGlassRadius);

    QLinearGradient glass(frameRect.topLeft(), frameRect.bottomLeft());
    glass.setColorAt(0.0, QColor(32, 42, 58, 224));
    glass.setColorAt(1.0, QColor(7, 11, 18, 205));
    painter->setPen(QPen(selected ? QColor(255, 203, 87) : accent.lighter(120),
                         selected ? 1.8 : 1.15));
    painter->setBrush(glass);
    painter->drawPath(glassPath);

    // Accent tint overlay inside the glass panel
    QPainterPath tintPath;
    tintPath.addRoundedRect(rect.adjusted(kGap + 2.0, kGap + 2.0, -(kGap + 2.0), -(kGap + 2.0)),
                            kGlassRadius - 1.5, kGlassRadius - 1.5);
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
        const QColor extStroke(205, 220, 240);
        const QColor extFill(255, 255, 255, 110);
        painter->setPen(QPen(extStroke, 1.5));
        painter->setBrush(extFill);
        const QRectF base(symbolRect.left() + 2.0, symbolRect.bottom() - symbolRect.height() * 0.34,
                          symbolRect.width() - 4.0, symbolRect.height() * 0.24);
        const QRectF top = base.translated(0.0, -symbolRect.height() * 0.42);
        painter->drawEllipse(base);
        painter->drawEllipse(top);
        painter->drawLine(QPointF(base.left(), base.center().y()), QPointF(top.left(), top.center().y()));
        painter->drawLine(QPointF(base.right(), base.center().y()), QPointF(top.right(), top.center().y()));
    } else if (operation == SceneDocument::TreeNode::RotateExtrude) {
        const QColor extStroke(205, 220, 240);
        const QColor extFill(255, 255, 255, 110);
        const QRectF sr = symbolRect.adjusted(1.5, 2.0, -1.5, -2.0);
        const QPointF c = sr.center();
        const qreal rx = sr.width()  * 0.46;
        const qreal ry = sr.height() * 0.46;
        const qreal hx = rx * 0.42;
        const qreal hy = ry * 0.42;

        QPainterPath torus;
        torus.addEllipse(c, rx, ry);
        torus.addEllipse(c, hx, hy);

        painter->setPen(Qt::NoPen);
        painter->setBrush(extFill);
        painter->drawPath(torus);

        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(extStroke, 1.5));
        painter->drawEllipse(c, rx, ry);
        painter->setPen(QPen(extStroke, 1.2));
        painter->drawEllipse(c, hx, hy);
    } else if (operation == SceneDocument::TreeNode::Resize) {
        painter->setPen(QPen(accent.darker(155), 1.3));
        const QRectF r = symbolRect.adjusted(2.0, 2.0, -2.0, -2.0);
        painter->drawRect(r);
        // Diagonal arrows at corners
        const qreal arrowLen = 4.0;
        for (int corner = 0; corner < 4; ++corner) {
            const int sx = (corner & 1) ? 1 : -1;
            const int sy = (corner & 2) ? 1 : -1;
            const QPointF baseC = (corner & 1) ? r.topRight() : r.topLeft();
            const QPointF cornerPt = (corner & 2) ? ((corner & 1) ? r.bottomRight() : r.bottomLeft()) : baseC;
            QPointF dir(float(-sx), float(-sy));
            const QPointF tip = cornerPt + dir * arrowLen;
            painter->drawLine(cornerPt, tip);
            // Arrowhead
            const QPointF perp(-dir.y(), dir.x());
            painter->drawLine(tip, tip - dir * 2.0 + perp * 1.5);
            painter->drawLine(tip, tip - dir * 2.0 - perp * 1.5);
        }
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
        QFont forFont = painter->font();
        forFont.setBold(true);
        forFont.setPointSizeF(qMax<qreal>(7.5, forFont.pointSizeF()));
        painter->setFont(forFont);
        painter->setPen(QColor(210, 222, 238));
        painter->drawText(symbolRect.adjusted(-2.0, -1.0, 2.0, 1.0), Qt::AlignCenter, QStringLiteral("for"));
    } else if (operation == SceneDocument::TreeNode::Conditional) {
        QFont ifFont = painter->font();
        ifFont.setBold(true);
        ifFont.setPointSizeF(qMax<qreal>(7.5, ifFont.pointSizeF()));
        painter->setFont(ifFont);
        painter->setPen(QColor(180, 170, 230));
        painter->drawText(symbolRect.adjusted(-2.0, -1.0, 2.0, 1.0), Qt::AlignCenter, QStringLiteral("if"));
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
    } else if (operation == SceneDocument::TreeNode::Offset) {
        paintFutureOffset(painter, symbolRect);
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
        painter->setPen(QColor(210, 222, 238));
        painter->drawText(rect, Qt::AlignCenter, QStringLiteral("M"));
        painter->restore();
    }
}

} // namespace SceneTreeGraphics
