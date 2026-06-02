#include "scenetreenoderenderer.h"
#include "scenetreecanvasgraphics.h"
#include "scenetreeexpressionlayout.h"
#include "scenetreegraphicsconstants.h"
#include "scenetreegraphicsitems.h"
#include "scenetreeiconpainter.h"
#include "scenetreetoolmetadata.h"
#include "scenetreepalette.h"

#include <QBrush>
#include <QFontMetricsF>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QVector3D>
#include <utility>

using namespace SceneTreeGraphics;

namespace {

void paintRoundedPanel(QPainter *painter, const QRectF &rect, qreal radius, const QPen &pen, const QBrush &brush)
{
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    painter->setPen(pen);
    painter->setBrush(brush);
    painter->drawPath(path);
}

void paintVerticalPillLabel(QPainter *painter, const QString &text, const QRectF &rect, const QColor &accent)
{
    if (!rect.isValid() || rect.height() < 18.0)
        return;

    painter->save();
    paintRoundedPanel(painter, rect, 6.0, QPen(accent, 1), QBrush(QColor(255, 255, 255, 115)));
    painter->translate(rect.center());
    painter->rotate(-90.0);
    painter->setPen(accent.darker(135));
    painter->drawText(QRectF(-rect.height() * 0.5, -rect.width() * 0.5, rect.height(), rect.width()),
                      Qt::AlignCenter,
                      text);
    painter->restore();
}

void paintLabel(QPainter *painter, const QString &text, const QPointF &position, const QColor &color)
{
    painter->setPen(color);
    painter->drawText(QRectF(position, QSizeF(220.0, 16.0)), Qt::AlignLeft | Qt::AlignVCenter, text);
}

QColor translucent(const QColor &color, int alpha)
{
    return QColor(color.red(), color.green(), color.blue(), alpha);
}

QRectF boundedVerticalLabelRect(qreal left, qreal top, qreal bottom, qreal width, qreal preferredHeight)
{
    if (bottom <= top)
        return QRectF();

    const qreal height = qMin(preferredHeight, bottom - top);
    if (height < 18.0)
        return QRectF();

    return QRectF(left, top + (bottom - top - height) * 0.5, width, height);
}

void paintPrimitiveBadge(QPainter *painter, const QString &number, const QRectF &iconRect)
{
    const QRectF badgeRect(iconRect.right() - 2.0, iconRect.top(), 15.0, 15.0);
    painter->setPen(QPen(QColor(82, 111, 146), 1));
    painter->setBrush(QColor(244, 248, 252));
    painter->drawEllipse(badgeRect);
    painter->setPen(QColor(30, 58, 90));
    painter->drawText(badgeRect, Qt::AlignCenter, number);
}

void paintHeaderGripDots(QPainter *painter, const QRectF &rect, const QColor &color)
{
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    const qreal dotR = 1.35;
    const qreal gap = 5.0;
    const QPointF center = rect.center();
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 2; ++col) {
            const QPointF dot(center.x() - gap * 0.5 + col * gap,
                              center.y() - gap + row * gap);
            painter->drawEllipse(dot, dotR, dotR);
        }
    }
}

void paintHeaderChevron(QPainter *painter, const QRectF &headerRect, const QColor &color, bool collapsed = false)
{
    const QPointF center(headerRect.right() - 18.0, headerRect.center().y() + 1.0);
    QPainterPath path;
    if (collapsed) {
        path.moveTo(center.x() - 2.0, center.y() - 4.5);
        path.lineTo(center.x() + 2.5, center.y());
        path.lineTo(center.x() - 2.0, center.y() + 4.5);
    } else {
        path.moveTo(center.x() - 4.5, center.y() - 2.0);
        path.lineTo(center.x(), center.y() + 2.5);
        path.lineTo(center.x() + 4.5, center.y() - 2.0);
    }
    painter->setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);
}

void paintGlossBadge(QPainter *painter,
                     const QRectF &rect,
                     const QString &text,
                     const QColor &top,
                     const QColor &bottom,
                     const QColor &textColor,
                     qreal radius = 4.0)
{
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 36));
    painter->drawRoundedRect(rect.translated(1.0, 1.0), radius, radius);

    QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
    gradient.setColorAt(0.0, top);
    gradient.setColorAt(1.0, bottom);
    paintRoundedPanel(painter, rect, radius, QPen(top.lighter(120), 1.0), QBrush(gradient));

    painter->save();
    QFont badgeFont = painter->font();
    badgeFont.setBold(true);
    badgeFont.setPointSizeF(qMax<qreal>(6.5, badgeFont.pointSizeF() - 1.5));
    painter->setFont(badgeFont);
    painter->setPen(textColor);
    painter->drawText(rect, Qt::AlignCenter, text);
    painter->restore();
}

// ── Polyhedron table item implementations ─────────────────────────────────────

} // namespace

PolyhedronTableItem::PolyhedronTableItem(const QRectF &rect,
                                         int groupNodeId,
                                         const SceneDocument *scene,
                                         CellCallback onCellEdited,
                                         int theme,
                                         int activeShapeNodeId,
                                         int activeParamIndex,
                                         int activeNumberStart,
                                         const QSet<int> &selectedElementNodeIds,
                                         int hoveredElementNodeId)
    : m_rect(QRectF(QPointF(0.0, 0.0), rect.size()))
    , m_groupNodeId(groupNodeId)
    , m_scene(scene)
    , m_onCellEdited(std::move(onCellEdited))
    , m_theme(theme)
    , m_activeShapeNodeId(activeShapeNodeId)
    , m_activeParamIndex(activeParamIndex)
    , m_activeNumberStart(activeNumberStart)
    , m_selectedElementNodeIds(selectedElementNodeIds)
    , m_hoveredElementNodeId(hoveredElementNodeId)
{
    setPos(rect.topLeft());
    setZValue(999.0);
    gatherData();
    computeLayout();
}

PolyhedronTableItem::Cell PolyhedronTableItem::cellAt(const QPointF &pos) const
{
    for (const Cell &c : m_cells)
        if (c.rect.contains(pos))
            return c;
    return Cell{};
}

void PolyhedronTableItem::gatherData()
{
    m_points.clear();
    m_faces.clear();
    if (!m_scene) return;
    const auto *grp = m_scene->treeNodeById(m_groupNodeId);
    if (!grp) return;

    for (const auto &child : grp->children) {
        if (child.type != SceneDocument::TreeNode::Primitive) continue;
        const ShapeNode *shape = m_scene->shapeById(child.shapeId);
        if (!shape) continue;
        if (shape->type == ShapeNode::Point3D)
            m_points.append({child.id, shape->position});
        else if (shape->type == ShapeNode::Face3D)
            m_faces.append({child.id, shape->polyhedronFaces.isEmpty()
                                    ? QVector<int>()
                                    : shape->polyhedronFaces.first()});
    }
}

