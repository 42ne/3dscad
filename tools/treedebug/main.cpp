#include "treedebugwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("TreeDebugger"));

    TreeDebugWindow window;
    window.show();

    return app.exec();
}
