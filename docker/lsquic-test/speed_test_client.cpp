/**
 * lsquic 速度测试客户端
 * 向服务端发送大量数据并统计传输速度
 */

#include <iostream>
#include <iomanip>
#include <cstring>
#include <chrono>
#include <atomic>
#include <thread>
#include <vector>
#include <csignal>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
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

// SSL 上下文
static SSL_CTX *g_ssl_ctx = nullptr;

// 全局变量
static lsquic_engine_t *g_engine = nullptr;
static lsquic_conn_t *g_conn = nullptr;
static lsquic_stream_t *g_stream = nullptr;
static struct event_base *g_event_base = nullptr;
static struct event *g_socket_event = nullptr;
static struct event *g_timer_event = nullptr;
static int g_socket_fd = -1;
static struct sockaddr_storage g_local_addr;
static struct sockaddr_storage g_peer_addr;

// 统计数据
static atomic<uint64_t> g_bytes_queued{0};    // 写入 lsquic 缓冲区的字节数
static atomic<bool> g_connected{false};
static atomic<bool> g_running{true};
static atomic<bool> g_complete{false};
static atomic<bool> g_stream_closed{false};   // 流是否已关闭 (表示所有数据已确认)
static steady_clock::time_point g_start_time;

// 配置
static string g_server_addr = "127.0.0.1";
static uint16_t g_server_port = 9331;
static uint64_t g_data_size_gb = 1;
static uint32_t g_buffer_size = 64 * 1024;

// 发送缓冲区
static vector<uint8_t> g_send_buffer;
static uint64_t g_total_to_send = 0;
static uint64_t g_sent = 0;

// 打印统计信息
// 注意: lsquic 不提供实时的 ACK 统计 API，我们只能跟踪写入缓冲区的字节数
// 流关闭时表示所有数据已被确认
void print_stats() {
    while (g_running && !g_complete) {
        this_thread::sleep_for(milliseconds(500));
        
        if (!g_connected) continue;
        
        auto now = steady_clock::now();
        auto elapsed = duration_cast<milliseconds>(now - g_start_time).count();
        
        if (elapsed > 0) {
            double mbps = (g_bytes_queued * 8.0) / (elapsed * 1000.0);
            double mb_queued = g_bytes_queued / (1024.0 * 1024.0);
            double progress = (g_bytes_queued * 100.0) / g_total_to_send;
            
            cout << "\r[Client] Queued: " << fixed << setprecision(2) << mb_queued << " MB"
                 << " | Speed: " << mbps << " Mbps"
                 << " | Progress: " << progress << "%    " << flush;
        }
    }
}


// lsquic 回调函数
static lsquic_conn_ctx_t* on_new_conn(void *stream_if_ctx, lsquic_conn_t *conn) {
    g_conn = conn;
    g_connected = true;
    g_start_time = steady_clock::now();
    cout << "\n[Client] Connected to server!" << endl;
    
    // 创建流
    lsquic_conn_make_stream(conn);
    return nullptr;
}

static void on_conn_closed(lsquic_conn_t *conn) {
    cout << "\n[Client] Connection closed" << endl;
    g_running = false;
}

static lsquic_stream_ctx_t* on_new_stream(void *stream_if_ctx, lsquic_stream_t *stream) {
    g_stream = stream;
    lsquic_stream_wantwrite(stream, 1);
    cout << "[Client] Stream created, starting data transfer: " << g_data_size_gb << " GB" << endl;
    return nullptr;
}

static void on_read(lsquic_stream_t *stream, lsquic_stream_ctx_t *ctx) {
    // 客户端不需要读数据
    unsigned char buf[1024];
    lsquic_stream_read(stream, buf, sizeof(buf));
}