void PolyhedronTableItem::computeLayout()
{
    m_cells.clear();
    m_tableWidth = 0;
    m_tableHeight = 0;

    const int ptCount  = m_points.size();
    const int faceCount = m_faces.size();
    if (ptCount == 0 && faceCount == 0) {
        constexpr qreal btnH = 18.0, tpad = 6.0, tgap = 4.0, tBtnW = 120.0;
        constexpr int numTemplates = 12, numCols = 2;
        constexpr int numRows = (numTemplates + numCols - 1) / numCols;
        const qreal colW = (tBtnW - tgap * (numCols - 1)) / numCols;
        for (int i = 0; i < numTemplates; ++i) {
            const qreal x = tpad + (i % numCols) * (colW + tgap);
            const qreal y = tpad + (i / numCols) * (btnH + tgap);
            m_cells.append({QRectF(x, y, colW, btnH), Cell::TemplateButton, i, -1, 0});
        }
        m_tableWidth  = tpad * 2 + tBtnW;
        m_tableHeight = tpad + numRows * btnH + (numRows - 1) * tgap + tpad;
        return;
    }

    const qreal labelW  = 36.0;
    const qreal faceColW = 30.0;
    const qreal pillW   = 44.0;
    const qreal smallW  = 24.0;   // face participation pills
    const qreal btnW    = 16.0;
    const qreal rowH    = 18.0;
    const qreal gap     = 3.0;
    const qreal pad     = 4.0;
    const qreal innerH  = rowH;
    const qreal sep     = 6.0;    // vertical separator between pt & face sections

    // ── Header row: [X] [Y] [Z]  |  per-face: [Rm] [Fn] [N] ──
    qreal y = pad + (rowH + gap) * 2.0 + sep * 0.5;

    // Compute the column x-offsets for the point section and face section.
    // Point section: btnW + gap + labelW + gap  (labels "PtN")
    // Then X, Y, Z columns
    // Then separator
    // Then per-face columns
    const qreal ptLabelWidth = btnW + gap + labelW;     // space for Remove + PtLabel

    const qreal pointHeaderY = pad + rowH + gap;

    // Header row: first, X Y Z headers over the point data columns
    qreal x = pad + ptLabelWidth;
    // X, Y, Z column headers (using PtLabel type with index=-2, sub=0/1/2)
    m_cells.append({QRectF(x, pointHeaderY, pillW, innerH), Cell::PtLabel, -2, 0, 0});
    x += pillW + gap;
    m_cells.append({QRectF(x, pointHeaderY, pillW, innerH), Cell::PtLabel, -2, 1, 0});
    x += pillW + gap;
    m_cells.append({QRectF(x, pointHeaderY, pillW, innerH), Cell::PtLabel, -2, 2, 0});
    x += pillW + gap;

    x += sep;

    // ── Face column headers ──
    const qreal faceSectionX = pad + ptLabelWidth + pillW * 3 + gap * 3 + sep;
    x = faceSectionX;
    for (int fi = 0; fi < faceCount; ++fi) {
        const int fnid = m_faces[fi].nodeId;

        const qreal removeY = y - (rowH + gap) * 2.0;
        const qreal labelY = y - rowH - gap;
        m_cells.append({QRectF(x + (faceColW - btnW) * 0.5, removeY, btnW, innerH),
                        Cell::RemoveFace, fi, -1, fnid});
        m_cells.append({QRectF(x, labelY, faceColW, innerH), Cell::FaceLabel, fi, -1, fnid});

        x += faceColW + sep * 0.5;
    }

    // ── Data rows (one per point) ──
    for (int pi = 0; pi < ptCount; ++pi) {
        x = pad;
        const int pid = m_points[pi].nodeId;

        // Remove point button
        m_cells.append({QRectF(x, y, btnW, innerH), Cell::RemovePt, pi, -1, pid});
        x += btnW + gap;

        // Pt label
        m_cells.append({QRectF(x, y, labelW, innerH), Cell::PtLabel, pi, -1, pid});
        x += labelW + gap;

        // X, Y, Z values
        for (int coord = 0; coord < 3; ++coord) {
            Cell::Type t = (coord == 0) ? Cell::PtX : (coord == 1) ? Cell::PtY : Cell::PtZ;
            m_cells.append({QRectF(x, y, pillW, innerH), t, pi, -1, pid});
            x += pillW + gap;
        }

        // ── Face participation cells ──
        x = faceSectionX;
        for (int fi = 0; fi < faceCount; ++fi) {
            const int fnid = m_faces[fi].nodeId;
            // FaceParticipate pill centred in the full column
            const qreal partX = x + (faceColW - smallW) * 0.5;
            m_cells.append({QRectF(partX, y, smallW, innerH), Cell::FaceParticipate, fi, pi, fnid});
            x += faceColW + sep * 0.5;
        }

        y += rowH + gap;
    }

    y += sep * 0.5;

    // ── Footer buttons ──
    // AddPt button (left side, spans pt section)
    {
        x = pad;
        const qreal addPtW = ptLabelWidth + pillW * 3 + gap * 3;
        m_cells.append({QRectF(x, y, addPtW, innerH), Cell::AddPt, -1, -1, 0});
    }

    // AutoFace button (spans the gap between AddPt and AddFace)
    {
        const qreal addPtW = ptLabelWidth + pillW * 3 + gap * 3;
        const qreal autoFaceX = pad + addPtW + gap;
        const qreal addFaceX = faceSectionX + faceCount * (faceColW + sep * 0.5);
        const qreal autoFaceW = qMax(60.0, addFaceX - autoFaceX - gap);
        m_cells.append({QRectF(autoFaceX, y, autoFaceW, innerH), Cell::AutoFace, -1, -1, 0});
    }

    // AddFace button (vertical strip on the right)
    {
        x = faceSectionX + faceCount * (faceColW + sep * 0.5);
        const qreal addFaceTop = pad;
        const qreal addFaceBottom = y + rowH;
        m_cells.append({QRectF(x, addFaceTop, btnW, qMax(innerH, addFaceBottom - addFaceTop)), Cell::AddFace, -1, -1, 0});
    }

    y += rowH + gap;
    {
        const qreal addPtW = ptLabelWidth + pillW * 3 + gap * 3;
        const qreal autoFaceMinWidth = 60.0;
        const qreal faceColRight = faceSectionX + faceCount * (faceColW + sep * 0.5);
        const qreal autoFaceRight = pad + addPtW + gap + autoFaceMinWidth;
        const qreal rightEdge = btnW + pad;
        m_tableWidth = qMax(faceColRight + rightEdge, autoFaceRight + gap + rightEdge);
    }
    m_cells.append({QRectF(pad, y, m_tableWidth - 2 * pad, innerH), Cell::ClearPolyhedron, -1, -1, 0});
    y += rowH + gap + pad;
    m_tableHeight = y;
}

QSizeF PolyhedronTableItem::estimateSize(int groupNodeId, const SceneDocument *scene)
{
    if (!scene) return QSizeF();
    const auto *grp = scene->treeNodeById(groupNodeId);
    if (!grp) return QSizeF();

    int ptCount = 0, faceCount = 0;
    for (const auto &child : grp->children) {
        if (child.type != SceneDocument::TreeNode::Primitive) continue;
        const ShapeNode *s = scene->shapeById(child.shapeId);
        if (!s) continue;
        if (s->type == ShapeNode::Point3D)  ++ptCount;
        else if (s->type == ShapeNode::Face3D) ++faceCount;
    }

    constexpr qreal rowH = 18.0, gap = 3.0, pad = 4.0, sep = 6.0;
    constexpr qreal btnW = 16.0, labelW = 36.0, pillW = 44.0, faceColW = 30.0;

    if (ptCount == 0 && faceCount == 0) {
        constexpr qreal btnH = 18.0, tpad = 6.0, tgap = 4.0, tBtnW = 120.0;
        constexpr int numRows = 6;   // ceil(12 / 2)
        return QSizeF(tpad * 2 + tBtnW, tpad + numRows * btnH + (numRows - 1) * tgap + tpad);
    }

    const qreal h = pad + (rowH+gap)*2.0 + sep*0.5
                    + ptCount*(rowH+gap)
                    + sep*0.5
                    + (rowH+gap)*2.0 + pad;   // *2 = footer row + Clear row

    const qreal ptLabelWidth = btnW + gap + labelW;
    const qreal addPtW = ptLabelWidth + pillW*3.0 + gap*3.0;
    const qreal faceSectionX = pad + addPtW + sep;
    const qreal faceColAreaRight = faceSectionX + faceCount*(faceColW + sep*0.5);
    const qreal autoFaceMinWidth = 60.0;
    const qreal autoFaceRight = pad + addPtW + gap + autoFaceMinWidth;
    const qreal rightAdd = btnW + pad;
    const qreal w = qMax(faceColAreaRight + rightAdd, autoFaceRight + gap + rightAdd);

    return QSizeF(w, h);
}

void PolyhedronTableItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setFont(sceneTreeGraphicsFont());
    const auto pt = static_cast<SceneTreePalette::Theme>(m_theme);

    struct Pill { QRectF r; QString text; bool active; };
    QVector<Pill> pills;

    for (const Cell &c : m_cells) {
        if (c.type == Cell::None || c.rect.isNull())
            continue;

        const bool isButton = (c.type == Cell::RemovePt || c.type == Cell::RemoveFace
                               || c.type == Cell::AddPt || c.type == Cell::AddFace
                               || c.type == Cell::AutoFace
                               || c.type == Cell::TemplateButton
                               || c.type == Cell::ClearPolyhedron);
        const bool isPill = (c.type == Cell::PtX || c.type == Cell::PtY || c.type == Cell::PtZ
                             || c.type == Cell::FaceParticipate);

        if (isButton) {
            const bool isAdd = (c.type == Cell::AddPt || c.type == Cell::AddFace);
            const QColor btnFill = (c.type == Cell::AutoFace)    ? QColor(60, 130, 210)
                                 : (c.type == Cell::TemplateButton) ? QColor(70, 120, 190)
                                 : (c.type == Cell::ClearPolyhedron) ? QColor(160, 55, 55)
                                 : isAdd ? QColor(70, 180, 70) : QColor(210, 70, 70);
            paintRoundedPanel(painter, c.rect, 4.0,
                              QPen(btnFill.darker(130), 1.0), QBrush(btnFill));
            painter->setPen(Qt::white);
            static const char *tmplNames[] = {"Pyramid","Prism","Box","Tetra","Octa","Frustum","Wedge","Hex","L-Shape","C-Shape","H-Shape","O-Shape"};
            const QString label = (c.type == Cell::AutoFace) ? QStringLiteral("AutoFace")
                               : (c.type == Cell::TemplateButton && c.index >= 0 && c.index < 12)
                                   ? QStringLiteral("+ ") + QString::fromLatin1(tmplNames[c.index])
                               : (c.type == Cell::ClearPolyhedron) ? QStringLiteral("Clear")
                               : isAdd ? QStringLiteral("+") : QStringLiteral("−");
            painter->drawText(c.rect, Qt::AlignCenter, label);
            continue;
        }

        if (isPill) {
            QString text;
            bool active = false;
            if (c.type == Cell::PtX || c.type == Cell::PtY || c.type == Cell::PtZ) {
                if (c.index >= 0 && c.index < m_points.size()) {
                    const int paramIdx = (c.type == Cell::PtX) ? 0 : (c.type == Cell::PtY) ? 1 : 2;
                    float v = (paramIdx == 0) ? m_points[c.index].position.x()
                            : (paramIdx == 1) ? m_points[c.index].position.y()
                            : m_points[c.index].position.z();
                    text = QString::number(static_cast<double>(v), 'f', 1);
                    active = (m_points[c.index].nodeId == m_activeShapeNodeId
                              && paramIdx == m_activeParamIndex);
                }
            } else if (c.type == Cell::FaceParticipate) {
                if (c.index >= 0 && c.index < m_faces.size()
                    && c.sub >= 0 && c.sub < m_points.size()) {
                    const int pos = m_faces[c.index].indices.indexOf(c.sub);
                    if (pos >= 0)
                        text = QString::number(pos);
                    else
                        text = QStringLiteral("\u2212");  // minus sign for "not participating"
                }
            }
            pills.append({c.rect, text, active});
            continue;
        }

        if (c.type == Cell::PtLabel || c.type == Cell::FaceLabel) {
            const bool selected = m_selectedElementNodeIds.contains(c.nodeId);
            const bool hovered = c.nodeId > 0 && c.nodeId == m_hoveredElementNodeId;
            if (selected || hovered) {
                const QRectF halo = c.rect.adjusted(-2.0, -1.5, 2.0, 1.5);
                paintRoundedPanel(painter, halo, 4.0,
                                  QPen(selected ? QColor(255, 210, 90, 210)
                                                : QColor(255, 220, 120, 130),
                                       selected ? 1.4 : 1.0),
                                  QBrush(selected ? QColor(255, 199, 64, 46)
                                                  : QColor(255, 210, 90, 24)));
            }
            painter->setPen(selected ? QColor(255, 232, 150)
                            : hovered ? QColor(255, 226, 145)
                                      : SceneTreePalette::textMuted(pt));
            QString label;
            Qt::Alignment alignment = Qt::AlignRight | Qt::AlignVCenter;
            if (c.type == Cell::PtLabel) {
                if (c.index == -2) {
                    static const char *xyz[] = {"X", "Y", "Z"};
                    label = QString::fromLatin1(xyz[qBound(0, c.sub, 2)]);
                    alignment = Qt::AlignCenter;
                } else {
                    label = QStringLiteral("Pt %1").arg(c.index);
                }
            } else {
                label = QStringLiteral("F%1").arg(c.index);
                alignment = Qt::AlignCenter;
            }
            painter->drawText(c.rect, alignment, label);
            continue;
        }
    }

    for (const Pill &p : pills) {
        if (p.text.isEmpty()) continue;
        paintRoundedPanel(painter, p.r, 4.0,
                          QPen(p.active ? SceneTreePalette::pillBorderActive()
                                        : SceneTreePalette::pillBorder(QColor(255,255,255,32), pt),
                               p.active ? 2 : 1),
                          QBrush(p.active ? SceneTreePalette::pillFillActive()
                                          : SceneTreePalette::pillFill(pt)));
        painter->setPen(SceneTreePalette::numText(pt));
        painter->drawText(p.r, Qt::AlignCenter, p.text);
    }
}

