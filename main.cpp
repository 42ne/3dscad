#include "mainwindow.h"

#include <QApplication>
#include <QEventLoop>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication a(argc, argv);

    // On Windows with multi-monitor setups the very first WGL context creation
    // in a process causes the GPU driver to briefly reconfigure its display
    // pipeline, which flickers all visible windows.  Creating and immediately
    // releasing a throwaway offscreen context here — before any window is shown
    // — moves that one-time driver initialisation to a point where nothing is
    // yet visible on screen.
    {
        QOffscreenSurface surface;
        surface.setFormat(fmt);
        surface.create();
        QOpenGLContext ctx;
        ctx.setFormat(fmt);
        if (ctx.create()) {
            ctx.makeCurrent(&surface);
            ctx.doneCurrent();
        }
    }

    MainWindow w;
    w.show();
    // Force the first paint (including OpenGL context initialisation inside
    // QOpenGLWidget) to complete before the event loop starts.  Without this,
    // the window can briefly appear blank/black while DWM composites it,
    // causing a visible flash during startup.
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    return a.exec();
}
