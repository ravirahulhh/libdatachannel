# 快速开始指南

## 一、编译

```bash
cd ice_quic_transport
./build_speed_test.sh
cd build
```

## 二、生成证书

```bash
openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt \
    -days 365 -nodes -subj "/CN=localhost"
```

## 三、运行测试（可选）

```bash
./test_connection.sh
```

## 四、启动服务端

**在服务器 A 上运行:**

```bash
./speed_test_server server.crt server.key
```

**记录输出的信息:**
- ufrag: `<记录这个值>`
- pwd: `<记录这个值>`
- candidates: `<记录所有候选>`

## 五、启动客户端

**在服务器 B 上运行:**

```bash
# 发送 1 GB 数据
./speed_test_client 1

# 或发送 5 GB 数据
./speed_test_client 5
```

**记录输出的信息:**
- ufrag: `<记录这个值>`
- pwd: `<记录这个值>`
- candidates: `<记录所有候选>`

## 六、交换 ICE 信息

### 在客户端输入服务端信息:

```
远程 ufrag: <服务端的ufrag>
远程 pwd: <服务端的pwd>
输入远程ICE候选 (每行一个，空行结束):
<服务端的candidate 1>
<服务端的candidate 2>
<空行>
```

### 在服务端输入客户端信息:

```
远程 ufrag: <客户端的ufrag>
远程 pwd: <客户端的pwd>
输入远程ICE候选 (每行一个，空行结束):
<客户端的candidate 1>
<客户端的candidate 2>
<空行>
```

## 七、开始传输

信息交换完成后，程序会自动:
1. 建立 ICE+QUIC 连接
2. 开始传输数据
3. 实时显示速度
4. 完成后显示统计信息

## 重要提示

### ✓ 候选格式

**正确** (不带 `a=` 前缀):
```
candidate:1 1 UDP 2130706431 192.168.1.100 54321 typ host
```

**错误** (带 `a=` 前缀):
```
a=candidate:1 1 UDP 2130706431 192.168.1.100 54321 typ host
```

> 新版本会自动移除 `a=` 前缀，但建议手动移除

### ✓ 防火墙

确保允许 UDP 流量:

```bash
# Ubuntu/Debian
sudo ufw allow proto udp

# CentOS/RHEL
sudo firewall-cmd --permanent --add-port=1024-65535/udp
sudo firewall-cmd --reload
```

### ✓ 网络连通性

测试两台服务器是否可以互相访问:

```bash
ping <对端IP>
```

## 预期输出

### 客户端:
```
[SPEED] Send: 125.34 Mbps | ACK: 124.89 Mbps | Sent: 156.25 MB | Acked: 155.87 MB
...
========== Transfer Complete ==========
Total Sent: 1024.00 MB (1.00 GB)
Total Acked: 1023.45 MB
Transfer Time: 65.23 seconds
Average Send Speed: 125.78 Mbps
Average ACK Speed: 125.45 Mbps
=======================================
```

### 服务端:
```
[SPEED] 124.89 Mbps | Received: 156.11 MB
...
========== Transfer Complete ==========
Total Received: 1024.00 MB (1.00 GB)
Transfer Time: 65.45 seconds
Average Speed: 125.32 Mbps
=======================================
```

## 遇到问题？

- **连接超时**: 查看 [TROUBLESHOOTING.md](TROUBLESHOOTING.md#1-连接超时)
- **速度慢**: 查看 [TROUBLESHOOTING.md](TROUBLESHOOTING.md#2-传输速度慢)
- **连接中断**: 查看 [TROUBLESHOOTING.md](TROUBLESHOOTING.md#3-连接中断)

## 完整文档

- [详细使用说明](SPEED_TEST_README.md)
- [故障排查指南](TROUBLESHOOTING.md)
