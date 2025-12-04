#!/bin/bash
# lsquic 速度测试快速部署脚本
# 用于在两台 Linux 服务器上快速部署和测试

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

print_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
print_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# 显示帮助
show_help() {
    cat << EOF
lsquic 速度测试部署脚本

用法:
  $0 <command> [options]

命令:
  build           构建 Docker 镜像 (首次需要较长时间编译 BoringSSL 和 lsquic)
  server          启动服务端
  client          启动客户端连接到指定服务器
  local-test      本地测试 (同一台机器上运行服务端和客户端)
  clean           清理容器和镜像
  help            显示帮助

示例:
  # 1. 在服务器 A 上构建并启动服务端
  $0 build
  $0 server

  # 2. 在服务器 B 上构建并启动客户端
  $0 build
  $0 client -s <服务器A的IP> -g 5

  # 3. 本地测试
  $0 local-test

选项:
  -s <address>    服务器地址 (客户端模式)
  -g <size>       发送数据大小 (GB), 默认 1
  -p <port>       端口号, 默认 9331

EOF
}

# 构建镜像
build_image() {
    local no_cache=""
    if [[ "$1" == "--no-cache" ]]; then
        no_cache="--no-cache"
        print_info "构建 lsquic 测试镜像 (无缓存)..."
    else
        print_info "构建 lsquic 测试镜像..."
    fi
    print_info "这可能需要 10-20 分钟 (编译 BoringSSL 和 lsquic)..."
    docker build $no_cache -t lsquic-speed-test:latest .
    print_info "镜像构建完成!"
}

# 启动服务端
start_server() {
    local port=${1:-9331}
    
    print_info "启动 lsquic 服务端 (端口: $port)..."
    
    # 停止已存在的容器
    docker rm -f lsquic-server 2>/dev/null || true
    
    # 启动服务端
    docker run -d \
        --name lsquic-server \
        --network host \
        lsquic-speed-test:latest \
        bash -c "
            cd /app &&
            openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes -subj '/CN=localhost' 2>/dev/null &&
            ./build/speed_test_server
        "
    
    print_info "服务端已启动!"
    print_info "查看日志: docker logs -f lsquic-server"
    print_info "确保防火墙开放 UDP 端口 $port"
}

# 启动客户端
start_client() {
    local server_addr=""
    local data_size=1
    local port=9331
    
    while [[ $# -gt 0 ]]; do
        case $1 in
            -s) server_addr="$2"; shift 2 ;;
            -g) data_size="$2"; shift 2 ;;
            -p) port="$2"; shift 2 ;;
            *) shift ;;
        esac
    done
    
    if [ -z "$server_addr" ]; then
        print_error "请指定服务器地址: $0 client -s <server_ip>"
        exit 1
    fi
    
    print_info "启动 lsquic 客户端..."
    print_info "服务器: $server_addr:$port"
    print_info "数据量: ${data_size}GB"
    
    # 停止已存在的容器
    docker rm -f lsquic-client 2>/dev/null || true
    
    # 启动客户端
    docker run -it --rm \
        --name lsquic-client \
        --network host \
        lsquic-speed-test:latest \
        /app/build/speed_test_client -s "$server_addr" -p "$port" -g "$data_size"
}

# 本地测试
local_test() {
    local data_size=${1:-1}
    
    print_info "启动本地测试 (数据量: ${data_size}GB)..."
    
    # 停止已存在的容器
    docker rm -f lsquic-server lsquic-client 2>/dev/null || true
    
    # 启动服务端
    docker run -d \
        --name lsquic-server \
        --network host \
        lsquic-speed-test:latest \
        bash -c "
            cd /app &&
            openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes -subj '/CN=localhost' 2>/dev/null &&
            ./build/speed_test_server
        "
    
    print_info "等待服务端启动..."
    sleep 3
    
    # 启动客户端
    docker run -it --rm \
        --name lsquic-client \
        --network host \
        lsquic-speed-test:latest \
        /app/build/speed_test_client -s 127.0.0.1 -g "$data_size"
    
    # 清理
    docker rm -f lsquic-server 2>/dev/null || true
}

# 清理
cleanup() {
    print_info "清理容器和镜像..."
    docker rm -f lsquic-server lsquic-client 2>/dev/null || true
    print_info "清理完成!"
}

# 主函数
main() {
    local cmd=${1:-help}
    shift || true
    
    case $cmd in
        build)
            build_image "$@"
            ;;
        server)
            start_server "$@"
            ;;
        client)
            start_client "$@"
            ;;
        local-test)
            local_test "$@"
            ;;
        clean)
            cleanup
            ;;
        help|--help|-h)
            show_help
            ;;
        *)
            print_error "未知命令: $cmd"
            show_help
            exit 1
            ;;
    esac
}

main "$@"
