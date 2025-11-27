# ICE+QUIC 传输层示例

本目录包含演示如何使用 ICE+QUIC P2P 传输库的示例程序。

## 前置条件

在运行示例之前，你需要：

1. **构建库和示例：**
   ```bash
   cd ice_quic_transport
   mkdir build && cd build
   cmake -DICE_QUIC_BUILD_EXAMPLES=ON ..
   make
   ```

2. **生成 TLS 证书**（服务器模式必需）：
   ```bash
   openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt \
       -days 365 -nodes -subj "/CN=localhost"
   ```

## 示例程序

### 1. 回显服务器 (`echo_server`)

一个接受 ICE+QUIC 连接并将接收到的数据回显给客户端的服务器。

**用法：**
```bash
./echo_server <证书路径> <密钥路径>
```

**示例：**
```bash
./echo_server server.crt server.key
```

**功能说明：**
1. 以服务器模式初始化 IceQuicTransport
2. 收集 ICE 候选项并打印
3. 提示输入远端 ICE 凭证和候选项
4. 将所有接收到的数据回显给发送方
5. 定期显示连接统计信息

### 2. 回显客户端 (`echo_client`)

一个连接到回显服务器并发送用户输入的客户端。

**用法：**
```bash
./echo_client
```

**功能说明：**
1. 以客户端模式初始化 IceQuicTransport
2. 收集 ICE 候选项并打印
3. 提示输入远端 ICE 凭证和候选项
4. 提供交互式提示符用于发送消息
5. 显示服务器的回显响应

**交互命令：**
- `/stats` - 显示连接统计信息
- `/quit` - 关闭连接并退出

### 3. 回环测试 (`loopback_test`)

一个在同一进程中同时创建客户端和服务器的自动化测试。

**用法：**
```bash
./loopback_test <证书路径> <密钥路径>
```

**示例：**
```bash
./loopback_test server.crt server.key
```

**功能说明：**
1. 同时创建服务器和客户端传输层
2. 以编程方式交换 ICE 凭证和候选项
3. 在两者之间建立连接
4. 发送测试消息并验证回显响应
5. 报告成功/失败结果

## 手动测试回显服务器和客户端

要手动测试回显服务器和客户端：

### 终端 1（服务器）：
```bash
./echo_server server.crt server.key
```

等待服务器打印其本地凭证和候选项。

### 终端 2（客户端）：
```bash
./echo_client
```

等待客户端打印其本地凭证和候选项。

### 交换信息：

1. **将服务器凭证复制到客户端：**
   - 在客户端提示时输入服务器的 `ufrag`
   - 在客户端提示时输入服务器的 `pwd`

2. **将服务器候选项复制到客户端：**
   - 输入每一行服务器候选项
   - 输入完成后按回车键（空行）结束

3. **将客户端凭证复制到服务器：**
   - 在服务器提示时输入客户端的 `ufrag`
   - 在服务器提示时输入客户端的 `pwd`

4. **将客户端候选项复制到服务器：**
   - 输入每一行客户端候选项
   - 输入完成后按回车键（空行）结束

### 发送消息：

连接建立后，在客户端终端输入消息。服务器会将消息回显回来。