Polygon2DTableItem::Polygon2DTableItem(const QRectF &rect,
                                       int nodeId,
                                       const ShapeNode *shape,
                                       int theme,
                                       int activeNodeId,
                                       int activeParamIndex,
                                       const QSet<int> &selectedPointIndices,
                                       int hoveredPointIndex)
    : m_rect(QRectF(QPointF(0.0, 0.0), rect.size()))
    , m_nodeId(nodeId)
    , m_shape(shape ? *shape : ShapeNode())
    , m_theme(theme)
    , m_activeNodeId(activeNodeId)
    , m_activeParamIndex(activeParamIndex)
    , m_selectedPointIndices(selectedPointIndices)
    , m_hoveredPointIndex(hoveredPointIndex)
{
    computeLayout();
}

Polygon2DTableItem::Cell Polygon2DTableItem::cellAt(const QPointF &pos) const
{
    for (const Cell &cell : m_cells) {
        if (cell.rect.contains(pos))
            return cell;
    }
    return Cell{};
}

void Polygon2DTableItem::computeLayout()
{
    m_cells.clear();
    const qreal pad = 4.0;
    const qreal gap = 3.0;
    const qreal rowH = 18.0;
    const qreal btnW = 16.0;
    const qreal labelW = 38.0;
    const qreal pillW = 48.0;

    qreal y = pad;
    qreal x = pad + btnW + gap + labelW + gap;
    m_cells.append({QRectF(x, y, pillW, rowH), Cell::HeaderLabel, -1, 0, m_nodeId});
    x += pillW + gap;
    m_cells.append({QRectF(x, y, pillW, rowH), Cell::HeaderLabel, -1, 1, m_nodeId});
    y += rowH + gap;

    const int pointCount = m_shape.polyhedronPoints.size();
    for (int i = 0; i < pointCount; ++i) {
        x = pad;
        m_cells.append({QRectF(x, y, btnW, rowH), Cell::RemovePt, i, -1, m_nodeId});
        x += btnW + gap;
        m_cells.append({QRectF(x, y, labelW, rowH), Cell::PtLabel, i, -1, m_nodeId});
        x += labelW + gap;
        m_cells.append({QRectF(x, y, pillW, rowH), Cell::PtX, i, -1, m_nodeId});
        x += pillW + gap;
        m_cells.append({QRectF(x, y, pillW, rowH), Cell::PtY, i, -1, m_nodeId});
        y += rowH + gap;
    }

    m_tableWidth = pad * 2.0 + btnW + gap + labelW + gap + pillW * 2.0 + gap;
    m_cells.append({QRectF(pad, y, m_tableWidth - pad * 2.0, rowH), Cell::AddPt, -1, -1, m_nodeId});
    y += rowH + gap + pad;
    m_tableHeight = y;
}

void Polygon2DTableItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setFont(sceneTreeGraphicsFont());
    const auto pt = static_cast<SceneTreePalette::Theme>(m_theme);

    for (const Cell &cell : m_cells) {
        if (cell.type == Cell::None)
            continue;

        if (cell.type == Cell::RemovePt || cell.type == Cell::AddPt) {
            const bool add = cell.type == Cell::AddPt;
            const QColor fill = add ? QColor(70, 180, 70) : QColor(210, 70, 70);
            paintRoundedPanel(painter, cell.rect, 4.0,
                              QPen(fill.darker(130), 1.0), QBrush(fill));
            painter->setPen(Qt::white);
            painter->drawText(cell.rect, Qt::AlignCenter, add ? QStringLiteral("+") : QStringLiteral("\u2212"));
            continue;
        }

        if (cell.type == Cell::HeaderLabel) {
            painter->setPen(SceneTreePalette::textMuted(pt));
            painter->drawText(cell.rect, Qt::AlignCenter, cell.sub == 0 ? QStringLiteral("X") : QStringLiteral("Y"));
            continue;
        }

        if (cell.type == Cell::PtLabel) {
            const bool selected = m_selectedPointIndices.contains(cell.index);
            const bool hovered = cell.index >= 0 && cell.index == m_hoveredPointIndex;
            if (selected || hovered) {
                const QRectF halo = cell.rect.adjusted(-2.0, -1.5, 2.0, 1.5);
                paintRoundedPanel(painter, halo, 4.0,
                                  QPen(selected ? QColor(255, 210, 90, 210)
                                                : QColor(255, 220, 120, 130),
                                       selected ? 1.4 : 1.0),
                                  QBrush(selected ? QColor(255, 199, 64, 46)
                                                  : QColor(255, 210, 90, 24)));
            }
            painter->setPen(selected ? QColor(255, 232, 150)
                            : hovered ? QColor(255, 226, 145)
                                      : SceneTreePalette::textMuted(pt));
            painter->drawText(cell.rect, Qt::AlignRight | Qt::AlignVCenter,
                              QStringLiteral("Pt %1").arg(cell.index));
            continue;
        }

        if (cell.index < 0 || cell.index >= m_shape.polyhedronPoints.size())
            continue;

        const bool isX = cell.type == Cell::PtX;
        const int paramIndex = cell.index * 2 + (isX ? 0 : 1);
        const qreal value = isX ? m_shape.polyhedronPoints[cell.index].x()
                                : m_shape.polyhedronPoints[cell.index].y();
        const bool active = m_nodeId == m_activeNodeId && paramIndex == m_activeParamIndex;
        paintRoundedPanel(painter, cell.rect, 4.0,
                          QPen(active ? SceneTreePalette::pillBorderActive()
                                      : SceneTreePalette::pillBorder(QColor(255,255,255,32), pt),
                               active ? 2 : 1),
                          QBrush(active ? SceneTreePalette::pillFillActive()
                                        : SceneTreePalette::pillFill(pt)));
        painter->setPen(SceneTreePalette::numText(pt));
        painter->drawText(cell.rect, Qt::AlignCenter, QString::number(value, 'f', 1));
    }
}

namespace {

class PrimitiveCardItem final : public QGraphicsItem
{
public:
    PrimitiveCardItem(const QRectF &rect,
                      const ShapeNode *shape,
                      const QString &number,
                      bool selected,
                      int activeParamIndex,
                      int activeNumberStart,
                      qreal opacity,
                      qreal zValue,
                      int theme = 0,
                      const QImage &thumbnail = QImage())
        : m_rect(rect)
        , m_shape(shape ? *shape : ShapeNode())
        , m_number(number)
        , m_selected(selected)
        , m_activeParamIndex(activeParamIndex)
        , m_activeNumberStart(activeNumberStart)
        , m_opacity(opacity)
        , m_theme(theme)
        , m_thumbnail(thumbnail)
    {
        setZValue(zValue);
    }

    QRectF boundingRect() const override { return m_rect.adjusted(-6.0, -6.0, 6.0, 6.0); }

