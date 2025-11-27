/**
 * @file echo_client.cpp
 * @brief ICE+QUIC Echo Client Example
 * 
 * This example demonstrates how to use IceQuicTransport in client mode
 * with the simplified API using getLocalDescription/setRemoteDescription.
 * 
 * The client:
 * 1. Initializes IceQuicTransport in client mode
 * 2. Gathers ICE candidates
 * 3. Displays local IceDescription (ufrag, pwd, candidates) for exchange
 * 4. Accepts remote IceDescription from stdin
 * 5. Sends user input and prints received echo responses
 * 
 * Usage:
 *   ./echo_client
 * 
 * @see echo_server.cpp for the server counterpart
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

int main(int argc, char* argv[]) {
    std::cout << "=== ICE+QUIC Echo Client ===" << std::endl;
    std::cout << std::endl;
    
    // Set up signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // Synchronization for connection state
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> connected{false};
    std::atomic<bool> gatheringComplete{false};
    
    // Active stream ID
    std::atomic<uint64_t> activeStreamId{0};
    
    try {
        // Configure the transport in client mode using simplified API
        IceQuicConfig config;
        config.stunServers = {"stun.l.google.com:19302"};  // Vector of "host:port"
        config.isServer = false;  // Client mode
        config.idleTimeoutMs = 60000;  // 60 second idle timeout
        config.maxStreams = 100;
        
        std::cout << "Configuration:" << std::endl;
        std::cout << "  STUN Servers: ";
        for (const auto& server : config.stunServers) {
            std::cout << server << " ";
        }
        std::cout << std::endl;
        std::cout << "  Mode: Client" << std::endl;
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
        callbacks.onStreamClosed = [&](uint64_t streamId) {
            std::cout << "[STREAM] Stream " << streamId << " closed" << std::endl;
            if (activeStreamId == streamId) {
                activeStreamId = 0;
            }
        };
        
        // Data received callback - print echo response
        callbacks.onData = [](uint64_t streamId, const uint8_t* data, size_t len) {
            std::string received(reinterpret_cast<const char*>(data), len);
            std::cout << "[ECHO] Received on stream " << streamId << ": " << received << std::endl;
        };
        
        // Create the transport
        std::cout << "Initializing ICE+QUIC transport..." << std::endl;
        IceQuicTransport transport(config, callbacks);
        
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
        
        // Open a stream for sending data
        std::cout << "Opening stream..." << std::endl;
        activeStreamId = transport.openStream();
        std::cout << "Stream " << activeStreamId << " opened." << std::endl;
        std::cout << std::endl;
        
        // Interactive loop - send user input
        std::cout << "=== Interactive Mode ===" << std::endl;
        std::cout << "Type messages to send to the server." << std::endl;
        std::cout << "Commands:" << std::endl;
        std::cout << "  /stats  - Show connection statistics" << std::endl;
        std::cout << "  /quit   - Close connection and exit" << std::endl;
        std::cout << "=========================" << std::endl;
        std::cout << std::endl;
        
        std::string input;
        while (g_running && connected) {
            std::cout << "> ";
            if (!std::getline(std::cin, input)) {
                break;
            }
            
            if (input.empty()) {
                continue;
            }
            
            // Handle commands
            if (input == "/quit") {
                std::cout << "Closing connection..." << std::endl;
                break;
            }
            
            if (input == "/stats") {
                std::cout << "Connection Statistics:" << std::endl;
                std::cout << "  State: " << transportStateToString(transport.getState()) << std::endl;
                std::cout << "  RTT: " << transport.getRtt() << " ms" << std::endl;
                std::cout << "  Bytes Sent: " << transport.getBytesSent() << std::endl;
                std::cout << "  Bytes Received: " << transport.getBytesReceived() << std::endl;
                continue;
            }
            
            // Send the message
            if (activeStreamId == 0) {
                std::cerr << "No active stream. Opening new stream..." << std::endl;
                activeStreamId = transport.openStream();
            }
            
            const uint8_t* data = reinterpret_cast<const uint8_t*>(input.c_str());
            size_t len = input.length();
            
            if (transport.send(activeStreamId, data, len)) {
                std::cout << "[SENT] " << len << " bytes on stream " << activeStreamId << std::endl;
            } else {
                std::cerr << "[ERROR] Failed to send message" << std::endl;
            }
        }
        
        // Graceful shutdown
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
