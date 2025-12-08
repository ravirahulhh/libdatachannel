# 段错误 (Segmentation Fault) 修复

## 问题描述

客户端在发送第一个数据包后立即崩溃，出现段错误：

```
[DEBUG] Sent packet #1 size=1200
[DEBUG] After process_conns, packets_sent=1
bash: line 10:    10 Segmentation fault      (core dumped)
```

## 根本原因

### 1. `peer_addr_len` 未重置
在 `read_socket` 函数的循环中，`peer_addr_len` 在第一次 `recvfrom` 调用后被修改，但在后续循环中没有重置。这导致第二次调用 `recvfrom` 时传入了错误的长度值，可能导致缓冲区溢出。

```cpp
// 错误的代码
socklen_t peer_addr_len = sizeof(peer_addr);  // 只初始化一次
while (true) {
    ssize_t nr = recvfrom(fd, buf, sizeof(buf), 0, 
                          (struct sockaddr*)&peer_addr, &peer_addr_len);
    // peer_addr_len 被 recvfrom 修改，下次循环使用错误的值
}
```

### 2. 无限循环风险
使用 `while (true)` 在非阻塞 socket 上可能导致无限循环，饿死其他事件处理。

### 3. 缺少空指针检查
回调函数中没有检查全局指针是否为 NULL，可能在清理过程中访问已释放的内存。

## 修复方案

### 1. 修复 `peer_addr_len` 重置问题

**客户端和服务器 (`read_socket` 函数):**
```cpp
// 修复后的代码
socklen_t peer_addr_len;

// 使用 for 循环限制最大迭代次数
for (int i = 0; i < 10; i++) {
    peer_addr_len = sizeof(peer_addr);  // 每次循环都重置
    
    ssize_t nr = recvfrom(fd, buf, sizeof(buf), 0, 
                          (struct sockaddr*)&peer_addr, &peer_addr_len);
    // ...
}
```

**关键改进:**
- 每次循环开始时重置 `peer_addr_len`
- 使用 `for` 循环替代 `while (true)`，限制最多读取 10 个包
- 避免饿死其他事件处理

### 2. 添加空指针检查

**客户端 (`read_socket`):**
```cpp
static void read_socket(evutil_socket_t fd, short what, void *arg) {
    if (!g_engine) {
        cerr << "[ERROR] read_socket: g_engine is NULL" << endl;
        return;
    }
    // ...
}
```

**客户端 (`timer_handler`):**
```cpp
static void timer_handler(evutil_socket_t fd, short what, void *arg) {
    if (!g_engine) {
        return;
    }
    // ...
    
    if (g_timer_event && lsquic_engine_earliest_adv_tick(g_engine, nullptr)) {
        struct timeval tv = {0, 1000};
        event_add(g_timer_event, &tv);
    }
}
```

**客户端 (`on_write`):**
```cpp
static void on_write(lsquic_stream_t *stream, lsquic_stream_ctx_t *ctx) {
    if (!stream) {
        cerr << "[ERROR] on_write: stream is NULL" << endl;
        return;
    }
    
    if (g_total_to_send == 0) {
        // 没有数据要发送，立即关闭
        lsquic_stream_shutdown(stream, 1);
        lsquic_stream_wantwrite(stream, 0);
        return;
    }
    // ...
}
```

### 3. 服务器端相同修复

服务器端的 `read_socket` 和 `timer_handler` 也应用了相同的修复。

## 技术细节

### `recvfrom` 的行为

`recvfrom` 函数会修改 `peer_addr_len` 参数：

```c
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);
```

- **输入**: `*addrlen` 应该包含 `src_addr` 缓冲区的大小
- **输出**: `*addrlen` 被设置为实际地址的大小

如果不在每次循环中重置，第二次调用可能会：
1. 传入错误的地址长度
2. 导致缓冲区溢出
3. 触发段错误

### 为什么限制循环次数

