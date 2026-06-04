#include "viewportglrenderer.h"
#include "viewportwidget.h"
#include "viewporthelpers.h"

#include <GL/gl.h>
#include <QtMath>

using namespace ViewportHelpers;

void ViewportGLRenderer::initialize(ViewportWidget &w)
{
    m_glMeshProgram = new QOpenGLShaderProgram(&w);
    m_glMeshProgram->addShaderFromSourceCode(
        QOpenGLShader::Vertex,
        "attribute vec3 a_position;\n"
        "attribute vec3 a_normal;\n"
        "attribute vec3 a_color;\n"
        "uniform mat4 u_mvp;\n"
        "varying vec3 v_normal;\n"
        "varying vec3 v_world_pos;\n"
        "varying vec3 v_color;\n"
        "void main() {\n"
        "    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
        "    v_normal    = a_normal;\n"
        "    v_world_pos = a_position;\n"
        "    v_color     = a_color;\n"
        "}\n");
    m_glMeshProgram->addShaderFromSourceCode(
        QOpenGLShader::Fragment,
        "#ifdef GL_ES\n"
        "precision mediump float;\n"
        "#endif\n"
        "varying vec3 v_normal;\n"
        "varying vec3 v_world_pos;\n"
        "varying vec3 v_color;\n"
        "uniform vec3 u_camera_pos;\n"
        "uniform vec3 u_light_direction_a;\n"
        "uniform vec3 u_light_direction_b;\n"
        "uniform vec3 u_light_direction_c;\n"
        "uniform vec3 u_light_color_a;\n"
        "uniform vec3 u_light_color_b;\n"
        "uniform vec3 u_light_color_c;\n"
        "uniform float u_light_intensity_a;\n"
        "uniform float u_light_intensity_b;\n"
        "uniform float u_light_intensity_c;\n"
        "uniform float u_ambient;\n"
        "uniform float u_specular_strength;\n"
        "void main() {\n"
        "    vec3 n = normalize(v_normal);\n"
        "    vec3 viewDir = normalize(u_camera_pos - v_world_pos);\n"
        "    vec3 lightA = normalize(u_light_direction_a);\n"
        "    vec3 lightB = normalize(u_light_direction_b);\n"
        "    vec3 lightC = normalize(u_light_direction_c);\n"
        "    float diffuseA = max(0.0, dot(n, lightA));\n"
        "    float diffuseB = max(0.0, dot(n, lightB));\n"
        "    float diffuseC = max(0.0, dot(n, lightC));\n"
        "    vec3 reflectedView = reflect(-viewDir, n);\n"
        "    float skyMix = clamp(reflectedView.z * 0.55 + 0.5, 0.0, 1.0);\n"
        "    vec3 environment = mix(vec3(0.18, 0.20, 0.22), vec3(0.58, 0.70, 0.82), skyMix);\n"
        "    float fresnel = pow(1.0 - max(0.0, dot(n, viewDir)), 3.0);\n"
        "    float specA = pow(max(0.0, dot(reflect(-lightA, n), viewDir)), 42.0);\n"
        "    float specB = pow(max(0.0, dot(reflect(-lightB, n), viewDir)), 26.0);\n"
        "    float rim = pow(1.0 - max(0.0, dot(n, viewDir)), 2.0);\n"
        "    vec3 warmLight = u_light_color_a * diffuseA * u_light_intensity_a;\n"
        "    vec3 coolLight = u_light_color_b * diffuseB * u_light_intensity_b;\n"
        "    vec3 sideLight = u_light_color_c * diffuseC * u_light_intensity_c;\n"
        "    vec3 shaded = v_color * (vec3(u_ambient + 0.10) + warmLight + coolLight + sideLight);\n"
        "    vec3 tintedEnvironment = mix(environment, v_color, 0.68);\n"
        "    shaded = mix(shaded, tintedEnvironment, 0.04 + fresnel * 0.09);\n"
        "    shaded += vec3(1.0, 0.92, 0.74) * specA * u_specular_strength;\n"
        "    shaded += vec3(0.70, 0.86, 1.0) * specB * (u_specular_strength * 0.43);\n"
        "    shaded += mix(vec3(0.55, 0.75, 1.0), v_color, 0.45) * rim * 0.055;\n"
        "    gl_FragColor = vec4(clamp(shaded, 0.0, 1.0), 1.0);\n"
        "}\n");
    m_glMeshProgram->link();

    m_glLineProgram = new QOpenGLShaderProgram(&w);
    m_glLineProgram->addShaderFromSourceCode(
        QOpenGLShader::Vertex,
        "attribute vec3 a_position;\n"
        "attribute vec4 a_color;\n"
        "uniform mat4 u_mvp;\n"
        "uniform float u_shimmer_phase;\n"
        "uniform float u_shimmer_amount;\n"
        "uniform float u_shimmer_base_alpha;\n"
        "uniform float u_depth_pull;\n"
        "varying vec4 v_color;\n"
        "void main() {\n"
        "    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
        "    gl_Position.z -= u_depth_pull * gl_Position.w;\n"
        "    float raw  = 0.5 + 0.5 * sin(dot(a_position, vec3(0.23, 0.14, 0.19)) + u_shimmer_phase);\n"
        "    float wave = raw * raw;\n"
        "    float s    = wave * u_shimmer_amount;\n"
        "    vec3 brightened = a_color.rgb + (vec3(1.0) - a_color.rgb) * s;\n"
        "    float alpha = a_color.a * u_shimmer_base_alpha * (0.55 + s * 0.45);\n"
        "    v_color = vec4(brightened, clamp(alpha, 0.0, 1.0));\n"
        "}\n");
    m_glLineProgram->addShaderFromSourceCode(
        QOpenGLShader::Fragment,
        "#ifdef GL_ES\n"
        "precision mediump float;\n"
        "#endif\n"
        "varying vec4 v_color;\n"
        "void main() {\n"
        "    gl_FragColor = v_color;\n"
        "}\n");
    m_glLineProgram->link();

    m_glFlatProgram = new QOpenGLShaderProgram(&w);
    m_glFlatProgram->addShaderFromSourceCode(
        QOpenGLShader::Vertex,
        "attribute vec3 a_position;\n"
        "attribute vec4 a_color;\n"
        "uniform mat4 u_mvp;\n"
        "uniform vec2 u_offset;\n"
        "varying vec4 v_color;\n"
        "void main() {\n"
        "    vec3 pos = vec3(a_position.x + u_offset.x, a_position.y + u_offset.y, a_position.z);\n"
        "    gl_Position = u_mvp * vec4(pos, 1.0);\n"
        "    v_color = a_color;\n"
        "}\n");
    m_glFlatProgram->addShaderFromSourceCode(
        QOpenGLShader::Fragment,
        "#ifdef GL_ES\n"
        "precision mediump float;\n"
        "#endif\n"
        "varying vec4 v_color;\n"
        "void main() {\n"
        "    gl_FragColor = v_color;\n"
        "}\n");
    m_glFlatProgram->link();

    m_vboGrid.create();
    m_vboGrid.setUsagePattern(QOpenGLBuffer::StaticDraw);
    {
        QVector<OpenGLLineVertex> gridVerts;
        gridVerts.reserve(504 + 6);
        auto appendLine = [&](const QVector3D &from, const QVector3D &to, const QColor &color) {
            const QVector4D c = colorToVector4(color);
            gridVerts.append({from, c});
            gridVerts.append({to,   c});
        };
        const QColor minor(128, 128, 128);
        for (int i = -120; i <= 120; i += 20) {
            appendLine({-120, float(i), 0}, {120, float(i), 0}, minor);
            appendLine({float(i), -120, 0}, {float(i), 120, 0}, minor);
        }
        appendLine({-130, 0, 0}, {130, 0, 0}, ViewportConstants::kGridAxisXColor);
        appendLine({0, -130, 0}, {0, 130, 0}, ViewportConstants::kGridAxisYColor);
        appendLine({0, 0, 0},    {0, 0, 90},  ViewportConstants::kGridAxisZColor);
        m_vboGrid.bind();
        m_vboGrid.allocate(gridVerts.constData(), gridVerts.size() * int(sizeof(OpenGLLineVertex)));
        m_vboGrid.release();
        m_vboGridCount = gridVerts.size();
    }

    m_vboMesh.create();        m_vboMesh.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_vboSelectionEdges.create(); m_vboSelectionEdges.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_vboHelperFront.create(); m_vboHelperFront.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_vboHelperXray.create();  m_vboHelperXray.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_vboShadow.create();      m_vboShadow.setUsagePattern(QOpenGLBuffer::DynamicDraw);
}

