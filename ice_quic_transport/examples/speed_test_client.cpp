/**
 * @file speed_test_client.cpp
 * @brief ICE+QUIC Speed Test Client
 * 
 * This example demonstrates high-throughput data transfer testing using IceQuicTransport.
 * 
 * The client:
 * 1. Initializes IceQuicTransport in client mode
 * 2. Gathers ICE candidates
 * 3. Displays local IceDescription for exchange
 * 4. Accepts remote IceDescription from stdin
 * 5. Sends specified amount of data to the server
 * 6. Displays real-time send and ACK speed statistics
 * 7. Shows average speed after transfer completion
 * 
 * Usage:
 *   ./speed_test_client [data_size_gb]
 * 
 * Examples:
 *   ./speed_test_client        # Send 1 GB (default)
 *   ./speed_test_client 5      # Send 5 GB
 *   ./speed_test_client 10     # Send 10 GB
 * 
 * @see speed_test_server.cpp for the server counterpart
 * @see echo_client.cpp for basic echo example
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
#include <vector>
#include <cstring>

using namespace ice_quic;

static std::atomic<bool> g_running{true};

// 速度统计
struct SpeedStats {
    std::atomic<uint64_t> totalBytesSent{0};
    std::atomic<uint64_t> totalBytesAcked{0};
    std::atomic<uint64_t> lastBytesSent{0};
    std::atomic<uint64_t> lastBytesAcked{0};
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastTime;
    std::mutex mtx;
    
    void reset() {
        totalBytesSent = 0;
        totalBytesAcked = 0;
        lastBytesSent = 0;
        lastBytesAcked = 0;
        startTime = std::chrono::steady_clock::now();
        lastTime = startTime;
    }
    
    void addBytesSent(uint64_t bytes) {
        totalBytesSent += bytes;
    }
    
    void updateBytesAcked(uint64_t bytes) {
        totalBytesAcked = bytes;
    }
    
    void printSpeed() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count();
        
        if (elapsed >= 1000) {  // 每秒打印一次
            uint64_t currentSent = totalBytesSent.load();
            uint64_t currentAcked = totalBytesAcked.load();
            uint64_t lastSent = lastBytesSent.load();
            uint64_t lastAcked = lastBytesAcked.load();
            
            uint64_t sentInPeriod = currentSent - lastSent;
            uint64_t ackedInPeriod = currentAcked - lastAcked;
            
            double sendSpeedMbps = (sentInPeriod * 8.0 / 1000000.0) / (elapsed / 1000.0);
            double ackSpeedMbps = (ackedInPeriod * 8.0 / 1000000.0) / (elapsed / 1000.0);
            double totalSentMB = currentSent / (1024.0 * 1024.0);
            double totalAckedMB = currentAcked / (1024.0 * 1024.0);
            
            std::cout << "[SPEED] Send: " << std::fixed << std::setprecision(2) 
                      << sendSpeedMbps << " Mbps | ACK: " 
                      << ackSpeedMbps << " Mbps | Sent: " 
                      << totalSentMB << " MB | Acked: "
                      << totalAckedMB << " MB" << std::endl;
            
            lastBytesSent = currentSent;
            lastBytesAcked = currentAcked;
            lastTime = now;
        }
    }
    
    void printFinalStats() {
        auto endTime = std::chrono::steady_clock::now();
        auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        
        uint64_t totalSent = totalBytesSent.load();
        uint64_t totalAcked = totalBytesAcked.load();
        double totalSentMB = totalSent / (1024.0 * 1024.0);
        double totalSentGB = totalSent / (1024.0 * 1024.0 * 1024.0);
        double totalAckedMB = totalAcked / (1024.0 * 1024.0);
        double avgSendSpeedMbps = (totalSent * 8.0 / 1000000.0) / (totalTime / 1000.0);
        double avgAckSpeedMbps = (totalAcked * 8.0 / 1000000.0) / (totalTime / 1000.0);
        
        std::cout << std::endl;
        std::cout << "========== Transfer Complete ==========" << std::endl;
        std::cout << "Total Sent: " << std::fixed << std::setprecision(2) << totalSentMB << " MB";
        if (totalSentGB >= 1.0) {
            std::cout << " (" << totalSentGB << " GB)";
        }
        std::cout << std::endl;
        std::cout << "Total Acked: " << totalAckedMB << " MB" << std::endl;
        std::cout << "Transfer Time: " << (totalTime / 1000.0) << " seconds" << std::endl;
        std::cout << "Average Send Speed: " << avgSendSpeedMbps << " Mbps" << std::endl;
        std::cout << "Average ACK Speed: " << avgAckSpeedMbps << " Mbps" << std::endl;
        std::cout << "=======================================" << std::endl;
    }
};

SpeedStats g_stats;

void signalHandler(int signum) {
    std::cout << "\nReceived signal " << signum << ", shutting down..." << std::endl;
    g_running = false;
}

int main(int argc, char* argv[]) {
    std::cout << "=== ICE+QUIC Speed Test Client ===" << std::endl;
    std::cout << std::endl;
    
    // Parse data size parameter (GB)
    double dataSizeGB = 1.0;  // Default 1 GB
    if (argc > 1) {
        try {
            dataSizeGB = std::stod(argv[1]);
            if (dataSizeGB <= 0) {
                std::cerr << "Error: Data size must be greater than 0" << std::endl;
                return 1;
            }
        } catch (...) {
            std::cerr << "Error: Invalid data size parameter" << std::endl;
            return 1;
        }
    }
    
    uint64_t totalDataSize = static_cast<uint64_t>(dataSizeGB * 1024 * 1024 * 1024);
    std::cout << "Will send " << dataSizeGB << " GB (" << totalDataSize << " bytes) of data" << std::endl;
    std::cout << std::endl;
    
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> connected{false};
    std::atomic<bool> gatheringComplete{false};
    std::atomic<uint64_t> activeStreamId{0};
    
    try {
        IceQuicConfig config;
        config.stunServers = {"stun.l.google.com:19302"};
        config.isServer = false;
        config.idleTimeoutMs = 300000;  // 5 minute idle timeout
        config.maxStreams = 100;
        
        std::cout << "Configuration:" << std::endl;
        std::cout << "  STUN Servers: ";
        for (const auto& server : config.stunServers) {
            std::cout << server << " ";
        }
        std::cout << std::endl;
        std::cout << "  Mode: Client" << std::endl;
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
            std::cout << std::endl;
            connected = true;
            cv.notify_all();
        };
        
        callbacks.onDisconnected = [&]() {
            std::cout << "[QUIC] Disconnected" << std::endl;
            connected = false;
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
            if (activeStreamId == streamId) {
                activeStreamId = 0;
            }
        };
        
        callbacks.onData = [](uint64_t streamId, const uint8_t* data, size_t len) {
            // Client doesn't need to process received data
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
        
        // Open stream
        std::cout << "Opening stream..." << std::endl;
        activeStreamId = transport.openStream();
        std::cout << "Stream " << activeStreamId << " opened." << std::endl;
        std::cout << std::endl;
        
        // Start sending data
        std::cout << "Starting data transfer..." << std::endl;
        g_stats.reset();
        
        const size_t chunkSize = 64 * 1024;  // 64 KB per chunk
        std::vector<uint8_t> buffer(chunkSize);
        
        // Fill test data
        for (size_t i = 0; i < chunkSize; ++i) {
            buffer[i] = static_cast<uint8_t>(i % 256);
        }
        
        uint64_t bytesSent = 0;
        
        while (g_running && connected && bytesSent < totalDataSize) {
            size_t toSend = std::min(chunkSize, static_cast<size_t>(totalDataSize - bytesSent));
            
            if (transport.send(activeStreamId, buffer.data(), toSend)) {
                bytesSent += toSend;
                g_stats.addBytesSent(toSend);
                
                // Update ACK bytes (using transport statistics)
                g_stats.updateBytesAcked(transport.getBytesSent());
                
                g_stats.printSpeed();
            } else {
                std::cerr << "[ERROR] Failed to send data" << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        
        std::cout << std::endl;
        std::cout << "Data transfer complete, waiting for acknowledgments..." << std::endl;
        
        // Wait for all data to be acknowledged
        for (int i = 0; i < 30 && g_running; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            g_stats.updateBytesAcked(transport.getBytesSent());
        }
        
        g_stats.printFinalStats();
        
        // Close stream
        if (activeStreamId != 0) {
            transport.closeStream(activeStreamId);
        }
        transport.close();
        
        std::cout << "Client stopped." << std::endl;
        
    } catch (const IceQuicException& e) {
        std::cerr << "IceQuicException: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
