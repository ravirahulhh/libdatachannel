/**
 * lsquic 速度测试服务端
 * 接收客户端数据并统计传输速度
 */

#include <iostream>
#include <iomanip>
#include <cstring>
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include <csignal>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <event2/event.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

extern "C" {
#include <lsquic.h>
}

using namespace std;
using namespace std::chrono;

// 全局变量
static lsquic_engine_t *g_engine = nullptr;
static struct event_base *g_event_base = nullptr;
static struct event *g_socket_event = nullptr;
static struct event *g_timer_event = nullptr;
static int g_socket_fd = -1;
static struct sockaddr_storage g_local_addr;

// 统计数据
static atomic<uint64_t> g_total_bytes{0};
static atomic<uint64_t> g_session_bytes{0};
static atomic<uint64_t> g_connections{0};
static atomic<bool> g_running{true};
static steady_clock::time_point g_session_start;
static uint64_t g_last_bytes{0};
static steady_clock::time_point g_last_time;

// 配置
static const uint16_t PORT = 9331;
static const char* CERT_FILE = "/app/cert.pem";
static const char* KEY_FILE = "/app/key.pem";

// SSL 上下文
static SSL_CTX *g_ssl_ctx = nullptr;

// 前向声明
static void process_conns(evutil_socket_t fd, short what, void *arg);
static void timer_handler(evutil_socket_t fd, short what, void *arg);

// 打印统计信息
void print_stats() {
    g_last_time = steady_clock::now();
    g_last_bytes = 0;
    
    while (g_running) {
        this_thread::sleep_for(milliseconds(500));
        
        auto now = steady_clock::now();
        uint64_t current = g_session_bytes;
        
        auto interval = duration_cast<milliseconds>(now - g_last_time).count();
        double instant_mbps = 0;
        if (interval > 0) {
            uint64_t delta = current - g_last_bytes;
            instant_mbps = (delta * 8.0) / (interval * 1000.0);
        }
        
        double avg_mbps = 0;
        if (g_connections > 0) {
            auto elapsed = duration_cast<milliseconds>(now - g_session_start).count();
            if (elapsed > 0) {
                avg_mbps = (current * 8.0) / (elapsed * 1000.0);
            }
        }
        
        double mb = current / (1024.0 * 1024.0);
        
        cout << "\r[Server] Connections: " << g_connections
             << " | Received: " << fixed << setprecision(2) << mb << " MB"
             << " | Instant: " << instant_mbps << " Mbps"
             << " | Avg: " << avg_mbps << " Mbps    " << flush;
        
        g_last_bytes = current;
        g_last_time = now;
    }
}


// 握手完成回调
static void on_hsk_done(lsquic_conn_t *conn, enum lsquic_hsk_status status) {
    cout << "[Server] Handshake done, status=" << (int)status << endl;
    if (status == LSQ_HSK_OK || status == LSQ_HSK_RESUMED_OK) {
        cout << "[Server] Handshake successful!" << endl;
    } else {
        cout << "[Server] Handshake failed!" << endl;
    }
}

// lsquic 回调函数
static lsquic_conn_ctx_t* on_new_conn(void *stream_if_ctx, lsquic_conn_t *conn) {
    g_connections++;
    g_session_bytes = 0;
    g_session_start = steady_clock::now();
    cout << "\n[Server] New connection object created! Total: " << g_connections << endl;
    return nullptr;
}

static void on_conn_closed(lsquic_conn_t *conn) {
    auto elapsed = duration_cast<milliseconds>(steady_clock::now() - g_session_start).count();
    double avg_mbps = (g_session_bytes * 8.0) / (elapsed * 1000.0);
    double gb = g_session_bytes / (1024.0 * 1024.0 * 1024.0);
    
    cout << "\n[Server] Connection closed!" << endl;
    cout << "[Server] Session received: " << fixed << setprecision(3) << gb << " GB" << endl;
    cout << "[Server] Session time: " << (elapsed / 1000.0) << " seconds" << endl;
    cout << "[Server] Average speed: " << avg_mbps << " Mbps" << endl;
}

