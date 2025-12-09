/**
 * @file loopback_test.cpp
 * @brief ICE+QUIC Loopback Test Example
 * 
 * This example demonstrates a complete ICE+QUIC connection in a single process
 * using the simplified API with SDP-based candidate exchange via
 * getLocalDescription/setRemoteDescription.
 * 
 * It creates both a client and server transport, exchanges IceDescription
 * structures programmatically, and demonstrates full connection and data exchange.
 * 
 * This is useful for:
 * - Testing the library without network setup
 * - Understanding the complete connection flow with simplified API
 * - Automated integration testing
 * - Demonstrating SDP-based candidate exchange
 * 
 * Usage:
 *   ./loopback_test <cert_path> <key_path>
 * 
 * Example:
 *   ./loopback_test server.crt server.key
 * 
 * To generate self-signed certificates for testing:
 *   openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt \
 *       -days 365 -nodes -subj "/CN=localhost"
 * 
 * @see echo_server.cpp for standalone server
 * @see echo_client.cpp for standalone client
 */

#include "ice_quic_transport.hpp"

#include <iostream>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>

using namespace ice_quic;

// Test configuration
static const int NUM_TEST_MESSAGES = 5;
static const std::chrono::seconds CONNECTION_TIMEOUT{30};
static const std::chrono::seconds MESSAGE_TIMEOUT{10};


/**
 * @brief Helper class to manage transport state and candidates
 * 
 * Wraps IceQuicTransport with state tracking and synchronization
 * for use in the loopback test.
 */
class TransportWrapper {
public:
    TransportWrapper(const std::string& name, bool isServer,
                     const std::string& certPath = "", const std::string& keyPath = "")
        : mName(name), mIsServer(isServer) {
        
        // Configure transport using simplified API
        mConfig.stunServers = {"stun.l.google.com:19302"};
        mConfig.isServer = isServer;
        mConfig.idleTimeoutMs = 30000;
        mConfig.maxStreams = 100;
        
        if (isServer) {
            mConfig.certPath = certPath;
            mConfig.keyPath = keyPath;
        }
        
        setupCallbacks();
    }
    
    void setupCallbacks() {
        mCallbacks.onLocalCandidate = [this](const std::string& candidateSdp) {
            log("Local candidate: " + candidateSdp);
        };
        
        mCallbacks.onGatheringComplete = [this]() {
            log("Gathering complete");
            mGatheringComplete = true;
            mCv.notify_all();
        };
        
        mCallbacks.onConnected = [this]() {
            log("Connected!");
            mConnected = true;
            mCv.notify_all();
        };
        
        mCallbacks.onDisconnected = [this]() {
            log("Disconnected");
            mConnected = false;
            mCv.notify_all();
        };
        
        mCallbacks.onFailed = [this](const std::string& error) {
            log("Failed: " + error);
            mFailed = true;
            mCv.notify_all();
        };
        
        mCallbacks.onStreamOpened = [this](uint64_t streamId) {
            log("Stream " + std::to_string(streamId) + " opened by peer");
        };
        
        mCallbacks.onStreamClosed = [this](uint64_t streamId) {
            log("Stream " + std::to_string(streamId) + " closed");
        };
        
        mCallbacks.onData = [this](uint64_t streamId, const uint8_t* data, size_t len) {
            std::string msg(reinterpret_cast<const char*>(data), len);
            log("Received on stream " + std::to_string(streamId) + ": " + msg);
            
            std::lock_guard<std::mutex> lock(mReceivedMtx);
            mReceivedMessages.push_back(msg);
            mCv.notify_all();
            
            // If server, echo back
            if (mIsServer && mTransport) {
                mTransport->send(streamId, data, len);
                log("Echoed back: " + msg);
            }
        };
    }
    
    void initialize() {
        log("Initializing...");
        mTransport = std::make_unique<IceQuicTransport>(mConfig, mCallbacks);
        log("Initialized");
    }
    
    void gatherCandidates() {
        log("Starting candidate gathering...");
        mTransport->gatherCandidates();
    }
    
    bool waitForGathering(std::chrono::seconds timeout) {
        std::unique_lock<std::mutex> lock(mMtx);
        return mCv.wait_for(lock, timeout, [this]() {
            return mGatheringComplete.load() || mFailed.load();
        });
    }
    
    bool waitForConnection(std::chrono::seconds timeout) {
        std::unique_lock<std::mutex> lock(mMtx);
        return mCv.wait_for(lock, timeout, [this]() {
            return mConnected.load() || mFailed.load();
        });
    }
    
    bool waitForMessages(size_t count, std::chrono::seconds timeout) {
        std::unique_lock<std::mutex> lock(mMtx);
        return mCv.wait_for(lock, timeout, [this, count]() {
            std::lock_guard<std::mutex> recvLock(mReceivedMtx);
            return mReceivedMessages.size() >= count || mFailed.load();
        });
    }
    
