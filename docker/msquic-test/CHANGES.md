# MsQuic 拥塞控制算法支持 - 更新说明

## 概述

为 MsQuic 速度测试工具添加了 CUBIC 和 BBR 拥塞控制算法的配置支持，允许用户在测试时选择不同的拥塞控制算法并比较性能。

## 主要更改

### 1. 代码修改

#### speed_test_server.cpp
- 添加 `CongestionAlgorithm` 全局变量（默认: "cubic"）
- 添加 `-c` 命令行参数用于选择拥塞控制算法
- 在连接建立时通过 `QUIC_SETTINGS` 设置拥塞控制算法
- 添加使用说明函数 `PrintUsage()`

#### speed_test_client.cpp
- 添加 `CongestionAlgorithm` 全局变量（默认: "cubic"）
- 添加 `-c` 命令行参数用于选择拥塞控制算法
- 在连接建立时通过 `QUIC_SETTINGS` 设置拥塞控制算法
- 更新 `PrintUsage()` 函数，添加拥塞控制算法选项

#### CMakeLists.txt
- 添加 `-DQUIC_API_ENABLE_PREVIEW_FEATURES` 宏定义以启用 BBR 支持

#### Dockerfile
- 在编译 MsQuic 时添加 `-DQUIC_ENABLE_PREVIEW_FEATURES=ON` 选项

### 2. 脚本更新

#### deploy.sh
- 更新 `start_server()` 函数，支持 `-c` 参数
- 更新 `start_client()` 函数，支持 `-c` 参数
- 更新帮助信息，说明拥塞控制算法选项

#### test_congestion_control.sh (新增)
- 自动化测试脚本，用于比较 CUBIC 和 BBR 性能
- 支持配置测试轮数、数据大小等参数
- 可通过环境变量自定义测试参数

### 3. 文档

#### README.md (新增)
- 完整的使用指南
- 拥塞控制算法介绍
- 快速开始教程
- 命令行参数说明
- 测试示例
- 故障排查指南

#### CONGESTION_CONTROL_TEST.md (新增)
- 详细的对比测试指南
- 不同网络场景的测试方法
- 性能指标说明
- 网络模拟工具使用说明

#### BUILD_AND_TEST.md (新增)
- 构建说明
- BBR 支持配置说明
- 快速测试步骤
- 验证方法
- 故障排查

## 技术细节

### BBR 支持

BBR 是 MsQuic 的预览功能，需要在编译时启用：

1. **编译 MsQuic 时**：
   ```cmake
   -DQUIC_ENABLE_PREVIEW_FEATURES=ON
   ```

2. **编译测试程序时**：
   ```cmake
   add_definitions(-DQUIC_API_ENABLE_PREVIEW_FEATURES)
   ```

3. **代码中使用**：
   ```cpp
   QUIC_SETTINGS settings = {};
   settings.CongestionControlAlgorithm = QUIC_CONGESTION_CONTROL_ALGORITHM_BBR;
   settings.IsSet.CongestionControlAlgorithm = TRUE;
   MsQuic->SetParam(Connection, QUIC_PARAM_CONN_SETTINGS, sizeof(settings), &settings);
   ```

### API 使用

拥塞控制算法通过 `QUIC_SETTINGS` 结构体设置，而不是单独的参数：

```cpp
// 正确的方式
QUIC_SETTINGS settings = {};
settings.CongestionControlAlgorithm = QUIC_CONGESTION_CONTROL_ALGORITHM_CUBIC; // 或 BBR
settings.IsSet.CongestionControlAlgorithm = TRUE;
MsQuic->SetParam(Connection, QUIC_PARAM_CONN_SETTINGS, sizeof(settings), &settings);

// 错误的方式（不存在这个参数）
// MsQuic->SetParam(Connection, QUIC_PARAM_CONN_CONGESTION_CONTROL_ALGORITHM, ...);
```

## 使用示例

### 基本使用

```bash
# 服务端使用 CUBIC
./speed_test_server -c cubic

# 服务端使用 BBR
./speed_test_server -c bbr

# 客户端使用 CUBIC
./speed_test_client -s 192.168.1.100 -g 5 -c cubic

# 客户端使用 BBR
./speed_test_client -s 192.168.1.100 -g 5 -c bbr
```

### 使用部署脚本

```bash
# 启动服务端（BBR）
./deploy.sh server -c bbr

# 连接并测试（BBR）
./deploy.sh client -s 192.168.1.100 -g 5 -c bbr
```

### 自动化测试

```bash
# 运行对比测试
docker run -it --rm --network host msquic-speed-test:latest bash
cd /app/build
../test_congestion_control.sh
```

## 预期效果

### CUBIC 特点
- 在低延迟、稳定网络中表现良好
- 启动较慢，逐步增加速度
- 遇到丢包时恢复较慢

### BBR 特点
- 快速达到最大带宽
- 在高延迟网络中保持高吞吐量
- 对网络抖动和丢包更有弹性
- 可能表现出更高的速度波动

### 测试场景建议

1. **理想网络**（低延迟、无丢包）：两者性能相近
2. **高延迟网络**（100ms+）：BBR 通常表现更好
3. **有丢包网络**（1-5%）：BBR 保持更稳定的吞吐量
4. **带宽受限网络**：BBR 更快达到带宽上限

## 兼容性

- **MsQuic 版本**: 需要支持预览功能的版本
- **操作系统**: Linux (Ubuntu 24.04 测试通过)
- **编译器**: GCC 13.3+ 或 Clang
- **CMake**: 3.13+

## 已知限制

1. BBR 是预览功能，API 可能在未来版本中变化
2. 某些网络环境下 BBR 的优势可能不明显
3. 需要足够的测试数据量（建议 5GB+）才能看到明显差异

## 后续改进建议

1. 添加更多拥塞控制算法（如果 MsQuic 支持）
2. 添加实时统计图表
3. 支持多流并发测试
4. 添加自动化性能报告生成
5. 支持更多网络参数配置

## 参考资料

- [MsQuic GitHub](https://github.com/microsoft/msquic)
- [MsQuic API 文档](https://github.com/microsoft/msquic/blob/main/docs/API.md)
- [MsQuic 预览功能](https://github.com/microsoft/msquic/blob/main/docs/PreviewFeatures.md)
- [BBR 论文](https://queue.acm.org/detail.cfm?id=3022184)
- [CUBIC 论文](https://www.cs.princeton.edu/courses/archive/fall16/cos561/papers/Cubic08.pdf)
