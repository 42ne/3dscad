#include "scenetreetoolbarrenderer.h"
#include "scenetreegraphicshelpers.h"
#include "scenetreepalette.h"

#include <QBrush>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QPainterPath>
#include <QPen>
#include <QStringList>
#include <QtGlobal>
#include <cmath>

using namespace SceneTreeGraphics;

namespace {

QStringList paletteTools()
{
    return {
        // ── Primitives ────────────────────────────────────────────────────────
        QStringLiteral("circle"),
        QStringLiteral("square"),
        QStringLiteral("polygon"),
        QStringLiteral("cube"),
        QStringLiteral("sphere"),
        QStringLiteral("cylinder"),
        QStringLiteral("cone"),
        // ── Boolean operations ────────────────────────────────────────────────
        QStringLiteral("union"),
        QStringLiteral("difference"),
        QStringLiteral("intersection"),
        // ── Transforms ────────────────────────────────────────────────────────
        QStringLiteral("translate"),
        QStringLiteral("rotate"),
        QStringLiteral("scale"),
        QStringLiteral("mirror"),
        QStringLiteral("color"),
        // ── Geometry algorithms ───────────────────────────────────────────────
        QStringLiteral("hull"),
        QStringLiteral("minkowski"),
        QStringLiteral("polyhedron"),
        QStringLiteral("linear_extrude"),
        // ── Structure / control flow ──────────────────────────────────────────
        QStringLiteral("module"),
        QStringLiteral("var"),
        QStringLiteral("for"),
    };
}

constexpr qreal OverlayMargin   = 12.0;
constexpr qreal OverlayTopGap   = 10.0;
constexpr qreal OverlayPadding  = 8.0;
constexpr qreal OverlayZ        = 10000.0;
constexpr qreal MinToolbarScale = 0.90;
constexpr qreal MaxToolbarScale = 1.08;

// Height reserved at the bottom when the toolbar is docked left
// (theme + bg switchers live at bottom-left).
constexpr qreal BottomReserveLeft  = 90.0;
// Height reserved at the bottom for right-docked toolbar (just a small pad).
constexpr qreal BottomReserveRight = 20.0;

qreal visualScaleForViewportScale(qreal viewportScale)
{
    if (viewportScale <= 0.0)
        return 1.0;
    return qBound(MinToolbarScale, std::pow(viewportScale, 0.12), MaxToolbarScale);
}

int columnCountForWidth(qreal viewportWidth, int toolCount, qreal toolbarScale)
{
    const qreal toolSide = ToolSize * toolbarScale;
    const qreal gap = ToolGap * toolbarScale;
    const qreal available = qMax<qreal>(toolSide, viewportWidth - OverlayMargin * 2.0 - OverlayPadding * 2.0);
    const int columns = qMax(1, static_cast<int>((available + gap) / (toolSide + gap)));
    return qMin(toolCount, columns);
}

} // namespace

SceneTreeToolbarRenderer::SceneTreeToolbarRenderer(QGraphicsScene *scene,
                                                   QVector<QGraphicsItem *> *toolbarItems,
                                                   int theme,
                                                   bool darkGlass)
    : m_scene(scene)
    , m_toolbarItems(toolbarItems)
    , m_theme(theme)
    , m_darkGlass(darkGlass)
{
}

