#include "mainwindow.h"
#include <QApplication>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("tinySA-App");
    app.setApplicationVersion("2.0.0");

    MainWindow win;
    win.show();
    return app.exec();
}
