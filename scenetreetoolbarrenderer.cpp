#include "scenetreetoolbarrenderer.h"
#include "scenetreegraphicshelpers.h"

#include <QBrush>
#include <QGraphicsScene>
#include <QPen>
#include <QStringList>

using namespace SceneTreeGraphics;

namespace {

QStringList paletteTools()
{
    return {
        QStringLiteral("cube"),
        QStringLiteral("sphere"),
        QStringLiteral("cylinder"),
        QStringLiteral("union"),
        QStringLiteral("difference"),
        QStringLiteral("intersection"),
        QStringLiteral("translate"),
        QStringLiteral("rotate"),
        QStringLiteral("scale"),
        QStringLiteral("var"),
        QStringLiteral("module")
    };
}

} // namespace

SceneTreeToolbarRenderer::SceneTreeToolbarRenderer(QGraphicsScene *scene)
    : m_scene(scene)
{
}

QRectF SceneTreeToolbarRenderer::render(PreviewMovedCallback onPreviewMoved,
                                        PreviewFinishedCallback onPreviewFinished,
                                        ToolDroppedCallback onDropped)
{
    const QStringList tools = paletteTools();
    const QRectF rect = toolbarRect(tools.size());

    addSoftShadow(m_scene, rect, -4.0);
    addRoundedPanel(m_scene, rect, CornerRadius, QPen(QColor(166, 174, 186)), QBrush(QColor(232, 235, 239)), -3.0);

    for (int i = 0; i < tools.size(); ++i) {
        auto *tool = createPaletteToolItem(tools[i],
                                           fillForTool(tools[i]),
                                           onPreviewMoved,
                                           onPreviewFinished,
                                           onDropped);
        tool->setPos(ToolbarX + i * (ToolSize + ToolGap), ToolbarY);
        m_scene->addItem(tool);
    }

    return rect;
}

QRectF SceneTreeToolbarRenderer::toolbarRect(int toolCount) const
{
    return QRectF(ToolbarX - 6.0,
                  ToolbarY - 6.0,
                  toolCount * ToolSize + (toolCount - 1) * ToolGap + 12.0,
                  ToolSize + 12.0);
}
