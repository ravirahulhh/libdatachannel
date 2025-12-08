# CUBIC vs BBR 拥塞控制算法对比测试指南

## 快速测试步骤

### 1. 本地快速测试

```bash
cd docker/msquic-test

# 构建镜像
./deploy.sh build

# 测试 CUBIC
docker run -d --name msquic-server --network host msquic-speed-test:latest \
  bash -c "cd /app && openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes -subj '/CN=localhost' 2>/dev/null && ./build/speed_test_server -c cubic"

sleep 2
docker run --rm --network host msquic-speed-test:latest \
  /app/build/speed_test_client -s 127.0.0.1 -g 1 -c cubic

docker stop msquic-server && docker rm msquic-server

# 测试 BBR
docker run -d --name msquic-server --network host msquic-speed-test:latest \
  bash -c "cd /app && openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes -subj '/CN=localhost' 2>/dev/null && ./build/speed_test_server -c bbr"

sleep 2
docker run --rm --network host msquic-speed-test:latest \
  /app/build/speed_test_client -s 127.0.0.1 -g 1 -c bbr

docker stop msquic-server && docker rm msquic-server
```

### 2. 使用测试脚本

```bash
# 进入容器
docker run -it --rm --network host msquic-speed-test:latest bash

# 在容器内运行测试脚本
cd /app/build
../test_congestion_control.sh
```

### 3. 两台服务器测试

#### 服务器 A (运行服务端)

```bash
# 测试 CUBIC
./deploy.sh server -c cubic

# 或测试 BBR
./deploy.sh server -c bbr
```

#### 服务器 B (运行客户端)

```bash
# 连接到服务器 A，测试 CUBIC
./deploy.sh client -s <服务器A的IP> -g 5 -c cubic

# 连接到服务器 A，测试 BBR
./deploy.sh client -s <服务器A的IP> -g 5 -c bbr
```

## 测试场景

### 场景 1: 理想网络环境 (低延迟、无丢包)

在这种环境下，CUBIC 和 BBR 的性能应该相近。

```bash
# CUBIC
./speed_test_client -s <server> -g 5 -c cubic

# BBR
./speed_test_client -s <server> -g 5 -c bbr
```

**预期结果**: 两者速度相近，BBR 可能启动稍快。

### 场景 2: 高延迟网络 (100ms+)

BBR 在高延迟网络中通常表现更好。

```bash
# 在服务器上模拟延迟
sudo tc qdisc add dev eth0 root netem delay 100ms

# 测试 CUBIC
./speed_test_client -s <server> -g 5 -c cubic

# 测试 BBR
./speed_test_client -s <server> -g 5 -c bbr

# 清除延迟
sudo tc qdisc del dev eth0 root
```

**预期结果**: BBR 应该达到更高的吞吐量。

### 场景 3: 有丢包的网络 (1-5% 丢包率)

BBR 对丢包的容忍度更高。

```bash
# 模拟 2% 丢包
sudo tc qdisc add dev eth0 root netem loss 2%

# 测试 CUBIC
./speed_test_client -s <server> -g 5 -c cubic

# 测试 BBR
./speed_test_client -s <server> -g 5 -c bbr

# 清除设置
sudo tc qdisc del dev eth0 root
```

**预期结果**: BBR 在丢包环境下保持更稳定的吞吐量。

### 场景 4: 带宽受限网络

测试在不同带宽限制下的表现。

```bash
# 限制带宽到 100Mbps
sudo tc qdisc add dev eth0 root tbf rate 100mbit burst 32kbit latency 400ms

# 测试两种算法
./speed_test_client -s <server> -g 2 -c cubic
./speed_test_client -s <server> -g 2 -c bbr

# 清除限制
sudo tc qdisc del dev eth0 root
```

**预期结果**: BBR 应该更快地达到带宽上限。

## 性能指标对比

### 关键指标

1. **启动时间**: 从连接建立到达到最大速度的时间
2. **平均吞吐量**: 整个传输过程的平均速度
3. **峰值吞吐量**: 达到的最高速度
4. **稳定性**: 速度波动程度
5. **完成时间**: 传输相同数据量所需的总时间

### 记录模板

```
测试环境:
- 网络延迟: ___ ms
- 丢包率: ___ %
- 带宽限制: ___ Mbps
- 数据大小: ___ GB

CUBIC 结果:
- 平均速度: ___ Mbps
- 峰值速度: ___ Mbps
- 完成时间: ___ 秒
- 观察: ___

BBR 结果:
- 平均速度: ___ Mbps
- 峰值速度: ___ Mbps
- 完成时间: ___ 秒
- 观察: ___

结论: ___
```

## 常见问题

### Q: 为什么 BBR 速度波动更大？

A: BBR 会主动探测网络带宽，这可能导致短期的速度波动，但通常能达到更高的平均吞吐量。

### Q: 在什么情况下应该使用 CUBIC？

A: 在低延迟、稳定的网络环境中，CUBIC 表现良好且更保守，适合对稳定性要求高的场景。

### Q: 在什么情况下应该使用 BBR？

A: 在高延迟、有丢包或带宽波动的网络中，BBR 通常能提供更好的性能。

### Q: 如何选择合适的缓冲区大小？

A: 
- 低延迟网络: 32-64 KB
- 高延迟网络: 64-128 KB
- 高带宽网络: 128-256 KB

可以通过 `-b` 参数测试不同大小的影响。

## 自动化测试脚本示例

```bash
#!/bin/bash
# 完整的对比测试脚本

SERVER="192.168.1.100"
SIZES=(1 5 10)
ALGORITHMS=("cubic" "bbr")

echo "开始拥塞控制算法对比测试"
echo "服务器: $SERVER"
echo ""

for size in "${SIZES[@]}"; do
    for algo in "${ALGORITHMS[@]}"; do
        echo "========================================="
        echo "测试: $algo - ${size}GB"
        echo "========================================="
        
        ./speed_test_client -s "$SERVER" -g "$size" -c "$algo" 2>&1 | tee "result_${algo}_${size}GB.log"
        
        echo ""
        sleep 5
    done
done

echo "测试完成！结果已保存到 result_*.log 文件"
```

## 网络模拟工具

### Linux tc (Traffic Control)

```bash
# 添加延迟
sudo tc qdisc add dev eth0 root netem delay 100ms

# 添加丢包
sudo tc qdisc add dev eth0 root netem loss 2%

# 添加带宽限制
sudo tc qdisc add dev eth0 root tbf rate 100mbit burst 32kbit latency 400ms

# 组合多个条件
sudo tc qdisc add dev eth0 root netem delay 100ms loss 2%

# 查看当前设置
sudo tc qdisc show dev eth0

# 删除所有设置
sudo tc qdisc del dev eth0 root
```

### macOS Network Link Conditioner

在 macOS 上可以使用 Network Link Conditioner 工具来模拟不同的网络条件。

## 参考资料

- [BBR: Congestion-Based Congestion Control](https://queue.acm.org/detail.cfm?id=3022184)
- [CUBIC: A New TCP-Friendly High-Speed TCP Variant](https://www.cs.princeton.edu/courses/archive/fall16/cos561/papers/Cubic08.pdf)
- [MsQuic Performance Guide](https://github.com/microsoft/msquic/blob/main/docs/Performance.md)