void ViewportGLRenderer::clearMeshCache()
{
    m_vboMeshKey.clear();
    m_selectionEdgeTopologyKey.clear();
    m_vboSelectionEdgesKey.clear();
}

void ViewportGLRenderer::clearEdgeCache()
{
    m_vboSelectionEdgesKey.clear();
}

void ViewportGLRenderer::renderGrid(ViewportWidget &w)
{
    if (!m_glLineProgram || !m_glLineProgram->isLinked() || !m_vboGrid.isCreated())
        return;

    glDisable(GL_DEPTH_TEST);
    glLineWidth(1.0f);
    m_glLineProgram->bind();

    const QMatrix4x4 mvp = buildProjectionMatrix(float(w.width()), float(w.height()), w.m_camera.distance, w.m_camera.orthographic)
                          * buildViewMatrix(w.m_camera.yaw, w.m_camera.pitch, w.m_camera.distance, w.m_camera.target);
    m_glLineProgram->setUniformValue("u_mvp", mvp);
    m_glLineProgram->setUniformValue("u_shimmer_phase", 0.0f);
    m_glLineProgram->setUniformValue("u_shimmer_amount", 0.0f);
    m_glLineProgram->setUniformValue("u_depth_pull", 0.0f);

    const int posLoc   = m_glLineProgram->attributeLocation("a_position");
    const int colorLoc = m_glLineProgram->attributeLocation("a_color");
    m_glLineProgram->enableAttributeArray(posLoc);
    m_glLineProgram->enableAttributeArray(colorLoc);

    m_vboGrid.bind();
    m_glLineProgram->setAttributeBuffer(posLoc,   GL_FLOAT, offsetof(OpenGLLineVertex, position), 3, sizeof(OpenGLLineVertex));
    m_glLineProgram->setAttributeBuffer(colorLoc, GL_FLOAT, offsetof(OpenGLLineVertex, color),    4, sizeof(OpenGLLineVertex));

    glDrawArrays(GL_LINES, 0, m_vboGridCount);

    m_vboGrid.release();
    m_glLineProgram->disableAttributeArray(posLoc);
    m_glLineProgram->disableAttributeArray(colorLoc);
    m_glLineProgram->release();
}

