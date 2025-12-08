#!/bin/bash
# 快速测试脚本 - 验证段错误是否已修复

set -e

echo "=== lsquic 快速测试 ==="
echo ""

# 检查 Docker
if ! command -v docker &> /dev/null; then
    echo "错误: 未安装 Docker"
    exit 1
fi

# 检查镜像是否存在
if ! docker image inspect lsquic-speed-test:latest &> /dev/null; then
    echo "错误: 镜像不存在，请先运行: ./deploy.sh build"
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
echo "3. 测试客户端 (发送 1MB 数据)..."
echo ""

# 运行客户端并捕获退出码
EXIT_CODE=0
docker run -it --rm \
    --name lsquic-client \
    --network host \
    lsquic-speed-test:latest \
    timeout 30 /app/build/speed_test_client -s 127.0.0.1 -g 0.001 || EXIT_CODE=$?

echo ""
echo "客户端退出码: $EXIT_CODE"

if [ $EXIT_CODE -eq 139 ]; then
    echo "❌ 段错误 (退出码 139) - 仍然存在问题"
    echo ""
    echo "建议运行详细调试:"
    echo "  ./debug-segfault.sh    # 使用 valgrind"
    echo "  ./debug-with-gdb.sh    # 使用 gdb"
    RESULT="FAILED"
elif [ $EXIT_CODE -eq 124 ]; then
    echo "⚠️  超时 (30秒) - 可能连接失败"
    RESULT="TIMEOUT"
elif [ $EXIT_CODE -eq 0 ]; then
    echo "✅ 成功完成"
    RESULT="SUCCESS"
else
    echo "⚠️  其他错误 (退出码 $EXIT_CODE)"
    RESULT="ERROR"
fi

echo ""
echo "4. 服务器日志 (最后 20 行):"
docker logs lsquic-server | tail -20

echo ""
echo "5. 清理..."
docker rm -f lsquic-server 2>/dev/null || true

echo ""
echo "=== 测试结果: $RESULT ==="

if [ "$RESULT" = "FAILED" ]; then
    exit 1
elif [ "$RESULT" = "TIMEOUT" ]; then
    exit 2
elif [ "$RESULT" = "ERROR" ]; then
    exit 3
else
    exit 0
fi