    // Rect of the center checkbox in scene coordinates (empty if N/A for this type).
    QRectF centerCheckRect() const
    {
        return ::centerCheckboxRect(m_rect);
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setOpacity(m_opacity);
        painter->setFont(sceneTreeGraphicsFont());

        // Optional user-set background — transparent by default, drawn only when alpha > 0.
        const QColor leafFill = SceneTreePalette::leafCardFill();
        if (leafFill.alpha() > 0) {
            const QColor leafBorder = leafFill.alpha() > 10
                ? (SceneTreePalette::isDarkTheme(static_cast<SceneTreePalette::Theme>(m_theme))
                       ? leafFill.lighter(150) : leafFill.darker(130))
                : Qt::transparent;
            paintRoundedPanel(painter, m_rect, CornerRadius,
                              QPen(leafBorder, 1.0), QBrush(leafFill));
        }

        const QRectF cardRect(m_rect.left(), m_rect.top(), PrimitiveCardWidth, m_rect.height());
        QRectF iconRect;

        if (!m_thumbnail.isNull()) {
            iconRect = paintToolbarIconFrame(painter, cardRect, QColor(178, 207, 238), m_selected);
            QPainterPath clip;
            clip.addRoundedRect(iconRect.adjusted(0.5, 0.5, -0.5, -0.5), 3.5, 3.5);
            painter->save();
            painter->setClipPath(clip);
            painter->drawImage(iconRect, m_thumbnail);
            painter->restore();
        } else {
            iconRect = paintToolbarIconFrame(painter, cardRect, QColor(178, 207, 238), m_selected);
            paintPrimitiveIcon(painter, m_shape.type, iconRect);
        }
        if (!m_number.isEmpty())
            paintPrimitiveBadge(painter, m_number, iconRect);

        // ── Center checkbox ──────────────────────────────────────────────
        const bool supportsCenter = (m_shape.type == ShapeNode::Cube
                                     || m_shape.type == ShapeNode::Cylinder
                                     || m_shape.type == ShapeNode::Cone
                                     || m_shape.type == ShapeNode::Square);
        if (supportsCenter) {
            const QRectF cbRect = ::centerCheckboxRect(m_rect);
            const auto pt = static_cast<SceneTreePalette::Theme>(m_theme);

            // Checkbox square
            painter->setPen(QPen(SceneTreePalette::pillBorder(QColor(255,255,255,48), pt), 1.2));
            painter->setBrush(m_shape.center
                ? SceneTreePalette::pillFillActive().lighter(130)
                : SceneTreePalette::pillFill(pt));
            painter->drawRoundedRect(cbRect.adjusted(0.5, 0.5, -0.5, -0.5), 3.0, 3.0);

            if (m_shape.center) {
                // Draw checkmark
                painter->setPen(QPen(SceneTreePalette::numText(pt), 2.0, Qt::SolidLine, Qt::RoundCap));
                const qreal cx = cbRect.center().x();
                const qreal cy = cbRect.center().y();
                QPainterPath check;
                check.moveTo(cx - 3.5, cy);
                check.lineTo(cx - 1.0, cy + 3.0);
                check.lineTo(cx + 4.0, cy - 2.5);
                painter->drawPath(check);
            }
        }

        // Parameters outside the card border — same style as transform card rows.
        const auto pt = static_cast<SceneTreePalette::Theme>(m_theme);
        QFont valueFont = painter->font();
        valueFont.setPointSizeF(qMax<qreal>(7.0, valueFont.pointSizeF() - 2.0));
        const QFontMetricsF metrics(valueFont);
        const QVector<ShapeParameterControl> controls = shapeParameterControls(m_shape);
        for (int i = 0; i < controls.size(); ++i) {
            const ShapeParameterControl &control = controls[i];
            const QRectF rowRect = shapeParameterControlRect(m_rect, i, controls.size());

            painter->setFont(valueFont);
            painter->setPen(SceneTreePalette::numLabelText(pt));
            painter->drawText(QRectF(rowRect.left(), rowRect.top(),
                                     PrimitiveParamLabelArea - 2.0, rowRect.height()),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              control.label + QStringLiteral(" ="));

            const QRectF textRect(rowRect.left() + PrimitiveParamLabelArea, rowRect.top(),
                                  rowRect.width() - PrimitiveParamLabelArea, rowRect.height());
            const QVector<ExpressionTextSpan> spans = expressionSpansInTextRect(textRect, control.expression, metrics);

            for (const ExpressionTextSpan &span : spans) {
                if (!span.number)
                    continue;
                const bool active = (i == m_activeParamIndex) && (span.start == m_activeNumberStart);
                paintRoundedPanel(painter, span.rect, 3.0,
                                  QPen(active ? SceneTreePalette::pillBorderActive()
                                              : SceneTreePalette::pillBorder(QColor(255,255,255,32), pt),
                                       active ? 2 : 1),
                                  QBrush(active ? SceneTreePalette::pillFillActive()
                                               : SceneTreePalette::pillFill(pt)));
            }

            for (const ExpressionTextSpan &span : spans) {
                painter->setPen(span.number ? SceneTreePalette::numText(pt)
                                            : SceneTreePalette::textMuted(pt));
                const Qt::Alignment align = span.number
                    ? (Qt::AlignHCenter | Qt::AlignVCenter)
                    : (Qt::AlignLeft   | Qt::AlignVCenter);
                painter->drawText(span.rect, align, span.text);
            }
        }
    }

private:
    QRectF m_rect;
    ShapeNode m_shape;
    QString m_number;
    bool m_selected = false;
    int m_activeParamIndex = -1;
    int m_activeNumberStart = -1;
    qreal m_opacity = 1.0;
    int m_theme = 0;
    QImage m_thumbnail;
};

class VariableCardItem final : public QGraphicsItem
{
public:
    VariableCardItem(const QRectF &rect,
                     const QString &name,
                     const QString &expression,
                     bool selected,
                     int activeNumberStart,
                     qreal opacity,
                     qreal zValue,
                     bool isParameter = false,
                     int depth = 0,
                     int theme = 0)
        : m_rect(rect)
        , m_name(name)
        , m_expression(expression)
        , m_selected(selected)
        , m_activeNumberStart(activeNumberStart)
        , m_opacity(opacity)
        , m_isParameter(isParameter)
        , m_depth(depth)
        , m_theme(theme)
    {
        Q_UNUSED(m_depth);  // reserved for future depth-tinted variable rows
        setZValue(zValue);
    }

    QRectF boundingRect() const override { return m_rect.adjusted(-6.0, -6.0, 6.0, 6.0); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setOpacity(m_opacity);
        painter->setFont(sceneTreeGraphicsFont());

        const auto pt = static_cast<SceneTreePalette::Theme>(m_theme);
        const bool dark = SceneTreePalette::isDarkTheme(pt);

        // Theme-aware accent / text colours.
        const QColor nameColor = dark
            ? (m_isParameter ? QColor(200, 215, 245) : QColor(235, 210, 160))
            : (m_isParameter ? QColor( 24,  36,  72) : QColor( 43,  37,  28));
        const QString badgeLabel = m_isParameter ? QStringLiteral("PAR") : QStringLiteral("VAR");

        // Row background — rounded card so it looks polished inside groups
        // and as a standalone drag-ghost.
        {
            const QColor rowBg = SceneTreePalette::variableFill(m_isParameter, pt);
            const QColor rowBorder = dark ? rowBg.lighter(148) : rowBg.darker(122);
            paintRoundedPanel(painter, m_rect, 5.0,
                              QPen(rowBorder, 1.0), QBrush(rowBg));
            if (m_selected) {
                paintRoundedPanel(painter,
                                  m_rect.adjusted(-3.0, -3.0, 3.0, 3.0),
                                  7.0,
                                  QPen(QColor(255, 203, 87), 2.4),
                                  Qt::NoBrush);
            }
        }

        // Badge pill — vertically centred in VariableHeight.
        const qreal badgeH = 13.0;
        const QRectF badgeRect(m_rect.left() + 6.0,
                               m_rect.top() + (VariableHeight - badgeH) * 0.5,
                               28.0,
                               badgeH);
        const QColor badgeTop = m_isParameter
                                    ? QColor(165, 205, 255)
                                    : QColor(255, 235, 170);
        const QColor badgeBottom = m_isParameter
                                       ? QColor(70, 118, 195)
                                       : QColor(185, 135, 42);
        const QColor badgeText = m_isParameter ? QColor(20, 45, 86) : QColor(62, 46, 20);
        paintGlossBadge(painter, badgeRect, badgeLabel, badgeTop, badgeBottom, badgeText);

        // Name, = and expression.
        const QFontMetricsF metrics(painter->font());
        const qreal nameW = metrics.horizontalAdvance(m_name);
        const QRectF textLineRect(m_rect.left() + 38.0,
                                  m_rect.top() + (VariableHeight - 16.0) * 0.5,
                                  nameW,
                                  16.0);

        painter->setPen(nameColor);
        painter->drawText(textLineRect, Qt::AlignLeft | Qt::AlignVCenter, m_name);

        painter->setPen(SceneTreePalette::numLabelText(pt));
        painter->drawText(QRectF(textLineRect.right() + 4.0, textLineRect.top(), 12.0, textLineRect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("="));

        const QVector<ExpressionTextSpan> spans = expressionTextSpans(m_rect, m_expression, metrics, nameW);
        for (const ExpressionTextSpan &span : spans) {
            if (!span.number)
                continue;
            const bool active = span.start == m_activeNumberStart;
            paintRoundedPanel(painter,
                              span.rect,
                              4.0,
                              QPen(active ? SceneTreePalette::pillBorderActive() : SceneTreePalette::pillBorder(QColor(255,255,255,32), pt), active ? 2 : 1),
                              QBrush(active ? SceneTreePalette::pillFillActive() : SceneTreePalette::pillFill(pt)));
        }

        for (const ExpressionTextSpan &span : spans) {
            painter->setPen(span.number ? SceneTreePalette::numText(pt) : SceneTreePalette::textMuted(pt));
            const Qt::Alignment align = span.number
                ? (Qt::AlignHCenter | Qt::AlignVCenter)
                : (Qt::AlignLeft   | Qt::AlignVCenter);
            painter->drawText(span.rect, align, span.text);
        }
    }

private:
    QRectF  m_rect;
    QString m_name;
    QString m_expression;
    bool    m_selected = false;
    int     m_activeNumberStart = -1;
    qreal   m_opacity = 1.0;
    bool    m_isParameter = false;
    int     m_depth = 0;
    int     m_theme = 0;
};

class GroupCardItem final : public QGraphicsItem
{
public:
    GroupCardItem(const QRectF &rect,
                  SceneDocument::TreeNode::Operation operation,
                  qreal cutSeparatorY,
                  qreal zValue,
                  bool selected,
                  bool empty,
                  bool showShadow,
                  bool showDifferenceLabels,
                  bool insertedPreview,
                  const QVector3D &transformValues = QVector3D(),
                  int activeTransformAxis = -1,
                  int activeTransformNumberStart = -1,
                  qreal transformHeaderWidth = TransformHeaderWidth,
                  const QStringList &transformExpressions = QStringList(),
                  const QString &loopVariable = QString(),
                  const QString &loopRangeExpression = QString(),
                  int activeForLoopNumberStart = -1,
                  const QColor &color = QColor(),
                  const QImage &thumbnail = QImage(),
                  const QString &moduleName = QString(),
                  int depth = 0,
                  int theme = 0,
                  bool collapsed = false)
        : m_rect(rect)
        , m_cutSeparatorY(cutSeparatorY)
        , m_transformValues(transformValues)
        , m_transformExpressions(transformExpressions)
        , m_transformHeaderWidth(transformHeaderWidth)
        , m_loopVariable(loopVariable)
        , m_loopRangeExpression(loopRangeExpression)
        , m_activeForLoopNumberStart(activeForLoopNumberStart)
        , m_activeTransformAxis(activeTransformAxis)
        , m_activeTransformNumberStart(activeTransformNumberStart)
        , m_color(color)
        , m_thumbnail(thumbnail)
        , m_moduleName(moduleName)
        , m_operation(operation)
        , m_selected(selected)
        , m_empty(empty)
        , m_showShadow(showShadow)
        , m_showDifferenceLabels(showDifferenceLabels)
        , m_insertedPreview(insertedPreview)
        , m_depth(depth)
        , m_theme(theme)
        , m_collapsed(collapsed)
    {
        setZValue(zValue);
    }

