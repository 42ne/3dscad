#include "subsettestwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("OpenSCAD Subset Test"));

    SubsetTestWindow w;
    w.resize(1100, 700);
    w.show();

    return app.exec();
}
