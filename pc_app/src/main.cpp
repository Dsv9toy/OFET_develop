#include <QApplication>

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("OFET");
    QApplication::setApplicationName("OfetMeter");

    MainWindow w;
    w.show();
    return app.exec();
}
