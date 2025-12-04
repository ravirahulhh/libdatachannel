# MsQuic 传输速度测试

使用 Docker 在两台 Linux 服务器之间测试 MsQuic QUIC 协议的传输速度。

## 快速开始

### 方式一：使用部署脚本

```bash
# 添加执行权限
chmod +x deploy.sh

# 1. 在服务器 A (接收端) 上
./deploy.sh build
./deploy.sh server

# 2. 在服务器 B (发送端) 上
./deploy.sh build
./deploy.sh client -s <服务器A的IP> -g 5  # 发送 5GB 数据
```

### 方式二：使用 Docker Compose (本地测试)

```bash
# 本地测试 (同一台机器)
docker-compose up --build
```

### 方式三：手动 Docker 命令

```bash
# 构建镜像
docker build -t msquic-speed-test:latest .

# 服务器 A - 启动服务端
docker run -d --name msquic-server --network host \
    msquic-speed-test:latest \
    bash -c "cd /app && \
        openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes -subj '/CN=localhost' && \
        ./build/speed_test_server"

# 服务器 B - 启动客户端
docker run -it --rm --network host \
    msquic-speed-test:latest \
    /app/build/speed_test_client -s <服务器A的IP> -g 1
```

## 命令行参数

### 服务端
服务端默认监听 UDP 4433 端口，无需额外参数。

### 客户端
```
-s <address>  服务器地址 (必需)
-g <size>     发送数据大小 (GB), 默认 1
-b <size>     缓冲区大小 (KB), 默认 64
-h            显示帮助
```

## 防火墙配置

确保服务器 A 开放 UDP 4433 端口：

```bash
# Ubuntu/Debian (ufw)
sudo ufw allow 4433/udp

# CentOS/RHEL (firewalld)
sudo firewall-cmd --add-port=4433/udp --permanent
sudo firewall-cmd --reload

# iptables
sudo iptables -A INPUT -p udp --dport 4433 -j ACCEPT
```

## 测试结果示例

```
=== MsQuic Speed Test Client ===
Server: 192.168.1.100:4433
Data size: 5 GB
Buffer size: 64 KB

[Client] Connecting to 192.168.1.100:4433...
[Client] Connected to server!
[Client] Starting data transfer: 5 GB
[Client] Sent: 5120.00 MB | Speed: 892.45 Mbps | Progress: 100.00%

=== Transfer Complete ===
Total sent: 5.00 GB
Time: 45.87 seconds
Average speed: 892.45 Mbps
```

## 性能优化建议

1. **网络配置**
   ```bash
   # 增加 UDP 缓冲区
   sudo sysctl -w net.core.rmem_max=26214400
   sudo sysctl -w net.core.wmem_max=26214400
   ```

2. **Docker 网络模式**
   - 使用 `--network host` 获得最佳性能
   - 避免使用 bridge 网络模式

3. **CPU 亲和性**
   ```bash
   docker run --cpuset-cpus="0-3" ...
   ```

## 故障排除

### 连接失败
- 检查防火墙是否开放 UDP 4433
- 确认服务端已启动: `docker logs msquic-server`
- 测试 UDP 连通性: `nc -vzu <server_ip> 4433`

### 速度慢
- 检查网络带宽: `iperf3 -c <server_ip>`
- 增加缓冲区大小: `-b 128` (128KB)
- 检查 CPU 使用率

### 证书错误
- 客户端默认不验证证书
- 如需验证，请配置正确的 CA 证书

## 文件说明

```
docker/msquic-test/
├── Dockerfile              # Docker 镜像定义
├── docker-compose.yml      # Docker Compose 配置
├── deploy.sh               # 快速部署脚本
├── CMakeLists.txt          # CMake 构建配置
├── speed_test_server.cpp   # 服务端源码
├── speed_test_client.cpp   # 客户端源码
└── README.md               # 本文档
```