    QRectF boundingRect() const override { return m_rect.adjusted(-2.0, -2.0, 6.0, 7.0); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setFont(sceneTreeGraphicsFont());

        // ----- Palette-derived colours -----
        const auto pt   = static_cast<SceneTreePalette::Theme>(m_theme);
        const bool dark = SceneTreePalette::isDarkTheme(pt);
        QColor fill = SceneTreePalette::groupFill(m_operation, m_depth, pt);
        if (m_operation == SceneDocument::TreeNode::Color && m_color.isValid()) {
            fill = m_color.lighter(dark ? 82 : 168);
            fill.setAlpha(dark ? 185 : 215);
        }

        // Body border: selected = golden; otherwise use per-op override or derive from fill.
        const QPen bodyBorderPen = m_selected
            ? QPen(QColor(255, 203, 87), 3)
            : QPen(SceneTreePalette::cardBorder(m_operation, fill, pt), 1.5);

        const QColor cTextPrimary = SceneTreePalette::cardTextPrimary(m_operation, pt);
        const QColor cTextMuted   = SceneTreePalette::textMuted(pt);
        const QColor cPillBorder  = SceneTreePalette::pillBorder(fill, pt);
        const QColor cPillFill    = SceneTreePalette::pillFill(pt);

        // Soft multi-layer shadow — rounded so it matches the card shape.
        if (m_showShadow) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0, 0, 0, dark ? 16 : 11));
            painter->drawRoundedRect(m_rect.adjusted(-1, 1, 4, 5), CornerRadius + 2.0, CornerRadius + 2.0);
            painter->setBrush(QColor(0, 0, 0, dark ? 24 : 16));
            painter->drawRoundedRect(m_rect.adjusted( 0, 1, 3, 4), CornerRadius + 1.0, CornerRadius + 1.0);
        }

        const QColor bodyFill = m_insertedPreview ? translucent(fill, qMin(fill.alpha() + 55, 230)) : fill;
        paintRoundedPanel(painter, m_rect, CornerRadius, bodyBorderPen, QBrush(bodyFill));

        if (isVerticalHeaderOperation(m_operation) && !m_collapsed) {
            paintVerticalHeader(painter, fill, dark, cTextPrimary, cTextMuted, cPillBorder, cPillFill);
        } else {
            paintHorizontalHeader(painter, fill, dark, cTextPrimary, cTextMuted, cPillBorder, cPillFill);
        }

        if (m_empty) {
            const QPointF emptyPosition = isVerticalHeaderOperation(m_operation)
                ? QPointF(m_rect.left() + TransformHeaderWidth + GroupPadding + PrimitiveIconSize + 8.0,
                          m_rect.top() + GroupPadding + 10.0)
                : QPointF(m_rect.left() + GroupPadding + PrimitiveIconSize + 8.0,
                          m_rect.top() + GroupHeaderHeight + GroupPadding + 10.0);
            paintLabel(painter, QStringLiteral("empty"), emptyPosition, cTextMuted);
        }

        if (m_operation != SceneDocument::TreeNode::Difference || m_cutSeparatorY <= 0.0)
            return;

        painter->setPen(QPen(cTextMuted, 1, Qt::DashLine));
        painter->drawLine(QPointF(m_rect.left() + GroupPadding, m_cutSeparatorY),
                          QPointF(m_rect.right() - GroupPadding, m_cutSeparatorY));

        if (m_showDifferenceLabels) {
            const qreal labelWidth  = 20.0;
            const qreal labelHeight = 42.0;
            const qreal labelLeft   = m_rect.left() + GroupPadding * 0.5;
            const qreal baseTop     = m_rect.top() + GroupHeaderHeight + GroupPadding;
            const qreal baseBottom  = m_cutSeparatorY - 4.0;
            const qreal cutTop      = m_cutSeparatorY + 4.0;
            const qreal cutBottom   = m_rect.bottom() - GroupPadding;
            paintVerticalPillLabel(painter, QStringLiteral("base"),
                                   boundedVerticalLabelRect(labelLeft, baseTop, baseBottom, labelWidth, labelHeight),
                                   cTextMuted.lighter(115));
            paintVerticalPillLabel(painter, QStringLiteral("cut"),
                                   boundedVerticalLabelRect(labelLeft, cutTop, cutBottom, labelWidth, labelHeight),
                                   cTextMuted);
        }
    }

