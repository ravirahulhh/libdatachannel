#!/bin/bash
# 使用 GDB 调试段错误

set -e

echo "=== 使用 GDB 调试 lsquic 客户端 ==="
echo ""

# 检查 Docker
if ! command -v docker &> /dev/null; then
    echo "错误: 未安装 Docker"
    exit 1
fi

echo "1. 清理旧容器..."
docker rm -f lsquic-server lsquic-client-gdb 2>/dev/null || true

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
echo "3. 启动 GDB 调试会话..."
echo "GDB 命令提示:"
echo "  run              - 运行程序"
echo "  bt               - 显示堆栈跟踪（崩溃后）"
echo "  info locals      - 显示局部变量"
echo "  print <var>      - 打印变量值"
echo "  quit             - 退出 GDB"
echo ""

docker run -it --rm \
    --name lsquic-client-gdb \
    --network host \
    --cap-add=SYS_PTRACE \
    --security-opt seccomp=unconfined \
    lsquic-speed-test:latest \
    bash -c "
        cd /app &&
        ulimit -c unlimited &&
        echo 'core.%e.%p' > /proc/sys/kernel/core_pattern 2>/dev/null || true &&
        gdb -ex 'set pagination off' \
            -ex 'run -s 127.0.0.1 -g 0.01' \
            -ex 'bt' \
            -ex 'info locals' \
            -ex 'quit' \
            ./build/speed_test_client
    "

echo ""
echo "4. 查看服务器日志:"
docker logs lsquic-server | tail -30

echo ""
echo "5. 清理..."
docker rm -f lsquic-server 2>/dev/null || true

echo ""
echo "=== 调试完成 ==="
