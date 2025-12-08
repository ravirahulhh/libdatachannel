/**
 * MsQuic 速度测试服务端
 * 接收客户端数据并统计传输速度
 */

#include <iostream>
#include <iomanip>
#include <cstring>
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include <msquic.h>

using namespace std;
using namespace std::chrono;

// 全局变量
const QUIC_API_TABLE* MsQuic = nullptr;
HQUIC Registration = nullptr;
HQUIC Configuration = nullptr;
HQUIC Listener = nullptr;

// 统计数据
atomic<uint64_t> TotalBytesReceived{0};
atomic<uint64_t> SessionBytesReceived{0};  // 当前会话接收的字节数
atomic<uint64_t> TotalConnections{0};
atomic<uint64_t> ActiveConnections{0};
atomic<bool> Running{true};
atomic<bool> HasActiveSession{false};
steady_clock::time_point StartTime;
steady_clock::time_point SessionStartTime;
uint64_t LastBytesReceived{0};  // 用于计算瞬时速度
steady_clock::time_point LastStatsTime;
mutex StatsMutex;

// 配置
const char* ALPN = "speedtest";
const uint16_t PORT = 9331;
string CongestionAlgorithm = "cubic";  // 默认使用 CUBIC，可选: cubic, bbr

// 证书配置 (自签名)
QUIC_CREDENTIAL_CONFIG CredConfig;

void PrintStats() {
    LastStatsTime = steady_clock::now();
    LastBytesReceived = 0;
    
    while (Running) {
        this_thread::sleep_for(milliseconds(500));
        
        auto now = steady_clock::now();
        uint64_t currentBytes = SessionBytesReceived;
        
        // 计算瞬时速度 (基于上次统计间隔)
        auto intervalMs = duration_cast<milliseconds>(now - LastStatsTime).count();
        double instantMbps = 0;
        if (intervalMs > 0) {
            uint64_t deltaBytes = currentBytes - LastBytesReceived;
            instantMbps = (deltaBytes * 8.0) / (intervalMs * 1000.0);
        }
        
        // 计算平均速度 (基于会话开始时间)
        double avgMbps = 0;
        if (HasActiveSession) {
            auto sessionElapsed = duration_cast<milliseconds>(now - SessionStartTime).count();
            if (sessionElapsed > 0) {
                avgMbps = (currentBytes * 8.0) / (sessionElapsed * 1000.0);
            }
        }
        
        double mbReceived = currentBytes / (1024.0 * 1024.0);
        
        cout << "\r[Server] Active: " << ActiveConnections
             << " | Received: " << fixed << setprecision(2) << mbReceived << " MB"
             << " | Instant: " << instantMbps << " Mbps"
             << " | Avg: " << avgMbps << " Mbps    " << flush;
        
        LastBytesReceived = currentBytes;
        LastStatsTime = now;
    }
}