private:
    void paintHorizontalHeader(QPainter *painter, const QColor &fill,
                                bool dark,
                                const QColor &cTextPrimary, const QColor &cTextMuted,
                                const QColor &cPillBorder, const QColor &cPillFill)
    {
        const QRectF headerRect(m_rect.left(), m_rect.top(),
                                m_rect.width(), GroupHeaderHeight);
        // Header fill clipped to the card outline: naturally follows the rounded top corners
        // and presents a flat bottom edge (no bubble-inside-card effect).
        const QColor rawHeaderFill = SceneTreePalette::groupHeaderColor(m_operation, fill, static_cast<SceneTreePalette::Theme>(m_theme));
        const QColor headerFill = m_insertedPreview
            ? translucent(rawHeaderFill, qMin(rawHeaderFill.alpha() + 55, 230))
            : rawHeaderFill;
        {
            painter->save();
            QPainterPath cardClip;
            cardClip.addRoundedRect(m_rect, CornerRadius, CornerRadius);
            painter->setClipPath(cardClip);
            painter->setPen(Qt::NoPen);
            painter->setBrush(headerFill);
            painter->drawRect(headerRect);
            painter->restore();
        }

        const QRectF gripRect(m_rect.left() + 10.0,
                              m_rect.top() + 6.0,
                              16.0,
                              GroupHeaderHeight - 12.0);
        paintHeaderGripDots(painter,
                            gripRect,
                            dark ? QColor(205, 214, 228, 205) : QColor(86, 96, 112, 205));

        const qreal iconLeft = m_rect.left() + 30.0;
        const qreal headerIconSize = m_operation == SceneDocument::TreeNode::Module
                                         ? PrimitiveIconSize - 6.0
                                         : PrimitiveIconSize - 4.0;
        const QRectF iconRect(iconLeft,
                              m_rect.top() + (GroupHeaderHeight - headerIconSize) * 0.5,
                              headerIconSize,
                              headerIconSize);
        const QColor iconAccent = dark ? fill.lighter(210) : fill.darker(125);

        if (!m_thumbnail.isNull() && !m_empty) {
            // Draw the live 3-D thumbnail in place of the abstract operation icon.
            // Suppress when empty: the cached thumbnail may be from a previous non-empty
            // state and would be misleading alongside the "empty" placeholder text.
            painter->save();
            QPainterPath clip;
            clip.addRoundedRect(iconRect.adjusted(0.5, 0.5, -0.5, -0.5), 4.0, 4.0);
            painter->setClipPath(clip);
            painter->drawImage(iconRect, m_thumbnail);
            painter->restore();
        } else {
            paintOperationIcon(painter, m_operation, iconRect, iconAccent);
        }

        if (m_operation == SceneDocument::TreeNode::For) {
            painter->save();
            painter->setClipRect(headerRect.adjusted(0.0, 0.0, -30.0, 0.0));
            const QString variableName    = m_loopVariable.trimmed().isEmpty()      ? QStringLiteral("i")         : m_loopVariable.trimmed();
            const QString rangeExpression = m_loopRangeExpression.trimmed().isEmpty() ? QStringLiteral("[0 : 1 : 3]") : m_loopRangeExpression.trimmed();
            const QString prefix = QStringLiteral("for (%1 = ").arg(variableName);
            // Use the canonical scene font directly — not painter->font() which may be
            // device-resolved to a slightly different pixel size — so that the pill rects
            // produced here match the rects produced by the hit-testing code in the widget.
            // Use the same metrics as the hit-testing code so pill rects match.
            const QFontMetricsF metrics(sceneTreeGraphicsFont());
            const auto pt2 = static_cast<SceneTreePalette::Theme>(m_theme);
            const qreal textLeft = iconRect.right() + 10.0;
            painter->setPen(SceneTreePalette::numLabelText(pt2));
            painter->drawText(QRectF(textLeft, m_rect.top() + 7.0,
                                     metrics.horizontalAdvance(prefix), 16.0),
                              Qt::AlignLeft | Qt::AlignVCenter, prefix);

            const QVector<ExpressionTextSpan> spans = forLoopRangeTextSpans(m_rect, variableName, rangeExpression, metrics);
            for (const ExpressionTextSpan &span : spans) {
                if (span.number) {
                    const bool active = span.start == m_activeForLoopNumberStart;
                    paintRoundedPanel(painter, span.rect, 3.0,
                                      QPen(active ? SceneTreePalette::cardPillBorderActive(m_operation) : cPillBorder, active ? 2 : 1),
                                      QBrush(active ? SceneTreePalette::cardPillFillActive(m_operation) : cPillFill));
                }
                painter->setPen(span.number ? SceneTreePalette::numText(pt2) : SceneTreePalette::textMuted(pt2));
                const Qt::Alignment align = span.number
                    ? (Qt::AlignHCenter | Qt::AlignVCenter)
                    : (Qt::AlignLeft | Qt::AlignVCenter);
                painter->drawText(span.rect, align, span.text);
            }
            const qreal suffixLeft = forLoopRangeTextRect(m_rect, variableName, metrics).left()
                                     + metrics.horizontalAdvance(rangeExpression);
            painter->setPen(SceneTreePalette::numLabelText(pt2));
            painter->drawText(QRectF(suffixLeft, m_rect.top() + 7.0, 10.0, 16.0),
                              Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral(")"));
            painter->restore();
        } else if (m_operation == SceneDocument::TreeNode::LinearExtrude) {
            painter->save();
            painter->setClipRect(headerRect.adjusted(0.0, 0.0, -30.0, 0.0));
            const QString heightExpression = !m_transformExpressions.isEmpty()
                && !m_transformExpressions.first().trimmed().isEmpty()
                    ? m_transformExpressions.first().trimmed()
                    : QString::number(m_transformValues.x() > 0.0f ? m_transformValues.x() : 20.0f, 'g');
            const QString prefix = QStringLiteral("linear_extrude(height = ");
            const QFontMetricsF metrics(sceneTreeGraphicsFont());
            const auto pt2 = static_cast<SceneTreePalette::Theme>(m_theme);
            const qreal textLeft = iconRect.right() + 10.0;
            painter->setPen(SceneTreePalette::numLabelText(pt2));
            painter->drawText(QRectF(textLeft, m_rect.top() + 7.0,
                                     metrics.horizontalAdvance(prefix), 16.0),
                              Qt::AlignLeft | Qt::AlignVCenter, prefix);

            const QVector<ExpressionTextSpan> spans = linearExtrudeHeightTextSpans(m_rect, heightExpression, metrics);
            for (const ExpressionTextSpan &span : spans) {
                if (span.number) {
                    const bool active = m_activeTransformAxis == 0 && span.start == m_activeTransformNumberStart;
                    paintRoundedPanel(painter, span.rect, 3.0,
                                      QPen(active ? SceneTreePalette::cardPillBorderActive(m_operation) : cPillBorder, active ? 2 : 1),
                                      QBrush(active ? SceneTreePalette::cardPillFillActive(m_operation) : cPillFill));
                }
                painter->setPen(span.number ? SceneTreePalette::numText(pt2) : SceneTreePalette::textMuted(pt2));
                const Qt::Alignment align = span.number
                    ? (Qt::AlignHCenter | Qt::AlignVCenter)
                    : (Qt::AlignLeft | Qt::AlignVCenter);
                painter->drawText(span.rect, align, span.text);
            }
            const qreal suffixLeft = linearExtrudeHeightTextRect(m_rect, metrics).left()
                                     + metrics.horizontalAdvance(heightExpression);
            painter->setPen(SceneTreePalette::numLabelText(pt2));
            painter->drawText(QRectF(suffixLeft, m_rect.top() + 7.0, 10.0, 16.0),
                              Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral(")"));
            painter->restore();
        } else {
            // Reserve space for the chevron (18 px from the right edge + 10 px gap)
            // so the label text never overlaps the collapse indicator.
            const qreal labelLeft  = iconRect.right() + 10.0;
            const qreal labelWidth = qMax(0.0, m_rect.right() - labelLeft - 28.0);
            painter->setPen(cTextPrimary);
            QString label = labelForOperation(m_operation);
            if (m_operation == SceneDocument::TreeNode::Module && !m_moduleName.trimmed().isEmpty())
                label += QStringLiteral(" ") + m_moduleName.trimmed();
            painter->drawText(QRectF(labelLeft, m_rect.top() + 7.0, labelWidth, 16.0),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              label);
        }

        if (m_operation != SceneDocument::TreeNode::Scene
            && (!isVerticalHeaderOperation(m_operation) || m_collapsed)) {
            // Dim the chevron when the group is empty — collapsing an empty container
            // is pointless, so the indicator is visually de-emphasised.
            const QColor chevronColor = m_empty
                ? (dark ? QColor(220, 228, 238, 65) : QColor(54, 64, 76, 65))
                : (dark ? QColor(220, 228, 238, 210) : QColor(54, 64, 76, 210));
            paintHeaderChevron(painter, headerRect, chevronColor, m_collapsed);
        }

        // Thin separator between header and body.
        const QColor sepColor = dark ? fill.lighter(148) : fill.darker(120);
        painter->setPen(QPen(sepColor, 1.0));
        const qreal sepY = m_rect.top() + GroupHeaderHeight - 0.5;
        painter->drawLine(QPointF(m_rect.left() + CornerRadius * 0.5, sepY),
                          QPointF(m_rect.right() - CornerRadius * 0.5, sepY));
    }

    void paintVerticalHeader(QPainter *painter, const QColor &fill,
                              bool dark,
                              const QColor & /*cTextPrimary*/, const QColor & /*cTextMuted*/,
                              const QColor &cPillBorder, const QColor &cPillFill)
    {
        // Left icon stripe — clipped to card shape so it follows the rounded corners
        // on the left side and has a flat right edge (no bubble-inside-card effect).
        const QRectF iconBorderRect(m_rect.left(), m_rect.top(),
                                    TransformIconWidth, m_rect.height());
        const QColor rawHeaderFill = SceneTreePalette::groupHeaderColor(m_operation, fill, static_cast<SceneTreePalette::Theme>(m_theme));
        const QColor headerFill = m_insertedPreview
            ? translucent(rawHeaderFill, qMin(rawHeaderFill.alpha() + 55, 230))
            : rawHeaderFill;
        {
            painter->save();
            QPainterPath cardClip;
            cardClip.addRoundedRect(m_rect, CornerRadius, CornerRadius);
            painter->setClipPath(cardClip);
            painter->setPen(Qt::NoPen);
            painter->setBrush(headerFill);
            painter->drawRect(iconBorderRect);
            painter->restore();
        }

        // Thin vertical separator between icon stripe and value area.
        const QColor stripeSep = dark ? fill.lighter(148) : fill.darker(120);
        painter->setPen(QPen(stripeSep, 1.0));
        const qreal sepX = m_rect.left() + TransformIconWidth - 0.5;
        painter->drawLine(QPointF(sepX, m_rect.top() + CornerRadius * 0.5),
                          QPointF(sepX, m_rect.bottom() - CornerRadius * 0.5));

        const QRectF iconRect(m_rect.left() + 6.0, m_rect.top() + 7.0,
                               PrimitiveIconSize - 6.0, PrimitiveIconSize - 6.0);
        const QColor iconAccent = dark ? fill.lighter(210) : fill.darker(125);
        paintOperationIcon(painter, m_operation, iconRect, iconAccent, 6.0);

        const QString opLabel = m_operation == SceneDocument::TreeNode::Translate ? QStringLiteral("T")
                              : m_operation == SceneDocument::TreeNode::Rotate   ? QStringLiteral("R")
                              : m_operation == SceneDocument::TreeNode::Mirror   ? QStringLiteral("M")
                              : m_operation == SceneDocument::TreeNode::Color    ? QStringLiteral("C")
                                                                                 : QStringLiteral("S");
        const QRectF opBadgeRect(iconRect.left() + 1.0,
                                 iconRect.bottom() - 1.0,
                                 iconRect.width() - 2.0,
                                 14.0);
        QColor badgeTop;
        QColor badgeBottom;
        if (m_operation == SceneDocument::TreeNode::Translate) {
            badgeTop = QColor(205, 224, 255);
            badgeBottom = QColor(100, 139, 210);
        } else if (m_operation == SceneDocument::TreeNode::Rotate) {
            badgeTop = QColor(226, 205, 255);
            badgeBottom = QColor(145, 98, 195);
        } else if (m_operation == SceneDocument::TreeNode::Mirror) {
            badgeTop = QColor(252, 215, 238);
            badgeBottom = QColor(175, 85, 138);
        } else if (m_operation == SceneDocument::TreeNode::Color) {
            badgeTop = QColor(255, 235, 126);
            badgeBottom = QColor(79, 163, 255);
        } else {
            badgeTop = QColor(205, 242, 218);
            badgeBottom = QColor(84, 158, 105);
        }
        paintGlossBadge(painter, opBadgeRect, opLabel, badgeTop, badgeBottom, QColor(24, 34, 44), 3.0);

        const QRectF collapseControlRect(m_rect.left(),
                                         m_rect.bottom() - GroupHeaderHeight,
                                         TransformIconWidth,
                                         GroupHeaderHeight);
        paintHeaderChevron(painter,
                           collapseControlRect,
                           dark ? QColor(220, 228, 238, 210) : QColor(54, 64, 76, 210));

        // X/Y/Z rows outside icon border.
        // Use the canonical scene font for metrics so pill rects are pixel-identical
        // to those produced by the widget's hit-testing code.
        const QFontMetricsF metrics(sceneTreeGraphicsFont());
        QFont valueFont = painter->font();
        valueFont.setPointSizeF(qMax<qreal>(7.0, valueFont.pointSizeF() - 2.0));

        const auto pt = static_cast<SceneTreePalette::Theme>(m_theme);
        const QColor axisLabelColor = SceneTreePalette::numLabelText(pt);

        if (m_operation == SceneDocument::TreeNode::Color) {
            const QColor color = m_color.isValid() ? m_color : QColor(79, 163, 255);
            static const QString channelLabels[3] = {QStringLiteral("R"), QStringLiteral("G"), QStringLiteral("B")};
            const int channelValues[3] = {color.red(), color.green(), color.blue()};
            for (int channel = 0; channel < 3; ++channel) {
                const QRectF rowRect = transformParameterControlRect(m_rect, channel, m_transformHeaderWidth);
                const bool rowActive = (channel == m_activeTransformAxis);
                const QString expr = QString::number(channelValues[channel]);

                painter->setFont(valueFont);
                painter->setPen(axisLabelColor);
                painter->drawText(QRectF(rowRect.left(), rowRect.top(), TransformParamLabelArea - 2.0, rowRect.height()),
                                  Qt::AlignLeft | Qt::AlignVCenter,
                                  channelLabels[channel] + QStringLiteral(" ="));

                const QRectF textRect(rowRect.left() + TransformParamLabelArea,
                                      rowRect.top(),
                                      rowRect.width() - TransformParamLabelArea,
                                      rowRect.height());
                const QVector<ExpressionTextSpan> spans = expressionSpansInTextRect(textRect, expr, metrics);
                for (const ExpressionTextSpan &span : spans) {
                    const bool numActive = rowActive && span.number && (span.start == m_activeTransformNumberStart);
                    if (span.number) {
                        paintRoundedPanel(painter, span.rect, 3.0,
                                          QPen(numActive ? SceneTreePalette::cardPillBorderActive(m_operation) : cPillBorder,
                                               numActive ? 2 : 1),
                                          QBrush(numActive ? SceneTreePalette::cardPillFillActive(m_operation) : cPillFill));
                    }
                    painter->setPen(span.number ? SceneTreePalette::numText(pt) : SceneTreePalette::textMuted(pt));
                    const Qt::Alignment align = span.number
                        ? (Qt::AlignHCenter | Qt::AlignVCenter)
                        : (Qt::AlignLeft | Qt::AlignVCenter);
                    painter->drawText(span.rect, align, span.text);
                }
            }
            return;
        }

        static const QString axisLabels[3] = {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
        for (int axis = 0; axis < 3; ++axis) {
            const QRectF rowRect   = transformParameterControlRect(m_rect, axis, m_transformHeaderWidth);
            const bool   rowActive = (axis == m_activeTransformAxis);

            // Expression for this axis.
            QString expr;
            if (axis < m_transformExpressions.size() && !m_transformExpressions[axis].isEmpty())
                expr = m_transformExpressions[axis];
            else {
                const float val = axis == 0 ? m_transformValues.x()
                                : axis == 1 ? m_transformValues.y()
                                            : m_transformValues.z();
                const int precision = m_operation == SceneDocument::TreeNode::Scale ? 1 : 0;
                expr = QString::number(val, 'f', precision);
            }

            painter->setFont(valueFont);
            painter->setPen(axisLabelColor);
            painter->drawText(QRectF(rowRect.left(), rowRect.top(), TransformParamLabelArea - 2.0, rowRect.height()),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              axisLabels[axis] + QStringLiteral(" ="));

            const QRectF textRect(rowRect.left() + TransformParamLabelArea,
                                  rowRect.top(),
                                  rowRect.width() - TransformParamLabelArea,
                                  rowRect.height());
            const QVector<ExpressionTextSpan> spans = expressionSpansInTextRect(textRect, expr, metrics);
            for (const ExpressionTextSpan &span : spans) {
                const bool numActive = rowActive && span.number && (span.start == m_activeTransformNumberStart);
                if (span.number) {
                    paintRoundedPanel(painter, span.rect, 3.0,
                                      QPen(numActive ? SceneTreePalette::cardPillBorderActive(m_operation) : cPillBorder,
                                           numActive ? 2 : 1),
                                      QBrush(numActive ? SceneTreePalette::cardPillFillActive(m_operation) : cPillFill));
                }
                // Numbers: span.rect is 4 px wider on each side → centre digit inside pill.
                painter->setPen(span.number ? SceneTreePalette::numText(pt) : SceneTreePalette::textMuted(pt));
                const Qt::Alignment align = span.number
                    ? (Qt::AlignHCenter | Qt::AlignVCenter)
                    : (Qt::AlignLeft | Qt::AlignVCenter);
                painter->drawText(span.rect, align, span.text);
            }
        }
    }

    QRectF      m_rect;
    qreal       m_cutSeparatorY = 0.0;
    QVector3D   m_transformValues;
    QStringList m_transformExpressions;
    qreal       m_transformHeaderWidth = TransformHeaderWidth;
    QString     m_loopVariable;
    QString     m_loopRangeExpression;
    int         m_activeForLoopNumberStart   = -1;
    int         m_activeTransformAxis        = -1;
    int         m_activeTransformNumberStart = -1;
    QColor      m_color;
    QImage      m_thumbnail;
    QString     m_moduleName;
    SceneDocument::TreeNode::Operation m_operation = SceneDocument::TreeNode::Union;
    bool        m_selected            = false;
    bool        m_empty               = false;
    bool        m_showShadow          = false;
    bool        m_showDifferenceLabels = false;
    bool        m_insertedPreview      = false;
    int         m_depth = 0;
    int         m_theme = 0;
    bool        m_collapsed = false;
};