static lsquic_stream_ctx_t* on_new_stream(void *stream_if_ctx, lsquic_stream_t *stream) {
    lsquic_stream_wantread(stream, 1);
    return nullptr;
}

static void on_read(lsquic_stream_t *stream, lsquic_stream_ctx_t *ctx) {
    unsigned char buf[64 * 1024];
    ssize_t nr;
    
    while ((nr = lsquic_stream_read(stream, buf, sizeof(buf))) > 0) {
        g_total_bytes += nr;
        g_session_bytes += nr;
    }
    
    if (nr == 0) {
        // EOF
        lsquic_stream_shutdown(stream, 0);
        lsquic_stream_wantread(stream, 0);
    }
}

static void on_write(lsquic_stream_t *stream, lsquic_stream_ctx_t *ctx) {
    // 服务端不需要写数据
}

static void on_close(lsquic_stream_t *stream, lsquic_stream_ctx_t *ctx) {
    // 流关闭
}

static const struct lsquic_stream_if stream_if = {
    .on_new_conn = on_new_conn,
    .on_conn_closed = on_conn_closed,
    .on_new_stream = on_new_stream,
    .on_read = on_read,
    .on_write = on_write,
    .on_close = on_close,
    .on_hsk_done = on_hsk_done,
};

// SSL 回调
static SSL_CTX* get_ssl_ctx(void *peer_ctx, const struct sockaddr *local) {
    return g_ssl_ctx;
}

// 发送数据包
static int send_packets(void *ctx, const struct lsquic_out_spec *specs, unsigned n_specs) {
    struct msghdr msg;
    int n_sent = 0;
    
    memset(&msg, 0, sizeof(msg));
    
    for (unsigned i = 0; i < n_specs; ++i) {
        msg.msg_name = (void*)specs[i].dest_sa;
        msg.msg_namelen = (specs[i].dest_sa->sa_family == AF_INET) ? 
                          sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
        msg.msg_iov = specs[i].iov;
        msg.msg_iovlen = specs[i].iovlen;
        
        if (sendmsg(g_socket_fd, &msg, 0) < 0) {
            break;
        }
        ++n_sent;
    }
    
    return n_sent;
}

// 调试计数器
static atomic<uint64_t> g_udp_packets{0};

// 处理接收的数据包
static void read_socket(evutil_socket_t fd, short what, void *arg) {
    if (!g_engine) {
        cerr << "[ERROR] read_socket: g_engine is NULL" << endl;
        return;
    }
    
    unsigned char buf[0xFFFF];
    struct sockaddr_storage peer_addr;
    socklen_t peer_addr_len;
    
    // 循环读取所有可用的数据包（最多 10 个，避免饿死其他事件）
    for (int i = 0; i < 10; i++) {
        peer_addr_len = sizeof(peer_addr);  // 每次循环都要重置
        
        ssize_t nr = recvfrom(fd, buf, sizeof(buf), 0, 
                              (struct sockaddr*)&peer_addr, &peer_addr_len);
        
        if (nr < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 没有更多数据
                break;
            }
            if (g_udp_packets < 5) {
                cerr << "[DEBUG] recvfrom error: " << strerror(errno) << endl;
            }
            break;
        }
        
        if (nr > 0) {
            g_udp_packets++;
            if (g_udp_packets <= 10) {
                char addr_str[INET_ADDRSTRLEN];
                struct sockaddr_in *sin = (struct sockaddr_in*)&peer_addr;
                inet_ntop(AF_INET, &sin->sin_addr, addr_str, sizeof(addr_str));
                cout << "\n[DEBUG] Received UDP packet #" << g_udp_packets 
                     << " from " << addr_str << ":" << ntohs(sin->sin_port)
                     << " size=" << nr << " bytes" << endl;
            }
            
            int ret = lsquic_engine_packet_in(g_engine, buf, nr,
                                              (struct sockaddr*)&g_local_addr,
                                              (struct sockaddr*)&peer_addr,
                                              nullptr, 0);
            if (ret != 0 && g_udp_packets <= 10) {
                cout << "[DEBUG] lsquic_engine_packet_in returned: " << ret << endl;
            }
        }
    }
    
    lsquic_engine_process_conns(g_engine);
    
    // 重新调度定时器
    int diff;
    if (g_timer_event && lsquic_engine_earliest_adv_tick(g_engine, &diff)) {
        struct timeval tv;
        if (diff > 0) {
            tv.tv_sec = diff / 1000000;
            tv.tv_usec = diff % 1000000;
        } else {
            tv.tv_sec = 0;
            tv.tv_usec = 1000;  // 1ms
        }
        event_add(g_timer_event, &tv);
    }
}

