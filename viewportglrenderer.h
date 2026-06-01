#ifndef VIEWPORTGLRENDERER_H
#define VIEWPORTGLRENDERER_H

#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QString>
#include <QVector3D>
#include <QVector4D>
#include <QVector>

struct OpenGLMeshVertex
{
    QVector3D position;
    QVector3D normal;
    QVector3D color;
};

struct OpenGLLineVertex
{
    QVector3D position;
    QVector4D color;
};

struct OpenGLFlatVertex
{
    QVector3D position;
    QVector4D color;
};

struct ViewportSelectionEdgeCandidate;

class ViewportWidget;

class ViewportGLRenderer
{
public:
    void initialize(ViewportWidget &w);
    void renderGrid(ViewportWidget &w);
    void renderContactShadows(ViewportWidget &w);
    void renderPreview(ViewportWidget &w);
    void clearMeshCache();
    void clearEdgeCache();

private:
    QOpenGLShaderProgram *m_glMeshProgram = nullptr;
    QOpenGLShaderProgram *m_glLineProgram = nullptr;
    QOpenGLShaderProgram *m_glFlatProgram = nullptr;
    QOpenGLBuffer m_vboGrid        { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_vboMesh        { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_vboSelectionEdges { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_vboHelperFront { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_vboHelperXray  { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_vboShadow      { QOpenGLBuffer::VertexBuffer };
    int     m_vboGridCount        = 0;
    int     m_vboMeshCount        = 0;
    int     m_vboSelectionHiddenEdgeCount = 0;
    int     m_vboSelectionEdgeCount = 0;
    int     m_vboSelectionSilhouetteCount = 0;
    int     m_vboHelperFrontCount = 0;
    int     m_vboHelperXrayCount  = 0;
    int     m_vboShadowCount      = 0;
    QString m_vboMeshKey;
    QVector<ViewportSelectionEdgeCandidate> m_selectionEdgeCandidates;
    QString m_selectionEdgeTopologyKey;
    QString m_vboSelectionEdgesKey;
    QString m_vboShadowKey;
};

#endif // VIEWPORTGLRENDERER_H
