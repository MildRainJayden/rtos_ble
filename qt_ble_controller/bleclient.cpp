/*
 * bleclient.cpp — BLE 客户端实现 (Qt6 适配版)
 *
 * JDY BLE 模块在蓝牙连接成功后自动进入串口透传模式。
 * 从 Qt 端看, 它就是一个 BLE UART 设备:
 *   - Write Characteristic: 发送命令到 STM32
 *   - Notify Characteristic: 接收 STM32 的 JSON 响应
 *
 * Qt5 → Qt6 关键 API 变化:
 *   1. error() 信号 → errorOccurred() (不再重载, 无需 QOverload)
 *   2. QLowEnergyService::WriteWithoutResponse → WriteMode::WriteWithoutResponse
 *      (枚举变为 enum class 作用域限定)
 *
 * UUID 策略:
 *   1. 动态发现: 连接后调用 discoverServices, 遍历所有 service,
 *      找到同时具有 write + notify 特征的作为 UART Service
 *   2. 兜底: 如果动态发现失败, 尝试已知 JDY UUID: 0xFFE0
 */

#include "bleclient.h"
#include <QDebug>

/* JDY UART Service (16-bit UUID) */
const QString BleClient::JDY_UART_SERVICE_UUID =
    QStringLiteral("{0000FFE0-0000-1000-8000-00805F9B34FB}");

/* 将 16-bit UUID 封装为 128-bit BLE 标准格式 */
QBluetoothUuid BleClient::serviceUuid16(quint16 uuid16)
{
    return QBluetoothUuid(QString("0000%1-0000-1000-8000-00805F9B34FB")
                          .arg(uuid16, 4, 16, QChar('0')).toUpper());
}

/*---------------------------------------------------------------------------*
 *  构造 / 析构
 *---------------------------------------------------------------------------*/
BleClient::BleClient(QObject *parent)
    : QObject(parent)
    , m_discoveryAgent(nullptr)
    , m_controller(nullptr)
    , m_uartService(nullptr)
    , m_status(Idle)
{
    /* 创建 BLE 扫描器 */
    m_discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    /* 仅扫描 BLE (低功耗蓝牙) 设备, 忽略经典蓝牙 */
    m_discoveryAgent->setLowEnergyDiscoveryTimeout(10000); /* 10 秒扫描 */

    /* Qt6: error() 信号已重命名为 errorOccurred(), 不再需要 QOverload */
    connect(m_discoveryAgent,
            &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &BleClient::onDeviceDiscovered);
    connect(m_discoveryAgent,
            &QBluetoothDeviceDiscoveryAgent::finished,
            this, &BleClient::onScanFinished);
    connect(m_discoveryAgent,
            &QBluetoothDeviceDiscoveryAgent::errorOccurred,
            this, &BleClient::onScanError);
}

BleClient::~BleClient()
{
    disconnect();
}

/*---------------------------------------------------------------------------*
 *  扫描
 *---------------------------------------------------------------------------*/
void BleClient::startScan()
{
    m_devices.clear();
    setStatus(Scanning);
    m_discoveryAgent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
    qDebug() << "[BLE] Scanning...";
}

void BleClient::stopScan()
{
    m_discoveryAgent->stop();
    setStatus(Idle);
}

void BleClient::onDeviceDiscovered(const QBluetoothDeviceInfo &info)
{
    /* 仅收集 BLE 设备 (非经典蓝牙) */
    if (info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration) {
        m_devices.append(info);
        int rssi = info.rssi();
        emit deviceDiscovered(info, rssi);
        qDebug() << "[BLE] Found:" << info.name() << info.address().toString()
                 << "RSSI:" << rssi;
    }
}

void BleClient::onScanFinished()
{
    qDebug() << "[BLE] Scan finished, found" << m_devices.size() << "devices";
    emit scanFinished();
    if (m_status == Scanning)
        setStatus(Idle);
}

void BleClient::onScanError(QBluetoothDeviceDiscoveryAgent::Error error)
{
    Q_UNUSED(error);
    emit errorOccurred(QString("Scan error: %1").arg(m_discoveryAgent->errorString()));
    setStatus(Error);
}

/*---------------------------------------------------------------------------*
 *  连接
 *---------------------------------------------------------------------------*/
