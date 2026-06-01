#include "viewportglrenderer.h"
#include "viewportwidget.h"
#include "viewporthelpers.h"

#include <GL/gl.h>
#include <QtMath>

using namespace ViewportHelpers;

void ViewportGLRenderer::renderGrid(ViewportWidget &w)
{
    if (!w.m_glLineProgram || !w.m_glLineProgram->isLinked() || !w.m_vboGrid.isCreated())
        return;

    glDisable(GL_DEPTH_TEST);
    glLineWidth(1.0f);
    w.m_glLineProgram->bind();

    const QMatrix4x4 mvp = buildProjectionMatrix(float(w.width()), float(w.height()), w.m_cameraDistance, w.m_orthographicProjection)
                          * buildViewMatrix(w.m_cameraYaw, w.m_cameraPitch, w.m_cameraDistance, w.m_cameraTarget);
    w.m_glLineProgram->setUniformValue("u_mvp", mvp);
    w.m_glLineProgram->setUniformValue("u_shimmer_phase", 0.0f);
    w.m_glLineProgram->setUniformValue("u_shimmer_amount", 0.0f);
    w.m_glLineProgram->setUniformValue("u_depth_pull", 0.0f);

    const int posLoc   = w.m_glLineProgram->attributeLocation("a_position");
    const int colorLoc = w.m_glLineProgram->attributeLocation("a_color");
    w.m_glLineProgram->enableAttributeArray(posLoc);
    w.m_glLineProgram->enableAttributeArray(colorLoc);

    w.m_vboGrid.bind();
    w.m_glLineProgram->setAttributeBuffer(posLoc,   GL_FLOAT, offsetof(OpenGLLineVertex, position), 3, sizeof(OpenGLLineVertex));
    w.m_glLineProgram->setAttributeBuffer(colorLoc, GL_FLOAT, offsetof(OpenGLLineVertex, color),    4, sizeof(OpenGLLineVertex));

    glDrawArrays(GL_LINES, 0, w.m_vboGridCount);

    w.m_vboGrid.release();
    w.m_glLineProgram->disableAttributeArray(posLoc);
    w.m_glLineProgram->disableAttributeArray(colorLoc);
    w.m_glLineProgram->release();
}

