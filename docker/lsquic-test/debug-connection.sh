#!/bin/bash
# lsquic 连接调试脚本

set -e

echo "=== lsquic 连接调试 ==="
echo ""

# 检查 Docker
if ! command -v docker &> /dev/null; then
    echo "错误: 未安装 Docker"
    exit 1
fi

echo "1. 清理旧容器..."
docker rm -f lsquic-server lsquic-client 2>/dev/null || true

echo ""
echo "2. 启动服务器..."
docker run -d \
    --name lsquic-server \
    --network host \
    lsquic-speed-test:latest \
    bash -c "
        cd /app &&
        openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes -subj '/CN=localhost' 2>/dev/null &&
        ./build/speed_test_server 2>&1
    "

echo "等待服务器启动..."
sleep 3

echo ""
echo "3. 检查服务器是否在监听..."
docker exec lsquic-server netstat -uln | grep 9331 || echo "警告: 端口 9331 未监听"

echo ""
echo "4. 查看服务器初始日志:"
docker logs lsquic-server

echo ""
echo "5. 测试 UDP 连接..."
echo "测试数据" | nc -u -w1 127.0.0.1 9331 || echo "注意: nc 测试可能不适用于 QUIC"

echo ""
echo "6. 启动客户端 (发送 10MB 数据，带详细日志)..."
docker run -it --rm \
    --name lsquic-client \
    --network host \
    lsquic-speed-test:latest \
    bash -c "
        cd /app &&
        echo '=== 客户端环境信息 ===' &&
        echo 'IP 地址:' &&
        ip addr show | grep 'inet ' &&
        echo '' &&
        echo '=== 路由表 ===' &&
        ip route &&
        echo '' &&
        echo '=== 启动客户端 ===' &&
        ./build/speed_test_client -s 127.0.0.1 -g 0.01 2>&1
    "

echo ""
echo "7. 查看服务器最终日志:"
docker logs lsquic-server | tail -50

echo ""
echo "8. 检查服务器进程状态:"
docker exec lsquic-server ps aux | grep speed_test || echo "服务器进程已退出"

echo ""
echo "9. 清理..."
docker rm -f lsquic-server 2>/dev/null || true

echo ""
echo "=== 调试完成 ==="
echo ""
echo "如果连接失败，请检查:"
echo "1. 服务器是否收到 UDP 包 (查看服务器日志中的 'Received UDP packet')"
echo "2. 客户端是否收到响应 (查看客户端日志中的 'Received packet')"
echo "3. QUIC 版本是否匹配 (都应该是 0x20)"
echo "4. ALPN 是否正确配置"
echo "5. 防火墙是否阻止了 UDP 9331 端口"
