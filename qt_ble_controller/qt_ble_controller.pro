# qmake 项目文件 — BLE LED Controller (Qt6)
# 目标平台: 树莓派 4B, Qt6
# 编译: qmake6 && make  (或 qmake && make, 取决于系统配置)

QT       += core gui widgets bluetooth

# Qt6 蓝牙模块需要显式链接 bluetooth 库
unix:!macx {
    LIBS += -lbluetooth
}

CONFIG   += c++17

TARGET   = qt_ble_controller
TEMPLATE = app

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    bleclient.cpp

HEADERS += \
    mainwindow.h \
    bleclient.h \
    ledprotocol.h