    IceDescription getLocalDescription() const {
        return mTransport->getLocalDescription();
    }
    
    void setRemoteDescription(const IceDescription& desc) {
        log("Setting remote description:");
        log("  ufrag: " + desc.ufrag);
        log("  pwd: " + desc.pwd);
        log("  candidates: " + std::to_string(desc.candidates.size()));
        mTransport->setRemoteDescription(desc);
        log("Remote description set");
    }
    
    void endOfRemoteCandidates() {
        mTransport->endOfRemoteCandidates();
        log("End of remote candidates signaled");
    }
    
    uint64_t openStream() {
        return mTransport->openStream();
    }
    
    bool send(uint64_t streamId, const std::string& message) {
        return mTransport->send(streamId, 
                               reinterpret_cast<const uint8_t*>(message.c_str()),
                               message.length());
    }
    
    void close() {
        if (mTransport) {
            mTransport->close();
        }
    }
    
    bool isConnected() const { return mConnected.load(); }
    bool isFailed() const { return mFailed.load(); }
    
    std::vector<std::string> getReceivedMessages() const {
        std::lock_guard<std::mutex> lock(mReceivedMtx);
        return mReceivedMessages;
    }
    
    TransportState getState() const {
        return mTransport ? mTransport->getState() : TransportState::Disconnected;
    }
    
    size_t getBytesSent() const {
        return mTransport ? mTransport->getBytesSent() : 0;
    }
    
    size_t getBytesReceived() const {
        return mTransport ? mTransport->getBytesReceived() : 0;
    }
    
private:
    void log(const std::string& message) {
        std::cout << "[" << mName << "] " << message << std::endl;
    }
    
    std::string mName;
    bool mIsServer;
    IceQuicConfig mConfig;
    IceQuicCallbacks mCallbacks;
    std::unique_ptr<IceQuicTransport> mTransport;
    
    std::vector<std::string> mReceivedMessages;
    mutable std::mutex mReceivedMtx;
    
    std::atomic<bool> mGatheringComplete{false};
    std::atomic<bool> mConnected{false};
    std::atomic<bool> mFailed{false};
    
