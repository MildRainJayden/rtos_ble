/*
 * mainwindow.cpp — 主窗口实现
 *
 * UI 采用纯代码布局 (不依赖 .ui 文件, 方便在树莓派上直接编译)
 */

#include "mainwindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QColorDialog>
#include <QFrame>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_bleClient(nullptr)
    , m_currentColor(Qt::red)
{
    setupUi();

    /* 创建 BLE 客户端 */
    m_bleClient = new BleClient(this);
    connect(m_bleClient, &BleClient::deviceDiscovered,
            this, &MainWindow::onBleDeviceDiscovered);
    connect(m_bleClient, &BleClient::scanFinished, this, [this]() {
        m_scanBtn->setEnabled(true);
        logMessage("INFO", "Scan finished");
    });
    connect(m_bleClient, &BleClient::statusChanged,
            this, &MainWindow::onBleStatusChanged);
    connect(m_bleClient, &BleClient::dataReceived,
            this, &MainWindow::onBleDataReceived);
    connect(m_bleClient, &BleClient::errorOccurred,
            this, &MainWindow::onBleError);

    /* 状态定时器: 每 2s 查询一次 */
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::onStatusTimer);

    /* 初始状态: 未连接, 控件禁用 */
    setUiEnabled(false);
    m_connectBtn->setEnabled(false);
}

/*---------------------------------------------------------------------------*
 *  UI 构建
 *---------------------------------------------------------------------------*/