static void on_write(lsquic_stream_t *stream, lsquic_stream_ctx_t *ctx) {
    if (g_sent >= g_total_to_send) {
        // 所有数据已写入缓冲区，关闭写端
        lsquic_stream_shutdown(stream, 1);  // 发送 FIN
        lsquic_stream_wantwrite(stream, 0);
        cout << "\n[Client] All data queued, waiting for transmission to complete..." << endl;
        return;
    }
    
    // 发送数据
    while (g_sent < g_total_to_send) {
        size_t to_send = min((uint64_t)g_buffer_size, g_total_to_send - g_sent);
        ssize_t nw = lsquic_stream_write(stream, g_send_buffer.data(), to_send);
        
        if (nw > 0) {
            g_sent += nw;
            g_bytes_queued = g_sent;
        } else if (nw == 0) {
            // 缓冲区满，等待下次回调
            break;
        } else {
            cerr << "\n[Client] Write error" << endl;
            break;
        }
    }
    
    lsquic_stream_flush(stream);
}

static void on_close(lsquic_stream_t *stream, lsquic_stream_ctx_t *ctx) {
    // 流关闭意味着所有数据都已被对端确认接收
    g_stream = nullptr;
    g_stream_closed = true;
    
    auto end_time = steady_clock::now();
    auto elapsed = duration_cast<milliseconds>(end_time - g_start_time).count();
    
    cout << "\n\n=== Transfer Complete (Stream Closed - All Data ACKed) ===" << endl;
    cout << "Total sent: " << (g_bytes_queued / (1024.0 * 1024.0 * 1024.0)) << " GB" << endl;
    cout << "Time: " << (elapsed / 1000.0) << " seconds" << endl;
    cout << "Average speed: " << ((g_bytes_queued * 8.0) / (elapsed * 1000.0)) << " Mbps" << endl;
    
    g_complete = true;
}

static const struct lsquic_stream_if stream_if = {
    .on_new_conn = on_new_conn,
    .on_conn_closed = on_conn_closed,
    .on_new_stream = on_new_stream,
    .on_read = on_read,
    .on_write = on_write,
    .on_close = on_close,
};

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

// 处理接收的数据包
static void read_socket(evutil_socket_t fd, short what, void *arg) {
    unsigned char buf[0xFFFF];
    struct sockaddr_storage peer_addr;
    socklen_t peer_addr_len = sizeof(peer_addr);
    
    ssize_t nr = recvfrom(fd, buf, sizeof(buf), 0, 
                          (struct sockaddr*)&peer_addr, &peer_addr_len);
    
    if (nr > 0) {
        lsquic_engine_packet_in(g_engine, buf, nr,
                                (struct sockaddr*)&g_local_addr,
                                (struct sockaddr*)&peer_addr,
                                nullptr, 0);
    }
    
    lsquic_engine_process_conns(g_engine);
    
    if (lsquic_engine_earliest_adv_tick(g_engine, nullptr)) {
        struct timeval tv = {0, 1000};
        event_add(g_timer_event, &tv);
    }
}

static void timer_handler(evutil_socket_t fd, short what, void *arg) {
    lsquic_engine_process_conns(g_engine);
    
    if (lsquic_engine_earliest_adv_tick(g_engine, nullptr)) {
        struct timeval tv = {0, 1000};
        event_add(g_timer_event, &tv);
    }
    
    if (g_complete && g_stream_closed) {
        // 流已关闭，所有数据已确认，可以退出
        static int wait_count = 0;
        if (++wait_count > 10) {
            event_base_loopbreak(g_event_base);
        }
    }
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
    
    // 绑定本地地址
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = 0;
    local.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(g_socket_fd, (struct sockaddr*)&local, sizeof(local)) < 0) {
        cerr << "Failed to bind socket" << endl;
        return false;
    }
    
    socklen_t len = sizeof(g_local_addr);
    getsockname(g_socket_fd, (struct sockaddr*)&g_local_addr, &len);
    
    // 解析服务器地址
    struct sockaddr_in *peer = (struct sockaddr_in*)&g_peer_addr;
    memset(peer, 0, sizeof(*peer));
    peer->sin_family = AF_INET;
    peer->sin_port = htons(g_server_port);
    
    if (inet_pton(AF_INET, g_server_addr.c_str(), &peer->sin_addr) != 1) {
        // 尝试 DNS 解析
        struct hostent *he = gethostbyname(g_server_addr.c_str());
        if (!he) {
            cerr << "Failed to resolve server address: " << g_server_addr << endl;
            return false;
        }
        memcpy(&peer->sin_addr, he->h_addr_list[0], he->h_length);
    }
    
    return true;
}

