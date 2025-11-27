/**
 * @file echo_server.cpp
 * @brief ICE+QUIC Echo Server Example
 * 
 * This example demonstrates how to use IceQuicTransport in server mode
 * with the simplified API using getLocalDescription/setRemoteDescription.
 * 
 * The server:
 * 1. Initializes IceQuicTransport in server mode
 * 2. Gathers ICE candidates
 * 3. Displays local IceDescription (ufrag, pwd, candidates) for exchange
 * 4. Accepts remote IceDescription from stdin
 * 5. Echoes received data back to the sender
 * 
 * Usage:
 *   ./echo_server <cert_path> <key_path>
 * 
 * Example:
 *   ./echo_server server.crt server.key
 * 
 * To generate self-signed certificates for testing:
 *   openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt \
 *       -days 365 -nodes -subj "/CN=localhost"
 * 
 * @see echo_client.cpp for the client counterpart
 * @see loopback_test.cpp for automated testing
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
#include <vector>

using namespace ice_quic;

// Global flag for graceful shutdown
static std::atomic<bool> g_running{true};

// Signal handler for graceful shutdown
void signalHandler(int signum) {
    std::cout << "\nReceived signal " << signum << ", shutting down..." << std::endl;
    g_running = false;
}

// Helper to print usage
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
    // Check command line arguments
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }
    
    const std::string certPath = argv[1];
    const std::string keyPath = argv[2];
    
    std::cout << "=== ICE+QUIC Echo Server ===" << std::endl;
    std::cout << std::endl;
    
    // Set up signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // Synchronization for connection state
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> connected{false};
    std::atomic<bool> gatheringComplete{false};
    
    try {
        // Configure the transport in server mode using simplified API
        IceQuicConfig config;
        config.stunServers = {"stun.l.google.com:19302"};  // Vector of "host:port"
        config.isServer = true;
        config.certPath = certPath;
        config.keyPath = keyPath;
        config.idleTimeoutMs = 60000;  // 60 second idle timeout
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
        
        // Set up callbacks
        IceQuicCallbacks callbacks;
        
        // ICE candidate callback - print each candidate as it's discovered
        callbacks.onLocalCandidate = [](const std::string& candidateSdp) {
            std::cout << "[ICE] Local candidate: " << candidateSdp << std::endl;
        };
        
        // Gathering complete callback
        callbacks.onGatheringComplete = [&]() {
            std::cout << std::endl;
            std::cout << "[ICE] Candidate gathering complete!" << std::endl;
            std::cout << std::endl;
            
            gatheringComplete = true;
            cv.notify_all();
        };
        
        // Connection established callback
        callbacks.onConnected = [&]() {
            std::cout << std::endl;
            std::cout << "[QUIC] Connection established!" << std::endl;
            std::cout << "Server is ready to echo data." << std::endl;
            std::cout << std::endl;
            
            connected = true;
            cv.notify_all();
        };
        
        // Disconnection callback
        callbacks.onDisconnected = [&]() {
            std::cout << "[QUIC] Disconnected" << std::endl;
            connected = false;
            cv.notify_all();
        };
        
        // Connection failed callback
        callbacks.onFailed = [&](const std::string& error) {
            std::cerr << "[ERROR] Connection failed: " << error << std::endl;
            g_running = false;
            cv.notify_all();
        };
        
        // Stream opened callback
        callbacks.onStreamOpened = [](uint64_t streamId) {
            std::cout << "[STREAM] Peer opened stream " << streamId << std::endl;
        };
        
        // Stream closed callback
        callbacks.onStreamClosed = [](uint64_t streamId) {
            std::cout << "[STREAM] Stream " << streamId << " closed" << std::endl;
        };
        
        // Data received callback - echo back to sender
        IceQuicTransport* transportPtr = nullptr;
        callbacks.onData = [&](uint64_t streamId, const uint8_t* data, size_t len) {
            std::string received(reinterpret_cast<const char*>(data), len);
            std::cout << "[DATA] Received on stream " << streamId << ": " << received << std::endl;
            
            // Echo the data back
            if (transportPtr && transportPtr->getState() == TransportState::Connected) {
                if (transportPtr->send(streamId, data, len)) {
                    std::cout << "[DATA] Echoed " << len << " bytes back on stream " << streamId << std::endl;
                } else {
                    std::cerr << "[ERROR] Failed to echo data" << std::endl;
                }
            }
        };
        
        // Create the transport
        std::cout << "Initializing ICE+QUIC transport..." << std::endl;
        IceQuicTransport transport(config, callbacks);
        transportPtr = &transport;
        
        std::cout << "Transport initialized successfully." << std::endl;
        std::cout << std::endl;
        
        // Start candidate gathering
        std::cout << "Starting ICE candidate gathering..." << std::endl;
        transport.gatherCandidates();
        
        // Wait for gathering to complete
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [&]() { return gatheringComplete.load() || !g_running; });
        }
        
        if (!g_running) {
            return 0;
        }
        
        // Get and display local description using simplified API
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
        
        // Prompt for remote description
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
        
        // Prompt for remote candidates
        std::cout << "Enter remote ICE candidates (one per line, empty line to finish):" << std::endl;
        std::string candidateLine;
        
        while (std::getline(std::cin, candidateLine)) {
            if (candidateLine.empty()) {
                break;
            }
            remoteDesc.candidates.push_back(candidateLine);
            std::cout << "  Added candidate #" << remoteDesc.candidates.size() << std::endl;
        }
        
        std::cout << std::endl;
        std::cout << "Collected " << remoteDesc.candidates.size() << " remote candidate(s)." << std::endl;
        
        // Set remote description using simplified API
        transport.setRemoteDescription(remoteDesc);
        std::cout << "Remote description set." << std::endl;
        std::cout << "Waiting for connection..." << std::endl;
        
        // Wait for connection or timeout
        {
            std::unique_lock<std::mutex> lock(mtx);
            if (!cv.wait_for(lock, std::chrono::seconds(30), [&]() { 
                return connected.load() || !g_running; 
            })) {
                std::cerr << "Connection timeout!" << std::endl;
                return 1;
            }
        }
        
        if (!g_running) {
            return 0;
        }
        
        // Main loop - keep running until shutdown
        std::cout << std::endl;
        std::cout << "Server running. Press Ctrl+C to stop." << std::endl;
        std::cout << std::endl;
        
        while (g_running && connected) {
            // Display statistics periodically
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            if (connected && transport.getState() == TransportState::Connected) {
                std::cout << "[STATS] RTT: " << transport.getRtt() << "ms"
                          << ", Sent: " << transport.getBytesSent() << " bytes"
                          << ", Received: " << transport.getBytesReceived() << " bytes" << std::endl;
            }
        }
        
        // Graceful shutdown
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
