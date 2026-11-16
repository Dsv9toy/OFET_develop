#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("OFET");
    QApplication::setApplicationName("OfetMeter");

    // Настройки храним в файле OFET/OfetMeter.ini рядом с программой:
    // при переносе папки на флешке настройки переезжают вместе с ней.
    // Если в папку программы писать нельзя, остаются стандартные настройки Windows (реестр).
    const QString appDir = QCoreApplication::applicationDirPath();
    if (QFileInfo(appDir).isWritable()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, appDir);
    }

    MainWindow w;
    w.show();
    return app.exec();
}
