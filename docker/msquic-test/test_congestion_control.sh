#!/bin/bash

# MsQuic 拥塞控制算法性能测试脚本
# 比较 CUBIC 和 BBR 的传输速度

set -e

# 配置
SERVER_ADDR="${SERVER_ADDR:-127.0.0.1}"
SERVER_PORT="${SERVER_PORT:-9331}"
DATA_SIZE="${DATA_SIZE:-1}"  # GB
BUFFER_SIZE="${BUFFER_SIZE:-64}"  # KB
TEST_ROUNDS="${TEST_ROUNDS:-3}"

echo "==================================="
echo "MsQuic 拥塞控制算法性能测试"
echo "==================================="
echo "服务器地址: $SERVER_ADDR:$SERVER_PORT"
echo "数据大小: ${DATA_SIZE} GB"
echo "缓冲区大小: ${BUFFER_SIZE} KB"
echo "测试轮数: $TEST_ROUNDS"
echo ""

# 测试函数
run_test() {
    local algo=$1
    local round=$2
    
    echo "-----------------------------------"
    echo "测试: $algo (第 $round 轮)"
    echo "-----------------------------------"
    
    # 运行客户端
    ./speed_test_client -s "$SERVER_ADDR" -p "$SERVER_PORT" \
                        -g "$DATA_SIZE" -b "$BUFFER_SIZE" \
                        -c "$algo"
    
    echo ""
    sleep 2
}

# 测试 CUBIC
echo ""
echo "========== 测试 CUBIC =========="
for i in $(seq 1 $TEST_ROUNDS); do
    run_test "cubic" "$i"
done

# 测试 BBR
echo ""
echo "========== 测试 BBR =========="
for i in $(seq 1 $TEST_ROUNDS); do
    run_test "bbr" "$i"
done

echo "==================================="
echo "测试完成！"
echo "==================================="
