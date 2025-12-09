# ICE+QUIC 速度测试故障排查指南

## 常见问题

### 1. 连接超时

**症状**: 显示 "连接超时!" 或 "Connection timeout!"

**可能原因和解决方案**:

#### A. ICE 候选格式错误

**问题**: 输入的候选格式不正确

**正确格式**: 候选应该是这样的格式（**不带** `a=` 前缀）:
```
candidate:1 1 UDP 2130706431 192.168.1.100 54321 typ host
candidate:2 1 UDP 1694498815 203.0.113.1 54321 typ srflx raddr 192.168.1.100 rport 54321
```

**错误格式**: 如果你的候选是这样的（**带** `a=` 前缀）:
```
a=candidate:1 1 UDP 2130706431 192.168.1.100 54321 typ host
```

**解决方案**: 
- 新版本的程序会自动移除 `a=` 前缀
- 如果使用旧版本，请手动移除 `a=` 前缀后再输入

#### B. 防火墙阻止 UDP 流量

**问题**: 防火墙阻止了 ICE/QUIC 使用的 UDP 端口

**检查方法**:
```bash
# 在服务器上检查防火墙状态
sudo ufw status  # Ubuntu/Debian
sudo firewall-cmd --list-all  # CentOS/RHEL
```

**解决方案**:
```bash
# Ubuntu/Debian - 允许 UDP 流量
sudo ufw allow proto udp

# CentOS/RHEL - 允许 UDP 流量
sudo firewall-cmd --permanent --add-port=1024-65535/udp
sudo firewall-cmd --reload
```

#### C. 网络不可达

**问题**: 两台服务器之间网络不通

**检查方法**:
```bash
# 从客户端 ping 服务端
ping <服务端IP>

# 检查路由
traceroute <服务端IP>
```

**解决方案**:
- 确保两台服务器在同一网络或可以互相访问
- 检查网络路由配置
- 如果在云环境，检查安全组/网络ACL设置

#### D. STUN 服务器不可达

**问题**: 无法连接到 STUN 服务器获取公网地址

**检查方法**:
```bash
# 测试 STUN 服务器连通性
nc -u -v stun.l.google.com 19302
```

**解决方案**:
- 使用其他 STUN 服务器（需要修改代码中的 `config.stunServers`）
- 可选的 STUN 服务器:
  - `stun.l.google.com:19302`
  - `stun1.l.google.com:19302`
  - `stun2.l.google.com:19302`
  - `stun.stunprotocol.org:3478`

### 2. 传输速度慢

**症状**: 传输速度远低于预期

**可能原因和解决方案**:

#### A. 网络带宽限制

**检查方法**:
```bash
# 使用 iperf3 测试网络带宽
# 在服务端
iperf3 -s

# 在客户端
iperf3 -c <服务端IP>
```

#### B. CPU 使用率过高

**检查方法**:
```bash
# 监控 CPU 使用率
top
htop
```

**解决方案**:
- 使用性能更好的服务器
- 调整数据块大小（修改代码中的 `chunkSize`）

#### C. 数据块大小不合适

**当前默认**: 64 KB

**调整方法**: 在代码中修改
```cpp
const size_t chunkSize = 128 * 1024;  // 改为 128 KB
```

### 3. 连接中断

**症状**: 传输过程中连接断开

**可能原因和解决方案**:

#### A. 空闲超时

**当前默认**: 5 分钟 (300000 ms)

**解决方案**: 增加超时时间（修改代码）
```cpp
config.idleTimeoutMs = 600000;  // 改为 10 分钟
```

#### B. 网络不稳定

**检查方法**:
```bash
# 持续 ping 检查网络稳定性
ping -c 100 <对端IP>
```

**解决方案**:
- 改善网络环境
- 使用有线连接而非无线

### 4. TLS 证书错误

**症状**: 服务端启动失败，提示证书相关错误

**解决方案**:

#### 生成新的自签名证书
```bash
openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt \
    -days 365 -nodes -subj "/CN=localhost"
```

#### 检查证书文件权限
```bash
chmod 600 server.key
chmod 644 server.crt
```

