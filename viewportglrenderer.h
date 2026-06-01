#ifndef VIEWPORTGLRENDERER_H
#define VIEWPORTGLRENDERER_H

#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
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

class ViewportWidget;

class ViewportGLRenderer
{
public:
    void renderGrid(ViewportWidget &w);
    void renderContactShadows(ViewportWidget &w);
    void renderPreview(ViewportWidget &w);
};

#endif // VIEWPORTGLRENDERER_H
