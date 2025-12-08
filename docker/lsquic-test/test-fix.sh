#!/bin/bash
# 快速测试脚本 - 验证 lsquic 修复

set -e

echo "=== lsquic 修复验证测试 ==="
echo ""

# 检查 Docker
if ! command -v docker &> /dev/null; then
    echo "错误: 未安装 Docker"
    exit 1
fi

echo "1. 清理旧容器..."
docker rm -f lsquic-server lsquic-client 2>/dev/null || true

echo ""
echo "2. 构建镜像 (这可能需要 10-20 分钟)..."
docker build -t lsquic-speed-test:latest .

echo ""
echo "3. 启动服务器..."
docker run -d \
    --name lsquic-server \
    --network host \
    lsquic-speed-test:latest \
    bash -c "
        cd /app &&
        openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes -subj '/CN=localhost' 2>/dev/null &&
        ./build/speed_test_server
    "

echo "等待服务器启动..."
sleep 5

echo ""
echo "4. 查看服务器日志 (前 20 行):"
docker logs lsquic-server | head -20

echo ""
echo "5. 启动客户端 (发送 10MB 数据)..."
docker run -it --rm \
    --name lsquic-client \
    --network host \
    lsquic-speed-test:latest \
    /app/build/speed_test_client -s 127.0.0.1 -g 0.01

echo ""
echo "6. 查看服务器最终日志:"
docker logs lsquic-server | tail -30

echo ""
echo "7. 清理..."
docker rm -f lsquic-server 2>/dev/null || true

echo ""
echo "=== 测试完成 ==="
