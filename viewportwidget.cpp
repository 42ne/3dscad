#include "viewportwidget.h"
#include "viewporthelpers.h"
#include "viewportsoftwarerenderer.h"
#include "viewportglrenderer.h"
#include "viewportaxisgizmo.h"

#include "scenedocument.h"
#include "scenemesh.h"
#include "scenetreegraphicshelpers.h"

#include <QCheckBox>
#include <QtConcurrent>
#include <QComboBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QOpenGLShaderProgram>
#include <QWheelEvent>

using namespace ViewportHelpers;




ViewportWidget::ViewportWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(500, 400);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

    m_openGLViewportCheckBox = new QCheckBox(QStringLiteral("OpenGL"), this);
    m_darkViewportCheckBox = new QCheckBox(QStringLiteral("Dark"), this);
    m_navigationOverlayCheckBox = new QCheckBox(QStringLiteral("Nav UI"), this);
    m_orthographicCheckBox = new QCheckBox(QStringLiteral("Ortho"), this);
    m_colorVariantComboBox = new QComboBox(this);
    m_lightingPresetComboBox = new QComboBox(this);
    m_darkViewportCheckBox->setChecked(m_darkViewportTheme);
    m_navigationOverlayCheckBox->setChecked(m_navigationOverlayEnabled);
    m_orthographicCheckBox->setChecked(m_orthographicProjection);
    m_colorVariantComboBox->addItems({
        QStringLiteral("Neutral"),
        QStringLiteral("Mint"),
        QStringLiteral("Clay"),
        QStringLiteral("Steel"),
        QStringLiteral("Amber")
    });
    m_lightingPresetComboBox->addItems({
        QStringLiteral("Studio"),
        QStringLiteral("Soft"),
        QStringLiteral("Side"),
        QStringLiteral("Contrast")
    });

    const QString controlStyle = QStringLiteral(
        "QCheckBox, QComboBox {"
        "  color: #eef2f6;"
        "  background: rgba(12, 16, 22, 150);"
        "  border: 1px solid rgba(230, 236, 244, 70);"
        "  border-radius: 5px;"
        "  padding: 3px 7px 3px 5px;"
        "}"
        "QComboBox::drop-down { border: 0px; width: 18px; }"
        "QCheckBox::indicator { width: 13px; height: 13px; }"
        "QCheckBox:disabled, QComboBox:disabled { color: rgba(238, 242, 246, 95); }");
    m_openGLViewportCheckBox->setStyleSheet(controlStyle);
    m_darkViewportCheckBox->setStyleSheet(controlStyle);
    m_navigationOverlayCheckBox->setStyleSheet(controlStyle);
    m_orthographicCheckBox->setStyleSheet(controlStyle);
    m_colorVariantComboBox->setStyleSheet(controlStyle);
    m_lightingPresetComboBox->setStyleSheet(controlStyle);
    m_openGLViewportCheckBox->setToolTip(QStringLiteral("Use OpenGL rendering for viewport meshes and grid."));
    m_darkViewportCheckBox->setToolTip(QStringLiteral("Switch viewport between dark and light theme."));
    m_navigationOverlayCheckBox->setToolTip(QStringLiteral("Show the viewport glass hint and selectable tree-path breadcrumb."));
    m_orthographicCheckBox->setToolTip(QStringLiteral("Switch between perspective and orthographic projection."));
    m_colorVariantComboBox->setToolTip(QStringLiteral("Choose viewport material color variant."));
    m_lightingPresetComboBox->setToolTip(QStringLiteral("Choose viewport lighting preset."));

    connect(m_openGLViewportCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        setRenderBackend(checked ? OpenGLRenderBackend : SoftwareRenderBackend);
    });
    connect(m_darkViewportCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_hasCustomAppearanceTheme) {
            clearCustomAppearanceTheme();
            emit builtInAppearanceSelected();
        }
        setDarkViewportTheme(checked);
        emit darkViewportThemeChanged(m_darkViewportTheme);
    });
    connect(m_navigationOverlayCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        m_navigationOverlayEnabled = checked;
        if (!checked)
            m_breadcrumbHits.clear();
        update();
    });
    connect(m_orthographicCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        m_orthographicProjection = checked;
        update();
    });
    connect(m_colorVariantComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_hasCustomAppearanceTheme) {
            clearCustomAppearanceTheme();
            emit builtInAppearanceSelected();
        }
        setViewportColorVariant(index);
        emit viewportColorVariantChanged(m_viewportColorVariant);
    });
    connect(m_lightingPresetComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_lightingPreset = qMax(0, index);
        update();
    });
    m_selectionShimmerTimer.setInterval(80);
    m_selectionShimmerTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_selectionShimmerTimer, &QTimer::timeout, this, [this]() {
        m_selectionShimmerPhase += 0.13f;
        if (m_selectionShimmerPhase >= 6.2831853f)
            m_selectionShimmerPhase -= 6.2831853f;
        update();
    });
    connect(&m_csgWatcher, &QFutureWatcher<CsgPreview>::finished,
            this, &ViewportWidget::onCsgPreviewReady);

    updateViewportControls();

}

void ViewportWidget::setScene(const SceneDocument *scene)
{
    m_scene = scene;
    m_shapes = scene ? &scene->shapes() : nullptr;
    invalidateCsgPreview();
    updateSelectionShimmerTimer();

    update();
}

void ViewportWidget::setShapes(const QVector<ShapeNode> *shapes)
{
    m_scene = nullptr;
    m_shapes = shapes;
    invalidateCsgPreview();
    updateSelectionShimmerTimer();

    update();
}

void ViewportWidget::setSelectedIndex(int index)
{
    m_selectedIndex = index;
    if (index >= 0)
        m_selectedGroupId = 0;
    m_vboMeshKey.clear();
    updateSelectionShimmerTimer();

    update();
}

void ViewportWidget::setSelectedGroupId(int groupId)
{
    m_selectedGroupId = groupId;
    if (groupId > 0)
        m_selectedIndex = -1;
    m_vboMeshKey.clear();
    updateSelectionShimmerTimer();

    update();
}

void ViewportWidget::setPolyhedronElementSelection(const QVector<int> &nodeIds)
{
    if (m_selectedPolyhedronElementNodeIds == nodeIds)
        return;

    m_selectedPolyhedronElementNodeIds = nodeIds;
    m_polyhedronSelectionToolHovered = false;
    update();
}

void ViewportWidget::setPolyhedronElementHover(int nodeId)
{
    if (m_hoveredPolyhedronElementNodeId == nodeId)
        return;

    m_hoveredPolyhedronElementNodeId = nodeId;
    update();
}

void ViewportWidget::setTreeTransformControlPreview(int groupId, SceneDocument::TreeNode::Operation operation, int axis)
{
    if (m_treeTransformPreviewGroupId == groupId
        && m_treeTransformPreviewOperation == operation
        && m_treeTransformPreviewAxis == axis) {
        return;
    }

    m_treeTransformPreviewGroupId = groupId;
    m_treeTransformPreviewOperation = operation;
    m_treeTransformPreviewAxis = axis;
    update();
}

void ViewportWidget::setTreeShapeParameterPreview(int shapeId, int parameter)
{
    if (m_treeShapePreviewShapeId == shapeId
        && m_treeShapePreviewParameter == parameter) {
        return;
    }

    m_treeShapePreviewShapeId = shapeId;
    m_treeShapePreviewParameter = parameter;
    update();
}

void ViewportWidget::setRenderBackend(RenderBackend backend)
{
    if (backend == OpenGLRenderBackend && !canUseOpenGLRenderBackend())
        backend = SoftwareRenderBackend;

    if (m_renderBackend == backend) {
        updateViewportControls();
        updateSelectionShimmerTimer();

        return;
    }

    m_renderBackend = backend;
    updateViewportControls();
    updateSelectionShimmerTimer();

    update();
}

void ViewportWidget::setDarkViewportTheme(bool darkTheme)
{
    if (m_darkViewportTheme == darkTheme) {
        updateViewportControls();
        return;
    }

    m_darkViewportTheme = darkTheme;
    updateViewportControls();
    update();
}

void ViewportWidget::setViewportColorVariant(int variant)
{
    constexpr int ColorVariantCount = 5;
    const int clamped = qBound(0, variant, ColorVariantCount - 1);
    if (m_viewportColorVariant == clamped) {
        updateViewportControls();
        return;
    }

    m_viewportColorVariant = clamped;
    updateViewportControls();
    update();
}

void ViewportWidget::setCustomAppearanceTheme(const ViewportAppearanceTheme &theme)
{
    m_customAppearanceTheme = theme;
    m_hasCustomAppearanceTheme = true;
    m_vboMeshKey.clear();
    update();
}

