#!/bin/bash
# Qt 命令行编译脚本 - 绕开 Qt Creator
# 在 Linux 端执行: bash /mnt/hgfs/shareWin/AICMedicineRobotSystem/build.sh

set -e

SRC_DIR="/mnt/hgfs/shareWin/AICMedicineRobotSystem"
BUILD_DIR="/mnt/hgfs/shareWin/build-AICMedicineRobotSystem-Desktop_Qt_5_5_0_GCC_64bit-Debug"

echo "=== 清理旧构建 ==="
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "=== qmake ==="
qmake "$SRC_DIR/AICMedicineRobotSystem.pro" -r CONFIG+=debug

echo "=== make (clean) ==="
make clean 2>/dev/null || true

echo "=== make (-j4) ==="
make -j4

echo "=== 编译完成 ==="
echo "可执行文件: $BUILD_DIR/AICMedicineRobotSystem"
echo ""
echo "启动方式:"
echo "  $BUILD_DIR/AICMedicineRobotSystem"