void BleClient::connectToDevice(const QBluetoothDeviceInfo &device)
{
    /* 如果正在连接中, 先断开 */
    if (m_controller) {
        m_controller->disconnectFromDevice();
        m_controller->deleteLater();
        m_controller = nullptr;
    }

    m_deviceName = device.name();
    setStatus(Connecting);

    /* 创建 BLE 控制器 */
    m_controller = QLowEnergyController::createCentral(device, this);

    connect(m_controller, &QLowEnergyController::connected,
            this, &BleClient::onConnected);
    connect(m_controller, &QLowEnergyController::disconnected,
            this, &BleClient::onDisconnected);
    /* Qt6: error() → errorOccurred() */
    connect(m_controller, &QLowEnergyController::errorOccurred,
            this, &BleClient::onControllerError);
    connect(m_controller, &QLowEnergyController::serviceDiscovered,
            this, &BleClient::onServiceDiscovered);
    connect(m_controller, &QLowEnergyController::discoveryFinished,
            this, &BleClient::onServiceDiscoveryFinished);

    m_controller->connectToDevice();
    qDebug() << "[BLE] Connecting to" << m_deviceName;
}

void BleClient::disconnect()
{
    if (m_uartService) {
        m_uartService->deleteLater();
        m_uartService = nullptr;
    }
    if (m_controller) {
        m_controller->disconnectFromDevice();
        m_controller->deleteLater();
        m_controller = nullptr;
    }
    m_rxBuffer.clear();
    setStatus(Disconnected);
}

void BleClient::onConnected()
{
    qDebug() << "[BLE] Connected, discovering services...";
    setStatus(Connected);

    /* 异步发现 GATT 服务 */
    m_controller->discoverServices();
}

void BleClient::onDisconnected()
{
    qDebug() << "[BLE] Disconnected";
    setStatus(Disconnected);
}

void BleClient::onControllerError(QLowEnergyController::Error error)
{
    Q_UNUSED(error);
    emit errorOccurred(QString("BLE error: %1").arg(m_controller->errorString()));
    setStatus(Error);
}

/*---------------------------------------------------------------------------*
 *  服务发现
 *---------------------------------------------------------------------------*/
void BleClient::onServiceDiscovered(const QBluetoothUuid &serviceUuid)
{
    qDebug() << "[BLE] Service discovered:" << serviceUuid.toString();
    Q_UNUSED(serviceUuid);
}

void BleClient::onServiceDiscoveryFinished()
{
    qDebug() << "[BLE] Service discovery finished";

    /* 尝试动态发现 UART Service */
    findUartService();

    if (!m_uartService) {
        emit errorOccurred("No UART service found on device");
        setStatus(Error);
    }
}

void BleClient::findUartService()
{
    /* 遍历所有已发现的 Service, 找第一个含 write+notify 特征的 */
    const QList<QBluetoothUuid> services = m_controller->services();
    for (const QBluetoothUuid &uuid : services) {
        QLowEnergyService *svc = m_controller->createServiceObject(uuid, this);
        if (!svc) continue;

        /* 异步处理: 创建服务 → 进入发现流程 */
        connect(svc, &QLowEnergyService::stateChanged,
                this, &BleClient::onServiceStateChanged);
        connect(svc, &QLowEnergyService::characteristicChanged,
                this, &BleClient::onCharacteristicChanged);
        connect(svc, &QLowEnergyService::characteristicWritten,
                this, &BleClient::onCharacteristicWritten);
        /* Qt6: error() → errorOccurred() */
        connect(svc, &QLowEnergyService::errorOccurred,
                this, &BleClient::onServiceError);

        /* 触发服务详情发现 (异步) */
        svc->discoverDetails();

        /* 先保存引用 (第一个服务优先) */
        if (!m_uartService) {
            m_uartService = svc;
        }
        return; /* 只处理第一个 Service */
    }
}

/*---------------------------------------------------------------------------*
 *  特征值处理
 *---------------------------------------------------------------------------*/
