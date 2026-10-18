#include <QApplication>
#include <QIcon>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("SCSPLuginManager");
    QApplication::setOrganizationName("SCS");
    QApplication::setWindowIcon(QIcon(":/icons/icon.ico"));

    MainWindow w;
    w.show();
    return app.exec();
}