void MainWindow::setupUi()
{
    setWindowTitle("BLE LED Controller");
    setMinimumSize(520, 700);

    QWidget *central = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);

    /* ===== 设备扫描区 ===== */
    m_deviceGroup = new QGroupBox("BLE Device");
    QVBoxLayout *devLayout = new QVBoxLayout(m_deviceGroup);

    QHBoxLayout *scanRow = new QHBoxLayout();
    m_scanBtn = new QPushButton("Scan");
    m_scanBtn->setFixedWidth(80);
    connect(m_scanBtn, &QPushButton::clicked, this, &MainWindow::onScanClicked);
    scanRow->addWidget(m_scanBtn);

    m_connectBtn = new QPushButton("Connect");
    m_connectBtn->setFixedWidth(80);
    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    scanRow->addWidget(m_connectBtn);
    scanRow->addStretch();
    devLayout->addLayout(scanRow);

    m_deviceList = new QListWidget();
    m_deviceList->setMaximumHeight(120);
    connect(m_deviceList, &QListWidget::itemClicked,
            this, &MainWindow::onDeviceSelected);
    devLayout->addWidget(m_deviceList);

    m_statusLabel = new QLabel("Status: Idle");
    devLayout->addWidget(m_statusLabel);

    mainLayout->addWidget(m_deviceGroup);

    /* ===== 颜色选择区 ===== */
    m_colorGroup = new QGroupBox("Color Control");
    QGridLayout *colorGrid = new QGridLayout(m_colorGroup);

    m_colorPickerBtn = new QPushButton("Pick Color");
    connect(m_colorPickerBtn, &QPushButton::clicked,
            this, &MainWindow::onColorPickerClicked);
    colorGrid->addWidget(m_colorPickerBtn, 0, 0, 1, 2);

    m_colorPreview = new QLabel();
    m_colorPreview->setFixedSize(60, 30);
    m_colorPreview->setStyleSheet(
        "background-color: rgb(255,0,0); border: 1px solid #999;");
    colorGrid->addWidget(m_colorPreview, 0, 2);

    /* R 滑块 */
    m_labelR = new QLabel("R: 255");
    colorGrid->addWidget(m_labelR, 1, 0);
    m_sliderR = new QSlider(Qt::Horizontal);
    m_sliderR->setRange(0, 255);
    m_sliderR->setValue(255);
    connect(m_sliderR, &QSlider::valueChanged, this, &MainWindow::onColorSliderChanged);
    colorGrid->addWidget(m_sliderR, 1, 1);

    /* G 滑块 */
    m_labelG = new QLabel("G: 0");
    colorGrid->addWidget(m_labelG, 2, 0);
    m_sliderG = new QSlider(Qt::Horizontal);
    m_sliderG->setRange(0, 255);
    m_sliderG->setValue(0);
    connect(m_sliderG, &QSlider::valueChanged, this, &MainWindow::onColorSliderChanged);
    colorGrid->addWidget(m_sliderG, 2, 1);

    /* B 滑块 */
    m_labelB = new QLabel("B: 0");
    colorGrid->addWidget(m_labelB, 3, 0);
    m_sliderB = new QSlider(Qt::Horizontal);
    m_sliderB->setRange(0, 255);
    m_sliderB->setValue(0);
    connect(m_sliderB, &QSlider::valueChanged, this, &MainWindow::onColorSliderChanged);
    colorGrid->addWidget(m_sliderB, 3, 1);

    m_sendColorBtn = new QPushButton("Apply Color");
    connect(m_sendColorBtn, &QPushButton::clicked,
            this, &MainWindow::onSendColorClicked);
    colorGrid->addWidget(m_sendColorBtn, 4, 0, 1, 3);

    mainLayout->addWidget(m_colorGroup);

    /* ===== 亮度 + 关闭 ===== */
    m_brightnessGroup = new QGroupBox("Brightness");
    QHBoxLayout *brightLayout = new QHBoxLayout(m_brightnessGroup);

    m_brightnessSlider = new QSlider(Qt::Horizontal);
    m_brightnessSlider->setRange(0, 100);
    m_brightnessSlider->setValue(80);
    connect(m_brightnessSlider, &QSlider::valueChanged,
            this, &MainWindow::onBrightnessChanged);
    brightLayout->addWidget(m_brightnessSlider);

    m_brightnessLabel = new QLabel("80%");
    m_brightnessLabel->setFixedWidth(40);
    brightLayout->addWidget(m_brightnessLabel);

    m_offBtn = new QPushButton("All Off");
    m_offBtn->setFixedWidth(80);
    connect(m_offBtn, &QPushButton::clicked, this, &MainWindow::onOffClicked);
    brightLayout->addWidget(m_offBtn);

    mainLayout->addWidget(m_brightnessGroup);

    /* ===== 特效区 ===== */
    m_effectGroup = new QGroupBox("Effects");
    QVBoxLayout *effectLayout = new QVBoxLayout(m_effectGroup);

    QHBoxLayout *effectBtnRow = new QHBoxLayout();
    m_rainbowBtn = new QPushButton("Rainbow");
    connect(m_rainbowBtn, &QPushButton::clicked, this, &MainWindow::onRainbowClicked);
    effectBtnRow->addWidget(m_rainbowBtn);

    m_breatheBtn = new QPushButton("Breathe");
    connect(m_breatheBtn, &QPushButton::clicked, this, &MainWindow::onBreatheClicked);
    effectBtnRow->addWidget(m_breatheBtn);

    m_chaseBtn = new QPushButton("Chase");
    connect(m_chaseBtn, &QPushButton::clicked, this, &MainWindow::onChaseClicked);
    effectBtnRow->addWidget(m_chaseBtn);

    m_gradientBtn = new QPushButton("Gradient");
    connect(m_gradientBtn, &QPushButton::clicked, this, &MainWindow::onGradientClicked);
    effectBtnRow->addWidget(m_gradientBtn);

    effectLayout->addLayout(effectBtnRow);

    QHBoxLayout *speedRow = new QHBoxLayout();
    m_speedLabel = new QLabel("Speed: 50");
    speedRow->addWidget(m_speedLabel);
    m_speedSlider = new QSlider(Qt::Horizontal);
    m_speedSlider->setRange(1, 100);
    m_speedSlider->setValue(50);
    connect(m_speedSlider, &QSlider::valueChanged, this, [this](int v) {
        m_speedLabel->setText(QString("Speed: %1").arg(v));
    });
    speedRow->addWidget(m_speedSlider);
    effectLayout->addLayout(speedRow);

    mainLayout->addWidget(m_effectGroup);

    /* ===== 日志区 ===== */
    m_logGroup = new QGroupBox("Communication Log");
    QVBoxLayout *logLayout = new QVBoxLayout(m_logGroup);
    m_logText = new QTextEdit();
    m_logText->setReadOnly(true);
    m_logText->setMaximumHeight(150);
    m_logText->setStyleSheet("font-family: monospace; font-size: 11px;");
    logLayout->addWidget(m_logText);
    mainLayout->addWidget(m_logGroup);

    setCentralWidget(central);
}

/*---------------------------------------------------------------------------*
 *  UI 辅助
 *---------------------------------------------------------------------------*/
void MainWindow::setUiEnabled(bool enabled)
{
    m_colorGroup->setEnabled(enabled);
    m_brightnessGroup->setEnabled(enabled);
    m_effectGroup->setEnabled(enabled);
}

