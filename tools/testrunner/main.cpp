#include "testrunnerwindow.h"
#include "../treedebug/treedebugwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Tree Test Runner"));

    TreeDebugWindow debugWindow(nullptr, false);
    debugWindow.setWindowTitle(QStringLiteral("Scene Tree Debugger — Test Target"));
    debugWindow.resize(900, 800);
    debugWindow.show();

    TestRunnerWindow runner(&debugWindow);
    runner.show();

    return app.exec();
}
