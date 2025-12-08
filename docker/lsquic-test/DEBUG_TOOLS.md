# lsquic 调试工具指南

## 概述

Docker 镜像已包含以下调试工具：
- **gdb** - GNU 调试器
- **valgrind** - 内存错误检测
- **strace** - 系统调用跟踪

## 快速开始

### 1. 快速测试（推荐首先运行）

```bash
chmod +x quick-test.sh
./quick-test.sh
```

**作用**: 运行一个简单的 1MB 数据传输测试，检测是否有段错误。

**输出**:
- ✅ 成功完成 - 没有问题
- ❌ 段错误 (退出码 139) - 需要进一步调试
- ⚠️ 超时 - 连接问题
- ⚠️ 其他错误 - 其他问题

### 2. 内存错误检测（段错误时使用）

```bash
chmod +x debug-segfault.sh
./debug-segfault.sh
```

**作用**: 使用 valgrind 检测内存错误、缓冲区溢出、未初始化内存访问等。

**输出示例**:
```
==12345== Invalid write of size 4
==12345==    at 0x401234: read_socket (speed_test_client.cpp:245)
==12345==    by 0x7F8B9C: event_base_loop (event.c:1234)
==12345==  Address 0x5a2b3c4 is 0 bytes after a block of size 1024 alloc'd
```

### 3. GDB 调试（查看崩溃堆栈）

```bash
chmod +x debug-with-gdb.sh
./debug-with-gdb.sh
```

**作用**: 在崩溃时自动显示堆栈跟踪和局部变量。

**输出示例**:
```
Program received signal SIGSEGV, Segmentation fault.
0x00401234 in read_socket (fd=5, what=2, arg=0x0) at speed_test_client.cpp:245
245         ssize_t nr = recvfrom(fd, buf, sizeof(buf), 0, ...);

(gdb) bt
#0  0x00401234 in read_socket (fd=5, what=2, arg=0x0)
#1  0x7f8b9c in event_base_loop (base=0x5a2b3c4)
#2  0x00401000 in main (argc=5, argv=0x7fff1234)
```

### 4. 系统调用跟踪（查看崩溃前操作）

```bash
chmod +x debug-with-strace.sh
./debug-with-strace.sh
```

**作用**: 显示程序执行的所有系统调用，帮助定位崩溃前的最后操作。

**输出示例**:
```
socket(AF_INET, SOCK_DGRAM, IPPROTO_IP) = 5
bind(5, {sa_family=AF_INET, sin_port=htons(0), sin_addr=inet_addr("0.0.0.0")}, 16) = 0
sendmsg(5, {msg_name={sa_family=AF_INET, sin_port=htons(9331), sin_addr=inet_addr("127.0.0.1")}, msg_namelen=16, msg_iov=[{iov_base=..., iov_len=1200}], msg_iovlen=1}, 0) = 1200
recvfrom(5, 0x7fff1234, 65535, 0, 0x7fff5678, [16]) = -1 EAGAIN
--- SIGSEGV {si_signo=SIGSEGV, si_code=SEGV_MAPERR, si_addr=0x0} ---
```

## 详细使用

### Valgrind 选项

```bash
docker run -it --rm --network host \
    --cap-add=SYS_PTRACE \
    lsquic-speed-test:latest \
    valgrind [选项] /app/build/speed_test_client -s 127.0.0.1 -g 0.01
```

**常用选项**:
- `--leak-check=full` - 完整的内存泄漏检查
- `--show-leak-kinds=all` - 显示所有类型的泄漏
- `--track-origins=yes` - 跟踪未初始化值的来源
- `--verbose` - 详细输出
- `--log-file=/tmp/valgrind.log` - 保存到文件

### GDB 命令

启动 GDB:
```bash
docker run -it --rm --network host \
    --cap-add=SYS_PTRACE \
    --security-opt seccomp=unconfined \
    lsquic-speed-test:latest \
    gdb /app/build/speed_test_client
```

**常用命令**:
```
(gdb) run -s 127.0.0.1 -g 0.01     # 运行程序
(gdb) bt                            # 显示堆栈跟踪
(gdb) bt full                       # 显示完整堆栈（包括局部变量）
(gdb) frame 0                       # 切换到栈帧 0
(gdb) info locals                   # 显示局部变量
(gdb) print g_engine                # 打印变量值
(gdb) print *g_conn                 # 打印指针指向的内容
(gdb) x/10x 0x5a2b3c4              # 查看内存内容
(gdb) break speed_test_client.cpp:245  # 设置断点
(gdb) continue                      # 继续执行
(gdb) quit                          # 退出
```