// SSL 回调 - 客户端需要提供 SSL_CTX
static SSL_CTX* get_ssl_ctx(void *peer_ctx, const struct sockaddr *local) {
    return g_ssl_ctx;
}

// 初始化 SSL (客户端模式)
static bool init_ssl() {
    g_ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!g_ssl_ctx) {
        cerr << "Failed to create SSL context" << endl;
        return false;
    }
    
    // 客户端不验证服务器证书 (测试用)
    SSL_CTX_set_verify(g_ssl_ctx, SSL_VERIFY_NONE, nullptr);
    
    // 设置 ALPN (lsquic 需要)
    static const unsigned char alpn[] = "\x02h3";  // HTTP/3
    SSL_CTX_set_alpn_protos(g_ssl_ctx, alpn, sizeof(alpn) - 1);
    
    return true;
}

// 初始化 lsquic 引擎
static bool init_engine() {
    if (lsquic_global_init(LSQUIC_GLOBAL_CLIENT) != 0) {
        cerr << "Failed to initialize lsquic" << endl;
        return false;
    }
    
    struct lsquic_engine_api api;
    memset(&api, 0, sizeof(api));
    
    struct lsquic_engine_settings settings;
    lsquic_engine_init_settings(&settings, 0);  // Client mode
    settings.es_max_streams_in = 100;
    settings.es_idle_timeout = 60;
    
    api.ea_settings = &settings;
    api.ea_stream_if = &stream_if;
    api.ea_stream_if_ctx = nullptr;
    api.ea_packets_out = send_packets;
    api.ea_packets_out_ctx = nullptr;
    api.ea_get_ssl_ctx = get_ssl_ctx;  // 添加 SSL 回调
    
    g_engine = lsquic_engine_new(0, &api);  // Client mode
    if (!g_engine) {
        cerr << "Failed to create lsquic engine" << endl;
        return false;
    }
    
    return true;
}

// 连接服务器
static bool connect_to_server() {
    cout << "[Client] Connecting to " << g_server_addr << ":" << g_server_port << "..." << endl;
    
    g_conn = lsquic_engine_connect(
        g_engine,
        N_LSQVER,
        (struct sockaddr*)&g_local_addr,
        (struct sockaddr*)&g_peer_addr,
        nullptr,
        nullptr,
        g_server_addr.c_str(),
        0,
        nullptr, 0,
        nullptr, 0
    );
    
    if (!g_conn) {
        cerr << "Failed to create connection" << endl;
        return false;
    }
    
    lsquic_engine_process_conns(g_engine);
    
    return true;
}

// 信号处理
static void signal_handler(int sig) {
    g_running = false;
    if (g_event_base) {
        event_base_loopbreak(g_event_base);
    }
}

void print_usage(const char* prog) {
    cout << "Usage: " << prog << " [options]" << endl;
    cout << "Options:" << endl;
    cout << "  -s <address>  Server address (default: 127.0.0.1)" << endl;
    cout << "  -p <port>     Server port (default: 9331)" << endl;
    cout << "  -g <size>     Data size in GB (default: 1)" << endl;
    cout << "  -b <size>     Buffer size in KB (default: 64)" << endl;
    cout << "  -h            Show this help" << endl;
}

int main(int argc, char *argv[]) {
    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            g_server_addr = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            g_server_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            g_data_size_gb = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            g_buffer_size = atoi(argv[++i]) * 1024;
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }
    
    cout << "=== lsquic Speed Test Client ===" << endl;
    cout << "Server: " << g_server_addr << ":" << g_server_port << endl;
    cout << "Data size: " << g_data_size_gb << " GB" << endl;
    cout << "Buffer size: " << (g_buffer_size / 1024) << " KB" << endl;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 初始化发送缓冲区
    g_send_buffer.resize(g_buffer_size, 'X');
    g_total_to_send = g_data_size_gb * 1024ULL * 1024ULL * 1024ULL;
    
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
    struct timeval tv = {0, 1000};
    event_add(g_timer_event, &tv);
    
    if (!connect_to_server()) {
        return 1;
    }
    
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
    
    return 0;
}
