#!/bin/bash
# deploy.sh — 部署 Qt6 BLE LED Controller 到树莓派
#
# 用法:
#   ./deploy.sh [pi@raspberrypi]
#
# 前置条件:
#   树莓派已安装 Qt6 开发包:
#     sudo apt install qt6-base-dev qt6-base-dev-tools libqt6bluetooth6-dev
#
#   如果蓝牙权限报错, 运行:
#     sudo setcap cap_net_raw,cap_net_admin+eip ./qt_ble_controller

set -e

PI_HOST="${1:-pi@raspberrypi.local}"
PI_DIR="/home/pi/qt_ble_controller"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=== Deploying Qt6 BLE Controller to ${PI_HOST} ==="

# 1. 拷贝源文件到树莓派
echo "[1/3] Copying source files..."
ssh "${PI_HOST}" "mkdir -p ${PI_DIR}"
scp -r "${SCRIPT_DIR}"/*.pro \
       "${SCRIPT_DIR}"/*.cpp \
       "${SCRIPT_DIR}"/*.h \
       "${PI_HOST}:${PI_DIR}/"

# 2. 编译 (树莓派上 Qt6 的 qmake 可能是 qmake6)
echo "[2/3] Building on Raspberry Pi..."
ssh "${PI_HOST}" "cd ${PI_DIR} && \
    (command -v qmake6 >/dev/null 2>&1 && qmake6 || qmake) && \
    make -j4"

# 3. 完成
echo "[3/3] Done!"
echo ""
echo "=== 启动方式 ==="
echo "  ssh ${PI_HOST}"
echo "  cd ${PI_DIR}"
echo "  sudo ./qt_ble_controller"
echo ""
echo "蓝牙权限 (免 sudo):"
echo "  sudo setcap cap_net_raw,cap_net_admin+eip ./qt_ble_controller"
echo ""
echo "=== 安装依赖 (首次部署需要) ==="
echo "  sudo apt update"
echo "  sudo apt install qt6-base-dev qt6-base-dev-tools libqt6bluetooth6-dev"