class ModuleCallCardItem final : public QGraphicsItem
{
public:
    ModuleCallCardItem(const QRectF &rect,
                       const QString &moduleName,
                       const QVector<ModuleCallParam> &params,
                       bool selected,
                       int activeParamVarNodeId,
                       int activeNumberStart,
                       qreal opacity,
                       qreal zValue,
                       int theme = 0,
                       const QImage &thumbnail = QImage())
        : m_rect(rect)
        , m_moduleName(moduleName)
        , m_params(params)
        , m_selected(selected)
        , m_activeParamVarNodeId(activeParamVarNodeId)
        , m_activeNumberStart(activeNumberStart)
        , m_opacity(opacity)
        , m_theme(theme)
        , m_thumbnail(thumbnail)
    {
        setZValue(zValue);
    }

    QRectF boundingRect() const override { return m_rect.adjusted(-6.0, -6.0, 6.0, 6.0); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setOpacity(m_opacity);
        painter->setFont(sceneTreeGraphicsFont());

        const auto pt = static_cast<SceneTreePalette::Theme>(m_theme);
        const QColor accent(38, 108, 148);

        // Optional user-set background — transparent by default, drawn only when alpha > 0.
        const QColor leafFill = SceneTreePalette::leafCardFill();
        if (leafFill.alpha() > 0) {
            const QColor leafBorder = leafFill.alpha() > 10
                ? (SceneTreePalette::isDarkTheme(pt)
                       ? leafFill.lighter(150) : leafFill.darker(130))
                : Qt::transparent;
            paintRoundedPanel(painter, m_rect, 5.0,
                              QPen(leafBorder, 1.0), QBrush(leafFill));
        }

        if (m_selected) {
            paintRoundedPanel(painter,
                              m_rect.adjusted(-3.0, -3.0, 3.0, 3.0),
                              5.0,
                              QPen(QColor(255, 193, 56), 2.2),
                              Qt::NoBrush);
        }

        // CALL badge — shows thumbnail when one is available, "CALL" text otherwise.
        const qreal badgeH = 16.0;
        const QRectF badgeRect(m_rect.left() + 6.0,
                               m_rect.top() + (VariableHeight - badgeH) * 0.5,
                               34.0,
                               badgeH);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 38));
        painter->drawRoundedRect(badgeRect.translated(1.0, 1.0), 4.0, 4.0);

        QLinearGradient badgeGradient(badgeRect.topLeft(), badgeRect.bottomLeft());
        badgeGradient.setColorAt(0.0, QColor(82, 145, 205));
        badgeGradient.setColorAt(1.0, QColor(24, 82, 135));
        paintRoundedPanel(painter, badgeRect, 4.0, QPen(QColor(170, 215, 255, 150), 1.0), QBrush(badgeGradient));

        if (!m_thumbnail.isNull()) {
            // Thumbnail replaces "CALL" text inside the badge.
            const QRectF thumbRect = badgeRect.adjusted(1.5, 1.5, -1.5, -1.5);
            QPainterPath clip;
            clip.addRoundedRect(thumbRect, 2.5, 2.5);
            painter->save();
            painter->setClipPath(clip);
            painter->drawImage(thumbRect, m_thumbnail);
            painter->restore();
        } else {
            painter->save();
            QFont badgeFont = painter->font();
            badgeFont.setBold(true);
            badgeFont.setPointSizeF(qMax<qreal>(6.5, badgeFont.pointSizeF() - 1.5));
            painter->setFont(badgeFont);
            painter->setPen(QColor(238, 248, 255));
            painter->drawText(badgeRect, Qt::AlignCenter, QStringLiteral("CALL"));
            painter->restore();
        }

        QFont valueFont = painter->font();
        valueFont.setPointSizeF(qMax<qreal>(7.0, valueFont.pointSizeF() - 2.0));
        painter->setFont(valueFont);
        const QFontMetricsF metrics(valueFont);
        const QColor cLabel = SceneTreePalette::numLabelText(pt);
        const QColor cMuted = SceneTreePalette::textMuted(pt);

        qreal x = m_rect.left() + 46.0;
        const QString nameOpen = m_moduleName + QStringLiteral("(");
        painter->setPen(cLabel);
        painter->drawText(QRectF(x, m_rect.top(), metrics.horizontalAdvance(nameOpen), VariableHeight),
                          Qt::AlignLeft | Qt::AlignVCenter, nameOpen);
        x += metrics.horizontalAdvance(nameOpen);

        if (m_params.isEmpty()) {
            painter->setPen(cMuted);
            painter->drawText(QRectF(x, m_rect.top(), 10.0, VariableHeight),
                              Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral(")"));
        } else {
            for (int i = 0; i < m_params.size(); ++i) {
                const QString nameEq = m_params[i].name + QStringLiteral(" = ");
                painter->setPen(cLabel);
                painter->drawText(QRectF(x, m_rect.top(), metrics.horizontalAdvance(nameEq), VariableHeight),
                                  Qt::AlignLeft | Qt::AlignVCenter, nameEq);
                x += metrics.horizontalAdvance(nameEq);

                constexpr qreal exprH = 14.0;
                const QRectF exprRect(x, m_rect.top() + (VariableHeight - exprH) * 0.5,
                                      m_rect.right() - x, exprH);
                const QVector<ExpressionTextSpan> spans =
                    expressionSpansInTextRect(exprRect, m_params[i].expression, metrics);
                for (const ExpressionTextSpan &span : spans) {
                    if (span.number) {
                        const bool active = m_params[i].varNodeId == m_activeParamVarNodeId
                                            && span.start == m_activeNumberStart;
                        paintRoundedPanel(painter, span.rect, 4.0,
                                          QPen(active ? SceneTreePalette::pillBorderActive()
                                                      : SceneTreePalette::pillBorder(QColor(255,255,255,32), pt),
                                               active ? 2 : 1),
                                          QBrush(active ? SceneTreePalette::pillFillActive()
                                                        : SceneTreePalette::pillFill(pt)));
                    }
                }
                for (const ExpressionTextSpan &span : spans) {
                    painter->setPen(span.number ? SceneTreePalette::numText(pt) : cMuted);
                    const Qt::Alignment align = span.number
                        ? (Qt::AlignHCenter | Qt::AlignVCenter)
                        : (Qt::AlignLeft   | Qt::AlignVCenter);
                    painter->drawText(span.rect, align, span.text);
                }

                x += metrics.horizontalAdvance(m_params[i].expression);

                if (i < m_params.size() - 1) {
                    const QString sep = QStringLiteral(", ");
                    painter->setPen(cMuted);
                    painter->drawText(QRectF(x, m_rect.top(), metrics.horizontalAdvance(sep), VariableHeight),
                                      Qt::AlignLeft | Qt::AlignVCenter, sep);
                    x += metrics.horizontalAdvance(sep);
                }
            }
            painter->setPen(cMuted);
            painter->drawText(QRectF(x, m_rect.top(), 10.0, VariableHeight),
                              Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral(")"));
        }
    }

