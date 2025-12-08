# lsquic 连接问题修复说明

## 问题描述

在两台服务器上使用 Docker 部署 lsquic 测试时，客户端连接后立即退出，没有数据传输。
- msquic 在相同环境下可以正常工作，说明网络没有问题
- 客户端日志显示发送了数据包但没有收到服务器响应
- 握手过程没有完成

## 根本原因分析

### 1. QUIC 版本不匹配
**问题**：
- 客户端使用 `N_LSQVER`（这是一个未定义的宏）
- 服务器使用 `LSQUIC_DF_VERSIONS`（默认版本集合）
- 两者可能不兼容

**修复**：
```cpp
// 客户端和服务器都使用相同的版本
settings.es_versions = (1 << LSQVER_I001);  // IETF QUIC v1 (RFC 9000)
```

### 2. ALPN (应用层协议协商) 缺失
**问题**：
- 客户端没有设置 ALPN
- 服务器没有 ALPN 选择回调
- QUIC 要求必须有 ALPN

**修复**：

客户端 (`speed_test_client.cpp`):
```cpp
// 设置 ALPN
const unsigned char alpn[] = "\x09speedtest";  // 长度前缀 + "speedtest"
if (SSL_CTX_set_alpn_protos(g_ssl_ctx, alpn, sizeof(alpn) - 1) != 0) {
    cerr << "Failed to set ALPN" << endl;
    return false;
}
```

服务器 (`speed_test_server.cpp`):
```cpp
// ALPN 选择回调
static int select_alpn(SSL *ssl, const unsigned char **out, unsigned char *outlen,
                      const unsigned char *in, unsigned int inlen, void *arg) {
    const unsigned char alpn[] = "speedtest";
    // ... 查找匹配的 ALPN
    return SSL_TLSEXT_ERR_OK;
}

// 在 init_ssl() 中注册回调
SSL_CTX_set_alpn_select_cb(g_ssl_ctx, select_alpn, nullptr);
```

### 3. SSL 上下文配置问题
**问题**：
- 使用了特定的 `TLS_client_method()` 和 `TLS_server_method()`
- 可能导致某些 TLS 1.3 特性不可用

**修复**：
```cpp
// 使用通用方法
g_ssl_ctx = SSL_CTX_new(TLS_method());  // 而不是 TLS_client_method()
```

### 4. 日志级别不足
**问题**：
- 只启用了 "debug" 级别
- 无法看到详细的连接和握手过程

**修复**：
```cpp
// 启用更详细的日志
lsquic_set_log_level("event=debug,engine=debug,conn=debug,stream=debug");
```

## 修改文件清单

### 1. `speed_test_client.cpp`
- ✅ 修改 QUIC 版本为 `LSQVER_I001`
- ✅ 添加 ALPN 配置
- ✅ 改用 `TLS_method()`
- ✅ 增强日志级别
- ✅ 修复 `lsquic_engine_connect` 参数

### 2. `speed_test_server.cpp`
- ✅ 修改 QUIC 版本为 `LSQVER_I001`
- ✅ 添加 ALPN 选择回调
- ✅ 改用 `TLS_method()`
- ✅ 增强日志级别
- ✅ 添加日志回调函数

### 3. 新增文件
- ✅ `README.md` - 使用说明和故障排除
- ✅ `FIXES.md` - 本文档
- ✅ `test-fix.sh` - 快速测试脚本

## 验证步骤

### 方法 1: 使用部署脚本

**服务器 A (运行服务端):**
```bash
cd docker/lsquic-test
./deploy.sh build
./deploy.sh server
```

**服务器 B (运行客户端):**
```bash
cd docker/lsquic-test
./deploy.sh build
./deploy.sh client -s <服务器A的IP> -g 1
```

### 方法 2: 本地测试
```bash
cd docker/lsquic-test
./deploy.sh build
./deploy.sh local-test
```

### 方法 3: 使用测试脚本
```bash
cd docker/lsquic-test
chmod +x test-fix.sh
./test-fix.sh
```

