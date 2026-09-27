#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("VideoForge Editor");
    QApplication::setOrganizationName("VideoForge");
    QApplication::setApplicationVersion("0.10.3");

    MainWindow window;
    window.show();
    return app.exec();
}