void MainWindow::logMessage(const QString &prefix, const QString &message)
{
    QString color = "gray";
    if (prefix == "TX") color = "#0077cc";
    else if (prefix == "RX") color = "#228B22";
    else if (prefix == "ERR") color = "#cc0000";

    m_logText->append(QString("<span style='color:%1'>[%2] %3</span>")
                      .arg(color, prefix, message.toHtmlEscaped()));
}

/*---------------------------------------------------------------------------*
 *  BLE 槽
 *---------------------------------------------------------------------------*/
void MainWindow::onScanClicked()
{
    m_deviceList->clear();
    m_bleDevices.clear();
    m_scanBtn->setEnabled(false);
    logMessage("INFO", "Scanning for BLE devices...");
    m_bleClient->startScan();
}

void MainWindow::onConnectClicked()
{
    int row = m_deviceList->currentRow();
    if (row >= 0 && row < m_bleDevices.size()) {
        logMessage("INFO", QString("Connecting to %1...")
                   .arg(m_bleDevices[row].name()));
        m_bleClient->connectToDevice(m_bleDevices[row]);
    }
}

void MainWindow::onDeviceSelected(QListWidgetItem *item)
{
    Q_UNUSED(item);
    m_connectBtn->setEnabled(true);
}

void MainWindow::onBleDeviceDiscovered(const QBluetoothDeviceInfo &device, int rssi)
{
    /* 去重: 同名设备只保留信号最强的 */
    for (int i = 0; i < m_bleDevices.size(); ++i) {
        if (m_bleDevices[i].address() == device.address()) {
            m_bleDevices[i] = device; /* 更新设备信息 (RSSI 可能已变) */
            /* 更新列表显示 */
            QListWidgetItem *item = m_deviceList->item(i);
            if (item) {
                item->setText(QString("%1  [RSSI:%2 dBm]")
                              .arg(device.name().isEmpty() ? "(Unknown)" : device.name())
                              .arg(rssi));
            }
            return;
        }
    }

    m_bleDevices.append(device);
    m_deviceList->addItem(QString("%1  [RSSI:%2 dBm]")
                          .arg(device.name().isEmpty() ? "(Unknown)" : device.name())
                          .arg(rssi));
}

void MainWindow::onBleStatusChanged(BleClient::Status status)
{
    switch (status) {
    case BleClient::Idle:
        m_statusLabel->setText("Status: Idle");
        m_statusLabel->setStyleSheet("color: gray;");
        setUiEnabled(false);
        m_statusTimer->stop();
        break;
    case BleClient::Scanning:
        m_statusLabel->setText("Status: Scanning...");
        m_statusLabel->setStyleSheet("color: blue;");
        break;
    case BleClient::Connecting:
        m_statusLabel->setText("Status: Connecting...");
        m_statusLabel->setStyleSheet("color: orange;");
        break;
    case BleClient::Connected:
        m_statusLabel->setText("Status: Connected (discovering services...)");
        m_statusLabel->setStyleSheet("color: orange;");
        break;
    case BleClient::Ready:
        m_statusLabel->setText(
            QString("Status: Ready — %1").arg(m_bleClient->connectedDeviceName()));
        m_statusLabel->setStyleSheet("color: green; font-weight: bold;");
        setUiEnabled(true);
        m_statusTimer->start(2000); /* 每 2s 查询状态 */
        break;
    case BleClient::Disconnected:
        m_statusLabel->setText("Status: Disconnected");
        m_statusLabel->setStyleSheet("color: red;");
        setUiEnabled(false);
        m_statusTimer->stop();
        break;
    case BleClient::Error:
        m_statusLabel->setText("Status: Error");
        m_statusLabel->setStyleSheet("color: red; font-weight: bold;");
        setUiEnabled(false);
        m_statusTimer->stop();
        break;
    }
}

void MainWindow::onBleDataReceived(const QString &data)
{
    logMessage("RX", data);

    /* 检查响应是否成功 */
    QString err = getResponseError(data);
    if (!err.isEmpty()) {
        logMessage("ERR", err);
    }
}

void MainWindow::onBleError(const QString &message)
{
    logMessage("ERR", message);
}

/*---------------------------------------------------------------------------*
 *  颜色控制
 *---------------------------------------------------------------------------*/
