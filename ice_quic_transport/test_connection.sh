#!/bin/bash
# ICE+QUIC 连接测试辅助脚本

echo "=== ICE+QUIC 连接测试辅助工具 ==="
echo ""

# 检查是否在 build 目录
if [ ! -f "speed_test_server" ] || [ ! -f "speed_test_client" ]; then
    echo "错误: 请在 build 目录下运行此脚本"
    echo "或者先编译程序: cd ice_quic_transport && ./build_speed_test.sh"
    exit 1
fi

echo "1. 网络连通性测试"
echo "===================="
echo ""
read -p "请输入对端服务器 IP 地址: " PEER_IP

if [ -z "$PEER_IP" ]; then
    echo "未输入 IP 地址，跳过网络测试"
else
    echo ""
    echo "测试 ICMP (ping)..."
    if ping -c 3 -W 2 "$PEER_IP" > /dev/null 2>&1; then
        echo "✓ ICMP 连通性正常"
    else
        echo "✗ ICMP 连通性失败 (可能被防火墙阻止，但不影响 UDP)"
    fi
    
    echo ""
    echo "测试 UDP 连通性..."
    echo "提示: 需要在对端运行: nc -u -l 12345"
    echo "(按 Ctrl+C 跳过此测试)"
    timeout 3 nc -u -v "$PEER_IP" 12345 2>&1 | head -1
fi

echo ""
echo "2. STUN 服务器测试"
echo "===================="
echo ""
echo "测试 Google STUN 服务器..."
if timeout 3 nc -u -v stun.l.google.com 19302 2>&1 | grep -q "succeeded\|open"; then
    echo "✓ STUN 服务器可达"
else
    echo "✗ STUN 服务器不可达"
    echo "  可能需要检查网络或使用其他 STUN 服务器"
fi

echo ""
echo "3. 防火墙检查"
echo "===================="
echo ""

# 检查 UFW (Ubuntu/Debian)
if command -v ufw &> /dev/null; then
    echo "检测到 UFW 防火墙:"
    sudo ufw status | grep -i "status\|udp" || echo "  UFW 未启用或无 UDP 规则"
fi

# 检查 firewalld (CentOS/RHEL)
if command -v firewall-cmd &> /dev/null; then
    echo "检测到 firewalld 防火墙:"
    sudo firewall-cmd --list-all | grep -i "services\|ports" || echo "  无特殊规则"
fi

# 检查 iptables
if command -v iptables &> /dev/null; then
    echo ""
    echo "iptables UDP 规则:"
    sudo iptables -L -n | grep -i udp | head -5 || echo "  无 UDP 相关规则"
fi

echo ""
echo "4. 系统资源检查"
echo "===================="
echo ""

echo "CPU 核心数: $(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo "未知")"
echo "可用内存: $(free -h 2>/dev/null | grep Mem | awk '{print $7}' || echo "未知")"

echo ""
echo "UDP 缓冲区大小:"
sysctl net.core.rmem_max net.core.wmem_max 2>/dev/null || echo "  无法获取"

echo ""
echo "5. TLS 证书检查"
echo "===================="
echo ""

if [ -f "server.crt" ] && [ -f "server.key" ]; then
    echo "✓ 找到证书文件"
    echo ""
    echo "证书信息:"
    openssl x509 -in server.crt -noout -subject -dates 2>/dev/null || echo "  无法读取证书"
    echo ""
    echo "证书有效性:"
    openssl x509 -in server.crt -noout -checkend 0 2>/dev/null && echo "  ✓ 证书有效" || echo "  ✗ 证书已过期"
else
    echo "✗ 未找到证书文件 (server.crt / server.key)"
    echo ""
    echo "生成自签名证书:"
    echo "  openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt \\"
    echo "      -days 365 -nodes -subj \"/CN=localhost\""
fi

echo ""
echo "6. 候选格式示例"
echo "===================="
echo ""
echo "正确的候选格式 (不带 a= 前缀):"
echo "  candidate:1 1 UDP 2130706431 192.168.1.100 54321 typ host"
echo ""
echo "如果你的候选是这样的:"
echo "  a=candidate:1 1 UDP 2130706431 192.168.1.100 54321 typ host"
echo ""
echo "新版本程序会自动移除 'a=' 前缀，但建议手动移除后输入"

echo ""
echo "=== 测试完成 ==="
echo ""
echo "如果所有检查都通过，可以开始运行速度测试:"
echo ""
echo "服务端: ./speed_test_server server.crt server.key"
echo "客户端: ./speed_test_client 1"
echo ""
echo "详细故障排查请查看: ../TROUBLESHOOTING.md"
