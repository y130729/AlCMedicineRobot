#include <QApplication>
#include "loginwindow.h"
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    LoginWindow loginWindow;
    MainWindow mainWindow;

    QObject::connect(&loginWindow, &LoginWindow::loginSuccess, [&]() {
        loginWindow.hide();
        mainWindow.show();
    });

    loginWindow.show();

    return a.exec();
}
