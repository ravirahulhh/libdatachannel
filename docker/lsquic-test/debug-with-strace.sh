#!/bin/bash
# 使用 strace 跟踪系统调用

set -e

echo "=== 使用 strace 跟踪系统调用 ==="
echo ""

# 检查 Docker
if ! command -v docker &> /dev/null; then
    echo "错误: 未安装 Docker"
    exit 1
fi

echo "1. 清理旧容器..."
docker rm -f lsquic-server lsquic-client-strace 2>/dev/null || true

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
echo "3. 使用 strace 跟踪客户端..."
echo "这会显示所有系统调用，帮助定位崩溃前的最后操作"
echo ""

docker run -it --rm \
    --name lsquic-client-strace \
    --network host \
    --cap-add=SYS_PTRACE \
    lsquic-speed-test:latest \
    bash -c "
        cd /app &&
        strace -f -t -e trace=all \
            ./build/speed_test_client -s 127.0.0.1 -g 0.01 2>&1 | tail -100
    "

echo ""
echo "4. 查看服务器日志:"
docker logs lsquic-server | tail -30

echo ""
echo "5. 清理..."
docker rm -f lsquic-server 2>/dev/null || true

echo ""
echo "=== 调试完成 ==="
