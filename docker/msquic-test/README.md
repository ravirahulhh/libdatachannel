# MsQuic 速度测试工具

基于 MsQuic 的网络传输速度测试工具，支持 CUBIC 和 BBR 拥塞控制算法对比测试。

## 功能特性

- 支持客户端/服务端模式
- 实时显示传输速度和进度
- 支持 CUBIC 和 BBR 拥塞控制算法
- 可配置数据大小和缓冲区大小
- Docker 容器化部署

## 拥塞控制算法

### CUBIC (默认)
- 传统的拥塞控制算法
- 适合高带宽、低延迟网络
- 更保守的拥塞窗口增长策略

### BBR (Bottleneck Bandwidth and RTT)
- Google 开发的现代拥塞控制算法
- 基于带宽和 RTT 的主动探测
- 在高延迟、丢包网络中表现更好
- 能更快地达到最大带宽利用率

## 快速开始

### 1. 构建镜像

```bash
cd docker/msquic-test
./deploy.sh build
```

### 2. 本地测试

```bash
# 使用默认配置 (CUBIC, 1GB)
./deploy.sh local-test

# 指定数据大小
./deploy.sh local-test 5
```

### 3. 服务器部署

#### 在服务器 A 上启动服务端

```bash
# 使用 CUBIC (默认)
./deploy.sh server

# 使用 BBR
./deploy.sh server -c bbr

# 查看日志
docker logs -f msquic-server
```

#### 在服务器 B 上启动客户端

```bash
# 使用 CUBIC 传输 5GB 数据
./deploy.sh client -s 192.168.1.100 -g 5

# 使用 BBR 传输 10GB 数据
./deploy.sh client -s 192.168.1.100 -g 10 -c bbr
```

## 拥塞控制算法对比测试

### 方法 1: 使用测试脚本

```bash
# 在容器内运行
docker exec -it msquic-server bash
cd /app/build

# 运行对比测试 (默认 3 轮，每轮 1GB)
../test_congestion_control.sh

# 自定义测试参数
SERVER_ADDR=192.168.1.100 DATA_SIZE=5 TEST_ROUNDS=5 ../test_congestion_control.sh
```

### 方法 2: 手动测试

```bash
# 1. 启动服务端 (CUBIC)
./deploy.sh server -c cubic

# 2. 运行客户端测试 CUBIC
./deploy.sh client -s <server_ip> -g 5 -c cubic

# 3. 重启服务端 (BBR)
./deploy.sh clean
./deploy.sh server -c bbr

# 4. 运行客户端测试 BBR
./deploy.sh client -s <server_ip> -g 5 -c bbr
```

## 命令行参数

### 服务端 (speed_test_server)

```bash
./speed_test_server [options]

选项:
  -c <algorithm>  拥塞控制算法: cubic 或 bbr (默认: cubic)
  -h              显示帮助
```

### 客户端 (speed_test_client)

```bash
./speed_test_client [options]

选项:
  -s <address>    服务器地址 (默认: 127.0.0.1)
  -p <port>       服务器端口 (默认: 9331)
  -g <size>       数据大小 (GB) (默认: 1)
  -b <size>       缓冲区大小 (KB) (默认: 64)
  -c <algorithm>  拥塞控制算法: cubic 或 bbr (默认: cubic)
  -h              显示帮助
```

## 测试示例

### 示例 1: 对比不同算法在相同条件下的性能

```bash
# CUBIC 测试
./speed_test_client -s 192.168.1.100 -g 10 -c cubic

# BBR 测试
./speed_test_client -s 192.168.1.100 -g 10 -c bbr
```

### 示例 2: 测试不同缓冲区大小的影响

```bash
# 小缓冲区 (32KB) + CUBIC
./speed_test_client -s 192.168.1.100 -g 5 -b 32 -c cubic

# 大缓冲区 (128KB) + BBR
./speed_test_client -s 192.168.1.100 -g 5 -b 128 -c bbr
```

### 示例 3: 高延迟网络测试

在高延迟网络环境下，BBR 通常表现更好：

```bash
# 使用 tc 模拟 100ms 延迟
sudo tc qdisc add dev eth0 root netem delay 100ms

# 测试 CUBIC
./speed_test_client -s <server> -g 5 -c cubic

# 测试 BBR
./speed_test_client -s <server> -g 5 -c bbr

# 清除延迟设置
sudo tc qdisc del dev eth0 root
```

## 性能指标

测试过程中会显示以下指标：

- **Queued**: 已排队发送的数据量
- **Acked**: 已确认接收的数据量
- **Speed**: 当前传输速度 (Mbps)
- **Pending**: 待确认的缓冲区数量
- **Progress**: 传输进度百分比
- **Instant**: 瞬时速度 (服务端)
- **Avg**: 平均速度

## 预期结果

### CUBIC 特点
- 启动较慢，逐步增加速度
- 在低延迟网络中表现稳定
- 遇到丢包时恢复较慢

### BBR 特点
- 快速达到最大带宽
- 在高延迟网络中保持高吞吐量
- 对网络抖动和丢包更有弹性
- 可能在某些网络中表现出更高的速度波动

## 故障排查

### 连接失败

1. 检查防火墙是否开放 UDP 9331 端口
2. 确认服务端正在运行: `docker logs msquic-server`
3. 验证网络连通性: `ping <server_ip>`

### 速度异常

1. 检查网络带宽限制
2. 尝试调整缓冲区大小 (`-b` 参数)
3. 查看系统资源使用情况 (CPU, 内存)
4. 尝试不同的拥塞控制算法

### 证书错误

服务端会自动生成自签名证书，如果遇到证书问题：

```bash
docker exec -it msquic-server bash
cd /app
openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes -subj '/CN=localhost'
```

## 清理

```bash
# 停止并删除容器
./deploy.sh clean

# 删除镜像
docker rmi msquic-speed-test:latest
```

## 技术细节

- **协议**: QUIC (基于 UDP)
- **TLS**: 自签名证书
- **默认端口**: 9331 (UDP)
- **ALPN**: speedtest
- **流控**: 最大 64 个待确认缓冲区

## 参考资料

- [MsQuic 官方文档](https://github.com/microsoft/msquic)
- [BBR 论文](https://queue.acm.org/detail.cfm?id=3022184)
- [CUBIC 论文](https://www.cs.princeton.edu/courses/archive/fall16/cos561/papers/Cubic08.pdf)
