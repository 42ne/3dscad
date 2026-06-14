// Prototype renderer for future toolbar icons (text, offset, projection,
// import) plus the existing resize glyph, drawn in the app's glass-frame style
// into a PNG contact sheet for visual review before integration.
//
// Build (offscreen, Qt5):
//   g++ -std=gnu++1z -O1 -fexceptions -I<Qt>/include -I<Qt>/include/QtCore \
//       -I<Qt>/include/QtGui tools/iconproto.cpp -o tools/iconproto.exe \
//       -lQt5Core -lQt5Gui
//   QT_QPA_PLATFORM=offscreen tools/iconproto.exe
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QLinearGradient>
#include <QPen>
#include <cmath>

static constexpr qreal ToolSize = 96.0; // larger than the real 54 for review clarity

// ── Glass frame, mirrors paintToolbarIconFrame() in scenetreeiconpainter.cpp ──
static QRectF paintFrame(QPainter *p, const QRectF &rect, const QColor &accent)
{
    const qreal scale = ToolSize / 54.0;
    const qreal kOuterRadius = 8.0 * scale;
    const qreal kGap         = 4.0 * scale;
    const qreal kGlassRadius = 5.5 * scale;

    QPainterPath outerPath;
    outerPath.addRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), kOuterRadius, kOuterRadius);
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(0, 0, 0, 88));
    p->drawPath(outerPath);

    const QRectF frameRect = rect.adjusted(kGap, kGap, -kGap, -kGap);
    QPainterPath glassPath;
    glassPath.addRoundedRect(frameRect, kGlassRadius, kGlassRadius);
    QLinearGradient glass(frameRect.topLeft(), frameRect.bottomLeft());
    glass.setColorAt(0.0, QColor(32, 42, 58, 224));
    glass.setColorAt(1.0, QColor(7, 11, 18, 205));
    p->setPen(QPen(accent.lighter(120), 1.15 * scale));
    p->setBrush(glass);
    p->drawPath(glassPath);

    QPainterPath tintPath;
    tintPath.addRoundedRect(rect.adjusted(kGap + 2.0 * scale, kGap + 2.0 * scale,
                                          -(kGap + 2.0 * scale), -(kGap + 2.0 * scale)),
                            kGlassRadius - 1.5, kGlassRadius - 1.5);
    QColor tint = accent; tint.setAlpha(34);
    p->setPen(Qt::NoPen);
    p->setBrush(tint);
    p->drawPath(tintPath);

    const qreal side = qMin(rect.width(), rect.height()) * (34.0 / ToolSize);
    const QPointF center = rect.center() + QPointF(0.0, -rect.height() * (1.0 / ToolSize));
    return QRectF(center.x() - side * 0.5, center.y() - side * 0.5, side, side);
}

// ── Glyphs ────────────────────────────────────────────────────────────────
static void glyphText(QPainter *p, const QRectF &g)
{
    // Bold "A" letterform (vector strokes) on a baseline — text/font marker.
    // NOTE: in the integrated icon this is a drawText("A") like the for/if
    // glyphs; here it is stroked so it renders under the offscreen platform.
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
    // crossbar at ~58% down the legs
    const qreal t = 0.58;
    const QPointF cl = apex + (bl - apex) * t;
    const QPointF cr = apex + (br - apex) * t;
    p->drawLine(cl, cr);
    // baseline
    QPen blp(QColor(150, 185, 224), g.height() * 0.045);
    blp.setCapStyle(Qt::RoundCap);
    p->setPen(blp);
    const qreal by = g.bottom() - g.height() * 0.06;
    p->drawLine(QPointF(g.left() + g.width() * 0.14, by),
                QPointF(g.right() - g.width() * 0.14, by));
    p->restore();
}

static void glyphOffset(QPainter *p, const QRectF &g)
{
    // Inner solid rounded shape + outer dashed offset outline + small gap arrow.
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
    // double-headed arrow showing the offset distance (top edge)
    p->setPen(QPen(stroke, g.width() * 0.030));
    const qreal ax = g.center().x();
    p->drawLine(QPointF(ax, outer.top()), QPointF(ax, inner.top()));
    p->restore();
}

