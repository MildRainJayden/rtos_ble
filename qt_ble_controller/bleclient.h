/*
 * bleclient.h — BLE 客户端封装 (Qt5 QLowEnergyController)
 *
 * 封装 BLE Central 操作:
 *   - 设备扫描 (QBluetoothDeviceDiscoveryAgent)
 *   - 连接 (QLowEnergyController)
 *   - 服务发现 + UART 特征值匹配
 *   - 数据收发 (write / notify)
 *
 * 设计要点:
 *   - 动态发现 JDY UART Service (不硬编码 UUID)
 *   - 信号驱动: 扫描结果/连接状态/收到数据 均通过 Qt 信号通知 UI
 *   - 异步操作: 所有 BLE 操作非阻塞 (事件驱动)
 *
 * 典型用法:
 *   BleClient *client = new BleClient(this);
 *   connect(client, &BleClient::deviceDiscovered, this, &MainWindow::onDeviceFound);
 *   connect(client, &BleClient::statusChanged, this, &MainWindow::onStatusChanged);
 *   client->startScan();
 *   // ... 用户选择设备 ...
 *   client->connectToDevice(deviceInfo);
 *   // ... 连接成功后 ...
 *   client->sendCommand("SET:color:255,0,0\n");
 */

#ifndef BLECLIENT_H
#define BLECLIENT_H

#include <QObject>
#include <QBluetoothDeviceInfo>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QLowEnergyCharacteristic>
#include <QBluetoothUuid>
#include <QList>

class BleClient : public QObject
{
    Q_OBJECT

public:
    /* 连接状态 */
    enum Status {
        Idle,           /* 空闲 (未扫描/未连接) */
        Scanning,       /* 扫描中 */
        Connecting,     /* 正在连接 */
        Connected,      /* 已连接, 正在发现服务 */
        Ready,          /* 就绪 (服务发现完成, 可收发数据) */
        Disconnected,   /* 已断开 */
        Error           /* 错误 */
    };
    Q_ENUM(Status)

    explicit BleClient(QObject *parent = nullptr);
    ~BleClient();

    /* 当前状态 */
    Status status() const { return m_status; }

    /* 已发现的设备列表 */
    QList<QBluetoothDeviceInfo> discoveredDevices() const { return m_devices; }

    /* 已连接的设备名称 */
    QString connectedDeviceName() const { return m_deviceName; }

public slots:
    /* 开始扫描 BLE 设备 (10 秒超时) */
    void startScan();

    /* 停止扫描 */
    void stopScan();

    /* 连接到指定设备 */
    void connectToDevice(const QBluetoothDeviceInfo &device);

    /* 断开连接 */
    void disconnect();

    /* 发送文本命令 (自动追加 \n, 如已有则不再追加) */
    void sendCommand(const QString &command);

signals:
    /* 发现 BLE 设备 */
    void deviceDiscovered(const QBluetoothDeviceInfo &device, int rssi);

    /* 扫描完成 */
    void scanFinished();

    /* 状态变化 */
    void statusChanged(BleClient::Status newStatus);

    /* 收到数据 (来自 JDY 模块的响应) */
    void dataReceived(const QString &data);

    /* 错误消息 */
    void errorOccurred(const QString &message);

private slots:
    /* 设备发现回调 */
    void onDeviceDiscovered(const QBluetoothDeviceInfo &info);
    void onScanFinished();
    void onScanError(QBluetoothDeviceDiscoveryAgent::Error error);

    /* 连接回调 */
    void onConnected();
    void onDisconnected();
    void onControllerError(QLowEnergyController::Error error);

    /* 服务发现回调 */
    void onServiceDiscovered(const QBluetoothUuid &serviceUuid);
    void onServiceDiscoveryFinished();

    /* 特征值回调 */
    void onServiceStateChanged(QLowEnergyService::ServiceState state);
    void onCharacteristicChanged(const QLowEnergyCharacteristic &c,
                                  const QByteArray &value);
    void onCharacteristicWritten(const QLowEnergyCharacteristic &c,
                                  const QByteArray &value);
    void onServiceError(QLowEnergyService::ServiceError error);

private:
    /* 更新状态并发射信号 */
    void setStatus(Status s);

    /* 查找 UART Service (含 write+notify 特征的) */
    void findUartService();

    /* ---- BLE 对象 ---- */
    QBluetoothDeviceDiscoveryAgent *m_discoveryAgent;
    QLowEnergyController           *m_controller;
    QLowEnergyService              *m_uartService;

    /* ---- UART 特征值 ---- */
    QLowEnergyCharacteristic m_txChar;  /* Write: App → JDY */
    QLowEnergyCharacteristic m_rxChar;  /* Notify: JDY → App */

    /* ---- 状态 ---- */
    Status m_status;
    QString m_deviceName;
    QList<QBluetoothDeviceInfo> m_devices;

    /* ---- 接收缓冲 (拼接分包数据) ---- */
    QByteArray m_rxBuffer;

    /* ---- 已知 JDY UART Service UUID (兜底) ---- */
    static const QString JDY_UART_SERVICE_UUID;

    /* BLE UUID 短格式 16-bit 封装 */
    static QBluetoothUuid serviceUuid16(quint16 uuid16);
};

#endif /* BLECLIENT_H */
