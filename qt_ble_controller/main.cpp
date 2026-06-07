/*
 * main.cpp — BLE LED Controller 入口
 *
 * 树莓派 4B Qt5 应用
 * 编译: qmake && make
 * 运行: ./qt_ble_controller
 *
 * 依赖:
 *   - Qt5 Widgets
 *   - Qt5 Bluetooth
 *
 * 树莓派安装依赖:
 *   sudo apt install qt5-default qtbase5-dev libqt5bluetooth5
 */

#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    /* 应用元信息 */
    app.setApplicationName("BLE LED Controller");
    app.setApplicationVersion("1.0.0");

    MainWindow window;
    window.show();

    return app.exec();
}