static void glyphProjection(QPainter *p, const QRectF &g)
{
    // Iso cube up top casting a flat footprint outline below, dashed droplines.
    p->save();
    const QColor stroke(214, 224, 244);
    const qreal w = g.width(), h = g.height();
    const QPointF c(g.center().x(), g.top() + h * 0.26);
    const qreal s = w * 0.28;
    // top rhombus of a small iso cube
    QPolygonF top;
    top << QPointF(c.x(), c.y() - s * 0.55)
        << QPointF(c.x() + s, c.y())
        << QPointF(c.x(), c.y() + s * 0.55)
        << QPointF(c.x() - s, c.y());
    p->setPen(QPen(stroke, w * 0.030));
    p->setBrush(QColor(255, 255, 255, 70));
    p->drawPolygon(top);
    // left/right faces
    p->drawLine(top[3], top[3] + QPointF(0, s * 0.7));
    p->drawLine(top[1], top[1] + QPointF(0, s * 0.7));
    p->drawLine(top[2], top[2] + QPointF(0, s * 0.7));
    p->drawLine(top[3] + QPointF(0, s * 0.7), top[2] + QPointF(0, s * 0.7));
    p->drawLine(top[1] + QPointF(0, s * 0.7), top[2] + QPointF(0, s * 0.7));
    // flat projected footprint (2D outline) at the bottom
    QPolygonF foot;
    const qreal fy = g.bottom() - h * 0.16;
    foot << QPointF(c.x(), fy - s * 0.42)
         << QPointF(c.x() + s, fy)
         << QPointF(c.x(), fy + s * 0.42)
         << QPointF(c.x() - s, fy);
    p->setBrush(QColor(150, 195, 240, 90));
    p->setPen(QPen(QColor(170, 205, 240), w * 0.028));
    p->drawPolygon(foot);
    // dashed droplines
    QPen dp(QColor(150, 180, 220), w * 0.022);
    dp.setStyle(Qt::DotLine);
    p->setPen(dp);
    p->drawLine(top[0] + QPointF(0, s * 0.7), foot[0]);
    p->drawLine(top[3] + QPointF(0, s * 0.7), foot[3]);
    p->drawLine(top[1] + QPointF(0, s * 0.7), foot[1]);
    p->restore();
}

static void glyphImport(QPainter *p, const QRectF &g)
{
    // Open tray with an arrow descending into it = import a file.
    p->save();
    const QColor stroke(214, 224, 244);
    const qreal w = g.width(), h = g.height();
    // arrow
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
    // tray (open-top container)
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

static void glyphResize(QPainter *p, const QRectF &g)
{
    // Existing design: rectangle with diagonal corner arrows.
    p->save();
    const QColor accent(178, 207, 238);
    p->setPen(QPen(accent.darker(135), g.width() * 0.035));
    const QRectF r = g.adjusted(g.width() * 0.14, g.height() * 0.14,
                                -g.width() * 0.14, -g.height() * 0.14);
    p->drawRect(r);
    const qreal arrowLen = g.width() * 0.14;
    auto corner = [&](const QPointF &pt, int sx, int sy) {
        QPointF dir(qreal(-sx), qreal(-sy));
        const qreal len = std::sqrt(2.0);
        dir /= len;
        const QPointF tip = pt + dir * arrowLen;
        p->drawLine(pt, tip);
        const QPointF perp(-dir.y(), dir.x());
        p->drawLine(tip, tip - dir * arrowLen * 0.5 + perp * arrowLen * 0.4);
        p->drawLine(tip, tip - dir * arrowLen * 0.5 - perp * arrowLen * 0.4);
    };
    corner(r.topLeft(), -1, -1);
    corner(r.bottomRight(), 1, 1);
    p->restore();
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    struct Entry { const char *name; QColor accent; void (*glyph)(QPainter *, const QRectF &); };
    const Entry entries[] = {
        {"text",       QColor(178, 207, 238), glyphText},
        {"offset",     QColor(200, 230, 210), glyphOffset},
        {"projection", QColor(212, 200, 240), glyphProjection},
        {"import",     QColor(244, 214, 180), glyphImport},
        {"resize",     QColor(200, 232, 200), glyphResize},
    };
    const int n = int(sizeof(entries) / sizeof(entries[0]));

    const int cell = int(ToolSize) + 48;
    const int labelH = 30;
    const int margin = 24;
    const int W = margin * 2 + cell * n;
    const int H = margin * 2 + cell + labelH;

    QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(22, 26, 33)); // dark toolbar-like background

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    for (int i = 0; i < n; ++i) {
        const qreal x = margin + i * cell + (cell - ToolSize) * 0.5;
        const qreal y = margin;
        const QRectF rect(x, y, ToolSize, ToolSize);
        const QRectF glyphRect = paintFrame(&p, rect, entries[i].accent);
        entries[i].glyph(&p, glyphRect);

        p.save();
        QFont lf = p.font();
        lf.setPointSizeF(11.0);
        p.setFont(lf);
        p.setPen(QColor(210, 220, 235));
        p.drawText(QRectF(margin + i * cell, y + ToolSize + 4, cell, labelH),
                   Qt::AlignCenter, QString::fromLatin1(entries[i].name));
        p.restore();
    }
    p.end();

    const QString out = QStringLiteral("tools/icon_preview.png");
    if (!img.save(out)) {
        qWarning("Failed to save %s", qPrintable(out));
        return 1;
    }
    qInfo("Wrote %s (%dx%d)", qPrintable(out), W, H);
    return 0;
}
