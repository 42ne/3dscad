#include "scenetreetoolbarrenderer.h"
#include "scenetreegraphicshelpers.h"

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
        // ── Structure / control flow ──────────────────────────────────────────
        QStringLiteral("module"),
        QStringLiteral("var"),
        QStringLiteral("for"),
    };
}

constexpr qreal OverlayMargin = 12.0;
constexpr qreal OverlayTopGap = 10.0;
constexpr qreal OverlayPadding = 8.0;
constexpr qreal OverlayZ = 10000.0;
constexpr qreal MinToolbarScale = 0.90;
constexpr qreal MaxToolbarScale = 1.08;

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

SceneTreeToolbarRenderer::SceneTreeToolbarRenderer(QGraphicsScene *scene, QVector<QGraphicsItem *> *toolbarItems)
    : m_scene(scene)
    , m_toolbarItems(toolbarItems)
{
}

QRectF SceneTreeToolbarRenderer::render(PreviewMovedCallback onPreviewMoved,
                                        PreviewFinishedCallback onPreviewFinished,
                                        ToolDroppedCallback onDropped,
                                        const QPointF &viewportTopLeft,
                                        qreal viewportWidth,
                                        qreal viewportScale)
{
    const QStringList tools = paletteTools();
    if (tools.isEmpty() || !m_scene)
        return QRectF();

    const qreal safeViewportScale = qMax<qreal>(0.001, std::abs(viewportScale));
    const qreal toolbarScale = visualScaleForViewportScale(safeViewportScale);
    const qreal toolSide = ToolSize * toolbarScale;
    const qreal gap = ToolGap * toolbarScale;
    const int columns = columnCountForWidth(viewportWidth, tools.size(), toolbarScale);
    const int rows = (tools.size() + columns - 1) / columns;
    const qreal panelWidth = columns * toolSide + (columns - 1) * gap + OverlayPadding * 2.0;
    const qreal panelHeight = rows * toolSide + (rows - 1) * gap + OverlayPadding * 2.0;

    const auto scenePointFromViewportPixels = [viewportTopLeft, safeViewportScale](qreal x, qreal y) {
        return viewportTopLeft + QPointF(x / safeViewportScale, y / safeViewportScale);
    };
    const QPointF panelTopLeft = scenePointFromViewportPixels(OverlayMargin, OverlayTopGap);
    const QRectF rect(panelTopLeft, QSizeF(panelWidth / safeViewportScale, panelHeight / safeViewportScale));
    const QRectF panelLocalRect(0.0, 0.0, panelWidth, panelHeight);

    QGraphicsItem *shadow = m_scene->addRect(panelLocalRect.translated(3.0, 4.0),
                                             Qt::NoPen,
                                             QBrush(QColor(0, 0, 0, 96)));
    shadow->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    shadow->setPos(panelTopLeft);
    shadow->setZValue(OverlayZ - 2.0);
    shadow->setOpacity(0.70);
    trackToolbarItem(shadow);

    QPainterPath panelPath;
    panelPath.addRoundedRect(panelLocalRect, CornerRadius, CornerRadius);
    QGraphicsItem *panel = m_scene->addPath(panelPath,
                                            QPen(QColor(148, 163, 184, 82), 1.0),
                                            QBrush(QColor(10, 16, 24, 178)));
    panel->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    panel->setPos(panelTopLeft);
    panel->setZValue(OverlayZ - 1.0);
    trackToolbarItem(panel);

    for (int i = 0; i < tools.size(); ++i) {
        auto *tool = createPaletteToolItem(tools[i],
                                           fillForTool(tools[i]),
                                           onPreviewMoved,
                                           onPreviewFinished,
                                           onDropped);
        const int column = i % columns;
        const int row = i / columns;
        tool->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        tool->setScale(toolbarScale);
        tool->setZValue(OverlayZ);
        tool->setPos(scenePointFromViewportPixels(OverlayMargin + OverlayPadding + column * (toolSide + gap),
                                                  OverlayTopGap + OverlayPadding + row * (toolSide + gap)));
        m_scene->addItem(tool);
        trackToolbarItem(tool);
    }

    return rect;
}

void SceneTreeToolbarRenderer::trackToolbarItem(QGraphicsItem *item) const
{
    if (m_toolbarItems && item)
        m_toolbarItems->append(item);
}