QUIC_STATUS QUIC_API StreamCallback(HQUIC Stream, void* Context, QUIC_STREAM_EVENT* Event) {
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_RECEIVE:
        TotalBytesReceived += Event->RECEIVE.TotalBufferLength;
        SessionBytesReceived += Event->RECEIVE.TotalBufferLength;
        break;
        
    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        {
            auto now = steady_clock::now();
            auto elapsed = duration_cast<milliseconds>(now - SessionStartTime).count();
            double avgMbps = (SessionBytesReceived * 8.0) / (elapsed * 1000.0);
            double gbReceived = SessionBytesReceived / (1024.0 * 1024.0 * 1024.0);
            
            cout << "\n[Server] Stream completed!" << endl;
            cout << "[Server] Session received: " << fixed << setprecision(3) << gbReceived << " GB" << endl;
            cout << "[Server] Session time: " << (elapsed / 1000.0) << " seconds" << endl;
            cout << "[Server] Average speed: " << avgMbps << " Mbps" << endl;
        }
        MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL, 0);
        break;
        
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        MsQuic->StreamClose(Stream);
        break;
        
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS QUIC_API ConnectionCallback(HQUIC Connection, void* Context, QUIC_CONNECTION_EVENT* Event) {
    switch (Event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        {
            TotalConnections++;
            ActiveConnections++;
            SessionBytesReceived = 0;  // 重置会话统计
            SessionStartTime = steady_clock::now();
            HasActiveSession = true;
            
            // 设置拥塞控制算法
            uint16_t ccAlgo;
            if (CongestionAlgorithm == "bbr") {
                ccAlgo = QUIC_CONGESTION_CONTROL_ALGORITHM_BBR;
                cout << "\n[Server] Client connected! Using BBR congestion control" << endl;
            } else {
                ccAlgo = QUIC_CONGESTION_CONTROL_ALGORITHM_CUBIC;
                cout << "\n[Server] Client connected! Using CUBIC congestion control" << endl;
            }
            
            MsQuic->SetParam(Connection, QUIC_PARAM_CONN_CONGESTION_CONTROL_ALGORITHM,
                           sizeof(ccAlgo), &ccAlgo);
            
            cout << "[Server] Active: " << ActiveConnections << ", Total: " << TotalConnections << endl;
            MsQuic->ConnectionSendResumptionTicket(Connection, QUIC_SEND_RESUMPTION_FLAG_NONE, 0, nullptr);
        }
        break;
        
    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
        MsQuic->SetCallbackHandler(Event->PEER_STREAM_STARTED.Stream, (void*)StreamCallback, nullptr);
        break;
        
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        cout << "\n[Server] Connection shutdown" << endl;
        break;
        
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        ActiveConnections--;
        HasActiveSession = (ActiveConnections > 0);
        MsQuic->ConnectionClose(Connection);
        break;
        
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS QUIC_API ListenerCallback(HQUIC Listener, void* Context, QUIC_LISTENER_EVENT* Event) {
    switch (Event->Type) {
    case QUIC_LISTENER_EVENT_NEW_CONNECTION:
        MsQuic->SetCallbackHandler(Event->NEW_CONNECTION.Connection, (void*)ConnectionCallback, nullptr);
        return MsQuic->ConnectionSetConfiguration(Event->NEW_CONNECTION.Connection, Configuration);
        
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

bool InitializeServer() {
    QUIC_STATUS status;
    
    // 打开 MsQuic
    status = MsQuicOpen2(&MsQuic);
    if (QUIC_FAILED(status)) {
        cerr << "MsQuicOpen2 failed: 0x" << hex << status << endl;
        return false;
    }
    
    // 创建 Registration
    const QUIC_REGISTRATION_CONFIG regConfig = { "speedtest-server", QUIC_EXECUTION_PROFILE_LOW_LATENCY };
    status = MsQuic->RegistrationOpen(&regConfig, &Registration);
    if (QUIC_FAILED(status)) {
        cerr << "RegistrationOpen failed: 0x" << hex << status << endl;
        return false;
    }
    
    // 配置 ALPN
    const QUIC_BUFFER alpn = { (uint32_t)strlen(ALPN), (uint8_t*)ALPN };
    
    // 服务器设置
    QUIC_SETTINGS settings = {};
    settings.IdleTimeoutMs = 60000;
    settings.IsSet.IdleTimeoutMs = TRUE;
    settings.PeerBidiStreamCount = 100;
    settings.IsSet.PeerBidiStreamCount = TRUE;
    settings.PeerUnidiStreamCount = 100;
    settings.IsSet.PeerUnidiStreamCount = TRUE;
    
    // 创建 Configuration
    status = MsQuic->ConfigurationOpen(Registration, &alpn, 1, &settings, sizeof(settings), nullptr, &Configuration);
    if (QUIC_FAILED(status)) {
        cerr << "ConfigurationOpen failed: 0x" << hex << status << endl;
        return false;
    }
    
    // 使用自签名证书
    memset(&CredConfig, 0, sizeof(CredConfig));
    CredConfig.Type = QUIC_CREDENTIAL_TYPE_NONE;
    CredConfig.Flags = QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
    
    // 尝试使用自生成证书
    CredConfig.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;
    QUIC_CERTIFICATE_FILE certFile;
    certFile.CertificateFile = "/app/cert.pem";
    certFile.PrivateKeyFile = "/app/key.pem";
    CredConfig.CertificateFile = &certFile;
    
    status = MsQuic->ConfigurationLoadCredential(Configuration, &CredConfig);
    if (QUIC_FAILED(status)) {
        cerr << "ConfigurationLoadCredential failed: 0x" << hex << status << endl;
        cerr << "Please generate certificates first:" << endl;
        cerr << "  openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes -subj '/CN=localhost'" << endl;
        return false;
    }
    
    return true;
}

bool StartListener() {
    QUIC_STATUS status;
    
    // 创建 Listener
    status = MsQuic->ListenerOpen(Registration, ListenerCallback, nullptr, &Listener);
    if (QUIC_FAILED(status)) {
        cerr << "ListenerOpen failed: 0x" << hex << status << endl;
        return false;
    }
    
    // 监听地址
    QUIC_ADDR addr = {};
    QuicAddrSetFamily(&addr, QUIC_ADDRESS_FAMILY_UNSPEC);
    QuicAddrSetPort(&addr, PORT);
    
    const QUIC_BUFFER alpn = { (uint32_t)strlen(ALPN), (uint8_t*)ALPN };
    
    status = MsQuic->ListenerStart(Listener, &alpn, 1, &addr);
    if (QUIC_FAILED(status)) {
        cerr << "ListenerStart failed: 0x" << hex << status << endl;
        return false;
    }
    
    return true;
}

void Cleanup() {
    Running = false;
    
    if (Listener) {
        MsQuic->ListenerClose(Listener);
    }
    if (Configuration) {
        MsQuic->ConfigurationClose(Configuration);
    }
    if (Registration) {
        MsQuic->RegistrationClose(Registration);
    }
    if (MsQuic) {
        MsQuicClose(MsQuic);
    }
}

void PrintUsage(const char* prog) {
    cout << "Usage: " << prog << " [options]" << endl;
    cout << "Options:" << endl;
    cout << "  -c <algorithm>  Congestion control algorithm: cubic or bbr (default: cubic)" << endl;
    cout << "  -h              Show this help" << endl;
}

int main(int argc, char* argv[]) {
    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            CongestionAlgorithm = argv[++i];
            if (CongestionAlgorithm != "cubic" && CongestionAlgorithm != "bbr") {
                cerr << "Invalid congestion control algorithm: " << CongestionAlgorithm << endl;
                cerr << "Valid options: cubic, bbr" << endl;
                return 1;
            }
        } else if (strcmp(argv[i], "-h") == 0) {
            PrintUsage(argv[0]);
            return 0;
        }
    }
    
    cout << "=== MsQuic Speed Test Server ===" << endl;
    cout << "Listening on port " << PORT << " (UDP)" << endl;
    cout << "Congestion Control: " << CongestionAlgorithm << endl;
    
    if (!InitializeServer()) {
        cerr << "Failed to initialize server" << endl;
        return 1;
    }
    
    if (!StartListener()) {
        cerr << "Failed to start listener" << endl;
        Cleanup();
        return 1;
    }
    
    cout << "Server started successfully. Waiting for connections..." << endl;
    cout << "Press Ctrl+C to stop\n" << endl;
    
    StartTime = steady_clock::now();
    
    // 启动统计线程
    thread statsThread(PrintStats);
    
    // 等待退出信号
    cout << "Server running..." << endl;
    while (Running) {
        this_thread::sleep_for(seconds(1));
    }
    
    statsThread.join();
    Cleanup();
    
    cout << "\nServer stopped." << endl;
    return 0;
}
