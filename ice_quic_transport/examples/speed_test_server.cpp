/**
 * @file speed_test_server.cpp
 * @brief ICE+QUIC Speed Test Server
 * 
 * This example demonstrates high-throughput data transfer testing using IceQuicTransport.
 * 
 * The server:
 * 1. Initializes IceQuicTransport in server mode
 * 2. Gathers ICE candidates
 * 3. Displays local IceDescription for exchange
 * 4. Accepts remote IceDescription from stdin
 * 5. Receives data and displays real-time speed statistics
 * 6. Shows average speed after transfer completion
 * 
 * Usage:
 *   ./speed_test_server <cert_path> <key_path>
 * 
 * Example:
 *   ./speed_test_server server.crt server.key
 * 
 * To generate self-signed certificates for testing:
 *   openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt \
 *       -days 365 -nodes -subj "/CN=localhost"
 * 
 * @see speed_test_client.cpp for the client counterpart
 * @see echo_server.cpp for basic echo example
 */

#include "ice_quic_transport.hpp"

#include <iostream>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <csignal>
#include <iomanip>

using namespace ice_quic;

static std::atomic<bool> g_running{true};

// 速度统计
struct SpeedStats {
    std::atomic<uint64_t> totalBytes{0};
    std::atomic<uint64_t> lastBytes{0};
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastTime;
    std::mutex mtx;
    
    void reset() {
        totalBytes = 0;
        lastBytes = 0;
        startTime = std::chrono::steady_clock::now();
        lastTime = startTime;
    }
    
    void addBytes(uint64_t bytes) {
        totalBytes += bytes;
    }
    
    void printSpeed() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count();
        
        if (elapsed >= 1000) {  // 每秒打印一次
            uint64_t currentTotal = totalBytes.load();
            uint64_t lastTotal = lastBytes.load();
            uint64_t bytesInPeriod = currentTotal - lastTotal;
            
            double speedMbps = (bytesInPeriod * 8.0 / 1000000.0) / (elapsed / 1000.0);
            double totalMB = currentTotal / (1024.0 * 1024.0);
            
            std::cout << "[SPEED] " << std::fixed << std::setprecision(2) 
                      << speedMbps << " Mbps | Received: " 
                      << totalMB << " MB" << std::endl;
            
            lastBytes = currentTotal;
            lastTime = now;
        }
    }
    
    void printFinalStats() {
        auto endTime = std::chrono::steady_clock::now();
        auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        
        uint64_t total = totalBytes.load();
        double totalMB = total / (1024.0 * 1024.0);
        double totalGB = total / (1024.0 * 1024.0 * 1024.0);
        double avgSpeedMbps = (total * 8.0 / 1000000.0) / (totalTime / 1000.0);
        
        std::cout << std::endl;
        std::cout << "========== Transfer Complete ==========" << std::endl;
        std::cout << "Total Received: " << std::fixed << std::setprecision(2) << totalMB << " MB";
        if (totalGB >= 1.0) {
            std::cout << " (" << totalGB << " GB)";
        }
        std::cout << std::endl;
        std::cout << "Transfer Time: " << (totalTime / 1000.0) << " seconds" << std::endl;
        std::cout << "Average Speed: " << avgSpeedMbps << " Mbps" << std::endl;
        std::cout << "=======================================" << std::endl;
    }
};

SpeedStats g_stats;

void signalHandler(int signum) {
    std::cout << "\nReceived signal " << signum << ", shutting down..." << std::endl;
    g_running = false;
}