void ViewportGLRenderer::renderContactShadows(ViewportWidget &w)
{
    if (!w.m_shapes || !m_glFlatProgram || !m_glFlatProgram->isLinked() || !m_vboShadow.isCreated())
        return;
    if (w.m_draggingShape || w.m_draggingGroup)
        return;

    const CsgPreview &preview = w.m_cachedCsgPreview;

    const QString fp = QString::number(w.m_cachedCsgFingerprint);
    if (fp != m_vboShadowKey) {
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
        m_vboShadow.bind();
        m_vboShadow.allocate(verts.constData(), verts.size() * int(sizeof(OpenGLFlatVertex)));
        m_vboShadow.release();
        m_vboShadowCount = verts.size();
        m_vboShadowKey   = fp;
    }

    if (m_vboShadowCount == 0)
        return;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_glFlatProgram->bind();

    const QMatrix4x4 mvp = buildProjectionMatrix(float(w.width()), float(w.height()), w.m_camera.distance, w.m_camera.orthographic)
                          * buildViewMatrix(w.m_camera.yaw, w.m_camera.pitch, w.m_camera.distance, w.m_camera.target);
    m_glFlatProgram->setUniformValue("u_mvp",    mvp);
    m_glFlatProgram->setUniformValue("u_offset", QVector2D(0, 0));

    const int posLoc   = m_glFlatProgram->attributeLocation("a_position");
    const int colorLoc = m_glFlatProgram->attributeLocation("a_color");
    m_glFlatProgram->enableAttributeArray(posLoc);
    m_glFlatProgram->enableAttributeArray(colorLoc);

    m_vboShadow.bind();
    m_glFlatProgram->setAttributeBuffer(posLoc,   GL_FLOAT, offsetof(OpenGLFlatVertex, position), 3, sizeof(OpenGLFlatVertex));
    m_glFlatProgram->setAttributeBuffer(colorLoc, GL_FLOAT, offsetof(OpenGLFlatVertex, color),    4, sizeof(OpenGLFlatVertex));
    glDrawArrays(GL_TRIANGLES, 0, m_vboShadowCount);

    m_vboShadow.release();
    m_glFlatProgram->disableAttributeArray(posLoc);
    m_glFlatProgram->disableAttributeArray(colorLoc);
    m_glFlatProgram->release();
    glDisable(GL_BLEND);
}