void ViewportWidget::clearCustomAppearanceTheme()
{
    if (!m_hasCustomAppearanceTheme)
        return;
    m_hasCustomAppearanceTheme = false;
    m_vboMeshKey.clear();
    update();
}

ViewportWidget::RenderBackend ViewportWidget::renderBackend() const
{
    return m_renderBackend;
}

QString ViewportWidget::renderBackendName() const
{
    if (m_renderBackend == OpenGLRenderBackend)
        return "OpenGL";

    return "Software";
}

bool ViewportWidget::isOpenGLRenderBackendAvailable() const
{
    return canUseOpenGLRenderBackend();
}

void ViewportWidget::startAsyncCsgCompute()
{
    // One compute at a time; if already running, onCsgPreviewReady() will
    // re-check m_csgPreviewDirty and re-enter if the scene changed again.
    if (m_csgComputing || !m_shapes)
        return;

    const uint fingerprint = m_scene
                                 ? sceneFingerprint(*m_scene)
                                 : shapesFingerprint(*m_shapes);

    // Nothing actually changed — skip redundant work.
    if (!m_csgPreviewDirty && fingerprint == m_cachedCsgFingerprint)
        return;

    m_csgComputing = true;
    m_csgPreviewDirty = false;
    m_pendingCsgFingerprint = fingerprint;

    if (m_scene) {
        // Snapshot the scene so the worker thread has its own independent copy.
        const SceneDocument::Snapshot snap = m_scene->snapshot();
        m_csgWatcher.setFuture(QtConcurrent::run([snap]() -> CsgPreview {
            SceneDocument doc;
            doc.restoreSnapshot(snap);
            return buildCsgPreview(doc);
        }));
    } else {
        const QVector<ShapeNode> shapes = *m_shapes;
        m_csgWatcher.setFuture(QtConcurrent::run([shapes]() -> CsgPreview {
            return buildCsgPreview(shapes);
        }));
    }
}

void ViewportWidget::onCsgPreviewReady()
{
    const CsgPreview completedPreview = m_csgWatcher.result();
    const uint completedFingerprint = m_pendingCsgFingerprint;
    m_csgComputing = false;

    // Do not flash a superseded transform during an interactive edit.
    if (m_csgPreviewDirty) {
        if (!m_draggingShape && !m_draggingGroup)
            startAsyncCsgCompute();
        update();
        return;
    }

    m_cachedCsgPreview = completedPreview;
    m_cachedCsgFingerprint = completedFingerprint;

    update();
    emit csgPreviewReady();
}

void ViewportWidget::invalidateCsgPreview()
{
    m_csgPreviewDirty = true;
    if (m_draggingShape || m_draggingGroup)
        return;

    startAsyncCsgCompute();
}

QString ViewportWidget::csgStatusText()
{
    if (!m_shapes)
        return QStringLiteral("CSG preview: empty");

    if (m_csgComputing || m_csgPreviewDirty)
        return QStringLiteral("CSG preview: computing…");

    return m_cachedCsgPreview.statusText;
}