private:
    QRectF m_rect;
    QString m_moduleName;
    QVector<ModuleCallParam> m_params;
    bool m_selected = false;
    int m_activeParamVarNodeId = 0;
    int m_activeNumberStart = -1;
    qreal m_opacity = 1.0;
    int m_theme = 0;
    QImage m_thumbnail;
};

} // namespace

SceneTreeNodeRenderer::SceneTreeNodeRenderer(QGraphicsScene *scene,
                                             int selectedNodeId,
                                             NodeSelectedCallback onSelected,
                                             int activeTransformNodeId,
                                             int activeTransformAxis,
                                             int activeTransformNumberStart,
                                             int activeShapeNodeId,
                                             int activeShapeParameter,
                                             int activeShapeParamNumberStart,
                                             int activeVariableNodeId,
                                             int activeVariableNumberStart,
                                             int activeForLoopNodeId,
                                             int activeForLoopNumberStart,
                                             int activeModuleCallNodeId,
                                             int activeModuleCallVarNodeId,
                                             int activeModuleCallNumberStart)
    : m_scene(scene)
    , m_selectedNodeId(selectedNodeId)
    , m_activeTransformNodeId(activeTransformNodeId)
    , m_activeTransformAxis(activeTransformAxis)
    , m_activeTransformNumberStart(activeTransformNumberStart)
    , m_activeShapeNodeId(activeShapeNodeId)
    , m_activeShapeParameter(activeShapeParameter)
    , m_activeShapeParamNumberStart(activeShapeParamNumberStart)
    , m_activeVariableNodeId(activeVariableNodeId)
    , m_activeVariableNumberStart(activeVariableNumberStart)
    , m_activeForLoopNodeId(activeForLoopNodeId)
    , m_activeForLoopNumberStart(activeForLoopNumberStart)
    , m_activeModuleCallNodeId(activeModuleCallNodeId)
    , m_activeModuleCallVarNodeId(activeModuleCallVarNodeId)
    , m_activeModuleCallNumberStart(activeModuleCallNumberStart)
    , m_onSelected(std::move(onSelected))
{
}

void SceneTreeNodeRenderer::renderPrimitive(const SceneDocument::TreeNode &node,
                                            const QRectF &rect,
                                            const QString &label,
                                            const ShapeNode *shape,
                                            const QImage &thumbnail)
{
    const int activeParamIndex = node.id == m_activeShapeNodeId ? m_activeShapeParameter : -1;
    const int activeNumberStart = node.id == m_activeShapeNodeId ? m_activeShapeParamNumberStart : -1;
    if (shape && shape->type == ShapeNode::Polygon2D) {
        const QRectF cardRect(rect.topLeft(), QSizeF(qMin(rect.width(), PrimitiveWidth), PrimitiveHeight));
        m_scene->addItem(new PrimitiveCardItem(cardRect, shape, primitiveNumberText(label, node.shapeId), node.id == m_selectedNodeId, activeParamIndex, activeNumberStart, 1.0, 5.0, m_theme, thumbnail));
        return;
    }
    m_scene->addItem(new PrimitiveCardItem(rect, shape, primitiveNumberText(label, node.shapeId), node.id == m_selectedNodeId, activeParamIndex, activeNumberStart, 1.0, 5.0, m_theme, thumbnail));
}

void SceneTreeNodeRenderer::renderVariable(const SceneDocument::TreeNode &node, const QRectF &rect)
{
    const int activeNumberStart = node.id == m_activeVariableNodeId ? m_activeVariableNumberStart : -1;
    m_scene->addItem(new VariableCardItem(rect, node.variableName, node.variableExpression,
                                          node.id == m_selectedNodeId, activeNumberStart,
                                          1.0, 5.0, node.isParameter,
                                          0,        // depth (variables: no depth-hue shift)
                                          m_theme));
}

void SceneTreeNodeRenderer::renderModuleCall(const SceneDocument::TreeNode &node,
                                             const QRectF &rect,
                                             const QVector<SceneTreeGraphics::ModuleCallParam> &params,
                                             const QImage &thumbnail)
{
    const int activeVarNodeId = node.id == m_activeModuleCallNodeId ? m_activeModuleCallVarNodeId : 0;
    const int activeNumStart = node.id == m_activeModuleCallNodeId ? m_activeModuleCallNumberStart : -1;
    m_scene->addItem(new ModuleCallCardItem(rect, node.moduleName, params, node.id == m_selectedNodeId, activeVarNodeId, activeNumStart, 1.0, 5.0, m_theme, thumbnail));
    m_scene->addItem(createTreeNodeSelectionItem(node.id, rect, 5.0, m_onSelected));
}

void SceneTreeNodeRenderer::renderGroup(const SceneDocument::TreeNode &node,
                                        const QRectF &rect,
                                        int depth,
                                        qreal cutSeparatorY,
                                        const QImage &thumbnail,
                                        bool collapsed)
{
    const QVector3D transformValues = node.operation == SceneDocument::TreeNode::Translate ? node.position
                                    : node.operation == SceneDocument::TreeNode::Rotate    ? node.rotation
                                    : node.operation == SceneDocument::TreeNode::Mirror    ? node.position
                                    : node.operation == SceneDocument::TreeNode::Scale     ? node.scale
                                    : node.operation == SceneDocument::TreeNode::LinearExtrude ? node.scale
                                                                                           : QVector3D();
    const int activeAxis = node.id == m_activeTransformNodeId ? m_activeTransformAxis : -1;
    const int activeNumberStart = node.id == m_activeTransformNodeId ? m_activeTransformNumberStart : -1;
    const int activeForLoopStart = node.id == m_activeForLoopNodeId ? m_activeForLoopNumberStart : -1;
    const qreal transformHeaderWidth = transformHeaderWidthForNode(node);
    const bool showEmptyText = !collapsed
                               && node.operation != SceneDocument::TreeNode::Module
                               && node.operation != SceneDocument::TreeNode::Scene
                               && node.children.isEmpty();
    m_scene->addItem(new GroupCardItem(rect, node.operation, cutSeparatorY, zForDepth(depth, -101.0),
                                       node.id == m_selectedNodeId, showEmptyText, true, true, false,
                                       transformValues, activeAxis, activeNumberStart,
                                       transformHeaderWidth, node.transformExpressions,
                                       node.loopVariable, node.loopRangeExpression, activeForLoopStart,
                                       node.color, thumbnail, node.moduleName,
                                       depth, m_theme, collapsed));
    m_scene->addItem(createTreeNodeSelectionItem(node.id,
                                                 rect,
                                                 zForDepth(depth, -80.0),
                                                 m_onSelected));
}

qreal SceneTreeNodeRenderer::zForDepth(int depth, qreal offset) const
{
    return depth * 10.0 + offset;
}


void SceneTreeNodeRenderer::renderPreviewTool(QGraphicsScene *scene,
                                              QVector<QGraphicsItem *> *items,
                                              const QString &tool,
                                              const QRectF &rect,
                                              int theme)
{
    SceneDocument::TreeNode::Operation operation;
    QGraphicsItem *item = nullptr;
    if (tool == QStringLiteral("par")) {
        item = new VariableCardItem(rect, QStringLiteral("par"), QStringLiteral("0"), false, -1, 0.78, 58.0, true, 0, theme);
    } else if (isVariableToolName(tool)) {
        item = new VariableCardItem(rect, QStringLiteral("var"), QStringLiteral("0"), false, -1, 0.78, 58.0, false, 0, theme);
    } else if (tool == QStringLiteral("call")) {
        item = new ModuleCallCardItem(rect, QStringLiteral("call"), {}, false, 0, -1, 0.78, 58.0, theme);
    } else if (operationForToolName(tool, &operation)) {
        item = new GroupCardItem(rect, operation, 0.0, 56.0, false, false, false, false, true,
                                 QVector3D(), -1, -1, TransformHeaderWidth, QStringList(),
                                 QString(), QString(), -1,
                                 QColor(), QImage(), QString(),
                                 0, theme);
    } else {
        ShapeNode shape;
        shape.type = primitiveTypeForTool(tool);
        item = new PrimitiveCardItem(rect, &shape, QString(), false, -1, -1, 0.78, 58.0, theme);
    }

    scene->addItem(item);
    appendPreviewItem(items, item);
}

void SceneTreeNodeRenderer::renderPreviewPrimitive(QGraphicsScene *scene,
                                                   QVector<QGraphicsItem *> *items,
                                                   const ShapeNode *shape,
                                                   int shapeId,
                                                   const QRectF &rect)
{
    auto *item = new PrimitiveCardItem(rect,
                                      shape,
                                      primitiveNumberText(QString(), shapeId),
                                      false,
                                      -1,
                                      -1,
                                      0.78,
                                      58.0,
                                      0);
    scene->addItem(item);
    appendPreviewItem(items, item);
}

void SceneTreeNodeRenderer::renderPreviewVariable(QGraphicsScene *scene,
                                                  QVector<QGraphicsItem *> *items,
                                                  const QString &name,
                                                  const QString &expression,
                                                  bool isParameter,
                                                  const QRectF &rect,
                                                  int theme)
{
    auto *item = new VariableCardItem(rect, name, expression, false, -1, 0.78, 58.0, isParameter, 0, theme);
    scene->addItem(item);
    appendPreviewItem(items, item);
}

void SceneTreeNodeRenderer::renderPreviewGroup(QGraphicsScene *scene,
                                               QVector<QGraphicsItem *> *items,
                                               SceneDocument::TreeNode::Operation operation,
                                               const QRectF &rect,
                                               qreal cutSeparatorY,
                                               int theme,
                                               int depth,
                                               const QColor &color)
{
    auto *item = new GroupCardItem(rect, operation, cutSeparatorY, 52.0, false, false, false, false, false,
                                   QVector3D(), -1, -1, TransformHeaderWidth, QStringList(),
                                   QString(), QString(), -1,
                                   color, QImage(), QString(),
                                   depth, theme);
    scene->addItem(item);
    appendPreviewItem(items, item);
}