void BleClient::onServiceStateChanged(QLowEnergyService::ServiceState state)
{
    QLowEnergyService *svc = qobject_cast<QLowEnergyService *>(sender());
    if (!svc || svc != m_uartService) return;

    if (state == QLowEnergyService::RemoteServiceDiscovered) {
        qDebug() << "[BLE] Service details discovered";

        /* 遍历特征值, 寻找 Write 和 Notify 特征 */
        const QList<QLowEnergyCharacteristic> chars = svc->characteristics();
        for (const QLowEnergyCharacteristic &c : chars) {
            QLowEnergyCharacteristic::PropertyTypes props = c.properties();

            qDebug() << "[BLE]   Char:" << c.uuid().toString()
                     << "props:" << props;

            /* Write (写) 特征: App → JDY */
            if (props.testFlag(QLowEnergyCharacteristic::WriteNoResponse) ||
                props.testFlag(QLowEnergyCharacteristic::Write)) {
                m_txChar = c;
                qDebug() << "[BLE]   → TX (Write):" << c.uuid().toString();
            }

            /* Notify (通知) 特征: JDY → App */
            if (props.testFlag(QLowEnergyCharacteristic::Notify)) {
                m_rxChar = c;
                qDebug() << "[BLE]   → RX (Notify):" << c.uuid().toString();

                /* 启用通知 (CCCD = Client Characteristic Configuration Descriptor)
                 * Qt6 中 QBluetoothUuid::ClientCharacteristicConfiguration 已移除,
                 * 改用 Bluetooth SIG 标准 UUID: 0x2902 */
                QLowEnergyDescriptor cccd = c.descriptor(
                    QBluetoothUuid(static_cast<quint16>(0x2902)));
                if (cccd.isValid()) {
                    svc->writeDescriptor(cccd, QByteArray::fromHex("0100"));
                }
            }
        }

        /* 检查是否找到了 TX 特征 */
        if (m_txChar.isValid()) {
            setStatus(Ready);
            qDebug() << "[BLE] Ready — UART service active";
        } else {
            emit errorOccurred("No writable characteristic found");
            setStatus(Error);
        }
    }
}

void BleClient::onCharacteristicChanged(const QLowEnergyCharacteristic &c,
                                         const QByteArray &value)
{
    Q_UNUSED(c);

    /* 追加到接收缓冲 */
    m_rxBuffer.append(value);

    /* 检查是否收到完整的行 (\n 结尾) */
    while (m_rxBuffer.contains('\n')) {
        int idx = m_rxBuffer.indexOf('\n');
        QByteArray line = m_rxBuffer.left(idx).trimmed();
        m_rxBuffer.remove(0, idx + 1);

        if (!line.isEmpty()) {
            QString text = QString::fromUtf8(line);
            qDebug() << "[BLE] ← RX:" << text;
            emit dataReceived(text);
        }
    }
}

void BleClient::onCharacteristicWritten(const QLowEnergyCharacteristic &c,
                                         const QByteArray &value)
{
    Q_UNUSED(c);
    QString text = QString::fromUtf8(value).trimmed();
    qDebug() << "[BLE] → TX:" << text;
}

void BleClient::onServiceError(QLowEnergyService::ServiceError error)
{
    /* Qt6: QLowEnergyService::errorString() 已移除, 手动映射错误码 */
    static const char *errNames[] = {
        "NoError",
        "OperationError",
        "CharacteristicWriteError",
        "DescriptorWriteError",
        "UnknownError",
        "CharacteristicReadError",
        "DescriptorReadError"
    };
    const char *errStr = (error < 7) ? errNames[error] : "Unknown";
    QLowEnergyService *svc = qobject_cast<QLowEnergyService *>(sender());
    Q_UNUSED(svc);
    emit errorOccurred(QString("Service error: %1").arg(errStr));
}


/*---------------------------------------------------------------------------*
 *  数据发送
 *---------------------------------------------------------------------------*/
void BleClient::sendCommand(const QString &command)
{
    if (m_status != Ready) {
        emit errorOccurred("Not ready — cannot send command");
        return;
    }

    if (!m_txChar.isValid()) {
        emit errorOccurred("No TX characteristic available");
        return;
    }

    /* 确保命令以 \n 结尾 */
    QString cmd = command;
    if (!cmd.endsWith('\n')) {
        cmd.append('\n');
    }

    QByteArray data = cmd.toUtf8();

    /* Qt6: enum class WriteMode — 必须用作用域限定语法 */
    if (m_txChar.properties().testFlag(QLowEnergyCharacteristic::WriteNoResponse)) {
        m_uartService->writeCharacteristic(m_txChar, data,
                            QLowEnergyService::WriteMode::WriteWithoutResponse);
    } else {
        m_uartService->writeCharacteristic(m_txChar, data,
                            QLowEnergyService::WriteMode::WriteWithResponse);
    }

    qDebug() << "[BLE] → TX:" << cmd.trimmed();
}

/*---------------------------------------------------------------------------*
 *  内部工具
 *---------------------------------------------------------------------------*/
void BleClient::setStatus(Status s)
{
    if (m_status != s) {
        m_status = s;
        emit statusChanged(s);
    }
}