    mutable std::mutex mMtx;
    std::condition_variable mCv;
};


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
    
    std::cout << "=== ICE+QUIC Loopback Test ===" << std::endl;
    std::cout << std::endl;
    std::cout << "This test demonstrates SDP-based candidate exchange using" << std::endl;
    std::cout << "the simplified getLocalDescription/setRemoteDescription API." << std::endl;
    std::cout << std::endl;
    
    try {
        // Create server and client wrappers
        std::cout << "--- Step 1: Initialize Transports ---" << std::endl;
        TransportWrapper server("SERVER", true, certPath, keyPath);
        TransportWrapper client("CLIENT", false);
        
        server.initialize();
        client.initialize();
        std::cout << std::endl;
        
        // Start candidate gathering on both
        std::cout << "--- Step 2: Gather ICE Candidates ---" << std::endl;
        server.gatherCandidates();
        client.gatherCandidates();
        
        // Wait for both to complete gathering
        if (!server.waitForGathering(CONNECTION_TIMEOUT)) {
            std::cerr << "Server gathering timeout!" << std::endl;
            return 1;
        }
        if (!client.waitForGathering(CONNECTION_TIMEOUT)) {
            std::cerr << "Client gathering timeout!" << std::endl;
            return 1;
        }
        std::cout << std::endl;
        
        // Exchange ICE descriptions using simplified API
        std::cout << "--- Step 3: Exchange ICE Descriptions (SDP-based) ---" << std::endl;
        
        // Get local descriptions from both peers
        IceDescription serverDesc = server.getLocalDescription();
        IceDescription clientDesc = client.getLocalDescription();
        
        std::cout << "Server IceDescription:" << std::endl;
        std::cout << "  ufrag: " << serverDesc.ufrag << std::endl;
        std::cout << "  pwd: " << serverDesc.pwd << std::endl;
        std::cout << "  candidates: " << serverDesc.candidates.size() << std::endl;
        for (const auto& c : serverDesc.candidates) {
            std::cout << "    " << c << std::endl;
        }
        
        std::cout << "Client IceDescription:" << std::endl;
        std::cout << "  ufrag: " << clientDesc.ufrag << std::endl;
        std::cout << "  pwd: " << clientDesc.pwd << std::endl;
        std::cout << "  candidates: " << clientDesc.candidates.size() << std::endl;
        for (const auto& c : clientDesc.candidates) {
            std::cout << "    " << c << std::endl;
        }
        std::cout << std::endl;
        
        // Set remote descriptions (exchange)
        std::cout << "--- Step 4: Set Remote Descriptions ---" << std::endl;
        server.setRemoteDescription(clientDesc);
        client.setRemoteDescription(serverDesc);
        
        // Signal end of remote candidates
        server.endOfRemoteCandidates();
        client.endOfRemoteCandidates();
        std::cout << std::endl;
        
        // Wait for connection
        std::cout << "--- Step 5: Wait for Connection ---" << std::endl;
        
        auto startTime = std::chrono::steady_clock::now();
        while (!server.isConnected() || !client.isConnected()) {
            if (server.isFailed() || client.isFailed()) {
                std::cerr << "Connection failed!" << std::endl;
                return 1;
            }
            
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (elapsed > CONNECTION_TIMEOUT) {
                std::cerr << "Connection timeout!" << std::endl;
                std::cerr << "Server state: " << transportStateToString(server.getState()) << std::endl;
                std::cerr << "Client state: " << transportStateToString(client.getState()) << std::endl;
                return 1;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "Both peers connected!" << std::endl;
        std::cout << std::endl;

        // Open a stream and send test messages
        std::cout << "--- Step 6: Send Test Messages ---" << std::endl;
        uint64_t streamId = client.openStream();
        std::cout << "Client opened stream " << streamId << std::endl;
        
        std::vector<std::string> sentMessages;
        for (int i = 1; i <= NUM_TEST_MESSAGES; i++) {
            std::string msg = "Test message #" + std::to_string(i);
            if (client.send(streamId, msg)) {
                std::cout << "Client sent: " << msg << std::endl;
                sentMessages.push_back(msg);
            } else {
                std::cerr << "Failed to send message " << i << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cout << std::endl;
        
        // Wait for echo responses
        std::cout << "--- Step 7: Wait for Echo Responses ---" << std::endl;
        if (!client.waitForMessages(NUM_TEST_MESSAGES, MESSAGE_TIMEOUT)) {
            std::cerr << "Timeout waiting for echo responses!" << std::endl;
        }
        
        auto receivedMessages = client.getReceivedMessages();
        std::cout << "Client received " << receivedMessages.size() << " echo responses" << std::endl;
        std::cout << std::endl;
        
        // Verify messages
        std::cout << "--- Step 8: Verify Results ---" << std::endl;
        bool allMatch = true;
        for (size_t i = 0; i < sentMessages.size() && i < receivedMessages.size(); i++) {
            if (sentMessages[i] == receivedMessages[i]) {
                std::cout << "  Message " << (i + 1) << ": OK" << std::endl;
            } else {
                std::cout << "  Message " << (i + 1) << ": MISMATCH" << std::endl;
                std::cout << "    Sent: " << sentMessages[i] << std::endl;
                std::cout << "    Received: " << receivedMessages[i] << std::endl;
                allMatch = false;
            }
        }
        
        if (receivedMessages.size() < sentMessages.size()) {
            std::cout << "  Missing " << (sentMessages.size() - receivedMessages.size()) 
                      << " responses" << std::endl;
            allMatch = false;
        }
        std::cout << std::endl;
        
        // Print statistics
        std::cout << "--- Statistics ---" << std::endl;
        std::cout << "Server:" << std::endl;
        std::cout << "  Bytes sent: " << server.getBytesSent() << std::endl;
        std::cout << "  Bytes received: " << server.getBytesReceived() << std::endl;
        std::cout << "Client:" << std::endl;
        std::cout << "  Bytes sent: " << client.getBytesSent() << std::endl;
        std::cout << "  Bytes received: " << client.getBytesReceived() << std::endl;
        std::cout << std::endl;
        
        // Cleanup
        std::cout << "--- Step 9: Cleanup ---" << std::endl;
        client.close();
        server.close();
        std::cout << "Connections closed." << std::endl;
        std::cout << std::endl;
        
        // Final result
        std::cout << "=== Test Result ===" << std::endl;
        if (allMatch && receivedMessages.size() == sentMessages.size()) {
            std::cout << "SUCCESS: All " << NUM_TEST_MESSAGES << " messages echoed correctly!" << std::endl;
            std::cout << std::endl;
            std::cout << "This test demonstrated:" << std::endl;
            std::cout << "  - Simplified stunServers configuration (vector of host:port)" << std::endl;
            std::cout << "  - SDP-based candidate exchange via IceDescription" << std::endl;
            std::cout << "  - getLocalDescription() / setRemoteDescription() API" << std::endl;
            return 0;
        } else {
            std::cout << "FAILURE: Message verification failed" << std::endl;
            return 1;
        }
        
    } catch (const IceQuicException& e) {
        std::cerr << "IceQuicException: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}
