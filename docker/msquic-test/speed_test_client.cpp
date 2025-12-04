/**
 * MsQuic 速度测试客户端
 * 向服务端发送大量数据并统计传输速度
 */

#include <iostream>
#include <cstring>
#include <chrono>
#include <atomic>
#include <thread>
#include <vector>
#include <iomanip>
#include <msquic.h>

using namespace std;
using namespace std::chrono;

// 全局变量
const QUIC_API_TABLE* MsQuic = nullptr;
HQUIC Registration = nullptr;
HQUIC Configuration = nullptr;
HQUIC Connection = nullptr;
HQUIC Stream = nullptr;

// 统计数据
atomic<uint64_t> TotalBytesSent{0};
atomic<bool> Connected{false};
atomic<bool> Running{true};
atomic<bool> SendComplete{false};
steady_clock::time_point StartTime;

// 配置
const char* ALPN = "speedtest";
string ServerAddress = "127.0.0.1";
uint16_t ServerPort = 9331;
uint64_t DataSizeGB = 1; // 默认发送 1GB 数据
uint32_t BufferSize = 64 * 1024; // 64KB 缓冲区

void PrintStats() {
    while (Running && !SendComplete) {
        this_thread::sleep_for(milliseconds(500));
        
        auto now = steady_clock::now();
        auto elapsed = duration_cast<milliseconds>(now - StartTime).count();
        
        if (elapsed > 0 && Connected) {
            double mbps = (TotalBytesSent * 8.0) / (elapsed * 1000.0);
            double mbSent = TotalBytesSent / (1024.0 * 1024.0);
            double progress = (TotalBytesSent * 100.0) / (DataSizeGB * 1024 * 1024 * 1024);
            
            cout << "\r[Client] Sent: " << fixed << setprecision(2) << mbSent << " MB"
                 << " | Speed: " << mbps << " Mbps"
                 << " | Progress: " << progress << "%" << flush;
        }
    }
}

