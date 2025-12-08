# lsquic 测试修复更新日志

## 2024-12-08 - 第二轮修复

### 问题
本地测试显示客户端发送了数据包但没有收到服务器响应，连接超时。

### 根本原因
1. **数据大小解析错误**: `-g 0.1` 被 `atoi()` 转换成 0，导致没有数据发送
2. **UDP 包处理不完整**: 只读取一个包就返回，可能丢失后续包
3. **服务器定时器未启动**: 定时器创建后没有立即启动
4. **缺少超时检测**: 客户端无法检测连接超时

### 修复内容

#### 1. 支持小数数据大小 (speed_test_client.cpp)
```cpp
// 之前
static uint64_t g_data_size_gb = 1;
g_data_size_gb = atoi(argv[++i]);  // 0.1 -> 0

// 之后
static double g_data_size_gb = 1.0;
g_data_size_gb = atof(argv[++i]);  // 0.1 -> 0.1
g_total_to_send = (uint64_t)(g_data_size_gb * 1024.0 * 1024.0 * 1024.0);
```

#### 2. 改进 UDP 包处理 (客户端和服务器)
```cpp
// 之前：只读取一个包
ssize_t nr = recvfrom(fd, buf, sizeof(buf), 0, ...);
if (nr > 0) {
    // 处理一个包
}

// 之后：循环读取所有可用的包
while (true) {
    ssize_t nr = recvfrom(fd, buf, sizeof(buf), 0, ...);
    if (nr < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;  // 没有更多数据
        }
        break;
    }
    if (nr > 0) {
        // 处理包
    }
}
```

#### 3. 启动服务器定时器 (speed_test_server.cpp)
```cpp
// 之前
g_timer_event = evtimer_new(g_event_base, timer_handler, nullptr);
// 定时器创建但未启动

// 之后
g_timer_event = evtimer_new(g_event_base, timer_handler, nullptr);
struct timeval tv = {0, 1000};
event_add(g_timer_event, &tv);  // 立即启动定时器
```

#### 4. 添加连接超时检测 (speed_test_client.cpp)
```cpp
static void timer_handler(...) {
    static int tick_count = 0;
    static int no_response_count = 0;
    
    tick_count++;
    
    // 检查连接超时 (10秒没有收到响应)
    if (!g_connected && tick_count > 10000 && g_packets_recv == 0) {
        no_response_count++;
        if (no_response_count > 5) {
            cerr << "\n[ERROR] Connection timeout: no response from server" << endl;
            g_running = false;
            event_base_loopbreak(g_event_base);
            return;
        }
    }
    // ...
}
```

#### 5. 改进日志输出
- 显示实际发送的 MB 数
- 增加数据包计数上限（从 5 到 10）
- 添加更详细的错误信息

### 新增工具

#### debug-connection.sh
详细的连接调试脚本，包括：
- 网络配置检查
- 端口监听验证
- 详细的日志输出
- 故障排除提示

### 测试方法

#### 快速测试
```bash
cd docker/lsquic-test
./test-fix.sh
```

#### 详细调试
```bash
cd docker/lsquic-test
chmod +x debug-connection.sh
./debug-connection.sh
```

#### 两台服务器测试
```bash
# 服务器 A
./deploy.sh build
./deploy.sh server

# 服务器 B
./deploy.sh build
./deploy.sh client -s <服务器A的IP> -g 0.1  # 发送 100MB
```

### 预期结果

成功连接后应该看到：

**服务器日志:**
```
[DEBUG] Server QUIC versions: 0x20
[DEBUG] Server engine created successfully
Server started successfully. Waiting for connections...

[DEBUG] Received UDP packet #1 from 127.0.0.1:xxxxx size=1200 bytes
[Server] New connection object created! Total: 1
[Server] Handshake done, status=0
[Server] Handshake successful!
```

**客户端日志:**
```
Data size: 0.01 GB
Total to send: 10.49 MB
[DEBUG] QUIC versions: 0x20
[Client] Connecting to 127.0.0.1:9331...
[DEBUG] Sent packet #1 size=1200
[DEBUG] Received packet #1 size=1200
[Client] on_new_conn called (connection object created)
[Client] Handshake done, status=0
[Client] Handshake successful!
[Client] Stream created, starting data transfer: 0.01 GB (10.49 MB)
```

### 已知问题和限制

1. **本地回环测试**: 在某些系统上，本地回环的 QUIC 连接可能有问题
   - 解决方案：使用两台不同的服务器测试

2. **防火墙**: 确保 UDP 9331 端口开放
   ```bash
   sudo ufw allow 9331/udp
   ```

3. **Docker 网络**: 使用 `--network host` 模式以避免 NAT 问题

### 下一步

如果连接仍然失败：

1. **检查 lsquic 日志**: 查找 "conn" 和 "handshake" 相关的错误
2. **抓包分析**: 使用 tcpdump 捕获 UDP 9331 流量
3. **对比 msquic**: 确认 msquic 在相同环境下工作正常
4. **检查 BoringSSL**: 验证 SSL 库是否正确编译

### 参考

- [lsquic 文档](https://github.com/litespeedtech/lsquic)
- [QUIC RFC 9000](https://www.rfc-editor.org/rfc/rfc9000.html)
- [BoringSSL](https://boringssl.googlesource.com/boringssl/)
