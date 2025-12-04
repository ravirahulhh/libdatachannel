# lsquic 传输速度测试

使用 Docker 在两台 Linux 服务器之间测试 lsquic QUIC 协议的传输速度。

## 快速开始

### 方式一：使用部署脚本 (推荐)

```bash
# 添加执行权限
chmod +x deploy.sh

# 1. 在服务器 A (接收端) 上
./deploy.sh build      # 首次构建需要 10-20 分钟
./deploy.sh server

# 2. 在服务器 B (发送端) 上
./deploy.sh build
./deploy.sh client -s <服务器A的IP> -g 5  # 发送 5GB 数据
```

### 方式二：使用 Docker Compose (本地测试)

```bash
# 本地测试 (同一台机器)
docker-compose up --build

# 或指定数据量
DATA_SIZE_GB=5 docker-compose up --build
```

### 方式三：手动 Docker 命令

```bash
# 构建镜像
docker build -t lsquic-speed-test:latest .

# 服务器 A - 启动服务端
docker run -d --name lsquic-server --network host \
    lsquic-speed-test:latest \
    bash -c "cd /app && \
        openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes -subj '/CN=localhost' && \
        ./build/speed_test_server"

# 服务器 B - 启动客户端
docker run -it --rm --network host \
    lsquic-speed-test:latest \
    /app/build/speed_test_client -s <服务器A的IP> -g 1
```

## 命令行参数

### 服务端
服务端默认监听 UDP 9331 端口，无需额外参数。

### 客户端
```
-s <address>  服务器地址 (必需)
-p <port>     服务器端口, 默认 9331
-g <size>     发送数据大小 (GB), 默认 1
-b <size>     缓冲区大小 (KB), 默认 64
-h            显示帮助
```

## 防火墙配置

确保服务器 A 开放 UDP 9331 端口：

```bash
# Ubuntu/Debian (ufw)
sudo ufw allow 9331/udp

# CentOS/RHEL (firewalld)
sudo firewall-cmd --add-port=9331/udp --permanent
sudo firewall-cmd --reload

# iptables
sudo iptables -A INPUT -p udp --dport 9331 -j ACCEPT
```

## 测试结果示例

```
=== lsquic Speed Test Client ===
Server: 192.168.1.100:9331
Data size: 5 GB
Buffer size: 64 KB

[Client] Connecting to 192.168.1.100:9331...
[Client] Connected to server!
[Client] Stream created, starting data transfer: 5 GB
[Client] Sent: 5120.00 MB | Speed: 856.32 Mbps | Progress: 100.00%

=== Transfer Complete ===
Total sent: 5.00 GB
Time: 47.82 seconds
Average speed: 856.32 Mbps
```

## 性能优化建议

1. **网络配置**
   ```bash
   # 增加 UDP 缓冲区
   sudo sysctl -w net.core.rmem_max=26214400
   sudo sysctl -w net.core.wmem_max=26214400
   sudo sysctl -w net.core.rmem_default=26214400
   sudo sysctl -w net.core.wmem_default=26214400
   ```

2. **Docker 网络模式**
   - 使用 `--network host` 获得最佳性能
   - 避免使用 bridge 网络模式

3. **CPU 亲和性**
   ```bash
   docker run --cpuset-cpus="0-3" ...
   ```

## lsquic vs msquic 对比测试

如果你想对比两种 QUIC 实现的性能，可以：

```bash
# 测试 lsquic
cd docker/lsquic-test
./deploy.sh local-test 5

# 测试 msquic
cd docker/msquic-test
./deploy.sh local-test 5
```

## 故障排除

### 连接失败
- 检查防火墙是否开放 UDP 9331
- 确认服务端已启动: `docker logs lsquic-server`
- 测试 UDP 连通性: `nc -vzu <server_ip> 9331`

### 构建失败
- 确保有足够的磁盘空间 (至少 5GB)
- 确保网络可以访问 GitHub (克隆 BoringSSL 和 lsquic)

### 速度慢
- 检查网络带宽: `iperf3 -c <server_ip>`
- 增加缓冲区大小: `-b 128` (128KB)
- 检查 CPU 使用率

## 文件说明

```
docker/lsquic-test/
├── Dockerfile              # Docker 镜像定义
├── docker-compose.yml      # Docker Compose 配置
├── deploy.sh               # 快速部署脚本
├── CMakeLists.txt          # CMake 构建配置
├── speed_test_server.cpp   # 服务端源码
├── speed_test_client.cpp   # 客户端源码
└── README.md               # 本文档
```

## 技术说明

- 基于 lsquic (LiteSpeed QUIC) 库
- 使用 BoringSSL 作为 TLS 后端
- 使用 libevent 进行事件驱动 I/O
- 支持 QUIC v1 协议