QUIC_STATUS QUIC_API StreamCallback(HQUIC Stream, void* Context, QUIC_STREAM_EVENT* Event) {
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        // 发送完成，可以继续发送
        free(Event->SEND_COMPLETE.ClientContext);
        break;
        
    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        cout << "\n[Client] Server shutdown send" << endl;
        break;
        
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        SendComplete = true;
        break;
        
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS QUIC_API ConnectionCallback(HQUIC Connection, void* Context, QUIC_CONNECTION_EVENT* Event) {
    switch (Event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        cout << "\n[Client] Connected to server!" << endl;
        Connected = true;
        break;
        
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        cout << "\n[Client] Connection shutdown by transport: 0x" << hex << Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status << endl;
        Running = false;
        break;
        
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        cout << "\n[Client] Connection shutdown by peer" << endl;
        Running = false;
        break;
        
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        Running = false;
        break;
        
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

bool InitializeClient() {
    QUIC_STATUS status;
    
    status = MsQuicOpen2(&MsQuic);
    if (QUIC_FAILED(status)) {
        cerr << "MsQuicOpen2 failed: 0x" << hex << status << endl;
        return false;
    }
    
    const QUIC_REGISTRATION_CONFIG regConfig = { "speedtest-client", QUIC_EXECUTION_PROFILE_LOW_LATENCY };
    status = MsQuic->RegistrationOpen(&regConfig, &Registration);
    if (QUIC_FAILED(status)) {
        cerr << "RegistrationOpen failed: 0x" << hex << status << endl;
        return false;
    }
    
    const QUIC_BUFFER alpn = { (uint32_t)strlen(ALPN), (uint8_t*)ALPN };
    
    QUIC_SETTINGS settings = {};
    settings.IdleTimeoutMs = 60000;
    settings.IsSet.IdleTimeoutMs = TRUE;
    
    status = MsQuic->ConfigurationOpen(Registration, &alpn, 1, &settings, sizeof(settings), nullptr, &Configuration);
    if (QUIC_FAILED(status)) {
        cerr << "ConfigurationOpen failed: 0x" << hex << status << endl;
        return false;
    }
    
    // 客户端凭证 (不验证服务器证书)
    QUIC_CREDENTIAL_CONFIG credConfig = {};
    credConfig.Type = QUIC_CREDENTIAL_TYPE_NONE;
    credConfig.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
    
    status = MsQuic->ConfigurationLoadCredential(Configuration, &credConfig);
    if (QUIC_FAILED(status)) {
        cerr << "ConfigurationLoadCredential failed: 0x" << hex << status << endl;
        return false;
    }
    
    return true;
}

bool ConnectToServer() {
    QUIC_STATUS status;
    
    status = MsQuic->ConnectionOpen(Registration, ConnectionCallback, nullptr, &Connection);
    if (QUIC_FAILED(status)) {
        cerr << "ConnectionOpen failed: 0x" << hex << status << endl;
        return false;
    }
    
    cout << "[Client] Connecting to " << ServerAddress << ":" << ServerPort << "..." << endl;
    
    status = MsQuic->ConnectionStart(Connection, Configuration, QUIC_ADDRESS_FAMILY_UNSPEC, 
                                     ServerAddress.c_str(), ServerPort);
    if (QUIC_FAILED(status)) {
        cerr << "ConnectionStart failed: 0x" << hex << status << endl;
        return false;
    }
    
    // 等待连接
    int timeout = 100; // 10秒超时
    while (!Connected && Running && timeout-- > 0) {
        this_thread::sleep_for(milliseconds(100));
    }
    
    return Connected;
}

void SendData() {
    QUIC_STATUS status;
    
    // 创建流
    status = MsQuic->StreamOpen(Connection, QUIC_STREAM_OPEN_FLAG_NONE, StreamCallback, nullptr, &Stream);
    if (QUIC_FAILED(status)) {
        cerr << "StreamOpen failed: 0x" << hex << status << endl;
        return;
    }
    
    status = MsQuic->StreamStart(Stream, QUIC_STREAM_START_FLAG_NONE);
    if (QUIC_FAILED(status)) {
        cerr << "StreamStart failed: 0x" << hex << status << endl;
        return;
    }
    
    cout << "[Client] Starting data transfer: " << DataSizeGB << " GB" << endl;
    StartTime = steady_clock::now();
    
    uint64_t totalToSend = DataSizeGB * 1024ULL * 1024ULL * 1024ULL;
    uint64_t sent = 0;
    
    // 创建发送缓冲区
    vector<uint8_t> dataPattern(BufferSize, 'X');
    
    while (sent < totalToSend && Running) {
        uint32_t chunkSize = min((uint64_t)BufferSize, totalToSend - sent);
        
        // 分配缓冲区
        QUIC_BUFFER* buffer = (QUIC_BUFFER*)malloc(sizeof(QUIC_BUFFER) + chunkSize);
        buffer->Length = chunkSize;
        buffer->Buffer = (uint8_t*)(buffer + 1);
        memcpy(buffer->Buffer, dataPattern.data(), chunkSize);
        
        QUIC_SEND_FLAGS flags = (sent + chunkSize >= totalToSend) ? 
                                QUIC_SEND_FLAG_FIN : QUIC_SEND_FLAG_NONE;
        
        status = MsQuic->StreamSend(Stream, buffer, 1, flags, buffer);
        if (QUIC_FAILED(status)) {
            cerr << "\nStreamSend failed: 0x" << hex << status << endl;
            free(buffer);
            break;
        }
        
        sent += chunkSize;
        TotalBytesSent = sent;
        
        // 简单的流控：避免发送过快
        if (sent % (10 * 1024 * 1024) == 0) {
            this_thread::sleep_for(microseconds(100));
        }
    }
    
    // 等待发送完成
    while (!SendComplete && Running) {
        this_thread::sleep_for(milliseconds(100));
    }
    
    auto endTime = steady_clock::now();
    auto elapsed = duration_cast<milliseconds>(endTime - StartTime).count();
    
    cout << "\n\n=== Transfer Complete ===" << endl;
    cout << "Total sent: " << (TotalBytesSent / (1024.0 * 1024.0 * 1024.0)) << " GB" << endl;
    cout << "Time: " << (elapsed / 1000.0) << " seconds" << endl;
    cout << "Average speed: " << ((TotalBytesSent * 8.0) / (elapsed * 1000.0)) << " Mbps" << endl;
}

void Cleanup() {
    Running = false;
    
    if (Stream) {
        MsQuic->StreamClose(Stream);
    }
    if (Connection) {
        MsQuic->ConnectionClose(Connection);
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
    cout << "  -s <address>  Server address (default: 127.0.0.1)" << endl;
    cout << "  -p <port>     Server port (default: 9331)" << endl;
    cout << "  -g <size>     Data size in GB (default: 1)" << endl;
    cout << "  -b <size>     Buffer size in KB (default: 64)" << endl;
    cout << "  -h            Show this help" << endl;
}

int main(int argc, char* argv[]) {
    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            ServerAddress = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            ServerPort = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            DataSizeGB = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            BufferSize = atoi(argv[++i]) * 1024;
        } else if (strcmp(argv[i], "-h") == 0) {
            PrintUsage(argv[0]);
            return 0;
        }
    }
    
    cout << "=== MsQuic Speed Test Client ===" << endl;
    cout << "Server: " << ServerAddress << ":" << ServerPort << endl;
    cout << "Data size: " << DataSizeGB << " GB" << endl;
    cout << "Buffer size: " << (BufferSize / 1024) << " KB" << endl;
    
    if (!InitializeClient()) {
        cerr << "Failed to initialize client" << endl;
        return 1;
    }
    
    if (!ConnectToServer()) {
        cerr << "Failed to connect to server" << endl;
        Cleanup();
        return 1;
    }
    
    // 启动统计线程
    thread statsThread(PrintStats);
    
    // 发送数据
    SendData();
    
    statsThread.join();
    Cleanup();
    
    return 0;
}