// ── Camera matrix helpers ──────────────────────────────────────────────────────
// buildViewMatrix: maps world-space points to camera-space using the same
// yaw-then-pitch orbit as toCameraPoint(). Verified to produce identical results.
void ViewportWidget::initializeGL()
{
    initializeOpenGLFunctions();
    const QColor background = viewportBackgroundColor(m_darkViewportTheme,
        m_hasCustomAppearanceTheme ? &m_customAppearanceTheme : nullptr);
    glClearColor(background.redF(), background.greenF(), background.blueF(), 1.0f);

    m_glMeshProgram = new QOpenGLShaderProgram(this);
    m_glMeshProgram->addShaderFromSourceCode(
        QOpenGLShader::Vertex,
        "attribute vec3 a_position;\n"   // world space
        "attribute vec3 a_normal;\n"     // world space
        "attribute vec3 a_color;\n"
        "uniform mat4 u_mvp;\n"
        "varying vec3 v_normal;\n"       // world space — passed through unchanged
        "varying vec3 v_world_pos;\n"    // world space — for view-direction in fragment
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
        "uniform vec3 u_camera_pos;\n"   // world space camera position
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
        "    float skyMix = clamp(reflectedView.z * 0.55 + 0.5, 0.0, 1.0);\n"  // z=up in world
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

    m_glLineProgram = new QOpenGLShaderProgram(this);
    m_glLineProgram->addShaderFromSourceCode(
        QOpenGLShader::Vertex,
        "attribute vec3 a_position;\n"  // world space
        "attribute vec4 a_color;\n"
        "uniform mat4 u_mvp;\n"
        "uniform float u_shimmer_phase;\n"
        "uniform float u_shimmer_amount;\n"
        "uniform float u_shimmer_base_alpha;\n"   // 1.0 = normal pass, <1 = bloom pass
        "uniform float u_depth_pull;\n"           // pull lines toward camera to beat GL_LEQUAL
        "varying vec4 v_color;\n"
        "void main() {\n"
        "    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
        "    gl_Position.z -= u_depth_pull * gl_Position.w;\n"
        "    float raw  = 0.5 + 0.5 * sin(dot(a_position, vec3(0.23, 0.14, 0.19)) + u_shimmer_phase);\n"
        "    float wave = raw * raw;\n"    // square → sharper peaks
        "    float s    = wave * u_shimmer_amount;\n"
        "    vec3 brightened = a_color.rgb + (vec3(1.0) - a_color.rgb) * s;\n"
        // Alpha stays between 55 % (trough) and ~100 % (peak) of the base value.
        // The previous (1 - amount*0.75 + s*0.75) formula dropped to 31 % at the
        // trough — most edge positions were nearly invisible against dark backgrounds.
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

    m_glFlatProgram = new QOpenGLShaderProgram(this);
    m_glFlatProgram->addShaderFromSourceCode(
        QOpenGLShader::Vertex,
        "attribute vec3 a_position;\n"  // world space
        "attribute vec4 a_color;\n"
        "uniform mat4 u_mvp;\n"
        "uniform vec2 u_offset;\n"      // XY world-space offset (for shadow multi-sample)
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

    // ── Grid VBO (built once, never changes) ────────────────────────────────────
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
        const QColor minor(128, 128, 128); // placeholder; real color set at draw time via clear+rebuild if theme changes
        for (int i = -120; i <= 120; i += 20) {
            appendLine({-120, float(i), 0}, {120, float(i), 0}, minor);
            appendLine({float(i), -120, 0}, {float(i), 120, 0}, minor);
        }
        appendLine({-130, 0, 0}, {130, 0, 0}, QColor(210, 80, 80));
        appendLine({0, -130, 0}, {0, 130, 0}, QColor(80, 180, 110));
        appendLine({0, 0, 0},    {0, 0, 90},  QColor(90, 150, 230));
        m_vboGrid.bind();
        m_vboGrid.allocate(gridVerts.constData(), gridVerts.size() * int(sizeof(OpenGLLineVertex)));
        m_vboGrid.release();
        m_vboGridCount = gridVerts.size();
    }

    // ── Mesh / helper / shadow VBOs — created here, filled on first draw ───────
    m_vboMesh.create();        m_vboMesh.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_vboSelectionEdges.create(); m_vboSelectionEdges.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_vboHelperFront.create(); m_vboHelperFront.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_vboHelperXray.create();  m_vboHelperXray.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_vboShadow.create();      m_vboShadow.setUsagePattern(QOpenGLBuffer::DynamicDraw);
}

void ViewportWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void ViewportWidget::resizeEvent(QResizeEvent *event)
{
    QOpenGLWidget::resizeEvent(event);
    updateViewportControls();
}

void ViewportWidget::paintGL()
{
    const QColor background = viewportBackgroundColor(m_darkViewportTheme,
        m_hasCustomAppearanceTheme ? &m_customAppearanceTheme : nullptr);
    glClearColor(background.redF(), background.greenF(), background.blueF(), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    const bool useOpenGLPreview = m_renderBackend == OpenGLRenderBackend && canUseOpenGLRenderBackend();
    if (useOpenGLPreview) {
        ViewportGLRenderer glRenderer;
        glRenderer.renderGrid(*this);
        glRenderer.renderContactShadows(*this);
        glRenderer.renderPreview(*this);
    }

    glDisable(GL_DEPTH_TEST);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    paintSoftware(painter, !useOpenGLPreview);

    painter.end();
}

// ── Static thumbnail renderer ────────────────────────────────────────────────
// Builds a rasterised preview image of a scene without any UI chrome.
// Designed to be called from a background thread (all state is local).
QImage ViewportWidget::renderThumbnail(const SceneDocument &scene, QSize thumbnailSize,
                                       const QColor &bgColor)
{
    QImage image(thumbnailSize, QImage::Format_ARGB32_Premultiplied);
    if (bgColor.alpha() == 0) {
        image.fill(Qt::transparent);
    } else {
        QPainter bgPainter(&image);
        QLinearGradient bg(QPointF(0.0, 0.0), QPointF(0.0, thumbnailSize.height()));
        bg.setColorAt(0.0, bgColor.lighter(145));
        bg.setColorAt(0.55, bgColor);
        bg.setColorAt(1.0, bgColor.darker(132));
        bgPainter.fillRect(image.rect(), bg);
        // Removed: an off-centre white ellipse (alpha 18) was added here as a
        // "studio light spot", but it produced a visible grey oval on dark
        // backgrounds that looked like a rendering artefact on many models.
    }

    const CsgPreview preview = buildCsgPreview(scene);
    if (preview.items.isEmpty())
        return image;

    // Compute bounding box from non-helper mesh items.
    QVector3D bbMin( 1e9f,  1e9f,  1e9f);
    QVector3D bbMax(-1e9f, -1e9f, -1e9f);
    bool hasMesh = false;
    for (const CsgRenderItem &item : preview.items) {
        if (item.helper) continue;
        for (const MeshTriangle &tri : item.mesh.triangles) {
            for (const QVector3D *v : {&tri.a, &tri.b, &tri.c}) {
                bbMin.setX(qMin(bbMin.x(), v->x()));
                bbMin.setY(qMin(bbMin.y(), v->y()));
                bbMin.setZ(qMin(bbMin.z(), v->z()));
                bbMax.setX(qMax(bbMax.x(), v->x()));
                bbMax.setY(qMax(bbMax.y(), v->y()));
                bbMax.setZ(qMax(bbMax.z(), v->z()));
                hasMesh = true;
            }
        }
    }
    if (!hasMesh)
        return image;

    const QVector3D cameraTarget = (bbMin + bbMax) * 0.5f;
    const float extent = (bbMax - bbMin).length();
    // Fill most of the shorter side so the shape reads as a solid object in small cards.
    // focalLength 420 matches projectWorldPoint's hardcoded value.
    const float shortSide = static_cast<float>(qMin(thumbnailSize.width(), thumbnailSize.height()));
    const float cameraDistance = qMax(extent * 420.0f / (shortSide * 0.94f), 20.0f);

    const float yaw   = -38.0f;
    const float pitch = -26.0f;
    const QVector<SceneLight> lights = thumbnailLights();

    auto project = [&](const QVector3D &world) {
        return projectWorldPoint(world, thumbnailSize, yaw, pitch, cameraDistance, cameraTarget);
    };

    QVector<Triangle2D> triangles;
    triangles.reserve(512);
    for (const CsgRenderItem &item : preview.items) {
        if (item.helper) continue;
        const QColor baseColor = (item.booleanMode == ShapeNode::Subtract) ? QColor(75,  90, 195)
                               : (item.booleanMode == ShapeNode::Intersect) ? QColor(155, 95, 215)
                               :                                               QColor(82, 138, 212);
        for (const MeshTriangle &mt : item.mesh.triangles) {
            const ProjectedPoint a = project(mt.a);
            const ProjectedPoint b = project(mt.b);
            const ProjectedPoint c = project(mt.c);
            Triangle2D tri;
            tri.a      = a.point;  tri.depthA = a.depth;
            tri.b      = b.point;  tri.depthB = b.depth;
            tri.c      = c.point;  tri.depthC = c.depth;
            tri.color  = thumbnailLitColor(baseColor.lighter(qMax(mt.shade, 112)), mt.normal, lights);
            tri.shapeIndex = item.shapeIndex;
            triangles.append(tri);
        }
    }

    QPainter painter(&image);
    QImage   rasterBuffer; // depth-rasterised triangle pixels (transparent bg)
    QVector<float> depthBuffer;
    drawTrianglesWithDepth(&painter, triangles, thumbnailSize,
                           nullptr, &depthBuffer, &rasterBuffer);
    painter.end();

    // -------------------------------------------------------------------
    // Post-process: silhouette outline.
    // Walk the rasterBuffer (transparent = background, opaque = geometry).
    // Any geometry pixel adjacent to a transparent pixel is a silhouette
    // edge — darken it in the final image to create a crisp 1-px outline.
    // -------------------------------------------------------------------
    {
        const int W = thumbnailSize.width();
        const int H = thumbnailSize.height();
        // Check 2-pixel radius so the outline reads clearly at display size.
        for (int y = 0; y < H; ++y) {
            QRgb *mainRow = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < W; ++x) {
                // Skip background pixels.
                if (qAlpha(reinterpret_cast<const QRgb *>(rasterBuffer.constScanLine(y))[x]) == 0)
                    continue;

                // Check 4-connected neighbours for transparency.
                bool edge =
                    (x == 0   || qAlpha(reinterpret_cast<const QRgb *>(rasterBuffer.constScanLine(y))[x - 1]) == 0) ||
                    (x == W-1 || qAlpha(reinterpret_cast<const QRgb *>(rasterBuffer.constScanLine(y))[x + 1]) == 0) ||
                    (y == 0   || qAlpha(reinterpret_cast<const QRgb *>(rasterBuffer.constScanLine(y - 1))[x]) == 0) ||
                    (y == H-1 || qAlpha(reinterpret_cast<const QRgb *>(rasterBuffer.constScanLine(y + 1))[x]) == 0);

                if (edge) {
                    const QRgb px = mainRow[x];
                    // A very light silhouette treatment keeps edges readable
                    // without making small primitives look like contour icons.
                    mainRow[x] = qRgba(qBound(0, int(qRed(px)   * 0.78f), 255),
                                       qBound(0, int(qGreen(px) * 0.78f), 255),
                                       qBound(0, int(qBlue(px)  * 0.78f), 255),
                                       qAlpha(px));
                }
            }
        }
    }

    return image;
}

void ViewportWidget::paintSoftware(QPainter &painter, bool drawSceneMeshes)
{
    ViewportSoftwareRenderer renderer;

    ViewportSoftwareRenderer::Context ctx;
    ctx.cameraYaw = m_cameraYaw;
    ctx.cameraPitch = m_cameraPitch;
    ctx.cameraDistance = m_cameraDistance;
    ctx.cameraTarget = m_cameraTarget;
    ctx.orthographicProjection = m_orthographicProjection;
    ctx.viewportSize = size();
    ctx.darkViewportTheme = m_darkViewportTheme;
    ctx.viewportColorVariant = m_viewportColorVariant;
    ctx.hasCustomAppearanceTheme = m_hasCustomAppearanceTheme;
    ctx.customAppearanceTheme = m_customAppearanceTheme;
    ctx.lightingPreset = m_lightingPreset;
    ctx.scene = m_scene;
    ctx.shapes = m_shapes;
    ctx.selectedIndex = m_selectedIndex;
    ctx.selectedGroupId = m_selectedGroupId;
    ctx.draggingShape = m_draggingShape;
    ctx.draggingGroup = m_draggingGroup;
    ctx.cachedCsgPreview = &m_cachedCsgPreview;
    ctx.csgComputing = m_csgComputing;
    ctx.selectionShimmerPhase = m_selectionShimmerPhase;
    ctx.pickBuffer = &m_pickBuffer;
    ctx.depthBuffer = &m_depthBuffer;
    ctx.renderImage = &m_renderImage;
    ctx.pickBufferSize = &m_pickBufferSize;

    const QString csgStatus = renderer.renderScene(painter, ctx, drawSceneMeshes);

    ViewportAxisGizmo gizmo;
    gizmo.drawPolyhedronElementSelectionOverlay(painter, *this);
    gizmo.drawPolyhedronSelectionMoveTool(painter, *this);

    if (m_navigationOverlayEnabled) {
        drawViewportHintOverlay(painter, csgStatus);
        drawSelectionBreadcrumb(painter);
    } else {
        m_breadcrumbHits.clear();
    }
    drawTreeTransformControlPreview(painter);
    drawTreeShapeParameterPreview(painter);
    gizmo.drawAxisGizmo(painter, *this);
}






void ViewportWidget::drawViewportHintOverlay(QPainter &painter, const QString &csgStatus) const
{
    const QString help = QStringLiteral("3D viewport: drag to orbit, wheel to zoom; select a transform breadcrumb to manipulate it.");
    const QString status = QStringLiteral("%1 | renderer: %2").arg(csgStatus, renderBackendName());

    QFont font = painter.font();
    font.setPointSizeF(qMax<qreal>(8.0, font.pointSizeF() - 0.2));
    const QFontMetricsF metrics(font);
    const qreal preferredWidth = qMax(metrics.horizontalAdvance(help), metrics.horizontalAdvance(status)) + 24.0;
    const qreal panelWidth = qBound<qreal>(260.0, preferredWidth, qMax<qreal>(260.0, width() - 128.0));
    const QRectF panelRect(10.0, 10.0, panelWidth, metrics.height() * 2.0 + 20.0);
    const int textWidth = qFloor(panelRect.width() - 24.0);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    drawGlassPanel(&painter, panelRect, m_darkViewportTheme,
                   m_hasCustomAppearanceTheme ? &m_customAppearanceTheme : nullptr);
    painter.setFont(font);
    painter.setPen(m_hasCustomAppearanceTheme ? m_customAppearanceTheme.text
                                              : m_darkViewportTheme ? QColor(232, 242, 255) : QColor(35, 47, 62));
    painter.drawText(QPointF(panelRect.left() + 12.0, panelRect.top() + 10.0 + metrics.ascent()),
                     metrics.elidedText(help, Qt::ElideRight, textWidth));
    painter.setPen(m_hasCustomAppearanceTheme ? m_customAppearanceTheme.mutedText
                                              : m_darkViewportTheme ? QColor(184, 205, 228) : QColor(74, 94, 115));
    painter.drawText(QPointF(panelRect.left() + 12.0, panelRect.top() + 10.0 + metrics.height() + metrics.ascent()),
                     metrics.elidedText(status, Qt::ElideRight, textWidth));
    painter.restore();
}

void ViewportWidget::drawSelectionBreadcrumb(QPainter &painter)
{
    m_breadcrumbHits.clear();
    if (!m_scene)
        return;

    int selectedNodeId = m_selectedGroupId;
    if (selectedNodeId <= 0 && m_shapes && m_selectedIndex >= 0 && m_selectedIndex < m_shapes->size())
        selectedNodeId = primitiveTreeNodeIdForShape(m_scene->treeRoot(), m_shapes->at(m_selectedIndex).id);
    if (selectedNodeId <= 0)
        return;

    QVector<const SceneDocument::TreeNode *> fullPath;
    if (!collectTreeNodePath(m_scene->treeRoot(), selectedNodeId, &fullPath))
        return;

    QVector<const SceneDocument::TreeNode *> path;
    for (const SceneDocument::TreeNode *node : fullPath) {
        if (!node)
            continue;
        if (node->type == SceneDocument::TreeNode::Group
            && node->operation == SceneDocument::TreeNode::Scene) {
            continue;
        }
        if (node->id != m_scene->treeRoot().id)
            path.append(node);
    }
    if (path.isEmpty())
        return;

    QFont font = painter.font();
    font.setBold(true);
    font.setPointSizeF(qMax<qreal>(8.0, font.pointSizeF() - 0.2));
    const QFontMetricsF metrics(font);
    const qreal panelTop = 106.0;
    const qreal panelHeight = metrics.height() + 22.0;
    const qreal panelWidth = qMax<qreal>(140.0, width() - 112.0);
    const QRectF panelRect(10.0, panelTop, panelWidth, panelHeight);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    drawGlassPanel(&painter, panelRect, m_darkViewportTheme,
                   m_hasCustomAppearanceTheme ? &m_customAppearanceTheme : nullptr);
    painter.setFont(font);

    qreal x = panelRect.left() + 10.0;
    const qreal chipTop = panelRect.top() + 7.0;
    const qreal chipHeight = metrics.height() + 8.0;
    const qreal rightLimit = panelRect.right() - 10.0;
    for (int i = 0; i < path.size(); ++i) {
        const SceneDocument::TreeNode *node = path.at(i);
        QString label;
        if (node->type == SceneDocument::TreeNode::Primitive) {
            const ShapeNode *shape = m_scene->shapeById(node->shapeId);
            label = shape ? SceneTreeGraphics::toolNameForPrimitiveType(shape->type)
                          : QStringLiteral("primitive");
        } else if (node->type == SceneDocument::TreeNode::ModuleCall) {
            label = node->moduleName.isEmpty() ? QStringLiteral("call") : node->moduleName;
        } else if (node->type == SceneDocument::TreeNode::Variable) {
            label = node->variableName;
        } else {
            label = SceneTreeGraphics::labelForOperation(node->operation);
        }

        const qreal chipWidth = metrics.horizontalAdvance(label) + 18.0;
        const qreal separatorWidth = i > 0 ? metrics.horizontalAdvance(QStringLiteral(">")) + 12.0 : 0.0;
        if (x + separatorWidth + chipWidth > rightLimit) {
            painter.setPen(QColor(184, 205, 228));
            painter.drawText(QRectF(x, chipTop, rightLimit - x, chipHeight), Qt::AlignVCenter, QStringLiteral("..."));
            break;
        }

        if (i > 0) {
            painter.setPen(QColor(140, 167, 192));
            painter.drawText(QRectF(x, chipTop, separatorWidth, chipHeight), Qt::AlignCenter, QStringLiteral(">"));
            x += separatorWidth;
        }

        const QRectF chipRect(x, chipTop, chipWidth, chipHeight);
        const bool selected = node->id == selectedNodeId;
        const bool manipulable = node->type == SceneDocument::TreeNode::Group
                                 && (node->operation == SceneDocument::TreeNode::Translate
                                     || node->operation == SceneDocument::TreeNode::Rotate);
        painter.setPen(QPen(selected ? QColor(255, 203, 87)
                                     : manipulable ? QColor(112, 205, 238)
                                                   : QColor(115, 145, 174),
                              selected ? 1.4 : 1.0));
        painter.setBrush(selected ? QColor(255, 193, 72, 42)
                                  : manipulable ? QColor(54, 124, 162, 46)
                                                : QColor(25, 35, 49, 90));
        painter.drawRoundedRect(chipRect, 5.0, 5.0);
        painter.setPen(selected ? QColor(255, 231, 166) : QColor(220, 233, 246));
        painter.drawText(chipRect, Qt::AlignCenter, label);
        m_breadcrumbHits.append({node->id, chipRect});
        x = chipRect.right() + 5.0;
    }

    painter.restore();
}

void ViewportWidget::drawTreeTransformControlPreview(QPainter &painter) const
{
    if (!m_scene || m_treeTransformPreviewGroupId <= 0 || m_treeTransformPreviewAxis < 0)
        return;

    const SceneDocument::TreeNode *group = m_scene->treeNodeById(m_treeTransformPreviewGroupId);
    if (!group)
        return;

    const bool translatePreview = m_treeTransformPreviewOperation == SceneDocument::TreeNode::Translate;
    const bool rotatePreview = m_treeTransformPreviewOperation == SceneDocument::TreeNode::Rotate;
    const bool scalePreview = m_treeTransformPreviewOperation == SceneDocument::TreeNode::Scale;
    if (!translatePreview && !rotatePreview && !scalePreview)
        return;

    const QVector3D origin = transformOriginForGroup(m_treeTransformPreviewGroupId);
    QVector3D localAxis;
    QColor accent;
    if (m_treeTransformPreviewAxis == 0) {
        localAxis = QVector3D(1.0f, 0.0f, 0.0f);
        accent = QColor(255, 95, 120);
    } else if (m_treeTransformPreviewAxis == 1) {
        localAxis = QVector3D(0.0f, 1.0f, 0.0f);
        accent = QColor(105, 245, 145);
    } else {
        localAxis = QVector3D(0.0f, 0.0f, 1.0f);
        accent = QColor(105, 180, 255);
    }

    auto project = [&](const QVector3D &world) {
        return projectWorldPoint(world, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance, m_cameraTarget, m_orthographicProjection).point;
    };
    auto axisName = [&]() {
        if (m_treeTransformPreviewAxis == 0)
            return QStringLiteral("X");
        if (m_treeTransformPreviewAxis == 1)
            return QStringLiteral("Y");
        return QStringLiteral("Z");
    };
    auto axisValue = [&]() {
        const QVector3D values = translatePreview
                                     ? group->position
                                     : rotatePreview
                                           ? group->rotation
                                           : group->scale;
        if (m_treeTransformPreviewAxis == 0)
            return values.x();
        if (m_treeTransformPreviewAxis == 1)
            return values.y();
        return values.z();
    };
    const QString valueLabel = QStringLiteral("%1%2 %3")
                                   .arg(scalePreview ? QStringLiteral("S") : rotatePreview ? QStringLiteral("R") : QStringLiteral("T"),
                                        axisName(),
                                        formatPreviewValue(axisValue(), scalePreview ? 1 : -1));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (translatePreview || scalePreview) {
        QVector3D worldAxis = worldAxisVectorForGroup(m_treeTransformPreviewGroupId, localAxis);
        if (worldAxis.lengthSquared() <= 0.0001f)
            worldAxis = localAxis;
        worldAxis.normalize();

        const QPointF center = project(origin);
        const float axisLength = scalePreview ? 42.0f : 34.0f;
        const QPointF negative = project(origin - worldAxis * axisLength);
        const QPointF positive = project(origin + worldAxis * axisLength);
        drawHaloLine(&painter, negative, positive, accent, 4.0);
        drawArrowHead(&painter, center, positive, accent);
        drawArrowHead(&painter, center, negative, accent);
        drawDirectionLabel(&painter, positive, center, "+");
        drawDirectionLabel(&painter, negative, center, "-");
        drawValueLabel(&painter, center + QPointF(0.0, -28.0), valueLabel);
    } else {
        QVector<QPointF> arcPoints;
        const float radius = 48.0f;
        const QVector<SceneDocument::TreeNode> stack = parentGroupStackForGroup(m_treeTransformPreviewGroupId);
        for (int step = -7; step <= 7; ++step) {
            const float angle = qDegreesToRadians(step * 8.0f);
            const float c = qCos(angle) * radius;
            const float s = qSin(angle) * radius;
            QVector3D localPoint;
            if (m_treeTransformPreviewAxis == 0)
                localPoint = QVector3D(0.0f, c, s);
            else if (m_treeTransformPreviewAxis == 1)
                localPoint = QVector3D(c, 0.0f, s);
            else
                localPoint = QVector3D(c, s, 0.0f);

            arcPoints.append(project(origin + transformVectorByGroupStack(localPoint, stack)));
        }

        if (arcPoints.size() >= 2) {
            drawHaloPolyline(&painter, QPolygonF(arcPoints), accent, 3.0);
            drawArrowHead(&painter, arcPoints[1], arcPoints.first(), accent);
            drawArrowHead(&painter, arcPoints[arcPoints.size() - 2], arcPoints.last(), accent);
            const QPointF center = project(origin);
            drawDirectionLabel(&painter, arcPoints.last(), center, "+");
            drawDirectionLabel(&painter, arcPoints.first(), center, "-");
            drawValueLabel(&painter, center + QPointF(0.0, -62.0), valueLabel);
        }
    }

    painter.restore();
}

void ViewportWidget::drawTreeShapeParameterPreview(QPainter &painter) const
{
    if (!m_shapes || m_treeShapePreviewShapeId <= 0 || m_treeShapePreviewParameter < 0)
        return;

    const ShapeNode *shape = nullptr;
    for (const ShapeNode &candidate : *m_shapes) {
        if (candidate.id == m_treeShapePreviewShapeId) {
            shape = &candidate;
            break;
        }
    }

    if (!shape)
        return;

    auto project = [&](const QVector3D &world) {
        return projectWorldPoint(world, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance, m_cameraTarget, m_orthographicProjection).point;
    };

    QVector<SceneDocument::TreeNode> parentGroups;
    if (m_scene)
        collectParentGroupStackForShape(m_scene->treeRoot(), shape->id, &parentGroups);

    auto transformed = [&](const QVector3D &local) {
        const QVector3D shapeSpace = rotatePoint(local, shape->rotation) + shape->position;
        return transformPointByGroupStack(shapeSpace, parentGroups);
    };

    auto drawDimension = [&](const QVector3D &localAxis,
                             float halfLength,
                             float sideOffset,
                             const QColor &accent,
                             const QString &label,
                             float value) {
        if (halfLength <= 0.001f)
            return;

        QVector3D localSide;
        if (qAbs(localAxis.z()) > 0.5f)
            localSide = QVector3D(1.0f, 0.0f, 0.0f);
        else
            localSide = QVector3D(0.0f, 0.0f, 1.0f);

        const QVector3D negative = transformed(-localAxis * halfLength + localSide * sideOffset);
        const QVector3D positive = transformed(localAxis * halfLength + localSide * sideOffset);
        const QVector3D center = transformed(localSide * sideOffset);
        const QPointF negativePoint = project(negative);
        const QPointF positivePoint = project(positive);
        const QPointF centerPoint = project(center);

        drawHaloLine(&painter, negativePoint, positivePoint, accent, 2.6);
        drawArrowHead(&painter, centerPoint, positivePoint, accent, 12.0f, 5.0f, 2.0);
        drawArrowHead(&painter, centerPoint, negativePoint, accent, 12.0f, 5.0f, 2.0);
        drawDirectionLabel(&painter, positivePoint, centerPoint, "+");
        drawDirectionLabel(&painter, negativePoint, centerPoint, "-");
        drawValueLabel(&painter, centerPoint + QPointF(0.0, -24.0), QStringLiteral("%1 %2").arg(label, formatPreviewValue(value)));
    };

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (shape->type == ShapeNode::Cube) {
        QVector3D localAxis;
        QColor accent;
        float halfLength = 0.0f;
        if (m_treeShapePreviewParameter == 0) {
            localAxis = QVector3D(1.0f, 0.0f, 0.0f);
            accent = QColor(255, 95, 120);
            halfLength = shape->size.x() * 0.5f;
            drawDimension(localAxis, halfLength, 9.0f, accent, QStringLiteral("X"), shape->size.x());
        } else if (m_treeShapePreviewParameter == 1) {
            localAxis = QVector3D(0.0f, 1.0f, 0.0f);
            accent = QColor(105, 245, 145);
            halfLength = shape->size.y() * 0.5f;
            drawDimension(localAxis, halfLength, 9.0f, accent, QStringLiteral("Y"), shape->size.y());
        } else if (m_treeShapePreviewParameter == 2) {
            localAxis = QVector3D(0.0f, 0.0f, 1.0f);
            accent = QColor(105, 180, 255);
            halfLength = shape->size.z() * 0.5f;
            drawDimension(localAxis, halfLength, 9.0f, accent, QStringLiteral("Z"), shape->size.z());
        }
    } else if (shape->type == ShapeNode::Cylinder && m_treeShapePreviewParameter == 1) {
        drawDimension(QVector3D(0.0f, 0.0f, 1.0f), shape->height * 0.5f, shape->radius + 8.0f, QColor(105, 180, 255), QStringLiteral("H"), shape->height);
    } else {
        const float radius = shape->radius;
        if (radius > 0.001f) {
            const QColor accent(255, 190, 85);
            QVector<QPointF> circlePoints;
            for (int step = 0; step <= 48; ++step) {
                const float angle = qDegreesToRadians(step * 360.0f / 48.0f);
                circlePoints.append(project(transformed(QVector3D(qCos(angle) * radius, qSin(angle) * radius, 0.0f))));
            }

            drawHaloPolyline(&painter, QPolygonF(circlePoints), accent, 2.4);

            const QVector3D center = transformed(QVector3D());
            const QVector3D edge = transformed(QVector3D(radius, 0.0f, 0.0f));
            const QVector3D inward = transformed(QVector3D(radius * 0.45f, 0.0f, 0.0f));
            const QPointF centerPoint = project(center);
            const QPointF edgePoint = project(edge);
            const QPointF inwardPoint = project(inward);
            drawHaloLine(&painter, centerPoint, edgePoint, accent, 2.6);
            drawArrowHead(&painter, inwardPoint, edgePoint, accent, 12.0f, 5.0f, 2.0);
            drawArrowHead(&painter, inwardPoint, centerPoint, accent, 12.0f, 5.0f, 2.0);
            drawDirectionLabel(&painter, edgePoint, inwardPoint, "+");
            drawDirectionLabel(&painter, centerPoint, inwardPoint, "-");
            drawValueLabel(&painter, inwardPoint + QPointF(0.0, -24.0), QStringLiteral("R %1").arg(formatPreviewValue(radius)));
        }
    }

    painter.restore();
}

bool ViewportWidget::canUseOpenGLRenderBackend() const
{
    return true;
}

void ViewportWidget::updateSelectionShimmerTimer()
{
    const bool hasSelection = m_shapes && (m_selectedIndex >= 0 || m_selectedGroupId > 0);
    const bool animate = hasSelection
                         && !m_draggingGroup
                         && m_renderBackend == OpenGLRenderBackend;
    if (animate) {
        if (!m_selectionShimmerTimer.isActive())
            m_selectionShimmerTimer.start();
    } else {
        m_selectionShimmerTimer.stop();
        m_selectionShimmerPhase = 0.0f;
    }
}

void ViewportWidget::updateViewportControls()
{
    if (!m_openGLViewportCheckBox || !m_darkViewportCheckBox || !m_navigationOverlayCheckBox
        || !m_orthographicCheckBox || !m_colorVariantComboBox || !m_lightingPresetComboBox) {
        return;
    }

    const bool openGLAvailable = canUseOpenGLRenderBackend();
    m_openGLViewportCheckBox->setEnabled(openGLAvailable);
    m_openGLViewportCheckBox->blockSignals(true);
    m_openGLViewportCheckBox->setChecked(m_renderBackend == OpenGLRenderBackend);
    m_openGLViewportCheckBox->blockSignals(false);

    m_darkViewportCheckBox->blockSignals(true);
    m_darkViewportCheckBox->setChecked(m_darkViewportTheme);
    m_darkViewportCheckBox->blockSignals(false);

    m_navigationOverlayCheckBox->blockSignals(true);
    m_navigationOverlayCheckBox->setChecked(m_navigationOverlayEnabled);
    m_navigationOverlayCheckBox->blockSignals(false);

    m_orthographicCheckBox->blockSignals(true);
    m_orthographicCheckBox->setChecked(m_orthographicProjection);
    m_orthographicCheckBox->blockSignals(false);

    m_colorVariantComboBox->blockSignals(true);
    m_colorVariantComboBox->setCurrentIndex(qBound(0, m_viewportColorVariant, m_colorVariantComboBox->count() - 1));
    m_colorVariantComboBox->blockSignals(false);
    m_lightingPresetComboBox->blockSignals(true);
    m_lightingPresetComboBox->setCurrentIndex(qBound(0, m_lightingPreset, m_lightingPresetComboBox->count() - 1));
    m_lightingPresetComboBox->blockSignals(false);

    const QSize openGLSize = m_openGLViewportCheckBox->sizeHint();
    const QSize darkSize = m_darkViewportCheckBox->sizeHint();
    const QSize navigationSize = m_navigationOverlayCheckBox->sizeHint();
    const QSize orthoSize = m_orthographicCheckBox->sizeHint();
    const QSize colorSize = m_colorVariantComboBox->sizeHint();
    const QSize lightingSize = m_lightingPresetComboBox->sizeHint();
    const int margin = 10;
    const int gap = 6;
    const int y = 70;
    int x = margin;
    m_openGLViewportCheckBox->setGeometry(x, y, openGLSize.width() + 10, openGLSize.height() + 2);
    x += m_openGLViewportCheckBox->width() + gap;
    m_darkViewportCheckBox->setGeometry(x, y, darkSize.width() + 10, darkSize.height() + 2);
    x += m_darkViewportCheckBox->width() + gap;
    m_navigationOverlayCheckBox->setGeometry(x, y, navigationSize.width() + 10, darkSize.height() + 2);
    x += m_navigationOverlayCheckBox->width() + gap;
    m_orthographicCheckBox->setGeometry(x, y, orthoSize.width() + 10, darkSize.height() + 2);
    x += m_orthographicCheckBox->width() + gap;
    m_colorVariantComboBox->setGeometry(x, y, qMax(92, colorSize.width() + 12), darkSize.height() + 2);
    x += m_colorVariantComboBox->width() + gap;
    m_lightingPresetComboBox->setGeometry(x, y, qMax(94, lightingSize.width() + 12), darkSize.height() + 2);

    m_openGLViewportCheckBox->raise();
    m_darkViewportCheckBox->raise();
    m_navigationOverlayCheckBox->raise();
    m_orthographicCheckBox->raise();
    m_colorVariantComboBox->raise();
    m_lightingPresetComboBox->raise();
}

QVector<SceneDocument::TreeNode> ViewportWidget::parentGroupStackForGroup(int groupId) const
{
    QVector<SceneDocument::TreeNode> groupStack;
    if (m_scene && groupId > 0)
        collectParentGroupStackForGroup(m_scene->treeRoot(), groupId, &groupStack);
    return groupStack;
}

QVector3D ViewportWidget::transformOriginForGroup(int groupId) const
{
    if (!m_scene || groupId <= 0)
        return QVector3D();

    const SceneDocument::TreeNode *group = m_scene->treeNodeById(groupId);
    if (!group)
        return QVector3D();

    return transformPointByGroupStack(group->position, parentGroupStackForGroup(groupId));
}

QVector3D ViewportWidget::worldAxisVectorForGroup(int groupId, const QVector3D &localAxis) const
{
    return transformVectorByGroupStack(localAxis, parentGroupStackForGroup(groupId));
}

QVector<SceneDocument::TreeNode> ViewportWidget::selectedParentGroupStack() const
{
    QVector<SceneDocument::TreeNode> groupStack;
    if (!m_scene)
        return groupStack;

    if (m_shapes && m_selectedIndex >= 0 && m_selectedIndex < m_shapes->size()) {
        const int shapeId = m_shapes->at(m_selectedIndex).id;
        collectParentGroupStackForShape(m_scene->treeRoot(), shapeId, &groupStack);
        return groupStack;
    }

    if (m_selectedGroupId > 0)
        collectParentGroupStackForGroup(m_scene->treeRoot(), m_selectedGroupId, &groupStack);

    return groupStack;
}

QVector3D ViewportWidget::selectedTransformOrigin() const
{
    const QVector<SceneDocument::TreeNode> parentGroups = selectedParentGroupStack();

    if (m_shapes && m_selectedIndex >= 0 && m_selectedIndex < m_shapes->size())
        return transformPointByGroupStack(m_shapes->at(m_selectedIndex).position, parentGroups);

    if (m_scene && m_selectedGroupId > 0) {
        if (const SceneDocument::TreeNode *group = m_scene->treeNodeById(m_selectedGroupId))
            return transformPointByGroupStack(group->position, parentGroups);
    }

    return QVector3D();
}

QVector3D ViewportWidget::selectedWorldAxisVector(const QVector3D &localAxis) const
{
    return transformVectorByGroupStack(localAxis, selectedParentGroupStack());
}

QVector3D ViewportWidget::selectedLocalDeltaFromWorldDelta(const QVector3D &worldDelta) const
{
    return inverseTransformVectorByGroupStack(worldDelta, selectedParentGroupStack());
}



int ViewportWidget::polyhedronGroupIdForElementNode(int nodeId) const
{
    if (!m_scene || nodeId <= 0)
        return 0;

    int parentId = 0;
    if (!SceneDocument::findChildParent(m_scene->treeRoot(), nodeId, &parentId, nullptr))
        return 0;

    const SceneDocument::TreeNode *parent = m_scene->treeNodeById(parentId);
    return parent && parent->operation == SceneDocument::TreeNode::Polyhedron ? parentId : 0;
}

QVector<int> ViewportWidget::selectedPolyhedronPointNodeIds() const
{
    QVector<int> pointNodeIds;
    if (!m_scene)
        return pointNodeIds;

    auto appendUnique = [&](int nodeId) {
        if (nodeId > 0 && !pointNodeIds.contains(nodeId))
            pointNodeIds.append(nodeId);
    };

    for (int nodeId : m_selectedPolyhedronElementNodeIds) {
        const SceneDocument::TreeNode *node = m_scene->treeNodeById(nodeId);
        const ShapeNode *shape = shapeForPrimitiveNode(m_scene, node);
        if (!shape)
            continue;
        if (shape->type == ShapeNode::Point3D) {
            appendUnique(nodeId);
            continue;
        }
        if (shape->type != ShapeNode::Face3D || shape->polyhedronFaces.isEmpty())
            continue;

        const int groupId = polyhedronGroupIdForElementNode(nodeId);
        const SceneDocument::TreeNode *group = groupId > 0 ? m_scene->treeNodeById(groupId) : nullptr;
        if (!group)
            continue;

        QVector<int> groupPointNodeIds;
        for (const SceneDocument::TreeNode &child : group->children) {
            const ShapeNode *pointShape = shapeForPrimitiveNode(m_scene, &child);
            if (pointShape && pointShape->type == ShapeNode::Point3D)
                groupPointNodeIds.append(child.id);
        }

        for (int pointIndex : shape->polyhedronFaces.first()) {
            if (pointIndex >= 0 && pointIndex < groupPointNodeIds.size())
                appendUnique(groupPointNodeIds[pointIndex]);
        }
    }
    return pointNodeIds;
}

QVector<SceneDocument::TreeNode> ViewportWidget::polyhedronSelectionParentGroupStack() const
{
    QVector<SceneDocument::TreeNode> groupStack;
    if (!m_scene || m_selectedPolyhedronElementNodeIds.isEmpty())
        return groupStack;

    const int groupId = polyhedronGroupIdForElementNode(m_selectedPolyhedronElementNodeIds.first());
    if (groupId > 0)
        collectParentGroupStackForGroup(m_scene->treeRoot(), groupId, &groupStack);
    return groupStack;
}

QVector3D ViewportWidget::polyhedronSelectionOrigin() const
{
    const QVector<int> pointNodeIds = selectedPolyhedronPointNodeIds();
    if (pointNodeIds.isEmpty())
        return QVector3D();

    const QVector<SceneDocument::TreeNode> parentGroups = polyhedronSelectionParentGroupStack();
    QVector3D sum;
    int count = 0;
    for (int nodeId : pointNodeIds) {
        const SceneDocument::TreeNode *node = m_scene ? m_scene->treeNodeById(nodeId) : nullptr;
        const ShapeNode *shape = shapeForPrimitiveNode(m_scene, node);
        if (!shape || shape->type != ShapeNode::Point3D)
            continue;
        sum += transformPointByGroupStack(shape->position, parentGroups);
        ++count;
    }
    return count > 0 ? sum / float(count) : QVector3D();
}

QVector3D ViewportWidget::polyhedronSelectionWorldAxisVector(const QVector3D &localAxis) const
{
    return transformVectorByGroupStack(localAxis, polyhedronSelectionParentGroupStack());
}

float ViewportWidget::polyhedronSelectionGizmoAxisLength() const
{
    const float viewportSide = static_cast<float>(qMax(1, qMin(width(), height())));
    const float baseScreenLength = qBound(52.0f, viewportSide * 0.12f, viewportSide * 0.20f);
    const QVector3D originCamera = toCameraPoint(polyhedronSelectionOrigin(),
                                                 m_cameraYaw,
                                                 m_cameraPitch,
                                                 m_cameraDistance,
                                                 m_cameraTarget);
    const float originDepth = qMax(8.0f, originCamera.z());
    const float objectZoom = 220.0f / originDepth;
    const float gizmoZoom = std::pow(objectZoom, 0.585f); // 2x object zoom -> ~1.5x gizmo zoom.
    const float screenLength = qBound(34.0f, baseScreenLength * gizmoZoom, viewportSide * 0.25f);
    return screenLength * originDepth / 420.0f;
}

QVector3D ViewportWidget::polyhedronSelectionLocalDeltaForMousePosition(const QPoint &position) const
{
    const QPoint pixelDelta = position - m_dragStartMousePosition;
    const float worldUnitsPerPixel = m_cameraDistance / 420.0f;
    const QVector<SceneDocument::TreeNode> parentGroups = polyhedronSelectionParentGroupStack();

    if (m_dragMode == PlaneDrag) {
        const QVector3D worldDelta(pixelDelta.x() * worldUnitsPerPixel,
                                   -pixelDelta.y() * worldUnitsPerPixel,
                                   0.0f);
        return inverseTransformVectorByGroupStack(worldDelta, parentGroups);
    }

    QVector3D axisVector;
    if (m_dragMode == AxisXDrag)
        axisVector = QVector3D(1.0f, 0.0f, 0.0f);
    else if (m_dragMode == AxisYDrag)
        axisVector = QVector3D(0.0f, 1.0f, 0.0f);
    else if (m_dragMode == AxisZDrag)
        axisVector = QVector3D(0.0f, 0.0f, 1.0f);

    if (axisVector.isNull())
        return QVector3D();

    const QVector3D origin = polyhedronSelectionOrigin();
    const QPointF screenOrigin = projectWorldPoint(origin, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance, m_cameraTarget, m_orthographicProjection).point;
    const QPointF screenEnd = projectWorldPoint(origin + polyhedronSelectionWorldAxisVector(axisVector * polyhedronSelectionGizmoAxisLength()),
                                                size(), m_cameraYaw, m_cameraPitch, m_cameraDistance, m_cameraTarget, m_orthographicProjection).point;
    QVector2D screenAxis(screenEnd - screenOrigin);
    if (screenAxis.lengthSquared() <= 0.0001f)
        return QVector3D();

    screenAxis.normalize();
    const float screenAmount = QVector2D::dotProduct(QVector2D(pixelDelta), screenAxis);
    return axisVector * screenAmount * worldUnitsPerPixel;
}


bool ViewportWidget::pickBreadcrumbNode(const QPoint &position, int *nodeId) const
{
    for (const BreadcrumbHit &hit : m_breadcrumbHits) {
        if (hit.rect.contains(position)) {
            *nodeId = hit.nodeId;
            return true;
        }
    }
    return false;
}

QVector3D ViewportWidget::dragDeltaForMousePosition(const QPoint &position) const
{
    const QPoint pixelDelta = position - m_dragStartMousePosition;
    const float worldUnitsPerPixel = m_cameraDistance / 420.0f;
    QVector3D worldDelta(pixelDelta.x() * worldUnitsPerPixel,
                         -pixelDelta.y() * worldUnitsPerPixel,
                         0.0f);

    if (m_dragMode == PlaneDrag)
        return selectedLocalDeltaFromWorldDelta(worldDelta);

    QVector3D axisVector;
    if (m_dragMode == AxisXDrag)
        axisVector = QVector3D(1.0f, 0.0f, 0.0f);
    else if (m_dragMode == AxisYDrag)
        axisVector = QVector3D(0.0f, 1.0f, 0.0f);
    else if (m_dragMode == AxisZDrag)
        axisVector = QVector3D(0.0f, 0.0f, 1.0f);

    const QVector3D origin = selectedTransformOrigin();
    const QPointF screenOrigin = projectWorldPoint(origin, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance, m_cameraTarget, m_orthographicProjection).point;
    const QPointF screenEnd = projectWorldPoint(origin + selectedWorldAxisVector(axisVector * 36.0f),
                                                size(),
                                                m_cameraYaw,
                                                m_cameraPitch,
                                                m_cameraDistance,
                                                m_cameraTarget,
                                                m_orthographicProjection).point;
    QVector2D screenAxis(screenEnd - screenOrigin);

    if (screenAxis.lengthSquared() <= 0.0001f)
        return QVector3D();

    screenAxis.normalize();
    const float screenAmount = QVector2D::dotProduct(QVector2D(pixelDelta), screenAxis);
    return axisVector * screenAmount * worldUnitsPerPixel;
}

QVector3D ViewportWidget::rotationDeltaForMousePosition(const QPoint &position) const
{
    if (!isRotationDragMode(m_dragMode))
        return QVector3D();

    QVector2D tangent = m_rotationDragScreenTangent;
    if (tangent.lengthSquared() <= 0.0001f) {
        tangent = QVector2D(1.0f, 0.0f);
    } else {
        tangent.normalize();
    }

    const QPoint pixelDelta = position - m_dragStartMousePosition;
    const float screenAmount = QVector2D::dotProduct(QVector2D(pixelDelta), tangent);
    return rotationVectorForMode(m_dragMode, screenAmount * 0.75f);
}

bool ViewportWidget::isRotationDragMode(DragMode dragMode) const
{
    return dragMode == RotateXDrag || dragMode == RotateYDrag || dragMode == RotateZDrag;
}

void ViewportWidget::mousePressEvent(QMouseEvent *event)
{
    m_lastMousePosition = event->pos();
    m_emptyClickCandidate = false;

    if (event->button() == Qt::RightButton) {
        m_panningViewport = true;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        int breadcrumbNodeId = 0;
        if (m_navigationOverlayEnabled && pickBreadcrumbNode(event->pos(), &breadcrumbNodeId)) {
            emit treeNodeClicked(breadcrumbNodeId);
            event->accept();
            return;
        }

        ViewportAxisGizmo gizmo;
        DragMode pickedPolyAxis = NoDrag;
        if (gizmo.pickPolyhedronSelectionAxis(event->pos(), &pickedPolyAxis, *this)) {
            m_draggingPolyhedronElements = true;
            m_dragPolyhedronElementNodeIds = m_selectedPolyhedronElementNodeIds;
            m_dragMode = pickedPolyAxis;
            m_dragStartMousePosition = event->pos();
            m_lastDragDelta = QVector3D();
            emit polyhedronElementsDragStarted(m_dragPolyhedronElementNodeIds);
            event->accept();
            return;
        }

        DragMode pickedAxis = NoDrag;
        if (gizmo.pickSelectedTransformAxis(event->pos(), &pickedAxis, *this)) {
            m_dragMode = pickedAxis;
            m_dragStartMousePosition = event->pos();
            m_lastDragDelta = QVector3D();
            m_lastRotationDelta = QVector3D();

            const QPointF screenOrigin = projectWorldPoint(selectedTransformOrigin(),
                                                           size(),
                                                           m_cameraYaw,
                                                           m_cameraPitch,
                                                           m_cameraDistance,
                                                           m_cameraTarget,
                                                           m_orthographicProjection).point;
            QVector2D radiusVector(QPointF(event->pos()) - screenOrigin);
            m_rotationDragScreenTangent = QVector2D(-radiusVector.y(), radiusVector.x());

            if (m_selectedGroupId > 0) {
                m_draggingGroup = true;
                m_dragGroupId = m_selectedGroupId;
                m_vboMeshKey.clear();
                m_vboSelectionEdgesKey.clear();
                updateSelectionShimmerTimer();
                if (isRotationDragMode(m_dragMode))
                    emit groupRotationDragStarted(m_selectedGroupId);
                else
                    emit groupDragStarted(m_selectedGroupId);
            } else {
                m_draggingShape = true;
                m_dragShapeIndex = m_selectedIndex;
                if (isRotationDragMode(m_dragMode))
                    emit shapeRotationDragStarted(m_selectedIndex);
                else
                    emit shapeDragStarted(m_selectedIndex);
            }
            return;
        }
    }

    if (event->button() == Qt::LeftButton && m_shapes) {
        const CsgPreview &preview = m_cachedCsgPreview;
        int helperShapeIndex = -1;
        float bestDistance = 8.0f;

        for (const CsgRenderItem &item : preview.items) {
            if (!item.helper)
                continue;

            for (const auto &edge : meshEdges(item.mesh)) {
                const QPointF a = projectWorldPoint(edge.first, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance, m_cameraTarget, m_orthographicProjection).point;
                const QPointF b = projectWorldPoint(edge.second, size(), m_cameraYaw, m_cameraPitch, m_cameraDistance, m_cameraTarget, m_orthographicProjection).point;
                const float distance = distanceToSegment(event->pos(), a, b);

                if (distance < bestDistance) {
                    bestDistance = distance;
                    helperShapeIndex = item.shapeIndex;
                }
            }
        }

        if (helperShapeIndex >= 0) {
            emit shapeClicked(helperShapeIndex);
            return;
        }
    }

    int shapeIndex = -1;
    if (event->button() == Qt::LeftButton
        && m_pickBufferSize == size()
        && event->pos().x() >= 0
        && event->pos().x() < m_pickBufferSize.width()
        && event->pos().y() >= 0
        && event->pos().y() < m_pickBufferSize.height()) {
        const int bufferIndex = event->pos().y() * m_pickBufferSize.width() + event->pos().x();

        if (bufferIndex >= 0 && bufferIndex < m_pickBuffer.size()) {
            shapeIndex = m_pickBuffer[bufferIndex];
        }
    }

    if (shapeIndex < 0) {
        if (event->button() == Qt::LeftButton) {
            m_emptyClickCandidate = true;
            m_emptyClickStartPosition = event->pos();
        }
        return;
    }

    emit shapeClicked(shapeIndex);
}

void ViewportWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panningViewport && (event->buttons() & Qt::RightButton)) {
        const QPoint delta = event->pos() - m_lastMousePosition;
        const float worldUnitsPerPixel = m_cameraDistance / 420.0f;
        const QVector3D right = cameraRightVector(m_cameraYaw);
        const QVector3D up = cameraUpVector(m_cameraYaw, m_cameraPitch);
        m_cameraTarget += (-right * delta.x() + up * delta.y()) * worldUnitsPerPixel;
        m_lastMousePosition = event->pos();
        update();
        event->accept();
        return;
    }

    if (m_draggingPolyhedronElements && (event->buttons() & Qt::LeftButton)) {
        const QVector3D localDelta = polyhedronSelectionLocalDeltaForMousePosition(event->pos());
        if ((localDelta - m_lastDragDelta).lengthSquared() < 0.0001f) {
            m_lastMousePosition = event->pos();
            return;
        }

        m_lastDragDelta = localDelta;
        emit polyhedronElementsDragged(localDelta);
        m_lastMousePosition = event->pos();
        return;
    }

    if ((m_draggingShape || m_draggingGroup) && (event->buttons() & Qt::LeftButton)) {
        if (isRotationDragMode(m_dragMode)) {
            const QVector3D rotationDelta = rotationDeltaForMousePosition(event->pos());

            if ((rotationDelta - m_lastRotationDelta).lengthSquared() < 0.0001f) {
                m_lastMousePosition = event->pos();
                return;
            }

            m_lastRotationDelta = rotationDelta;
            if (m_draggingGroup)
                emit groupRotated(m_dragGroupId, rotationDelta);
            else
                emit shapeRotated(m_dragShapeIndex, rotationDelta);
            m_lastMousePosition = event->pos();
            return;
        }

        const QVector3D worldDelta = dragDeltaForMousePosition(event->pos());

        if ((worldDelta - m_lastDragDelta).lengthSquared() < 0.0001f) {
            m_lastMousePosition = event->pos();
            return;
        }

        m_lastDragDelta = worldDelta;
        if (m_draggingGroup)
            emit groupDragged(m_dragGroupId, worldDelta);
        else
            emit shapeDragged(m_dragShapeIndex, worldDelta);
        m_lastMousePosition = event->pos();
        return;
    }

    if (!(event->buttons() & Qt::LeftButton)) {
        ViewportAxisGizmo gizmo;
        DragMode ignored = NoDrag;
        const bool hovered = gizmo.hitTestPolyhedronSelection(event->pos(), *this)
                             || gizmo.pickPolyhedronSelectionAxis(event->pos(), &ignored, *this);
        if (m_polyhedronSelectionToolHovered != hovered) {
            m_polyhedronSelectionToolHovered = hovered;
            setCursor(hovered ? Qt::SizeAllCursor : Qt::ArrowCursor);
            update();
        }
    }

    if (event->buttons() & Qt::LeftButton) {
        if (m_emptyClickCandidate
            && (event->pos() - m_emptyClickStartPosition).manhattanLength() > 3) {
            m_emptyClickCandidate = false;
        }

        const QPoint delta = event->pos() - m_lastMousePosition;
        m_cameraYaw = normalizedDegrees(m_cameraYaw - delta.x() * 0.45f);
        m_cameraPitch = normalizedDegrees(m_cameraPitch + delta.y() * 0.35f);
        update();
    }

    m_lastMousePosition = event->pos();
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton && m_panningViewport) {
        m_panningViewport = false;
        unsetCursor();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_draggingPolyhedronElements) {
        m_emptyClickCandidate = false;
        m_draggingPolyhedronElements = false;
        m_dragPolyhedronElementNodeIds.clear();
        m_dragMode = NoDrag;
        m_lastDragDelta = QVector3D();
        emit polyhedronElementsDragFinished();
        update();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && (m_draggingShape || m_draggingGroup)) {
        m_emptyClickCandidate = false;
        const int shapeIndex = m_dragShapeIndex;
        const int groupId = m_dragGroupId;
        const bool wasDraggingGroup = m_draggingGroup;
        const bool wasRotating = isRotationDragMode(m_dragMode);
        m_draggingShape = false;
        m_draggingGroup = false;
        m_dragMode = NoDrag;
        m_dragShapeIndex = -1;
        m_dragGroupId = 0;
        m_rotationDragScreenTangent = QVector2D();
        m_vboMeshKey.clear();
        m_vboSelectionEdgesKey.clear();
        updateSelectionShimmerTimer();
        if (wasDraggingGroup) {
            if (wasRotating)
                emit groupRotationDragFinished(groupId);
            else
                emit groupDragFinished(groupId);
            if (m_csgPreviewDirty && !m_csgComputing)
                startAsyncCsgCompute();
        } else {
            if (wasRotating)
                emit shapeRotationDragFinished(shapeIndex);
            else
                emit shapeDragFinished(shapeIndex);
        }
        return;
    }

    if (event->button() == Qt::LeftButton && m_emptyClickCandidate) {
        const bool clickedWithoutDrag = (event->pos() - m_emptyClickStartPosition).manhattanLength() <= 3;
        m_emptyClickCandidate = false;
        if (clickedWithoutDrag)
            emit emptyClicked();
    }
}

void ViewportWidget::wheelEvent(QWheelEvent *event)
{
    const float factor = m_cameraDistance * 0.001f;
    m_cameraDistance -= event->angleDelta().y() * qMax(0.05f, factor);
    m_cameraDistance = qBound(2.0f, m_cameraDistance, 8000.0f);
    update();
}
