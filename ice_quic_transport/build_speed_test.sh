#!/bin/bash
# 编译 ICE+QUIC 速度测试工具

set -e

echo "=== 编译 ICE+QUIC 速度测试工具 ==="

# 检查是否在正确的目录
if [ ! -f "CMakeLists.txt" ]; then
    echo "错误: 请在 ice_quic_transport 目录下运行此脚本"
    exit 1
fi

# 创建 build 目录
mkdir -p build
cd build

# 运行 CMake
echo "运行 CMake..."
cmake .. -DICE_QUIC_BUILD_EXAMPLES=ON

# 编译速度测试程序
echo "编译速度测试程序..."
make speed_test_server speed_test_client -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo "=== 编译完成 ==="
echo ""
echo "可执行文件位于:"
echo "  - $(pwd)/speed_test_server"
echo "  - $(pwd)/speed_test_client"
echo ""
echo "使用方法:"
echo ""
echo "1. 生成 TLS 证书 (如果还没有):"
echo "   openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt \\"
echo "       -days 365 -nodes -subj \"/CN=localhost\""
echo ""
echo "2. 在服务器 A 上运行服务端:"
echo "   ./speed_test_server server.crt server.key"
echo ""
echo "3. 在服务器 B 上运行客户端 (发送 5 GB 数据):"
echo "   ./speed_test_client 5"
echo ""
echo "详细说明请查看: ../SPEED_TEST_README.md"
