/*
 * mainwindow.h — 主窗口
 *
 * BLE LED 灯带控制界面
 * 包含: 设备扫描 / 颜色选择 / 特效控制 / 亮度调节 / 状态日志
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QTextEdit>
#include <QGroupBox>
#include <QTimer>
#include <QColor>

#include "bleclient.h"
#include "ledprotocol.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() = default;

private slots:
    /* BLE */
    void onScanClicked();
    void onConnectClicked();
    void onDeviceSelected(QListWidgetItem *item);
    void onBleDeviceDiscovered(const QBluetoothDeviceInfo &device, int rssi);
    void onBleStatusChanged(BleClient::Status status);
    void onBleDataReceived(const QString &data);
    void onBleError(const QString &message);

    /* 颜色 */
    void onColorPickerClicked();
    void onColorSliderChanged();
    void onSendColorClicked();

    /* 亮度 */
    void onBrightnessChanged(int value);
    void onOffClicked();

    /* 特效 */
    void onRainbowClicked();
    void onBreatheClicked();
    void onChaseClicked();
    void onGradientClicked();

    /* 状态定时器 */
    void onStatusTimer();

private:
    void setupUi();
    void setUiEnabled(bool enabled);
    void logMessage(const QString &prefix, const QString &message);

    /* ---- BLE ---- */
    BleClient *m_bleClient;

    /* ---- UI: 设备区 ---- */
    QGroupBox   *m_deviceGroup;
    QPushButton *m_scanBtn;
    QPushButton *m_connectBtn;
    QListWidget *m_deviceList;
    QLabel      *m_statusLabel;

    /* ---- UI: 颜色控制 ---- */
    QGroupBox   *m_colorGroup;
    QPushButton *m_colorPickerBtn;
    QSlider     *m_sliderR;
    QSlider     *m_sliderG;
    QSlider     *m_sliderB;
    QLabel      *m_labelR;
    QLabel      *m_labelG;
    QLabel      *m_labelB;
    QPushButton *m_sendColorBtn;
    QLabel      *m_colorPreview;

    /* ---- UI: 亮度 + 关闭 ---- */
    QGroupBox   *m_brightnessGroup;
    QSlider     *m_brightnessSlider;
    QLabel      *m_brightnessLabel;
    QPushButton *m_offBtn;

    /* ---- UI: 特效 ---- */
    QGroupBox   *m_effectGroup;
    QPushButton *m_rainbowBtn;
    QPushButton *m_breatheBtn;
    QPushButton *m_chaseBtn;
    QPushButton *m_gradientBtn;
    QLabel      *m_speedLabel;
    QSlider     *m_speedSlider;

    /* ---- UI: 日志 ---- */
    QGroupBox   *m_logGroup;
    QTextEdit   *m_logText;

    /* ---- 状态定时器 ---- */
    QTimer      *m_statusTimer;

    /* ---- 当前颜色 ---- */
    QColor m_currentColor;

    /* BLE 设备信息缓存 */
    QList<QBluetoothDeviceInfo> m_bleDevices;
};

#endif /* MAINWINDOW_H */
