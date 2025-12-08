#!/bin/bash
# 段错误调试脚本

set -e

echo "=== lsquic 段错误调试 ==="
echo ""

# 检查 Docker
if ! command -v docker &> /dev/null; then
    echo "错误: 未安装 Docker"
    exit 1
fi

echo "1. 清理旧容器..."
docker rm -f lsquic-server lsquic-client-debug 2>/dev/null || true

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
echo "3. 使用 valgrind 运行客户端..."
echo "这会检测内存错误、缓冲区溢出等问题"
echo ""

docker run -it --rm \
    --name lsquic-client-debug \
    --network host \
    --cap-add=SYS_PTRACE \
    lsquic-speed-test:latest \
    bash -c "
        cd /app &&
        echo '=== 使用 valgrind 检测内存错误 ===' &&
        valgrind \
            --leak-check=full \
            --show-leak-kinds=all \
            --track-origins=yes \
            --verbose \
            --log-file=/tmp/valgrind.log \
            ./build/speed_test_client -s 127.0.0.1 -g 0.01 2>&1 || true &&
        echo '' &&
        echo '=== Valgrind 报告 ===' &&
        cat /tmp/valgrind.log
    "

echo ""
echo "4. 查看服务器日志:"
docker logs lsquic-server | tail -30

echo ""
echo "5. 清理..."
docker rm -f lsquic-server 2>/dev/null || true

echo ""
echo "=== 调试完成 ==="