QRectF SceneTreeToolbarRenderer::render(PreviewMovedCallback onPreviewMoved,
                                        PreviewFinishedCallback onPreviewFinished,
                                        ToolDroppedCallback onDropped,
                                        ToolHoverChangedCallback onHoverChanged,
                                        const QPointF &viewportTopLeft,
                                        qreal viewportWidth,
                                        qreal viewportHeight,
                                        qreal viewportScale,
                                        int toolbarSide,
                                        QRectF *outPanelVpRect,
                                        QVector<QRectF> *outToolVpRects)
{
    const QStringList tools = paletteTools();
    if (tools.isEmpty() || !m_scene)
        return QRectF();

    const qreal safeViewportScale = qMax<qreal>(0.001, std::abs(viewportScale));
    const qreal toolbarScale = visualScaleForViewportScale(safeViewportScale);
    const qreal toolSide = ToolSize * toolbarScale;
    const qreal gap = ToolGap * toolbarScale;
    const int toolCount = tools.size();

    // ── Layout geometry ──────────────────────────────────────────────────────
    int columns, rows;
    qreal panelWidth, panelHeight;
    qreal panelVpX, panelVpY;

    if (toolbarSide == 0) {
        // Top: horizontal, full-width row layout
        columns  = columnCountForWidth(viewportWidth, toolCount, toolbarScale);
        rows     = (toolCount + columns - 1) / columns;
        panelWidth  = columns * toolSide + (columns - 1) * gap + OverlayPadding * 2.0;
        panelHeight = rows    * toolSide + (rows    - 1) * gap + OverlayPadding * 2.0;
        panelVpX = OverlayMargin;
        panelVpY = OverlayTopGap;
    } else {
        // Left (1) or Right (2): vertical, column-major layout
        const qreal reserve = (toolbarSide == 1) ? BottomReserveLeft : BottomReserveRight;
        const qreal maxH    = qMax<qreal>(toolSide + OverlayPadding * 2.0,
                                          viewportHeight - OverlayTopGap - reserve);

        // Find the minimum number of columns so the panel fits vertically
        rows    = toolCount;
        columns = 1;
        while (columns < toolCount) {
            const qreal h = rows * toolSide + (rows - 1) * gap + OverlayPadding * 2.0;
            if (h <= maxH)
                break;
            ++columns;
            rows = (toolCount + columns - 1) / columns;
        }

        panelWidth  = columns * toolSide + (columns - 1) * gap + OverlayPadding * 2.0;
        // Extend the glass panel to fill all available vertical space so it
        // reaches from the top gap down to the bottom switchers.
        panelHeight = maxH;
        panelVpX = (toolbarSide == 1)
            ? OverlayMargin
            : (viewportWidth - OverlayMargin - panelWidth);
        panelVpY = OverlayTopGap;
    }

    const auto scenePointFromViewportPixels = [&](qreal x, qreal y) -> QPointF {
        return viewportTopLeft + QPointF(x / safeViewportScale, y / safeViewportScale);
    };

    const QPointF panelTopLeft  = scenePointFromViewportPixels(panelVpX, panelVpY);
    const QRectF  rect(panelTopLeft,
                       QSizeF(panelWidth / safeViewportScale, panelHeight / safeViewportScale));
    const QRectF  panelLocalRect(0.0, 0.0, panelWidth, panelHeight);

    const bool darkGlass  = m_darkGlass;
    const bool customGlass = SceneTreePalette::hasCustomTheme();
    const TreeAppearanceTheme customTheme = SceneTreePalette::customTheme();

    // Output VP rects for drag-zone hit testing
    if (outPanelVpRect)
        *outPanelVpRect = QRectF(panelVpX, panelVpY, panelWidth, panelHeight);
    if (outToolVpRects)
        outToolVpRects->clear();

    // ── Drop shadow ──────────────────────────────────────────────────────────
    QGraphicsItem *shadow = m_scene->addRect(panelLocalRect.translated(3.0, 4.0),
                                             Qt::NoPen,
                                             QBrush(QColor(0, 0, 0, darkGlass ? 96 : 38)));
    shadow->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    shadow->setAcceptedMouseButtons(Qt::NoButton);
    shadow->setPos(panelTopLeft);
    shadow->setZValue(OverlayZ - 2.0);
    shadow->setOpacity(darkGlass ? 0.70 : 0.42);
    trackToolbarItem(shadow);

    // ── Glass panel ──────────────────────────────────────────────────────────
    QPainterPath panelPath;
    panelPath.addRoundedRect(panelLocalRect, CornerRadius, CornerRadius);
    QGraphicsItem *panel = m_scene->addPath(panelPath,
                                            QPen(customGlass ? customTheme.glassBorder
                                                             : darkGlass ? QColor(148, 163, 184, 82)
                                                                         : QColor(118, 136, 156, 58), 1.0),
                                            QBrush(customGlass ? customTheme.glassBottom
                                                               : darkGlass ? QColor(10, 16, 24, 178)
                                                                           : QColor(250, 253, 255, 88)));
    panel->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    panel->setAcceptedMouseButtons(Qt::NoButton);
    panel->setPos(panelTopLeft);
    panel->setZValue(OverlayZ - 1.0);
    trackToolbarItem(panel);

    // ── Tool icons ───────────────────────────────────────────────────────────
    for (int i = 0; i < toolCount; ++i) {
        int col, row;
        if (toolbarSide == 0) {
            // Horizontal top: row-major (left-to-right, then next row)
            col = i % columns;
            row = i / columns;
        } else {
            // Vertical side: column-major (top-to-bottom, then next column)
            col = i / rows;
            row = i % rows;
        }

        const qreal toolVpX = panelVpX + OverlayPadding + col * (toolSide + gap);
        const qreal toolVpY = panelVpY + OverlayPadding + row * (toolSide + gap);

        auto *tool = createPaletteToolItem(tools[i],
                                           fillForTool(tools[i]),
                                           m_theme,
                                           onPreviewMoved,
                                           onPreviewFinished,
                                           onDropped,
                                           onHoverChanged);
        tool->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        tool->setScale(toolbarScale);
        tool->setZValue(OverlayZ);
        tool->setPos(scenePointFromViewportPixels(toolVpX, toolVpY));
        m_scene->addItem(tool);
        trackToolbarItem(tool);

        if (outToolVpRects)
            outToolVpRects->append(QRectF(toolVpX, toolVpY, toolSide, toolSide));
    }

    return rect;
}

void SceneTreeToolbarRenderer::trackToolbarItem(QGraphicsItem *item) const
{
    if (m_toolbarItems && item)
        m_toolbarItems->append(item);
}