void ViewportGLRenderer::renderPreview(ViewportWidget &w)
{
    if (!w.m_shapes || !m_glMeshProgram || !m_glMeshProgram->isLinked()
        || !m_vboMesh.isCreated())
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

    if (dragging || meshKey != m_vboMeshKey) {
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
                QColor color(ViewportConstants::kDefaultMeshColor);
                if (shape.booleanMode == ShapeNode::Subtract)
                    color = ViewportConstants::kSubtractColor;
                else if (shape.booleanMode == ShapeNode::Intersect)
                    color = ViewportConstants::kIntersectColor;
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
                    color = ViewportConstants::kSubtractColor;
                else if (!item.computed && item.booleanMode == ShapeNode::Intersect)
                    color = ViewportConstants::kIntersectColor;
                if (hasViewportSelection)
                    color = selectionHighlightColor(color, selected);
                appendMesh(item.mesh, color);
            }
        }

        m_vboMesh.bind();
        m_vboMesh.allocate(verts.constData(), verts.size() * int(sizeof(OpenGLMeshVertex)));
        m_vboMesh.release();
        m_vboMeshCount = verts.size();

        m_vboHelperFront.bind();
        m_vboHelperFront.allocate(frontVerts.constData(), frontVerts.size() * int(sizeof(OpenGLFlatVertex)));
        m_vboHelperFront.release();
        m_vboHelperFrontCount = frontVerts.size();

        m_vboHelperXray.bind();
        m_vboHelperXray.allocate(xrayVerts.constData(), xrayVerts.size() * int(sizeof(OpenGLFlatVertex)));
        m_vboHelperXray.release();
        m_vboHelperXrayCount = xrayVerts.size();

        if (!dragging)
            m_vboMeshKey = meshKey;
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
        // Priority 1a: group selection — per-group 3D mesh (async computed)
        if (selectedTreeNodeId > 0
                && w.m_cachedSelectionMeshGroupId == selectedTreeNodeId
                && !w.m_cachedSelectionMesh.triangles.isEmpty()) {
            visitor(w.m_cachedSelectionMesh);
            return;
        }
        // Priority 1b: primitive selection — interaction mesh (synchronously built)
        if (selectedTreeNodeId == 0 && w.m_selectedIndex >= 0
                && w.m_cachedSelectionMeshGroupId == -1
                && !w.m_cachedSelectionMesh.triangles.isEmpty()) {
            visitor(w.m_cachedSelectionMesh);
            return;
        }

        // Fallback: flat helpers (while group selection mesh is still computing)
        for (const CsgRenderItem &item : w.m_cachedCsgPreview.items) {
            const bool selected = !w.m_draggingGroup
                                  && itemBelongsToSelection(item, w.m_shapes, selectedShapeIds, selectedTreeNodeId);
            if (selected && (!item.helper || item.booleanMode != ShapeNode::Subtract))
                visitor(item.mesh);
        }
    };
    if (dragging || selectionTopologyKey != m_selectionEdgeTopologyKey) {
        m_selectionEdgeCandidates.clear();
        forEachSelectedMesh([&](const SceneMesh &mesh) {
            m_selectionEdgeCandidates += selectionEdgeTopology(mesh);
        });
        m_selectionEdgeTopologyKey = selectionTopologyKey;
        m_vboSelectionEdgesKey.clear();
    }

    const QString selectionEdgesKey = dragging
        ? QString()
        : (selectionTopologyKey
           + "|" + QString::number(w.m_camera.yaw, 'f', 2)
           + "|" + QString::number(w.m_camera.pitch, 'f', 2));
    if (dragging || selectionEdgesKey != m_vboSelectionEdgesKey) {
        QVector<OpenGLLineVertex> hiddenEdgeGlowVerts;
        QVector<OpenGLLineVertex> hiddenEdgeCoreVerts;
        QVector<OpenGLLineVertex> structuralEdgeGlowVerts;
        QVector<OpenGLLineVertex> structuralEdgeCoreVerts;
        QVector<OpenGLLineVertex> silhouetteGlowVerts;
        QVector<OpenGLLineVertex> silhouetteCoreVerts;
        const QVector4D hiddenEdgeGlowColor = colorToVector4(QColor(80, 180, 255, 90));
        const QVector4D hiddenEdgeCoreColor = colorToVector4(QColor(140, 210, 255, 180));
        const QVector4D structuralEdgeGlowColor = colorToVector4(QColor(255, 183, 64, 110));
        const QVector4D structuralEdgeCoreColor = colorToVector4(QColor(255, 218, 128, 220));
        const QVector4D silhouetteGlowColor = colorToVector4(QColor(255, 185, 60, 175));
        const QVector4D silhouetteCoreColor = colorToVector4(QColor(255, 222, 134, 255));
        for (const ViewportSelectionEdgeCandidate &edge : m_selectionEdgeCandidates) {
            if (edge.structural) {
                // Classify by face visibility: visible face → gold, all faces hidden → blue
                bool hasVisibleFace = edge.normals.isEmpty();
                for (const QVector3D &normal : edge.normals) {
                    if (w.m_camera.toCameraDirection(normal).z() < 0.0f) {
                        hasVisibleFace = true;
                        break;
                    }
                }
                if (hasVisibleFace) {
                    structuralEdgeGlowVerts.append({edge.from, structuralEdgeGlowColor});
                    structuralEdgeGlowVerts.append({edge.to, structuralEdgeGlowColor});
                    structuralEdgeCoreVerts.append({edge.from, structuralEdgeCoreColor});
                    structuralEdgeCoreVerts.append({edge.to, structuralEdgeCoreColor});
                } else {
                    hiddenEdgeGlowVerts.append({edge.from, hiddenEdgeGlowColor});
                    hiddenEdgeGlowVerts.append({edge.to, hiddenEdgeGlowColor});
                    hiddenEdgeCoreVerts.append({edge.from, hiddenEdgeCoreColor});
                    hiddenEdgeCoreVerts.append({edge.to, hiddenEdgeCoreColor});
                }
            } else {
                bool hasVisibleFace = false;
                bool hasHiddenFace  = false;
                for (const QVector3D &normal : edge.normals) {
                    const bool visible = w.m_camera.toCameraDirection(normal).z() < 0.0f;
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
        m_vboSelectionEdges.bind();
        m_vboSelectionEdges.allocate(hiddenEdgeGlowVerts.constData(),
                                     hiddenEdgeGlowVerts.size() * int(sizeof(OpenGLLineVertex)));
        m_vboSelectionEdges.release();
        m_vboSelectionHiddenEdgeCount = hiddenEdgeSegmentCount;
        m_vboSelectionEdgeCount = structuralEdgeSegmentCount;
        m_vboSelectionSilhouetteCount = silhouetteSegmentCount;
        m_vboSelectionEdgesKey = selectionEdgesKey;
    }

    if (m_vboMeshCount == 0)
        return;

    const QMatrix4x4 V   = buildViewMatrix(w.m_camera.yaw, w.m_camera.pitch, w.m_camera.distance, w.m_camera.target);
    const QMatrix4x4 P   = buildProjectionMatrix(float(w.width()), float(w.height()), w.m_camera.distance, w.m_camera.orthographic);
    const QMatrix4x4 mvp = P * V;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

    const QVector3D cameraWorldPos = V.inverted().map(QVector3D(0.0f, 0.0f, 0.0f));

    m_glMeshProgram->bind();
    m_glMeshProgram->setUniformValue("u_mvp",        mvp);
    m_glMeshProgram->setUniformValue("u_camera_pos", cameraWorldPos);

    const QVector<SceneLight> lights = viewportLightsForPreset(w.m_lightingPreset);
    if (lights.size() >= 3) {
        auto setLight = [&w, &lights, this](int i, const char *d, const char *c, const char *n) {
            m_glMeshProgram->setUniformValue(d, lights[i].direction);
            m_glMeshProgram->setUniformValue(c, colorToVector(lights[i].color));
            m_glMeshProgram->setUniformValue(n, lights[i].intensity);
        };
        setLight(0, "u_light_direction_a", "u_light_color_a", "u_light_intensity_a");
        setLight(1, "u_light_direction_b", "u_light_color_b", "u_light_intensity_b");
        setLight(2, "u_light_direction_c", "u_light_color_c", "u_light_intensity_c");
    }
    m_glMeshProgram->setUniformValue("u_ambient",          viewportAmbientForLightingPreset(w.m_lightingPreset));
    m_glMeshProgram->setUniformValue("u_specular_strength", viewportSpecularForLightingPreset(w.m_lightingPreset));

    const int posLoc    = m_glMeshProgram->attributeLocation("a_position");
    const int normalLoc = m_glMeshProgram->attributeLocation("a_normal");
    const int colorLoc  = m_glMeshProgram->attributeLocation("a_color");
    m_glMeshProgram->enableAttributeArray(posLoc);
    m_glMeshProgram->enableAttributeArray(normalLoc);
    m_glMeshProgram->enableAttributeArray(colorLoc);

    m_vboMesh.bind();
    m_glMeshProgram->setAttributeBuffer(posLoc,    GL_FLOAT, offsetof(OpenGLMeshVertex, position), 3, sizeof(OpenGLMeshVertex));
    m_glMeshProgram->setAttributeBuffer(normalLoc, GL_FLOAT, offsetof(OpenGLMeshVertex, normal),   3, sizeof(OpenGLMeshVertex));
    m_glMeshProgram->setAttributeBuffer(colorLoc,  GL_FLOAT, offsetof(OpenGLMeshVertex, color),    3, sizeof(OpenGLMeshVertex));
    glDrawArrays(GL_TRIANGLES, 0, m_vboMeshCount);
    m_vboMesh.release();

    m_glMeshProgram->disableAttributeArray(posLoc);
    m_glMeshProgram->disableAttributeArray(normalLoc);
    m_glMeshProgram->disableAttributeArray(colorLoc);
    m_glMeshProgram->release();

    const bool hasHelpers = (m_vboHelperFrontCount > 0 || m_vboHelperXrayCount > 0)
                            && m_glFlatProgram && m_glFlatProgram->isLinked();
    if (hasHelpers) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glEnable(GL_POLYGON_OFFSET_FILL);

        m_glFlatProgram->bind();
        m_glFlatProgram->setUniformValue("u_mvp",    mvp);
        m_glFlatProgram->setUniformValue("u_offset", QVector2D(0, 0));

        const int hPosLoc   = m_glFlatProgram->attributeLocation("a_position");
        const int hColorLoc = m_glFlatProgram->attributeLocation("a_color");
        m_glFlatProgram->enableAttributeArray(hPosLoc);
        m_glFlatProgram->enableAttributeArray(hColorLoc);

        auto drawHelperVbo = [&](QOpenGLBuffer &vbo, int count,
                                 GLenum depthFunc, float polyOff,
                                 GLint stencilRef, GLenum stencilFunc) {
            if (count == 0) return;
            glDepthFunc(depthFunc);
            glPolygonOffset(polyOff, polyOff);
            glStencilFunc(stencilFunc, stencilRef, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            vbo.bind();
            m_glFlatProgram->setAttributeBuffer(hPosLoc,   GL_FLOAT, offsetof(OpenGLFlatVertex, position), 3, sizeof(OpenGLFlatVertex));
            m_glFlatProgram->setAttributeBuffer(hColorLoc, GL_FLOAT, offsetof(OpenGLFlatVertex, color),    4, sizeof(OpenGLFlatVertex));
            glDrawArrays(GL_TRIANGLES, 0, count);
            vbo.release();
        };

        drawHelperVbo(m_vboHelperFront, m_vboHelperFrontCount,
                      GL_LESS, -1.0f, 1, GL_EQUAL);
        drawHelperVbo(m_vboHelperXray, m_vboHelperXrayCount,
                      GL_GREATER, 2.0f, 0, GL_ALWAYS);

        m_glFlatProgram->disableAttributeArray(hPosLoc);
        m_glFlatProgram->disableAttributeArray(hColorLoc);
        m_glFlatProgram->release();
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(0.0f, 0.0f);
        glDisable(GL_BLEND);
    }

    if ((m_vboSelectionEdgeCount > 0 || m_vboSelectionHiddenEdgeCount > 0
         || m_vboSelectionSilhouetteCount > 0)
        && m_glLineProgram && m_glLineProgram->isLinked()) {
        glDisable(GL_STENCIL_TEST);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);

        m_glLineProgram->bind();
        m_glLineProgram->setUniformValue("u_mvp", mvp);
        m_glLineProgram->setUniformValue("u_shimmer_phase", w.m_selectionShimmerPhase);
        const float pulseSin  = 0.5f + 0.5f * sinf(w.m_selectionShimmerPhase * 0.65f);
        const float glowBoost = 1.0f + pulseSin * 0.7f;
        const int edgePosLoc = m_glLineProgram->attributeLocation("a_position");
        const int edgeColorLoc = m_glLineProgram->attributeLocation("a_color");
        m_glLineProgram->enableAttributeArray(edgePosLoc);
        m_glLineProgram->enableAttributeArray(edgeColorLoc);
        m_vboSelectionEdges.bind();
        m_glLineProgram->setAttributeBuffer(edgePosLoc, GL_FLOAT, offsetof(OpenGLLineVertex, position), 3, sizeof(OpenGLLineVertex));
        m_glLineProgram->setAttributeBuffer(edgeColorLoc, GL_FLOAT, offsetof(OpenGLLineVertex, color), 4, sizeof(OpenGLLineVertex));
        glEnable(GL_BLEND);

        m_glLineProgram->setUniformValue("u_depth_pull", 0.0f);
        glDepthFunc(GL_GREATER);
        if (m_vboSelectionHiddenEdgeCount > 0) {
            m_glLineProgram->setUniformValue("u_shimmer_base_alpha", 1.0f);
            m_glLineProgram->setUniformValue("u_shimmer_amount", 0.18f);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glLineWidth(4.5f);
            glDrawArrays(GL_LINES, 0, m_vboSelectionHiddenEdgeCount);
            glLineWidth(1.0f);
            glDrawArrays(GL_LINES, m_vboSelectionHiddenEdgeCount, m_vboSelectionHiddenEdgeCount);
        }

        m_glLineProgram->setUniformValue("u_depth_pull", 0.0003f);
        glDepthFunc(GL_LEQUAL);
        int visibleOffset = m_vboSelectionHiddenEdgeCount * 2;
        if (m_vboSelectionEdgeCount > 0) {
            m_glLineProgram->setUniformValue("u_shimmer_base_alpha", 0.28f);
            m_glLineProgram->setUniformValue("u_shimmer_amount", 0.95f);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glLineWidth(7.0f * glowBoost);
            glDrawArrays(GL_LINES, visibleOffset, m_vboSelectionEdgeCount);

            m_glLineProgram->setUniformValue("u_shimmer_base_alpha", 1.0f);
            m_glLineProgram->setUniformValue("u_shimmer_amount", 0.78f);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glLineWidth(4.5f * glowBoost);
            glDrawArrays(GL_LINES, visibleOffset, m_vboSelectionEdgeCount);
            glLineWidth(2.5f);
            glDrawArrays(GL_LINES, visibleOffset + m_vboSelectionEdgeCount, m_vboSelectionEdgeCount);
            visibleOffset += m_vboSelectionEdgeCount * 2;
        }

        if (m_vboSelectionSilhouetteCount > 0) {
            m_glLineProgram->setUniformValue("u_shimmer_base_alpha", 0.22f);
            m_glLineProgram->setUniformValue("u_shimmer_amount", 0.95f);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glDepthFunc(GL_LEQUAL);
            glLineWidth(10.0f * glowBoost);
            glDrawArrays(GL_LINES, visibleOffset, m_vboSelectionSilhouetteCount);

            m_glLineProgram->setUniformValue("u_shimmer_base_alpha", 1.0f);
            m_glLineProgram->setUniformValue("u_shimmer_amount", 0.92f);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glLineWidth(6.0f * glowBoost);
            glDrawArrays(GL_LINES, visibleOffset, m_vboSelectionSilhouetteCount);
            glLineWidth(3.2f);
            glDrawArrays(GL_LINES, visibleOffset + m_vboSelectionSilhouetteCount, m_vboSelectionSilhouetteCount);
        }
        glLineWidth(1.0f);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_glLineProgram->setUniformValue("u_depth_pull", 0.0f);
        m_vboSelectionEdges.release();
        m_glLineProgram->disableAttributeArray(edgePosLoc);
        m_glLineProgram->disableAttributeArray(edgeColorLoc);
        m_glLineProgram->release();
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
    }

    glDisable(GL_STENCIL_TEST);
}