void ViewportGLRenderer::renderContactShadows(ViewportWidget &w)
{
    if (!w.m_shapes || !w.m_glFlatProgram || !w.m_glFlatProgram->isLinked() || !w.m_vboShadow.isCreated())
        return;
    if (w.m_draggingShape || w.m_draggingGroup)
        return;

    const CsgPreview &preview = w.m_cachedCsgPreview;

    const QString fp = QString::number(w.m_cachedCsgFingerprint);
    if (fp != w.m_vboShadowKey) {
        struct Sample { float dx, dy; int alpha; };
        static constexpr Sample kSamples[] = {
            { 0.0f,  0.0f, 35},
            { 1.0f,  0.0f,  6}, {-1.0f,  0.0f,  6}, { 0.0f,  1.0f,  6}, { 0.0f, -1.0f,  6},
            { 0.7f,  0.7f,  5}, {-0.7f,  0.7f,  5}, { 0.7f, -0.7f,  5}, {-0.7f, -0.7f,  5},
            { 2.0f,  0.0f,  3}, {-2.0f,  0.0f,  3}, { 0.0f,  2.0f,  3}, { 0.0f, -2.0f,  3},
        };
        QVector<OpenGLFlatVertex> verts;
        for (const Sample &s : kSamples) {
            const QVector4D color = colorToVector4(QColor(0, 0, 0, s.alpha));
            for (const CsgRenderItem &item : preview.items) {
                if (item.helper) continue;
                for (const MeshTriangle &tri : item.mesh.triangles) {
                    if (tri.normal.z() <= 0.0f) continue;
                    verts.append({QVector3D(tri.a.x() + s.dx, tri.a.y() + s.dy, 0.0f), color});
                    verts.append({QVector3D(tri.b.x() + s.dx, tri.b.y() + s.dy, 0.0f), color});
                    verts.append({QVector3D(tri.c.x() + s.dx, tri.c.y() + s.dy, 0.0f), color});
                }
            }
        }
        w.m_vboShadow.bind();
        w.m_vboShadow.allocate(verts.constData(), verts.size() * int(sizeof(OpenGLFlatVertex)));
        w.m_vboShadow.release();
        w.m_vboShadowCount = verts.size();
        w.m_vboShadowKey   = fp;
    }

    if (w.m_vboShadowCount == 0)
        return;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    w.m_glFlatProgram->bind();

    const QMatrix4x4 mvp = buildProjectionMatrix(float(w.width()), float(w.height()), w.m_cameraDistance, w.m_orthographicProjection)
                          * buildViewMatrix(w.m_cameraYaw, w.m_cameraPitch, w.m_cameraDistance, w.m_cameraTarget);
    w.m_glFlatProgram->setUniformValue("u_mvp",    mvp);
    w.m_glFlatProgram->setUniformValue("u_offset", QVector2D(0, 0));

    const int posLoc   = w.m_glFlatProgram->attributeLocation("a_position");
    const int colorLoc = w.m_glFlatProgram->attributeLocation("a_color");
    w.m_glFlatProgram->enableAttributeArray(posLoc);
    w.m_glFlatProgram->enableAttributeArray(colorLoc);

    w.m_vboShadow.bind();
    w.m_glFlatProgram->setAttributeBuffer(posLoc,   GL_FLOAT, offsetof(OpenGLFlatVertex, position), 3, sizeof(OpenGLFlatVertex));
    w.m_glFlatProgram->setAttributeBuffer(colorLoc, GL_FLOAT, offsetof(OpenGLFlatVertex, color),    4, sizeof(OpenGLFlatVertex));
    glDrawArrays(GL_TRIANGLES, 0, w.m_vboShadowCount);

    w.m_vboShadow.release();
    w.m_glFlatProgram->disableAttributeArray(posLoc);
    w.m_glFlatProgram->disableAttributeArray(colorLoc);
    w.m_glFlatProgram->release();
    glDisable(GL_BLEND);
}