### strace 选项

```bash
docker run -it --rm --network host \
    --cap-add=SYS_PTRACE \
    lsquic-speed-test:latest \
    strace [选项] /app/build/speed_test_client -s 127.0.0.1 -g 0.01
```

**常用选项**:
- `-f` - 跟踪子进程
- `-t` - 显示时间戳
- `-e trace=network` - 只跟踪网络相关调用
- `-e trace=memory` - 只跟踪内存相关调用
- `-e trace=all` - 跟踪所有系统调用
- `-o /tmp/strace.log` - 保存到文件

## 常见问题诊断

### 段错误 (SIGSEGV)

**症状**: 程序崩溃，退出码 139

**诊断步骤**:
1. 运行 `./quick-test.sh` 确认问题
2. 运行 `./debug-segfault.sh` 查看 valgrind 报告
3. 查找 "Invalid read" 或 "Invalid write" 错误
4. 检查报告中的文件名和行号

**常见原因**:
- 空指针访问
- 缓冲区溢出
- 使用已释放的内存
- 栈溢出

### 内存泄漏

**症状**: 程序运行一段时间后内存持续增长

**诊断步骤**:
1. 使用 valgrind 运行较长时间
2. 查看 "definitely lost" 和 "possibly lost" 报告
3. 检查分配但未释放的内存

**示例**:
```
==12345== LEAK SUMMARY:
==12345==    definitely lost: 1,024 bytes in 1 blocks
==12345==    indirectly lost: 0 bytes in 0 blocks
==12345==      possibly lost: 0 bytes in 0 blocks
```

### 未初始化内存

**症状**: 程序行为不确定，有时工作有时不工作

**诊断步骤**:
1. 使用 `valgrind --track-origins=yes`
2. 查找 "Conditional jump or move depends on uninitialised value(s)"
3. 检查变量初始化

### 缓冲区溢出

**症状**: 段错误，数据损坏

**诊断步骤**:
1. 使用 valgrind 检测
2. 查找 "Invalid write of size N"
3. 检查数组边界和字符串操作

## 性能分析

### 使用 strace 分析系统调用

```bash
strace -c /app/build/speed_test_client -s 127.0.0.1 -g 0.01
```

**输出**:
```
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 45.23    0.012345        1234        10           sendmsg
 32.15    0.008765         876        10           recvfrom
 12.34    0.003456         345        10           poll
...
```

## 最佳实践

### 1. 逐步调试

1. 先运行 `quick-test.sh` 确认问题
2. 如果有段错误，运行 `debug-segfault.sh`
3. 如果需要更详细信息，运行 `debug-with-gdb.sh`
4. 如果怀疑系统调用问题，运行 `debug-with-strace.sh`

### 2. 保存调试输出

```bash
./debug-segfault.sh > debug-output.txt 2>&1
```

### 3. 对比工作和不工作的情况

如果问题间歇性出现，对比成功和失败时的日志。

### 4. 检查环境差异

- 网络配置
- 防火墙规则
- Docker 版本
- 内核版本

## 进阶调试

### 使用 AddressSanitizer

修改 `CMakeLists.txt`:
```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g -O1")
```

重新构建:
```bash
./deploy.sh build --no-cache
```

运行:
```bash
./quick-test.sh
```

**优点**:
- 比 valgrind 快
- 检测更多类型的错误
- 更精确的错误定位

**缺点**:
- 需要重新编译
- 增加内存使用

### 使用 ThreadSanitizer

检测多线程问题:
```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -g")
```

### 使用 UndefinedBehaviorSanitizer

检测未定义行为:
```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined -g")
```

## 参考资料

- [Valgrind 手册](https://valgrind.org/docs/manual/manual.html)
- [GDB 文档](https://sourceware.org/gdb/documentation/)
- [strace 手册](https://man7.org/linux/man-pages/man1/strace.1.html)
- [AddressSanitizer](https://github.com/google/sanitizers/wiki/AddressSanitizer)

## 获取帮助

如果以上方法都无法解决问题：

1. 收集所有调试输出
2. 记录复现步骤
3. 检查 lsquic GitHub issues
4. 提供完整的环境信息（OS、Docker 版本、网络配置等）