#### 验证证书
```bash
openssl x509 -in server.crt -text -noout
openssl rsa -in server.key -check
```

## 调试技巧

### 1. 启用详细日志

修改代码添加更多日志输出，或使用调试器：

```bash
# 使用 gdb 调试
gdb ./speed_test_server
gdb ./speed_test_client
```

### 2. 抓包分析

使用 tcpdump 或 Wireshark 抓包分析：

```bash
# 抓取 UDP 包
sudo tcpdump -i any -n udp -w capture.pcap

# 使用 Wireshark 打开 capture.pcap 分析
```

### 3. 检查系统资源

```bash
# 检查内存使用
free -h

# 检查磁盘 I/O
iostat -x 1

# 检查网络统计
netstat -s
```

### 4. 测试本地回环

先在同一台机器上测试，确保程序本身工作正常：

```bash
# 使用 127.0.0.1 或 localhost 进行测试
# 这样可以排除网络问题
```

## 性能优化建议

### 1. 网络层面
- 使用千兆或万兆网卡
- 减少网络跳数
- 使用专用网络而非公网

### 2. 系统层面
```bash
# 增加 UDP 缓冲区大小
sudo sysctl -w net.core.rmem_max=26214400
sudo sysctl -w net.core.wmem_max=26214400
sudo sysctl -w net.core.rmem_default=26214400
sudo sysctl -w net.core.wmem_default=26214400
```

### 3. 应用层面
- 调整数据块大小
- 调整发送频率
- 使用多个流并行传输

## 获取帮助

如果以上方法都无法解决问题，请提供以下信息：

1. 完整的错误日志
2. 两端的 ICE 候选信息
3. 网络拓扑（是否在同一局域网、是否跨公网等）
4. 操作系统版本
5. 防火墙配置
6. 抓包文件（如果可能）

## 成功连接的示例

### 服务端输出示例
```
=== ICE+QUIC 速度测试服务器 ===
初始化传输...
开始收集ICE候选...
[ICE] 本地候选: candidate:1 1 UDP 2130706431 192.168.1.100 54321 typ host
[ICE] 本地候选: candidate:2 1 UDP 1694498815 203.0.113.1 54321 typ srflx raddr 192.168.1.100 rport 54321
[ICE] 候选收集完成

=== 本地ICE描述 ===
ufrag: abc123
pwd: xyz789
candidates:
  candidate:1 1 UDP 2130706431 192.168.1.100 54321 typ host
  candidate:2 1 UDP 1694498815 203.0.113.1 54321 typ srflx raddr 192.168.1.100 rport 54321
===================

输入远程ICE描述:
远程 ufrag: def456
远程 pwd: uvw012
输入远程ICE候选 (每行一个，空行结束):
candidate:1 1 UDP 2130706431 192.168.1.200 54322 typ host
candidate:2 1 UDP 1694498815 203.0.113.2 54322 typ srflx raddr 192.168.1.200 rport 54322

设置远程描述，共 2 个候选...
等待连接... (最多等待 60 秒)
[QUIC] 连接已建立，准备接收数据...
```

### 客户端输出示例
```
=== ICE+QUIC 速度测试客户端 ===
将发送 1.0 GB (1073741824 字节) 数据
初始化传输...
开始收集ICE候选...
[ICE] 本地候选: candidate:1 1 UDP 2130706431 192.168.1.200 54322 typ host
[ICE] 候选收集完成

=== 本地ICE描述 ===
ufrag: def456
pwd: uvw012
candidates:
  candidate:1 1 UDP 2130706431 192.168.1.200 54322 typ host
===================

输入远程ICE描述:
远程 ufrag: abc123
远程 pwd: xyz789
输入远程ICE候选 (每行一个，空行结束):
candidate:1 1 UDP 2130706431 192.168.1.100 54321 typ host

设置远程描述，共 1 个候选...
等待连接... (最多等待 60 秒)
[QUIC] 连接已建立
打开流...
流 1 已打开

开始发送数据...
[发送速度] 125.34 Mbps | [ACK速度] 124.89 Mbps | 已发送: 156.25 MB | 已确认: 155.87 MB
```
