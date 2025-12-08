# 构建和测试指南

## 构建说明

### 启用 BBR 支持

本项目已配置为支持 CUBIC 和 BBR 拥塞控制算法。BBR 需要 MsQuic 的预览功能支持。

#### Dockerfile 配置

```dockerfile
# 编译 MsQuic 时启用预览功能
cmake -DQUIC_ENABLE_PREVIEW_FEATURES=ON ...
```

#### CMakeLists.txt 配置

```cmake
# 启用预览功能宏定义
add_definitions(-DQUIC_API_ENABLE_PREVIEW_FEATURES)
```

### 构建 Docker 镜像

```bash
cd docker/msquic-test
./deploy.sh build
```

构建过程包括：
1. 安装依赖包
2. 克隆并编译 MsQuic（启用预览功能）
3. 编译测试程序

## 快速测试

### 1. 本地测试（推荐用于验证）

```bash
# 测试 CUBIC
docker run -d --name test-server --network host msquic-speed-test:latest \
  bash -c "cd /app && openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes -subj '/CN=localhost' 2>/dev/null && ./build/speed_test_server -c cubic"

sleep 2

docker run --rm --network host msquic-speed-test:latest \
  /app/build/speed_test_client -s 127.0.0.1 -g 1 -c cubic

docker stop test-server && docker rm test-server

# 测试 BBR
docker run -d --name test-server --network host msquic-speed-test:latest \
  bash -c "cd /app && openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes -subj '/CN=localhost' 2>/dev/null && ./build/speed_test_server -c bbr"

sleep 2

docker run --rm --network host msquic-speed-test:latest \
  /app/build/speed_test_client -s 127.0.0.1 -g 1 -c bbr

docker stop test-server && docker rm test-server
```

### 2. 使用部署脚本

```bash
# 启动服务端（CUBIC）
./deploy.sh server -c cubic

# 在另一个终端运行客户端
./deploy.sh client -s 127.0.0.1 -g 1 -c cubic

# 清理
./deploy.sh clean

# 启动服务端（BBR）
./deploy.sh server -c bbr

# 在另一个终端运行客户端
./deploy.sh client -s 127.0.0.1 -g 1 -c bbr
```

### 3. 使用自动化测试脚本

```bash
# 进入容器
docker run -it --rm --network host msquic-speed-test:latest bash

# 在容器内
cd /app/build

# 在后台启动服务端
../speed_test_server -c cubic &
SERVER_PID=$!

# 等待服务端启动
sleep 2

# 运行测试脚本
../test_congestion_control.sh

# 停止服务端
kill $SERVER_PID
```

## 验证 BBR 是否正常工作

### 方法 1: 查看日志输出

运行客户端时，应该看到：

```
[Client] Connected to server!
[Client] Using BBR congestion control
```

服务端应该显示：

```
[Server] Client connected! Using BBR congestion control
```

### 方法 2: 比较性能差异

在高延迟网络环境下，BBR 应该表现出更好的性能：

```bash
# 添加 100ms 延迟
sudo tc qdisc add dev eth0 root netem delay 100ms

# 测试 CUBIC
./speed_test_client -s <server> -g 2 -c cubic
# 记录平均速度

# 测试 BBR
./speed_test_client -s <server> -g 2 -c bbr
# 记录平均速度，应该高于 CUBIC

# 清除延迟
sudo tc qdisc del dev eth0 root
```

## 故障排查

### 编译错误：找不到 BBR 常量

**错误信息**:
```
error: 'QUIC_CONGESTION_CONTROL_ALGORITHM_BBR' was not declared in this scope
```

**解决方案**:
1. 确认 Dockerfile 中启用了预览功能：
   ```dockerfile
   -DQUIC_ENABLE_PREVIEW_FEATURES=ON
   ```

2. 确认 CMakeLists.txt 中添加了宏定义：
   ```cmake
   add_definitions(-DQUIC_API_ENABLE_PREVIEW_FEATURES)
   ```

3. 重新构建镜像：
   ```bash
   docker rmi msquic-speed-test:latest
   ./deploy.sh build
   ```

### 运行时错误：连接失败

**可能原因**:
1. 防火墙阻止 UDP 9331 端口
2. 服务端未启动
3. 证书问题

**解决方案**:
```bash
# 检查服务端日志
docker logs msquic-server

# 检查端口
sudo netstat -ulnp | grep 9331

# 检查防火墙（Ubuntu/Debian）
sudo ufw status
sudo ufw allow 9331/udp

# 检查防火墙（CentOS/RHEL）
sudo firewall-cmd --list-all
sudo firewall-cmd --add-port=9331/udp --permanent
sudo firewall-cmd --reload
```

### BBR 性能未达预期

**检查项**:
1. 确认网络环境有足够的延迟或丢包（BBR 在理想网络中优势不明显）
2. 增加数据传输量（至少 5GB）
3. 检查系统资源（CPU、内存、网络带宽）
4. 尝试调整缓冲区大小

```bash
# 测试不同缓冲区大小
./speed_test_client -s <server> -g 5 -b 32 -c bbr   # 32KB
./speed_test_client -s <server> -g 5 -b 64 -c bbr   # 64KB
./speed_test_client -s <server> -g 5 -b 128 -c bbr  # 128KB
```

## 性能测试建议

### 测试环境要求

1. **网络带宽**: 至少 100 Mbps
2. **延迟**: 建议测试不同延迟场景（0ms, 50ms, 100ms, 200ms）
3. **丢包率**: 建议测试不同丢包率（0%, 1%, 2%, 5%）
4. **数据量**: 建议至少 5GB 以获得稳定结果

### 测试步骤

1. **基准测试**（无延迟、无丢包）
   ```bash
   ./speed_test_client -s <server> -g 10 -c cubic
   ./speed_test_client -s <server> -g 10 -c bbr
   ```

2. **高延迟测试**
   ```bash
   sudo tc qdisc add dev eth0 root netem delay 100ms
   ./speed_test_client -s <server> -g 10 -c cubic
   ./speed_test_client -s <server> -g 10 -c bbr
   sudo tc qdisc del dev eth0 root
   ```

3. **丢包测试**
   ```bash
   sudo tc qdisc add dev eth0 root netem loss 2%
   ./speed_test_client -s <server> -g 10 -c cubic
   ./speed_test_client -s <server> -g 10 -c bbr
   sudo tc qdisc del dev eth0 root
   ```

4. **综合测试**
   ```bash
   sudo tc qdisc add dev eth0 root netem delay 100ms loss 2%
   ./speed_test_client -s <server> -g 10 -c cubic
   ./speed_test_client -s <server> -g 10 -c bbr
   sudo tc qdisc del dev eth0 root
   ```

### 结果记录

建议记录以下指标：
- 平均吞吐量 (Mbps)
- 峰值吞吐量 (Mbps)
- 传输时间 (秒)
- 速度稳定性（观察速度波动）

## 参考资料

- [MsQuic 官方文档](https://github.com/microsoft/msquic)
- [MsQuic API 参考](https://github.com/microsoft/msquic/blob/main/docs/API.md)
- [MsQuic 预览功能](https://github.com/microsoft/msquic/blob/main/docs/PreviewFeatures.md)
- [BBR 拥塞控制算法](https://queue.acm.org/detail.cfm?id=3022184)