void ViewportGLRenderer::renderPreview(ViewportWidget &w)
{
    if (!w.m_shapes || !w.m_glMeshProgram || !w.m_glMeshProgram->isLinked()
        || !w.m_vboMesh.isCreated())
        return;

    const bool dragging = w.m_draggingShape || w.m_draggingGroup;

    const QSet<int> selectedShapeIds = selectedViewportShapeIds(w.m_scene,
                                                                w.m_shapes,
                                                                w.m_selectedIndex,
                                                                w.m_selectedGroupId);
    const int selectedTreeNodeId = selectionHasTreeNodeId(w.m_scene, w.m_selectedGroupId) ? w.m_selectedGroupId : 0;
    const bool hasViewportSelection = (!selectedShapeIds.isEmpty() || selectedTreeNodeId > 0)
                                      && !w.m_draggingGroup;

    const QString meshKey = dragging
        ? QString()
        : (QString::number(w.m_cachedCsgFingerprint)
           + "|" + QString::number(w.m_selectedIndex)
           + "|" + QString::number(w.m_selectedGroupId)
           + "|" + (w.m_darkViewportTheme ? QChar('d') : QChar('l'))
           + "|" + QString::number(w.m_viewportColorVariant)
           + "|" + (w.m_hasCustomAppearanceTheme ? w.m_customAppearanceTheme.name : QStringLiteral("builtin"))
           + (w.m_draggingGroup ? QStringLiteral("|drag") : QStringLiteral("|idle")));

    if (dragging || meshKey != w.m_vboMeshKey) {
        QVector<OpenGLMeshVertex> verts;
        QVector<OpenGLFlatVertex> frontVerts;
        QVector<OpenGLFlatVertex> xrayVerts;

        auto appendMesh = [&](const SceneMesh &mesh, const QColor &baseColor) {
            for (const MeshTriangle &t : mesh.triangles) {
                const QVector3D col = colorToVector(baseColor.lighter(t.shade));
                verts.append({t.a, t.normal, col});
                verts.append({t.b, t.normal, col});
                verts.append({t.c, t.normal, col});
            }
        };

        if (dragging) {
            for (const ShapeNode &shape : *w.m_shapes) {
                QColor color(80, 160, 255);
                if (shape.booleanMode == ShapeNode::Subtract)
                    color = QColor(225, 95, 95);
                else if (shape.booleanMode == ShapeNode::Intersect)
                    color = QColor(150, 115, 240);
                const bool selected = selectedShapeIds.contains(shape.id);
                if (hasViewportSelection)
                    color = selectionHighlightColor(color, selected);
                appendMesh(interactionMeshForShape(w.m_scene, shape), color);
            }
        } else {
            const CsgPreview &preview = w.m_cachedCsgPreview;

            for (const CsgRenderItem &item : preview.items) {
                const bool selected = !w.m_draggingGroup
                                      && itemBelongsToSelection(item, w.m_shapes, selectedShapeIds, selectedTreeNodeId);
                if (item.helper) {
                    if (item.booleanMode != ShapeNode::Subtract)
                        continue;
                    const QColor fc = selected
                        ? QColor(188, 210, 218, 58)
                        : (w.m_darkViewportTheme ? QColor(188, 210, 218, 22) : QColor(60, 90, 110, 28));
                    const QColor xc = selected
                        ? QColor(188, 210, 218, 26)
                        : (w.m_darkViewportTheme ? QColor(188, 210, 218,  8) : QColor(60, 90, 110, 12));
                    const QVector4D cf = colorToVector4(fc);
                    const QVector4D cx = colorToVector4(xc);
                    for (const MeshTriangle &t : item.mesh.triangles) {
                        frontVerts.append({t.a, cf});
                        frontVerts.append({t.b, cf});
                        frontVerts.append({t.c, cf});
                        xrayVerts.append({t.a, cx});
                        xrayVerts.append({t.b, cx});
                        xrayVerts.append({t.c, cx});
                    }
                    continue;
                }

                QColor color = item.computed
                    ? (item.color.isValid() ? item.color : viewportComputedSolidColor(w.m_darkViewportTheme, w.m_viewportColorVariant, w.m_hasCustomAppearanceTheme ? &w.m_customAppearanceTheme : nullptr))
                    : viewportPlainSolidColor(w.m_darkViewportTheme, w.m_viewportColorVariant, w.m_hasCustomAppearanceTheme ? &w.m_customAppearanceTheme : nullptr);
                if (!item.computed && item.color.isValid())
                    color = item.color;
                if (!item.computed && item.booleanMode == ShapeNode::Subtract)
                    color = QColor(225, 95, 95);
                else if (!item.computed && item.booleanMode == ShapeNode::Intersect)
                    color = QColor(150, 115, 240);
                if (hasViewportSelection)
                    color = selectionHighlightColor(color, selected);
                appendMesh(item.mesh, color);
            }
        }

        w.m_vboMesh.bind();
        w.m_vboMesh.allocate(verts.constData(), verts.size() * int(sizeof(OpenGLMeshVertex)));
        w.m_vboMesh.release();
        w.m_vboMeshCount = verts.size();

        w.m_vboHelperFront.bind();
        w.m_vboHelperFront.allocate(frontVerts.constData(), frontVerts.size() * int(sizeof(OpenGLFlatVertex)));
        w.m_vboHelperFront.release();
        w.m_vboHelperFrontCount = frontVerts.size();

        w.m_vboHelperXray.bind();
        w.m_vboHelperXray.allocate(xrayVerts.constData(), xrayVerts.size() * int(sizeof(OpenGLFlatVertex)));
        w.m_vboHelperXray.release();
        w.m_vboHelperXrayCount = xrayVerts.size();

        if (!dragging)
            w.m_vboMeshKey = meshKey;
    }

    const QString selectionTopologyKey = w.m_draggingGroup
        ? QStringLiteral("dragging-group-no-selection")
        : dragging
        ? QString()
        : (QString::number(w.m_cachedCsgFingerprint)
           + "|" + QString::number(w.m_selectedIndex)
           + "|" + QString::number(w.m_selectedGroupId));
    auto forEachSelectedMesh = [&](const auto &visitor) {
        if (w.m_draggingGroup)
            return;
        if (dragging) {
            for (const ShapeNode &shape : *w.m_shapes) {
                if (selectedShapeIds.contains(shape.id))
                    visitor(interactionMeshForShape(w.m_scene, shape));
            }
            return;
        }
        for (const CsgRenderItem &item : w.m_cachedCsgPreview.items) {
            const bool selected = !w.m_draggingGroup
                                  && itemBelongsToSelection(item, w.m_shapes, selectedShapeIds, selectedTreeNodeId);
            if (selected && (!item.helper || item.booleanMode != ShapeNode::Subtract))
                visitor(item.mesh);
        }
    };
    if (dragging || selectionTopologyKey != w.m_selectionEdgeTopologyKey) {
        w.m_selectionEdgeCandidates.clear();
        forEachSelectedMesh([&](const SceneMesh &mesh) {
            w.m_selectionEdgeCandidates += selectionEdgeTopology(mesh);
        });
        w.m_selectionEdgeTopologyKey = selectionTopologyKey;
        w.m_vboSelectionEdgesKey.clear();
    }

    const QString selectionEdgesKey = dragging
        ? QString()
        : (selectionTopologyKey
           + "|" + QString::number(w.m_cameraYaw, 'f', 2)
           + "|" + QString::number(w.m_cameraPitch, 'f', 2));
    if (dragging || selectionEdgesKey != w.m_vboSelectionEdgesKey) {
        QVector<OpenGLLineVertex> hiddenEdgeGlowVerts;
        QVector<OpenGLLineVertex> hiddenEdgeCoreVerts;
        QVector<OpenGLLineVertex> structuralEdgeGlowVerts;
        QVector<OpenGLLineVertex> structuralEdgeCoreVerts;
        QVector<OpenGLLineVertex> silhouetteGlowVerts;
        QVector<OpenGLLineVertex> silhouetteCoreVerts;
        const QVector4D hiddenEdgeGlowColor = colorToVector4(QColor(180, 210, 240, 18));
        const QVector4D hiddenEdgeCoreColor = colorToVector4(QColor(210, 228, 248, 40));
        const QVector4D structuralEdgeGlowColor = colorToVector4(QColor(255, 183, 64, 110));
        const QVector4D structuralEdgeCoreColor = colorToVector4(QColor(255, 218, 128, 220));
        const QVector4D silhouetteGlowColor = colorToVector4(QColor(255, 185, 60, 175));
        const QVector4D silhouetteCoreColor = colorToVector4(QColor(255, 222, 134, 255));
        for (const ViewportSelectionEdgeCandidate &edge : w.m_selectionEdgeCandidates) {
            if (edge.structural) {
                structuralEdgeGlowVerts.append({edge.from, structuralEdgeGlowColor});
                structuralEdgeGlowVerts.append({edge.to, structuralEdgeGlowColor});
                structuralEdgeCoreVerts.append({edge.from, structuralEdgeCoreColor});
                structuralEdgeCoreVerts.append({edge.to, structuralEdgeCoreColor});
                hiddenEdgeGlowVerts.append({edge.from, hiddenEdgeGlowColor});
                hiddenEdgeGlowVerts.append({edge.to, hiddenEdgeGlowColor});
                hiddenEdgeCoreVerts.append({edge.from, hiddenEdgeCoreColor});
                hiddenEdgeCoreVerts.append({edge.to, hiddenEdgeCoreColor});
            } else {
                bool hasVisibleFace = false;
                bool hasHiddenFace  = false;
                for (const QVector3D &normal : edge.normals) {
                    const bool visible = toCameraDirection(normal, w.m_cameraYaw, w.m_cameraPitch).z() >= 0.0f;
                    hasVisibleFace = hasVisibleFace || visible;
                    hasHiddenFace  = hasHiddenFace  || !visible;
                }
                if (hasVisibleFace && hasHiddenFace) {
                    silhouetteGlowVerts.append({edge.from, silhouetteGlowColor});
                    silhouetteGlowVerts.append({edge.to,   silhouetteGlowColor});
                    silhouetteCoreVerts.append({edge.from, silhouetteCoreColor});
                    silhouetteCoreVerts.append({edge.to,   silhouetteCoreColor});
                }
            }
        }

        const int hiddenEdgeSegmentCount = hiddenEdgeCoreVerts.size();
        const int structuralEdgeSegmentCount = structuralEdgeCoreVerts.size();
        const int silhouetteSegmentCount = silhouetteCoreVerts.size();
        hiddenEdgeGlowVerts += hiddenEdgeCoreVerts;
        hiddenEdgeGlowVerts += structuralEdgeGlowVerts;
        hiddenEdgeGlowVerts += structuralEdgeCoreVerts;
        hiddenEdgeGlowVerts += silhouetteGlowVerts;
        hiddenEdgeGlowVerts += silhouetteCoreVerts;
        w.m_vboSelectionEdges.bind();
        w.m_vboSelectionEdges.allocate(hiddenEdgeGlowVerts.constData(),
                                     hiddenEdgeGlowVerts.size() * int(sizeof(OpenGLLineVertex)));
        w.m_vboSelectionEdges.release();
        w.m_vboSelectionHiddenEdgeCount = hiddenEdgeSegmentCount;
        w.m_vboSelectionEdgeCount = structuralEdgeSegmentCount;
        w.m_vboSelectionSilhouetteCount = silhouetteSegmentCount;
        w.m_vboSelectionEdgesKey = selectionEdgesKey;
    }

    if (w.m_vboMeshCount == 0)
        return;

    const QMatrix4x4 V   = buildViewMatrix(w.m_cameraYaw, w.m_cameraPitch, w.m_cameraDistance, w.m_cameraTarget);
    const QMatrix4x4 P   = buildProjectionMatrix(float(w.width()), float(w.height()), w.m_cameraDistance, w.m_orthographicProjection);
    const QMatrix4x4 mvp = P * V;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

    const QVector3D cameraWorldPos = V.inverted().map(QVector3D(0.0f, 0.0f, 0.0f));

    w.m_glMeshProgram->bind();
    w.m_glMeshProgram->setUniformValue("u_mvp",        mvp);
    w.m_glMeshProgram->setUniformValue("u_camera_pos", cameraWorldPos);

    const QVector<SceneLight> lights = viewportLightsForPreset(w.m_lightingPreset);
    if (lights.size() >= 3) {
        auto setLight = [&w, &lights](int i, const char *d, const char *c, const char *n) {
            w.m_glMeshProgram->setUniformValue(d, lights[i].direction);
            w.m_glMeshProgram->setUniformValue(c, colorToVector(lights[i].color));
            w.m_glMeshProgram->setUniformValue(n, lights[i].intensity);
        };
        setLight(0, "u_light_direction_a", "u_light_color_a", "u_light_intensity_a");
        setLight(1, "u_light_direction_b", "u_light_color_b", "u_light_intensity_b");
        setLight(2, "u_light_direction_c", "u_light_color_c", "u_light_intensity_c");
    }
    w.m_glMeshProgram->setUniformValue("u_ambient",          viewportAmbientForLightingPreset(w.m_lightingPreset));
    w.m_glMeshProgram->setUniformValue("u_specular_strength", viewportSpecularForLightingPreset(w.m_lightingPreset));

    const int posLoc    = w.m_glMeshProgram->attributeLocation("a_position");
    const int normalLoc = w.m_glMeshProgram->attributeLocation("a_normal");
    const int colorLoc  = w.m_glMeshProgram->attributeLocation("a_color");
    w.m_glMeshProgram->enableAttributeArray(posLoc);
    w.m_glMeshProgram->enableAttributeArray(normalLoc);
    w.m_glMeshProgram->enableAttributeArray(colorLoc);

    w.m_vboMesh.bind();
    w.m_glMeshProgram->setAttributeBuffer(posLoc,    GL_FLOAT, offsetof(OpenGLMeshVertex, position), 3, sizeof(OpenGLMeshVertex));
    w.m_glMeshProgram->setAttributeBuffer(normalLoc, GL_FLOAT, offsetof(OpenGLMeshVertex, normal),   3, sizeof(OpenGLMeshVertex));
    w.m_glMeshProgram->setAttributeBuffer(colorLoc,  GL_FLOAT, offsetof(OpenGLMeshVertex, color),    3, sizeof(OpenGLMeshVertex));
    glDrawArrays(GL_TRIANGLES, 0, w.m_vboMeshCount);
    w.m_vboMesh.release();

    w.m_glMeshProgram->disableAttributeArray(posLoc);
    w.m_glMeshProgram->disableAttributeArray(normalLoc);
    w.m_glMeshProgram->disableAttributeArray(colorLoc);
    w.m_glMeshProgram->release();

    const bool hasHelpers = (w.m_vboHelperFrontCount > 0 || w.m_vboHelperXrayCount > 0)
                            && w.m_glFlatProgram && w.m_glFlatProgram->isLinked();
    if (hasHelpers) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glEnable(GL_POLYGON_OFFSET_FILL);

        w.m_glFlatProgram->bind();
        w.m_glFlatProgram->setUniformValue("u_mvp",    mvp);
        w.m_glFlatProgram->setUniformValue("u_offset", QVector2D(0, 0));

        const int hPosLoc   = w.m_glFlatProgram->attributeLocation("a_position");
        const int hColorLoc = w.m_glFlatProgram->attributeLocation("a_color");
        w.m_glFlatProgram->enableAttributeArray(hPosLoc);
        w.m_glFlatProgram->enableAttributeArray(hColorLoc);

        auto drawHelperVbo = [&](QOpenGLBuffer &vbo, int count,
                                 GLenum depthFunc, float polyOff,
                                 GLint stencilRef, GLenum stencilFunc) {
            if (count == 0) return;
            glDepthFunc(depthFunc);
            glPolygonOffset(polyOff, polyOff);
            glStencilFunc(stencilFunc, stencilRef, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            vbo.bind();
            w.m_glFlatProgram->setAttributeBuffer(hPosLoc,   GL_FLOAT, offsetof(OpenGLFlatVertex, position), 3, sizeof(OpenGLFlatVertex));
            w.m_glFlatProgram->setAttributeBuffer(hColorLoc, GL_FLOAT, offsetof(OpenGLFlatVertex, color),    4, sizeof(OpenGLFlatVertex));
            glDrawArrays(GL_TRIANGLES, 0, count);
            vbo.release();
        };

        drawHelperVbo(w.m_vboHelperFront, w.m_vboHelperFrontCount,
                      GL_LESS, -1.0f, 1, GL_EQUAL);
        drawHelperVbo(w.m_vboHelperXray, w.m_vboHelperXrayCount,
                      GL_GREATER, 2.0f, 0, GL_ALWAYS);

        w.m_glFlatProgram->disableAttributeArray(hPosLoc);
        w.m_glFlatProgram->disableAttributeArray(hColorLoc);
        w.m_glFlatProgram->release();
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(0.0f, 0.0f);
        glDisable(GL_BLEND);
    }

    if ((w.m_vboSelectionEdgeCount > 0 || w.m_vboSelectionHiddenEdgeCount > 0
         || w.m_vboSelectionSilhouetteCount > 0)
        && w.m_glLineProgram && w.m_glLineProgram->isLinked()) {
        glDisable(GL_STENCIL_TEST);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);

        w.m_glLineProgram->bind();
        w.m_glLineProgram->setUniformValue("u_mvp", mvp);
        w.m_glLineProgram->setUniformValue("u_shimmer_phase", w.m_selectionShimmerPhase);
        const float pulseSin  = 0.5f + 0.5f * sinf(w.m_selectionShimmerPhase * 0.65f);
        const float glowBoost = 1.0f + pulseSin * 0.7f;
        const int edgePosLoc = w.m_glLineProgram->attributeLocation("a_position");
        const int edgeColorLoc = w.m_glLineProgram->attributeLocation("a_color");
        w.m_glLineProgram->enableAttributeArray(edgePosLoc);
        w.m_glLineProgram->enableAttributeArray(edgeColorLoc);
        w.m_vboSelectionEdges.bind();
        w.m_glLineProgram->setAttributeBuffer(edgePosLoc, GL_FLOAT, offsetof(OpenGLLineVertex, position), 3, sizeof(OpenGLLineVertex));
        w.m_glLineProgram->setAttributeBuffer(edgeColorLoc, GL_FLOAT, offsetof(OpenGLLineVertex, color), 4, sizeof(OpenGLLineVertex));
        glEnable(GL_BLEND);

        w.m_glLineProgram->setUniformValue("u_depth_pull", 0.0f);
        glDepthFunc(GL_GREATER);
        if (w.m_vboSelectionHiddenEdgeCount > 0) {
            w.m_glLineProgram->setUniformValue("u_shimmer_base_alpha", 1.0f);
            w.m_glLineProgram->setUniformValue("u_shimmer_amount", 0.18f);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glLineWidth(4.5f);
            glDrawArrays(GL_LINES, 0, w.m_vboSelectionHiddenEdgeCount);
            glLineWidth(1.0f);
            glDrawArrays(GL_LINES, w.m_vboSelectionHiddenEdgeCount, w.m_vboSelectionHiddenEdgeCount);
        }

        w.m_glLineProgram->setUniformValue("u_depth_pull", 0.0003f);
        glDepthFunc(GL_LEQUAL);
        int visibleOffset = w.m_vboSelectionHiddenEdgeCount * 2;
        if (w.m_vboSelectionEdgeCount > 0) {
            w.m_glLineProgram->setUniformValue("u_shimmer_base_alpha", 0.28f);
            w.m_glLineProgram->setUniformValue("u_shimmer_amount", 0.95f);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glLineWidth(7.0f * glowBoost);
            glDrawArrays(GL_LINES, visibleOffset, w.m_vboSelectionEdgeCount);

            w.m_glLineProgram->setUniformValue("u_shimmer_base_alpha", 1.0f);
            w.m_glLineProgram->setUniformValue("u_shimmer_amount", 0.78f);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glLineWidth(4.5f * glowBoost);
            glDrawArrays(GL_LINES, visibleOffset, w.m_vboSelectionEdgeCount);
            glLineWidth(2.5f);
            glDrawArrays(GL_LINES, visibleOffset + w.m_vboSelectionEdgeCount, w.m_vboSelectionEdgeCount);
            visibleOffset += w.m_vboSelectionEdgeCount * 2;
        }

        if (w.m_vboSelectionSilhouetteCount > 0) {
            w.m_glLineProgram->setUniformValue("u_shimmer_base_alpha", 0.22f);
            w.m_glLineProgram->setUniformValue("u_shimmer_amount", 0.95f);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glDepthFunc(GL_LEQUAL);
            glLineWidth(10.0f * glowBoost);
            glDrawArrays(GL_LINES, visibleOffset, w.m_vboSelectionSilhouetteCount);

            w.m_glLineProgram->setUniformValue("u_shimmer_base_alpha", 1.0f);
            w.m_glLineProgram->setUniformValue("u_shimmer_amount", 0.92f);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glLineWidth(6.0f * glowBoost);
            glDrawArrays(GL_LINES, visibleOffset, w.m_vboSelectionSilhouetteCount);
            glLineWidth(3.2f);
            glDrawArrays(GL_LINES, visibleOffset + w.m_vboSelectionSilhouetteCount, w.m_vboSelectionSilhouetteCount);
        }
        glLineWidth(1.0f);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        w.m_glLineProgram->setUniformValue("u_depth_pull", 0.0f);
        w.m_vboSelectionEdges.release();
        w.m_glLineProgram->disableAttributeArray(edgePosLoc);
        w.m_glLineProgram->disableAttributeArray(edgeColorLoc);
        w.m_glLineProgram->release();
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
    }

    glDisable(GL_STENCIL_TEST);
}