static void timer_handler(evutil_socket_t fd, short what, void *arg) {
    if (!g_engine) {
        return;
    }
    
    lsquic_engine_process_conns(g_engine);
    
    // 重新调度定时器
    int diff;
    if (g_timer_event && lsquic_engine_earliest_adv_tick(g_engine, &diff)) {
        struct timeval tv;
        if (diff > 0) {
            tv.tv_sec = diff / 1000000;
            tv.tv_usec = diff % 1000000;
        } else {
            tv.tv_sec = 0;
            tv.tv_usec = 1000;  // 1ms
        }
        event_add(g_timer_event, &tv);
    }
}


// ALPN 选择回调
static int select_alpn(SSL *ssl, const unsigned char **out, unsigned char *outlen,
                      const unsigned char *in, unsigned int inlen, void *arg) {
    // 查找 "speedtest" ALPN
    const unsigned char alpn[] = "speedtest";
    const unsigned char *p = in;
    
    while (p < in + inlen) {
        unsigned char len = *p++;
        if (len == sizeof(alpn) - 1 && memcmp(p, alpn, len) == 0) {
            *out = p;
            *outlen = len;
            cout << "[DEBUG] ALPN selected: speedtest" << endl;
            return SSL_TLSEXT_ERR_OK;
        }
        p += len;
    }
    
    cout << "[DEBUG] ALPN not found, using first available" << endl;
    // 如果没找到，使用第一个
    if (inlen > 0) {
        *outlen = in[0];
        *out = in + 1;
        return SSL_TLSEXT_ERR_OK;
    }
    
    return SSL_TLSEXT_ERR_NOACK;
}

