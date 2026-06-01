#ifndef VIEWPORTSOFTWARERENDERER_H
#define VIEWPORTSOFTWARERENDERER_H

#include "appearancethemes.h"
#include "scenedocument.h"
#include "scenemesh.h"
#include "csgevaluator.h"
#include "shapenode.h"
#include "viewportcamera.h"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QSize>
#include <QString>
#include <QVector>
#include <QVector3D>

struct ViewportSelectionEdgeCandidate;

class ViewportSoftwareRenderer
{
public:
    struct Context {
        // Camera
        ViewportCamera camera;
        // Viewport
        QSize viewportSize;
        // Theme
        bool darkViewportTheme = true;
        int viewportColorVariant = 0;
        bool hasCustomAppearanceTheme = false;
        ViewportAppearanceTheme customAppearanceTheme;
        int lightingPreset = 0;
        // Scene
        const SceneDocument *scene = nullptr;
        const QVector<ShapeNode> *shapes = nullptr;
        // Selection / interaction state
        int selectedIndex = -1;
        int selectedGroupId = 0;
        bool draggingShape = false;
        bool draggingGroup = false;
        // CSG preview
        const CsgPreview *cachedCsgPreview = nullptr;
        bool csgComputing = false;
        // Animation
        float selectionShimmerPhase = 0.0f;
        // Output buffers (pointers to ViewportWidget members, filled during render)
        QVector<int> *pickBuffer = nullptr;
        QVector<float> *depthBuffer = nullptr;
        QImage *renderImage = nullptr;
        QSize *pickBufferSize = nullptr;
    };

    QString renderScene(QPainter &painter, const Context &ctx, bool drawSceneMeshes = true);

private:
    QVector3D transformOriginForGroup(const Context &ctx) const;
    QVector3D worldAxisVectorForGroup(const Context &ctx, const QVector3D &localAxis) const;
};

#endif // VIEWPORTSOFTWARERENDERER_H
