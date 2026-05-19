#include "mainwindow.h"

#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication a(argc, argv);
    MainWindow w;

    w.resize(1200, 800);
    w.show();
    return a.exec();
}