// 初始化 SSL
static bool init_ssl() {
    g_ssl_ctx = SSL_CTX_new(TLS_method());  // 使用通用方法
    if (!g_ssl_ctx) {
        cerr << "Failed to create SSL context" << endl;
        return false;
    }
    
    if (SSL_CTX_use_certificate_file(g_ssl_ctx, CERT_FILE, SSL_FILETYPE_PEM) != 1) {
        cerr << "Failed to load certificate: " << CERT_FILE << endl;
        cerr << "Please generate certificates first:" << endl;
        cerr << "  openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes -subj '/CN=localhost'" << endl;
        return false;
    }
    
    if (SSL_CTX_use_PrivateKey_file(g_ssl_ctx, KEY_FILE, SSL_FILETYPE_PEM) != 1) {
        cerr << "Failed to load private key: " << KEY_FILE << endl;
        return false;
    }
    
    // 设置 ALPN 回调
    SSL_CTX_set_alpn_select_cb(g_ssl_ctx, select_alpn, nullptr);
    
    // 设置 TLS 1.3
    SSL_CTX_set_min_proto_version(g_ssl_ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(g_ssl_ctx, TLS1_3_VERSION);
    
    cout << "[DEBUG] SSL_CTX configured with ALPN support" << endl;
    
    return true;
}

// 初始化 socket
static bool init_socket() {
    g_socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_socket_fd < 0) {
        cerr << "Failed to create socket" << endl;
        return false;
    }
    
    // 设置非阻塞
    int flags = fcntl(g_socket_fd, F_GETFL, 0);
    fcntl(g_socket_fd, F_SETFL, flags | O_NONBLOCK);
    
    // 增大缓冲区
    int buf_size = 4 * 1024 * 1024;
    setsockopt(g_socket_fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
    setsockopt(g_socket_fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
    
    // 绑定地址
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(g_socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        cerr << "Failed to bind socket to port " << PORT << endl;
        return false;
    }
    
    memcpy(&g_local_addr, &addr, sizeof(addr));
    
    return true;
}

// lsquic 日志回调
static int log_buf(void *ctx, const char *buf, size_t len) {
    fwrite(buf, 1, len, stderr);
    return (int)len;
}

// 初始化 lsquic 引擎
static bool init_engine() {
    // 设置 lsquic 日志
    struct lsquic_logger_if logger_if = { .log_buf = log_buf };
    lsquic_logger_init(&logger_if, nullptr, LLTS_HHMMSSMS);
    lsquic_set_log_level("event=debug,engine=debug,conn=debug,stream=debug");
    
    if (lsquic_global_init(LSQUIC_GLOBAL_SERVER | LSQUIC_GLOBAL_CLIENT) != 0) {
        cerr << "Failed to initialize lsquic" << endl;
        return false;
    }
    
    struct lsquic_engine_api api;
    memset(&api, 0, sizeof(api));
    
    struct lsquic_engine_settings settings;
    lsquic_engine_init_settings(&settings, LSENG_SERVER);
    settings.es_max_streams_in = 100;
    settings.es_idle_timeout = 60;
    // 明确使用 IETF QUIC v1
    settings.es_versions = (1 << LSQVER_I001);
    
    cout << "[DEBUG] Server QUIC versions: 0x" << hex << settings.es_versions << dec << endl;
    
    api.ea_settings = &settings;
    api.ea_stream_if = &stream_if;
    api.ea_stream_if_ctx = nullptr;
    api.ea_packets_out = send_packets;
    api.ea_packets_out_ctx = nullptr;
    api.ea_get_ssl_ctx = get_ssl_ctx;
    
    g_engine = lsquic_engine_new(LSENG_SERVER, &api);
    if (!g_engine) {
        cerr << "Failed to create lsquic engine" << endl;
        return false;
    }
    
    cout << "[DEBUG] Server engine created successfully" << endl;
    
    return true;
}

// 信号处理
static void signal_handler(int sig) {
    g_running = false;
    if (g_event_base) {
        event_base_loopbreak(g_event_base);
    }
}

int main(int argc, char *argv[]) {
    cout << "=== lsquic Speed Test Server ===" << endl;
    cout << "Listening on port " << PORT << " (UDP)" << endl;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    if (!init_ssl()) {
        return 1;
    }
    
    if (!init_socket()) {
        return 1;
    }
    
    if (!init_engine()) {
        return 1;
    }
    
    // 初始化 libevent
    g_event_base = event_base_new();
    if (!g_event_base) {
        cerr << "Failed to create event base" << endl;
        return 1;
    }
    
    g_socket_event = event_new(g_event_base, g_socket_fd, EV_READ | EV_PERSIST, read_socket, nullptr);
    event_add(g_socket_event, nullptr);
    
    g_timer_event = evtimer_new(g_event_base, timer_handler, nullptr);
    // 启动定时器
    struct timeval tv = {0, 1000};
    event_add(g_timer_event, &tv);
    
    cout << "Server started successfully. Waiting for connections..." << endl;
    cout << "Press Ctrl+C to stop\n" << endl;
    
    // 启动统计线程
    thread stats_thread(print_stats);
    
    // 运行事件循环
    event_base_dispatch(g_event_base);
    
    g_running = false;
    stats_thread.join();
    
    // 清理
    if (g_timer_event) event_free(g_timer_event);
    if (g_socket_event) event_free(g_socket_event);
    if (g_event_base) event_base_free(g_event_base);
    if (g_engine) lsquic_engine_destroy(g_engine);
    if (g_socket_fd >= 0) close(g_socket_fd);
    if (g_ssl_ctx) SSL_CTX_free(g_ssl_ctx);
    lsquic_global_cleanup();
    
    cout << "\nServer stopped." << endl;
    return 0;
}