在非阻塞 socket 上：
- `recvfrom` 在没有数据时返回 -1，errno 设置为 EAGAIN/EWOULDBLOCK
- 但在高流量情况下，可能一直有数据可读
- 无限循环会阻止其他事件（如定时器）被处理
- 限制为 10 次迭代是一个合理的平衡

### libevent 的事件循环

libevent 使用边缘触发（edge-triggered）或水平触发（level-triggered）模式：
- 如果还有数据可读，下次事件循环会再次触发 `read_socket`
- 因此不需要在一次回调中读取所有数据
- 限制迭代次数可以保证公平性

## 验证修复

### 重新构建并测试

```bash
cd docker/lsquic-test
./deploy.sh build
./debug-connection.sh
```

### 预期结果

修复后不应该再出现段错误，应该看到：

```
[DEBUG] Sent packet #1 size=1200
[DEBUG] Received packet #1 size=1200
[Client] on_new_conn called (connection object created)
[Client] Handshake done, status=0
[Client] Handshake successful!
```

### 如果仍然崩溃

镜像已包含 gdb、valgrind 和 strace 调试工具。

1. **快速测试**:
   ```bash
   chmod +x quick-test.sh
   ./quick-test.sh
   ```
   这会运行一个简单的测试并报告是否有段错误。

2. **使用 valgrind 检测内存错误**:
   ```bash
   chmod +x debug-segfault.sh
   ./debug-segfault.sh
   ```
   这会显示详细的内存错误报告。

3. **使用 GDB 调试**:
   ```bash
   chmod +x debug-with-gdb.sh
   ./debug-with-gdb.sh
   ```
   这会在崩溃时显示堆栈跟踪和局部变量。

4. **使用 strace 跟踪系统调用**:
   ```bash
   chmod +x debug-with-strace.sh
   ./debug-with-strace.sh
   ```
   这会显示崩溃前的最后系统调用。

5. **手动使用 valgrind**:
   ```bash
   # 启动服务器
   ./deploy.sh server
   
   # 在另一个终端
   docker run -it --rm --network host \
       --cap-add=SYS_PTRACE \
       lsquic-speed-test:latest \
       valgrind --leak-check=full --track-origins=yes \
       /app/build/speed_test_client -s 127.0.0.1 -g 0.01
   ```

6. **使用 AddressSanitizer** (需要重新编译):
   在 CMakeLists.txt 中添加：
   ```cmake
   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g")
   ```
   然后重新构建镜像。

## 相关问题

### 为什么 msquic 没有这个问题？

msquic 使用不同的事件处理模型：
- 内部管理 socket 和事件循环
- 自动处理缓冲区和地址长度
- 更高级的抽象，减少了手动内存管理

### lsquic 的设计哲学

lsquic 提供更底层的控制：
- 应用程序负责 socket 管理
- 应用程序负责事件循环
- 更灵活但需要更小心的编程

## 最佳实践

### 1. 总是重置 socklen_t

```cpp
for (int i = 0; i < MAX_PACKETS; i++) {
    socklen_t addr_len = sizeof(addr);  // 每次都重置
    recvfrom(fd, buf, size, 0, (struct sockaddr*)&addr, &addr_len);
}
```

### 2. 限制循环迭代

```cpp
// 好的做法
for (int i = 0; i < 10; i++) {
    // 处理数据包
}

// 避免
while (true) {
    // 可能无限循环
}
```

### 3. 检查空指针

```cpp
if (!ptr) {
    // 处理错误
    return;
}
// 使用 ptr
```

### 4. 使用 RAII

```cpp
// C++ 风格
std::unique_ptr<lsquic_engine_t, decltype(&lsquic_engine_destroy)> 
    engine(lsquic_engine_new(...), lsquic_engine_destroy);
```

## 总结

这次段错误是由于 `recvfrom` 的 `peer_addr_len` 参数没有在循环中重置导致的。修复方法是：

1. 在每次循环开始时重置 `peer_addr_len`
2. 使用 `for` 循环替代 `while (true)`
3. 添加空指针检查
4. 改进错误处理

这些修复提高了代码的健壮性和安全性。
