# MsQuic 传输速度测试 Docker 镜像
# 基于 Ubuntu 24.04，包含 msquic 编译和测试环境

FROM ubuntu:24.04

LABEL maintainer="msquic-test"
LABEL description="MsQuic transport speed test environment"

ENV DEBIAN_FRONTEND=noninteractive

# 安装基础依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    curl \
    pkg-config \
    libssl-dev \
    libnuma-dev \
    liburing-dev \
    lttng-tools \
    liblttng-ust-dev \
    iperf3 \
    net-tools \
    iputils-ping \
    iproute2 \
    && rm -rf /var/lib/apt/lists/*

# 设置工作目录
WORKDIR /opt

# 克隆并编译 msquic
RUN git clone --recursive https://github.com/microsoft/msquic.git && \
    cd msquic && \
    mkdir build && cd build && \
    cmake -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE=Release \
        -DQUIC_BUILD_TOOLS=ON \
        -DQUIC_BUILD_TEST=OFF \
        -DQUIC_BUILD_PERF=ON \
        .. && \
    cmake --build . --config Release -j$(nproc) && \
    cmake --install . && \
    ldconfig

# 复制测试程序源码
WORKDIR /app
COPY speed_test_server.cpp .
COPY speed_test_client.cpp .
COPY CMakeLists.txt .

# 编译测试程序
RUN mkdir build && cd build && \
    cmake .. && \
    cmake --build . -j$(nproc)

# 暴露 QUIC 端口 (UDP)
EXPOSE 9331/udp

# 默认命令
CMD ["/bin/bash"]
