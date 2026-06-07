# Qt6 BLE LED Controller 使用说明

## 树莓派环境准备

```bash
# 1. 安装 Qt6 开发包
sudo apt update
sudo apt install qt6-base-dev qt6-base-dev-tools libqt6bluetooth6-dev

# 2. 编译
cd ~/qt_ble_controller
qmake6   # 如果提示 command not found, 试试 qmake
make -j4

# 3. 运行 (蓝牙需要权限)
sudo ./qt_ble_controller
```

## 免 sudo 运行蓝牙

```bash
sudo setcap cap_net_raw,cap_net_admin+eip ./qt_ble_controller
./qt_ble_controller   # 不再需要 sudo
```

## 一键部署

```bash
# 在 PC 端执行 (把代码拷到树莓派并编译)
./deploy.sh pi@raspberrypi.local
```

## 操作指南

1. 确保 STM32 端的 JDY 模块已上电、已配置 AT 指令
2. 点击 **Scan** 扫描 BLE 设备 → 找到 "LED_BLE" (JDY 广播名)
3. 选中设备 → 点击 **Connect**
4. 状态变成 **Ready** (绿色) 后即可操作：

| 功能 | 操作 |
|------|------|
| 设置颜色 | 拖动 RGB 滑块 → 点击 Apply Color |
| 选色器 | 点击 Pick Color 用系统取色器 |
| 调节亮度 | 拖动 Brightness 滑块 (实时发送) |
| 关闭灯带 | 点击 All Off |
| 彩虹特效 | 点击 Rainbow (Speed 控制速度) |
| 呼吸特效 | 点击 Breathe (使用当前颜色) |
| 追色特效 | 点击 Chase |
| 渐变特效 | 点击 Gradient (当前颜色 → 白色) |

## 与 STM32 通信协议

```
发送: SET:color:255,0,0\n         → 全部红色
发送: SET:rainbow:50\n            → 彩虹特效, 速度 50
发送: SET:brightness:80\n         → 亮度 80%
接收: {"cmd":"SET:color","status":"ok","data":{...}}
```

通信日志在界面底部 **Communication Log** 区域实时显示。

## 故障排查

| 现象 | 检查 |
|------|------|
| 扫描不到设备 | `sudo hciconfig hci0 up`; 树莓派蓝牙是否启用 |
| 连接失败 | JDY 模块是否上电; 距离是否过远 |
| 连接但一直 "discovering services" | JDY 模块型号不同, UUID 不匹配 → 查看日志确认 |
| 发送命令无响应 | 检查 STM32 UART 接线; 确认波特率 115200 |
| Permission denied (蓝牙) | 用 `sudo` 运行 或 `setcap` 授权 |
| qmake6 找不到 | `sudo apt install qt6-base-dev-tools` |