void MainWindow::onColorPickerClicked()
{
    QColor c = QColorDialog::getColor(m_currentColor, this, "Select LED Color");
    if (c.isValid()) {
        m_currentColor = c;
        /* 同步到滑块 */
        m_sliderR->blockSignals(true);
        m_sliderG->blockSignals(true);
        m_sliderB->blockSignals(true);
        m_sliderR->setValue(c.red());
        m_sliderG->setValue(c.green());
        m_sliderB->setValue(c.blue());
        m_sliderR->blockSignals(false);
        m_sliderG->blockSignals(false);
        m_sliderB->blockSignals(false);
        /* 更新标签和预览 */
        m_labelR->setText(QString("R: %1").arg(c.red()));
        m_labelG->setText(QString("G: %1").arg(c.green()));
        m_labelB->setText(QString("B: %1").arg(c.blue()));
        m_colorPreview->setStyleSheet(
            QString("background-color: rgb(%1,%2,%3); border: 1px solid #999;")
            .arg(c.red()).arg(c.green()).arg(c.blue()));
    }
}

void MainWindow::onColorSliderChanged()
{
    int r = m_sliderR->value();
    int g = m_sliderG->value();
    int b = m_sliderB->value();

    m_currentColor = QColor(r, g, b);
    m_labelR->setText(QString("R: %1").arg(r));
    m_labelG->setText(QString("G: %1").arg(g));
    m_labelB->setText(QString("B: %1").arg(b));
    m_colorPreview->setStyleSheet(
        QString("background-color: rgb(%1,%2,%3); border: 1px solid #999;")
        .arg(r).arg(g).arg(b));
}

void MainWindow::onSendColorClicked()
{
    int r = m_sliderR->value();
    int g = m_sliderG->value();
    int b = m_sliderB->value();

    QString cmd = buildSetColor(r, g, b);
    logMessage("TX", cmd.trimmed());
    m_bleClient->sendCommand(cmd);
}

/*---------------------------------------------------------------------------*
 *  亮度 / 关闭
 *---------------------------------------------------------------------------*/
void MainWindow::onBrightnessChanged(int value)
{
    m_brightnessLabel->setText(QString("%1%").arg(value));
    QString cmd = buildSetBrightness(value);
    logMessage("TX", cmd.trimmed());
    m_bleClient->sendCommand(cmd);
}

void MainWindow::onOffClicked()
{
    QString cmd = buildSetOff();
    logMessage("TX", cmd.trimmed());
    m_bleClient->sendCommand(cmd);
}

/*---------------------------------------------------------------------------*
 *  特效
 *---------------------------------------------------------------------------*/
void MainWindow::onRainbowClicked()
{
    int speed = m_speedSlider->value();
    QString cmd = buildSetRainbow(speed);
    logMessage("TX", cmd.trimmed());
    m_bleClient->sendCommand(cmd);
}

void MainWindow::onBreatheClicked()
{
    int r = m_sliderR->value();
    int g = m_sliderG->value();
    int b = m_sliderB->value();
    int speed = m_speedSlider->value() / 10; /* 映射: 1-100 → 1-10 */
    if (speed < 1) speed = 1;
    if (speed > 10) speed = 10;

    QString cmd = buildSetBreathe(r, g, b, speed);
    logMessage("TX", cmd.trimmed());
    m_bleClient->sendCommand(cmd);
}

void MainWindow::onChaseClicked()
{
    int r = m_sliderR->value();
    int g = m_sliderG->value();
    int b = m_sliderB->value();
    int speed = m_speedSlider->value() / 10;
    if (speed < 1) speed = 1;
    if (speed > 10) speed = 10;

    QString cmd = buildSetChase(r, g, b, 10, speed);
    logMessage("TX", cmd.trimmed());
    m_bleClient->sendCommand(cmd);
}

void MainWindow::onGradientClicked()
{
    int r1 = m_sliderR->value();
    int g1 = m_sliderG->value();
    int b1 = m_sliderB->value();
    /* 渐变到白色 */
    QString cmd = buildSetGradient(r1, g1, b1, 255, 255, 255);
    logMessage("TX", cmd.trimmed());
    m_bleClient->sendCommand(cmd);
}

/*---------------------------------------------------------------------------*
 *  状态定时器
 *---------------------------------------------------------------------------*/
void MainWindow::onStatusTimer()
{
    if (m_bleClient->status() == BleClient::Ready) {
        m_bleClient->sendCommand(buildGetStatus());
    }
}
