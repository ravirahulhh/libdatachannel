# lsquic 速度测试

## 问题诊断

### 原始问题
客户端连接服务器后立即退出，没有数据传输。日志显示：
- 客户端发送了数据包但没有收到服务器响应
- 握手没有完成

### 根本原因
1. **QUIC 版本不匹配**：客户端使用 `N_LSQVER`（未定义），服务器使用默认版本
2. **ALPN 协议协商缺失**：客户端和服务器没有正确配置 ALPN
3. **SSL 上下文配置问题**：使用了特定的 TLS 方法而不是通用方法

### 修复方案
1. **统一 QUIC 版本**：客户端和服务器都使用 `LSQVER_I001` (IETF QUIC v1 / RFC 9000)
2. **添加 ALPN 支持**：
   - 客户端：使用 `SSL_CTX_set_alpn_protos` 设置 "speedtest"
   - 服务器：使用 `SSL_CTX_set_alpn_select_cb` 处理 ALPN 选择
3. **改进 SSL 配置**：使用 `TLS_method()` 而不是 `TLS_client_method()` / `TLS_server_method()`
4. **增强日志**：添加更详细的调试日志以便诊断问题

## 使用方法

### 1. 构建镜像
```bash
./deploy.sh build
```

### 2. 在服务器 A 上启动服务端
```bash
./deploy.sh server
```

### 3. 在服务器 B 上启动客户端
```bash
./deploy.sh client -s <服务器A的IP> -g 5
```

### 4. 本地测试
```bash
./deploy.sh local-test
```

## 调试技巧

### 查看服务器日志
```bash
docker logs -f lsquic-server
```

### 查看客户端日志
客户端以交互模式运行，直接在终端查看输出

### 调试段错误

镜像已包含 gdb、valgrind 和 strace 调试工具。

#### 使用 GDB 调试
```bash
chmod +x debug-with-gdb.sh
./debug-with-gdb.sh
```

#### 使用 Valgrind 检测内存错误
```bash
chmod +x debug-segfault.sh
./debug-segfault.sh
```

#### 使用 strace 跟踪系统调用
```bash
chmod +x debug-with-strace.sh
./debug-with-strace.sh
```

#### 手动调试
```bash
# 启动服务器
./deploy.sh server

# 在另一个终端使用 GDB
docker run -it --rm --network host \
    --cap-add=SYS_PTRACE \
    lsquic-speed-test:latest \
    gdb -ex 'run -s <server_ip> -g 0.01' \
    /app/build/speed_test_client
```

### 网络检查
```bash
# 检查 UDP 端口是否开放
nc -u -v <服务器IP> 9331

# 使用 tcpdump 抓包
tcpdump -i any -n udp port 9331
```

### 防火墙配置
确保 UDP 端口 9331 开放：
```bash
# Ubuntu/Debian
sudo ufw allow 9331/udp

# CentOS/RHEL
sudo firewall-cmd --add-port=9331/udp --permanent
sudo firewall-cmd --reload
```

## 与 msquic 的对比

| 特性 | lsquic | msquic |
|------|--------|--------|
| QUIC 实现 | LiteSpeed QUIC | Microsoft QUIC |
| TLS 库 | BoringSSL | OpenSSL/Schannel |
| 版本支持 | IETF QUIC v1 | IETF QUIC v1 |
| 配置复杂度 | 较高 | 较低 |
| 文档质量 | 一般 | 较好 |

## 常见问题

### Q: 客户端连接后立即退出
A: 检查 QUIC 版本是否匹配，ALPN 是否正确配置

### Q: 服务器没有收到数据包
A: 检查防火墙和网络配置，确保 UDP 端口开放

### Q: 握手失败
A: 检查 SSL 证书是否正确生成，TLS 版本是否为 1.3

### Q: 编译失败
A: 确保 BoringSSL 和 lsquic 正确编译，检查库文件路径

## 性能优化建议

1. **增大缓冲区**：`-b 128` (128KB)
2. **调整拥塞控制**：lsquic 支持 BBR 和 Cubic
3. **优化网络参数**：
   ```bash
   # 增大 UDP 缓冲区
   sysctl -w net.core.rmem_max=26214400
   sysctl -w net.core.wmem_max=26214400
   ```

## 参考资料

- [lsquic GitHub](https://github.com/litespeedtech/lsquic)
- [QUIC RFC 9000](https://www.rfc-editor.org/rfc/rfc9000.html)
- [BoringSSL](https://boringssl.googlesource.com/boringssl/)
