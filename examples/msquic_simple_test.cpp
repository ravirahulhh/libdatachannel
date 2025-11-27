/**
 * MsQuic 简单测试程序
 * 验证 MsQuic 库是否正确安装和链接
 */

#include <iostream>
#include <cstring>
#include <msquic.h>

void PrintMsQuicVersion() {
    uint32_t version[4] = {0};
    uint32_t versionSize = sizeof(version);
    
    std::cout << "MsQuic Library Information:" << std::endl;
    std::cout << "  API Version: " << QUIC_API_VERSION_2 << std::endl;
    
    // 尝试获取库版本
    const QUIC_API_TABLE* MsQuic = nullptr;
    QUIC_STATUS status = MsQuicOpen2(&MsQuic);
    
    if (QUIC_SUCCEEDED(status)) {
        std::cout << "  Status: Successfully loaded" << std::endl;
        
        // 获取版本信息
        status = MsQuic->GetParam(nullptr, QUIC_PARAM_GLOBAL_LIBRARY_VERSION,
                                  &versionSize, version);
        if (QUIC_SUCCEEDED(status)) {
            std::cout << "  Version: " << version[0] << "." << version[1] 
                      << "." << version[2] << "." << version[3] << std::endl;
        }
        
        // 获取支持的特性
        QUIC_SETTINGS settings = {};
        uint32_t settingsSize = sizeof(settings);
        status = MsQuic->GetParam(nullptr, QUIC_PARAM_GLOBAL_SETTINGS,
                                  &settingsSize, &settings);
        if (QUIC_SUCCEEDED(status)) {
            std::cout << "\nSupported Features:" << std::endl;
            std::cout << "  Max Worker Queue Delay: " 
                      << settings.MaxWorkerQueueDelayUs << " us" << std::endl;
        }
        
        MsQuicClose(MsQuic);
    } else {
        std::cerr << "  Status: Failed to load (0x" << std::hex << status << ")" << std::endl;
        std::cerr << "\nPossible reasons:" << std::endl;
        std::cerr << "  1. MsQuic library not installed" << std::endl;
        std::cerr << "  2. Library path not in LD_LIBRARY_PATH (Linux)" << std::endl;
        std::cerr << "  3. Library not in system PATH (Windows)" << std::endl;
        return;
    }
}

// 简单的连接测试
QUIC_STATUS QUIC_API ConnectionCallback(HQUIC Connection, void* Context, 
                                        QUIC_CONNECTION_EVENT* Event) {
    switch (Event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        std::cout << "Connection established!" << std::endl;
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        std::cout << "Connection shutdown by transport" << std::endl;
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        std::cout << "Connection shutdown by peer" << std::endl;
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

void TestBasicFunctionality() {
    std::cout << "\n=== Testing Basic MsQuic Functionality ===" << std::endl;
    
    const QUIC_API_TABLE* MsQuic = nullptr;
    QUIC_STATUS status = MsQuicOpen2(&MsQuic);
    
    if (QUIC_FAILED(status)) {
        std::cerr << "Failed to open MsQuic" << std::endl;
        return;
    }
    
    // 创建 Registration
    HQUIC registration = nullptr;
    const QUIC_REGISTRATION_CONFIG regConfig = {
        "test-app",
        QUIC_EXECUTION_PROFILE_LOW_LATENCY
    };
    
    status = MsQuic->RegistrationOpen(&regConfig, &registration);
    if (QUIC_SUCCEEDED(status)) {
        std::cout << "✓ Registration created successfully" << std::endl;
        
        // 创建 Configuration
        HQUIC configuration = nullptr;
        const QUIC_BUFFER alpn = { sizeof("test") - 1, (uint8_t*)"test" };
        
        QUIC_SETTINGS settings = {};
        settings.IdleTimeoutMs = 30000;
        settings.IsSet.IdleTimeoutMs = TRUE;
        settings.PeerBidiStreamCount = 10;
        settings.IsSet.PeerBidiStreamCount = TRUE;
        
        status = MsQuic->ConfigurationOpen(registration, &alpn, 1, &settings,
                                           sizeof(settings), nullptr, &configuration);
        if (QUIC_SUCCEEDED(status)) {
            std::cout << "✓ Configuration created successfully" << std::endl;
            
            // 创建 Connection
            HQUIC connection = nullptr;
            status = MsQuic->ConnectionOpen(registration, ConnectionCallback,
                                           nullptr, &connection);
            if (QUIC_SUCCEEDED(status)) {
                std::cout << "✓ Connection created successfully" << std::endl;
                
                // 清理
                MsQuic->ConnectionClose(connection);
            } else {
                std::cerr << "✗ Failed to create connection: 0x" 
                          << std::hex << status << std::endl;
            }
            
            MsQuic->ConfigurationClose(configuration);
        } else {
            std::cerr << "✗ Failed to create configuration: 0x" 
                      << std::hex << status << std::endl;
        }
        
        MsQuic->RegistrationClose(registration);
    } else {
        std::cerr << "✗ Failed to create registration: 0x" 
                  << std::hex << status << std::endl;
    }
    
    MsQuicClose(MsQuic);
    std::cout << "\n=== Test Complete ===" << std::endl;
}

void PrintUsageInstructions() {
    std::cout << "\n=== Installation Instructions ===" << std::endl;
    std::cout << "\nLinux (Ubuntu/Debian):" << std::endl;
    std::cout << "  wget https://packages.microsoft.com/config/ubuntu/$(lsb_release -rs)/packages-microsoft-prod.deb" << std::endl;
    std::cout << "  sudo dpkg -i packages-microsoft-prod.deb" << std::endl;
    std::cout << "  sudo apt-get update" << std::endl;
    std::cout << "  sudo apt-get install -y libmsquic" << std::endl;
    
    std::cout << "\nmacOS:" << std::endl;
    std::cout << "  brew tap microsoft/msquic" << std::endl;
    std::cout << "  brew install msquic" << std::endl;
    
    std::cout << "\nWindows:" << std::endl;
    std::cout << "  vcpkg install msquic" << std::endl;
    
    std::cout << "\nFrom source:" << std::endl;
    std::cout << "  git clone --recursive https://github.com/microsoft/msquic.git" << std::endl;
    std::cout << "  cd msquic && mkdir build && cd build" << std::endl;
    std::cout << "  cmake -G \"Unix Makefiles\" .." << std::endl;
    std::cout << "  cmake --build . --config Release" << std::endl;
    std::cout << "  sudo cmake --install ." << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "=== MsQuic Integration Test ===" << std::endl;
    std::cout << "This program tests if MsQuic is properly installed\n" << std::endl;
    
    // 打印版本信息
    PrintMsQuicVersion();
    
    // 测试基本功能
    TestBasicFunctionality();
    
    // 打印安装说明
    if (argc > 1 && std::string(argv[1]) == "--help") {
        PrintUsageInstructions();
    }
    
    std::cout << "\nFor installation instructions, run with --help" << std::endl;
    
    return 0;
}
