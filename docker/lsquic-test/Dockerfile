# lsquic 传输速度测试 Docker 镜像
# 基于 Ubuntu 20.04，包含 lsquic + BoringSSL 编译环境

FROM ubuntu:20.04

LABEL maintainer="lsquic-test"
LABEL description="lsquic transport speed test environment"

ENV DEBIAN_FRONTEND=noninteractive

# 安装基础依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    curl \
    pkg-config \
    zlib1g-dev \
    libevent-dev \
    software-properties-common \
    net-tools \
    iputils-ping \
    iproute2 \
    iperf3 \
    && rm -rf /var/lib/apt/lists/*

# 安装 CMake 3.22+ (BoringSSL 需要)
RUN wget https://github.com/Kitware/CMake/releases/download/v3.22.0/cmake-3.22.0-linux-x86_64.sh && \
    chmod +x cmake-3.22.0-linux-x86_64.sh && \
    ./cmake-3.22.0-linux-x86_64.sh --skip-license --prefix=/usr/local && \
    rm cmake-3.22.0-linux-x86_64.sh

# 安装 Go (BoringSSL 编译需要)
RUN add-apt-repository ppa:longsleep/golang-backports && \
    apt-get update && \
    apt-get install -y golang-1.21-go && \
    cp /usr/lib/go-1.21/bin/go* /usr/bin/.

ENV GOROOT=/usr/lib/go-1.21

WORKDIR /src

# 克隆并编译 BoringSSL
RUN git clone --depth=1 https://github.com/google/boringssl.git && \
    cd boringssl && \
    mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc)

# 克隆并编译 lsquic (只编译库，不编译示例程序)
RUN git clone --recursive https://github.com/litespeedtech/lsquic.git && \
    cd lsquic && \
    cmake -DBORINGSSL_DIR=/src/boringssl \
          -DBORINGSSL_LIB=/src/boringssl/build \
          -DBORINGSSL_LIB_ssl=/src/boringssl/build/ssl/libssl.a \
          -DBORINGSSL_LIB_crypto=/src/boringssl/build/crypto/libcrypto.a \
          -DCMAKE_BUILD_TYPE=Release \
          -DLSQUIC_BIN=OFF \
          -DLSQUIC_TESTS=OFF \
          . && \
    make -j$(nproc)

# 复制测试程序源码
WORKDIR /app
COPY speed_test_server.cpp .
COPY speed_test_client.cpp .
COPY CMakeLists.txt .

# 编译测试程序
RUN mkdir build && cd build && \
    cmake -DLSQUIC_DIR=/src/lsquic \
          -DBORINGSSL_DIR=/src/boringssl \
          .. && \
    make -j$(nproc)

# 暴露 QUIC 端口 (UDP)
EXPOSE 9331/udp

# 默认命令
CMD ["/bin/bash"]
