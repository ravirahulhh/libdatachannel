# ICE+QUIC 速度测试工具

这是基于 ice_quic_transport 的速度测试工具，用于在两台服务器之间测试传输速度。

## 功能特性

- **可配置数据量**: 支持配置传输几个 GB 的数据
- **实时速度显示**: 
  - 客户端显示发送速度和 ACK 确认速度
  - 服务端显示接收速度
- **平均速度统计**: 传输完成后显示整体平均速度
- **中文界面**: 友好的中文提示信息

## 编译

```bash
cd ice_quic_transport/build
cmake ..
make speed_test_server speed_test_client
```

编译完成后，可执行文件位于 `build` 目录：
- `speed_test_server` - 服务端程序
- `speed_test_client` - 客户端程序

## 准备工作

### 1. 生成 TLS 证书（服务端需要）

```bash
openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt \
    -days 365 -nodes -subj "/CN=localhost"
```

这会生成两个文件：
- `server.crt` - TLS 证书
- `server.key` - 私钥

## 使用方法

### 服务端（Server）

在服务器 A 上运行：

```bash
./speed_test_server server.crt server.key
```

程序会：
1. 初始化 ICE+QUIC 传输
2. 收集 ICE 候选
3. 显示本地 ICE 描述信息（ufrag, pwd, candidates）
4. 等待输入远程 ICE 描述信息

**记录服务端显示的信息**，需要发送给客户端。

### 客户端（Client）

在服务器 B 上运行：

```bash
# 发送 1 GB 数据（默认）
./speed_test_client

# 发送 5 GB 数据
./speed_test_client 5

# 发送 10 GB 数据
./speed_test_client 10
```

程序会：
1. 初始化 ICE+QUIC 传输
2. 收集 ICE 候选
3. 显示本地 ICE 描述信息
4. 等待输入远程 ICE 描述信息

**记录客户端显示的信息**，需要发送给服务端。

### ICE 信息交换

两端都会显示类似以下的信息：

```
=== 本地ICE描述 ===
ufrag: abc123
pwd: xyz789
candidates:
  candidate:1 1 UDP 2130706431 192.168.1.100 54321 typ host
  candidate:2 1 UDP 1694498815 203.0.113.1 54321 typ srflx raddr 192.168.1.100 rport 54321
===================
```

**交换步骤**：

1. **服务端** → **客户端**: 将服务端的 ufrag, pwd, candidates 发送给客户端
2. **客户端** → **服务端**: 将客户端的 ufrag, pwd, candidates 发送给服务端

3. 在**客户端**输入服务端的信息：
   ```
   远程 ufrag: <服务端的ufrag>
   远程 pwd: <服务端的pwd>
   输入远程ICE候选 (每行一个，空行结束):
   <服务端的candidate 1>
   <服务端的candidate 2>
   <空行>
   ```

4. 在**服务端**输入客户端的信息（格式相同）

### 连接和传输

信息交换完成后：
1. 两端会自动建立 ICE+QUIC 连接
2. 客户端开始发送数据
3. 两端实时显示传输速度
4. 传输完成后显示统计信息

## 输出示例

### 客户端输出

```
=== ICE+QUIC 速度测试客户端 ===
将发送 5.0 GB (5368709120 字节) 数据
...
[QUIC] 连接已建立
打开流...
流 1 已打开

开始发送数据...
[发送速度] 125.34 Mbps | [ACK速度] 124.89 Mbps | 已发送: 156.25 MB | 已确认: 155.87 MB
[发送速度] 128.76 Mbps | [ACK速度] 128.21 Mbps | 已发送: 312.50 MB | 已确认: 311.74 MB
...

========== 传输完成 ==========
总发送数据: 5120.00 MB (5.00 GB)
总确认数据: 5118.45 MB
传输时间: 327.45 秒
平均发送速度: 125.23 Mbps
平均ACK速度: 125.11 Mbps
=============================
```

### 服务端输出

```
=== ICE+QUIC 速度测试服务器 ===
...
[QUIC] 连接已建立，准备接收数据...
[流] 对端打开流 1
[接收速度] 124.89 Mbps | 已接收: 156.11 MB
[接收速度] 128.21 Mbps | 已接收: 312.22 MB
...

========== 传输完成 ==========
总接收数据: 5120.00 MB (5.00 GB)
传输时间: 327.89 秒
平均速度: 125.05 Mbps
=============================
```

## 性能调优建议

1. **网络环境**: 确保两台服务器之间网络连接良好
2. **防火墙**: 确保 UDP 端口未被阻止
3. **STUN 服务器**: 默认使用 Google STUN，可以在代码中修改为其他 STUN 服务器
4. **数据块大小**: 默认 64KB，可以在代码中调整 `chunkSize` 变量
5. **超时设置**: 默认 5 分钟空闲超时，大数据传输可能需要调整

## 故障排查

如果遇到连接问题，请查看详细的故障排查指南: [TROUBLESHOOTING.md](TROUBLESHOOTING.md)

### 快速检查清单

**连接超时**:
- ✓ ICE 候选格式是否正确（不要包含 `a=` 前缀）
- ✓ 防火墙是否允许 UDP 流量
- ✓ 两台服务器网络是否可达
- ✓ STUN 服务器是否可访问

**传输速度慢**:
- ✓ 使用 iperf3 测试网络带宽
- ✓ 检查 CPU 使用率
- ✓ 尝试调整数据块大小

**连接中断**:
- ✓ 增加 `idleTimeoutMs` 值
- ✓ 检查网络稳定性（使用 ping 测试）

详细的解决方案请参考 [TROUBLESHOOTING.md](TROUBLESHOOTING.md)

## 技术细节

- **传输协议**: ICE (Interactive Connectivity Establishment) + QUIC
- **ICE 库**: libnice
- **QUIC 库**: lsquic (基于 BoringSSL)
- **数据块大小**: 64 KB
- **统计更新频率**: 每秒
- **默认超时**: 5 分钟

## 注意事项

1. 服务端需要 TLS 证书和私钥
2. 两端需要手动交换 ICE 描述信息
3. 确保两台服务器时间同步以获得准确的速度统计
4. 大数据量传输可能需要较长时间，请耐心等待