## 预期结果

修复后，你应该看到：

**服务器日志:**
```
=== lsquic Speed Test Server ===
Listening on port 9331 (UDP)
[DEBUG] Server QUIC versions: 0x2
[DEBUG] Server engine created successfully
Server started successfully. Waiting for connections...

[DEBUG] Received UDP packet #1 from <client_ip>:xxxxx size=1200 bytes
[Server] New connection object created! Total: 1
[Server] Handshake done, status=0
[Server] Handshake successful!
[Server] Connections: 1 | Received: 512.00 MB | Instant: 850.5 Mbps | Avg: 820.3 Mbps
```

**客户端日志:**
```
=== lsquic Speed Test Client ===
Server: <server_ip>:9331
Data size: 1 GB
[DEBUG] QUIC versions: 0x2
[DEBUG] lsquic engine created successfully
[Client] Connecting to <server_ip>:9331...
[DEBUG] Sent packet #1 size=1200
[DEBUG] Received packet #1 size=1200
[Client] on_new_conn called (connection object created)
[Client] Handshake done, status=0
[Client] Handshake successful!
[Client] Stream created, starting data transfer: 1 GB
[Client] Queued: 512.00 MB | Speed: 850.5 Mbps | Progress: 50.00%
```

## 关键改进点

1. **版本统一**: 客户端和服务器使用相同的 QUIC 版本 (IETF v1)
2. **ALPN 支持**: 正确实现应用层协议协商
3. **SSL 兼容性**: 使用通用 TLS 方法提高兼容性
4. **调试能力**: 详细的日志帮助快速定位问题

## 与 msquic 的差异

| 方面 | lsquic | msquic |
|------|--------|--------|
| ALPN 配置 | 需要手动设置 | 自动处理 |
| 版本协商 | 需要明确指定 | 自动协商 |
| SSL 配置 | 需要手动配置 BoringSSL | 内置支持 |
| 错误处理 | 需要检查返回值 | 回调机制更清晰 |
| 文档 | 较少 | 较完善 |

## 故障排除

### 如果仍然连接失败

1. **检查防火墙**:
   ```bash
   # 确保 UDP 9331 端口开放
   sudo ufw allow 9331/udp
   ```

2. **检查网络连通性**:
   ```bash
   # 测试 UDP 连接
   nc -u -v <server_ip> 9331
   ```

3. **抓包分析**:
   ```bash
   # 在服务器上
   tcpdump -i any -n udp port 9331 -w lsquic.pcap
   # 用 Wireshark 分析 lsquic.pcap
   ```

4. **查看详细日志**:
   ```bash
   # 服务器
   docker logs -f lsquic-server
   
   # 客户端会直接输出到终端
   ```

5. **验证证书**:
   ```bash
   # 进入服务器容器
   docker exec -it lsquic-server bash
   ls -la /app/*.pem
   openssl x509 -in /app/cert.pem -text -noout
   ```

## 性能调优建议

1. **增大缓冲区**:
   ```bash
   ./deploy.sh client -s <ip> -g 5 -b 128  # 128KB 缓冲区
   ```

2. **系统参数优化**:
   ```bash
   # 增大 UDP 缓冲区
   sudo sysctl -w net.core.rmem_max=26214400
   sudo sysctl -w net.core.wmem_max=26214400
   sudo sysctl -w net.core.rmem_default=26214400
   sudo sysctl -w net.core.wmem_default=26214400
   ```

3. **Docker 网络优化**:
   ```bash
   # 使用 host 网络模式 (已在脚本中使用)
   docker run --network host ...
   ```

## 总结

这些修复解决了 lsquic 客户端和服务器之间的连接问题，主要是通过：
1. 统一 QUIC 版本
2. 正确配置 ALPN
3. 改进 SSL 设置
4. 增强调试能力

现在 lsquic 应该能够像 msquic 一样在两台服务器之间正常工作了。