void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " <cert_path> <key_path>" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Arguments:" << std::endl;
    std::cerr << "  cert_path  Path to TLS certificate file (PEM format)" << std::endl;
    std::cerr << "  key_path   Path to TLS private key file (PEM format)" << std::endl;
    std::cerr << std::endl;
    std::cerr << "To generate self-signed certificates for testing:" << std::endl;
    std::cerr << "  openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt \\" << std::endl;
    std::cerr << "      -days 365 -nodes -subj \"/CN=localhost\"" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }
    
    const std::string certPath = argv[1];
    const std::string keyPath = argv[2];
    
    std::cout << "=== ICE+QUIC Speed Test Server ===" << std::endl;
    std::cout << std::endl;
    
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> connected{false};
    std::atomic<bool> gatheringComplete{false};
    std::atomic<bool> transferComplete{false};
    
    try {
        IceQuicConfig config;
        config.stunServers = {"stun.l.google.com:19302"};
        config.isServer = true;
        config.certPath = certPath;
        config.keyPath = keyPath;
        config.idleTimeoutMs = 300000;  // 5 minute idle timeout
        config.maxStreams = 100;
        
        std::cout << "Configuration:" << std::endl;
        std::cout << "  STUN Servers: ";
        for (const auto& server : config.stunServers) {
            std::cout << server << " ";
        }
        std::cout << std::endl;
        std::cout << "  Mode: Server" << std::endl;
        std::cout << "  Certificate: " << certPath << std::endl;
        std::cout << "  Private Key: " << keyPath << std::endl;
        std::cout << std::endl;
        
        IceQuicCallbacks callbacks;
        
        callbacks.onLocalCandidate = [](const std::string& candidateSdp) {
            std::cout << "[ICE] Local candidate: " << candidateSdp << std::endl;
        };
        
        callbacks.onGatheringComplete = [&]() {
            std::cout << std::endl;
            std::cout << "[ICE] Candidate gathering complete!" << std::endl;
            std::cout << std::endl;
            gatheringComplete = true;
            cv.notify_all();
        };
        
        callbacks.onConnected = [&]() {
            std::cout << std::endl;
            std::cout << "[QUIC] Connection established!" << std::endl;
            std::cout << "Server is ready to receive data." << std::endl;
            std::cout << std::endl;
            connected = true;
            g_stats.reset();
            cv.notify_all();
        };
        
        callbacks.onDisconnected = [&]() {
            std::cout << "[QUIC] Disconnected" << std::endl;
            connected = false;
            if (!transferComplete) {
                g_stats.printFinalStats();
                transferComplete = true;
            }
            cv.notify_all();
        };
        
        callbacks.onFailed = [&](const std::string& error) {
            std::cerr << "[ERROR] Connection failed: " << error << std::endl;
            g_running = false;
            cv.notify_all();
        };
        
        callbacks.onStreamOpened = [](uint64_t streamId) {
            std::cout << "[STREAM] Peer opened stream " << streamId << std::endl;
        };
        
        callbacks.onStreamClosed = [&](uint64_t streamId) {
            std::cout << "[STREAM] Stream " << streamId << " closed" << std::endl;
            if (!transferComplete) {
                g_stats.printFinalStats();
                transferComplete = true;
            }
        };
        
        callbacks.onData = [&](uint64_t streamId, const uint8_t* data, size_t len) {
            g_stats.addBytes(len);
            g_stats.printSpeed();
        };
        
        std::cout << "Initializing ICE+QUIC transport..." << std::endl;
        IceQuicTransport transport(config, callbacks);
        
        std::cout << "Transport initialized successfully." << std::endl;
        std::cout << std::endl;
        
        std::cout << "Starting ICE candidate gathering..." << std::endl;
        transport.gatherCandidates();
        
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [&]() { return gatheringComplete.load() || !g_running; });
        }
        
        if (!g_running) return 0;
        
        IceDescription localDesc = transport.getLocalDescription();
        
        std::cout << "=== LOCAL ICE DESCRIPTION ===" << std::endl;
        std::cout << "ufrag: " << localDesc.ufrag << std::endl;
        std::cout << "pwd: " << localDesc.pwd << std::endl;
        std::cout << "candidates:" << std::endl;
        for (const auto& candidate : localDesc.candidates) {
            std::cout << "  " << candidate << std::endl;
        }
        std::cout << "=============================" << std::endl;
        std::cout << std::endl;
        
        std::cout << "Enter remote ICE description:" << std::endl;
        IceDescription remoteDesc;
        
        std::cout << "Remote ufrag: ";
        if (!std::getline(std::cin, remoteDesc.ufrag) || remoteDesc.ufrag.empty()) {
            std::cerr << "Error: Remote ufrag is required" << std::endl;
            return 1;
        }
        
        std::cout << "Remote pwd: ";
        if (!std::getline(std::cin, remoteDesc.pwd) || remoteDesc.pwd.empty()) {
            std::cerr << "Error: Remote pwd is required" << std::endl;
            return 1;
        }
        
        std::cout << "Enter remote ICE candidates (one per line, empty line to finish):" << std::endl;
        std::string candidateLine;
        while (std::getline(std::cin, candidateLine)) {
            if (candidateLine.empty()) break;
            
            // Remove "a=" prefix if present
            if (candidateLine.find("a=") == 0) {
                candidateLine = candidateLine.substr(2);
            }
            
            // Skip empty lines and comments
            if (!candidateLine.empty() && candidateLine[0] != '#') {
                remoteDesc.candidates.push_back(candidateLine);
                std::cout << "  Added candidate #" << remoteDesc.candidates.size() << std::endl;
            }
        }
        
        std::cout << std::endl;
        std::cout << "Collected " << remoteDesc.candidates.size() << " remote candidate(s)." << std::endl;
        
        transport.setRemoteDescription(remoteDesc);
        std::cout << "Remote description set." << std::endl;
        
        // Signal that all remote candidates have been received
        transport.endOfRemoteCandidates();
        std::cout << "End of remote candidates signaled." << std::endl;
        std::cout << "Waiting for connection..." << std::endl;
        
        {
            std::unique_lock<std::mutex> lock(mtx);
            if (!cv.wait_for(lock, std::chrono::seconds(60), [&]() { 
                return connected.load() || !g_running; 
            })) {
                std::cerr << "Connection timeout!" << std::endl;
                return 1;
            }
        }
        
        if (!g_running) return 0;
        
        std::cout << std::endl;
        std::cout << "Server running. Press Ctrl+C to stop." << std::endl;
        std::cout << std::endl;
        
        while (g_running && connected) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (!transferComplete) {
            g_stats.printFinalStats();
        }
        
        std::cout << "Closing connection..." << std::endl;
        transport.close();
        
        std::cout << "Server stopped." << std::endl;
        
    } catch (const IceQuicException& e) {
        std::cerr << "IceQuicException: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
