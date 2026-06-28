#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Zapiska"));
    QApplication::setApplicationName(QStringLiteral("Zapiska Marine Recorder"));

    MainWindow window;
    window.show();

    return app.exec();
}
