/**
 * @file automated_echo_test.cpp
 * @brief Automated test for echo_server and echo_client functionality
 * 
 * This test creates both server and client in separate threads within
 * the same process, exchanges ICE credentials and candidates programmatically,
 * and verifies bidirectional communication works correctly.
 * 
 * Unlike loopback_test which uses a single TransportWrapper, this test
 * simulates the actual echo_server/echo_client behavior more closely.
 * 
 * Usage:
 *   ./automated_echo_test <cert_path> <key_path>
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
#include <queue>
#include <memory>
#include <functional>

using namespace ice_quic;

// Test configuration
static const int NUM_TEST_MESSAGES = 10;
static const std::chrono::seconds GATHERING_TIMEOUT{30};
static const std::chrono::seconds CONNECTION_TIMEOUT{30};
static const std::chrono::seconds MESSAGE_TIMEOUT{10};

// Shared state for credential/candidate exchange
struct SharedState {
    std::mutex mtx;
    std::condition_variable cv;
    
    // Server info
    std::string serverUfrag;
    std::string serverPwd;
    std::vector<std::string> serverCandidates;
    bool serverGatheringDone = false;
    
    // Client info
    std::string clientUfrag;
    std::string clientPwd;
    std::vector<std::string> clientCandidates;
    bool clientGatheringDone = false;
    
    // Connection state
    bool serverConnected = false;
    bool clientConnected = false;
    bool testFailed = false;
    std::string failureReason;
};

// Echo server implementation
class EchoServer {
public:
    EchoServer(SharedState& state, const std::string& certPath, const std::string& keyPath)
        : mState(state), mCertPath(certPath), mKeyPath(keyPath) {}
    
    void run() {
        try {
            // Configure server
            IceQuicConfig config;
            config.stunServer = "stun.l.google.com";
            config.stunPort = 19302;
            config.isServer = true;
            config.certPath = mCertPath;
            config.keyPath = mKeyPath;
            config.idleTimeoutMs = 30000;
            config.maxStreams = 100;
            config.maxMessageSize = 65536;
            
            // Set up callbacks
            IceQuicCallbacks callbacks;
            
            callbacks.onLocalCandidate = [this](const std::string& candidate) {
                std::lock_guard<std::mutex> lock(mState.mtx);
                mState.serverCandidates.push_back(candidate);
                log("Local candidate: " + candidate);
            };
            
            callbacks.onGatheringComplete = [this]() {
                log("Gathering complete");
                std::lock_guard<std::mutex> lock(mState.mtx);
                mState.serverGatheringDone = true;
                mState.cv.notify_all();
            };
            
            callbacks.onConnected = [this]() {
                log("Connected!");
                std::lock_guard<std::mutex> lock(mState.mtx);
                mState.serverConnected = true;
                mState.cv.notify_all();
            };
            
            callbacks.onDisconnected = [this]() {
                log("Disconnected");
                mRunning = false;
            };
            
            callbacks.onFailed = [this](const std::string& error) {
                log("Failed: " + error);
                std::lock_guard<std::mutex> lock(mState.mtx);
                mState.testFailed = true;
                mState.failureReason = "Server: " + error;
                mState.cv.notify_all();
                mRunning = false;
            };
            
            callbacks.onStreamOpened = [this](uint64_t streamId) {
                log("Stream " + std::to_string(streamId) + " opened");
            };
            
            callbacks.onData = [this](uint64_t streamId, const uint8_t* data, size_t len) {
                std::string msg(reinterpret_cast<const char*>(data), len);
                log("Received: " + msg);
                
                // Echo back
                if (mTransport && mTransport->getState() == TransportState::Connected) {
                    mTransport->send(streamId, data, len);
                    log("Echoed: " + msg);
                    mEchoCount++;
                }
            };
            
            // Create transport
            log("Initializing...");
            mTransport = std::make_unique<IceQuicTransport>(config, callbacks);
            
            // Get credentials
            auto [ufrag, pwd] = mTransport->getLocalCredentials();
            {
                std::lock_guard<std::mutex> lock(mState.mtx);
                mState.serverUfrag = ufrag;
                mState.serverPwd = pwd;
            }
            log("Credentials: ufrag=" + ufrag);
            
            // Start gathering
            log("Starting candidate gathering...");
            mTransport->gatherCandidates();
            
            // Wait for gathering to complete
            {
                std::unique_lock<std::mutex> lock(mState.mtx);
                if (!mState.cv.wait_for(lock, GATHERING_TIMEOUT, [this]() {
                    return mState.serverGatheringDone || mState.testFailed;
                })) {
                    log("Gathering timeout!");
                    mState.testFailed = true;
                    mState.failureReason = "Server gathering timeout";
                    mState.cv.notify_all();
                    return;
                }
            }
            
            if (mState.testFailed) return;
            
            // Wait for client to be ready
            {
                std::unique_lock<std::mutex> lock(mState.mtx);
                mState.cv.wait(lock, [this]() {
                    return mState.clientGatheringDone || mState.testFailed;
                });
            }
            
            if (mState.testFailed) return;
            
            // Set remote credentials and candidates
            log("Setting remote credentials...");
            mTransport->setRemoteCredentials(mState.clientUfrag, mState.clientPwd);
            
            log("Adding remote candidates...");
            for (const auto& candidate : mState.clientCandidates) {
                if (mTransport->addRemoteCandidate(candidate)) {
                    log("Added: " + candidate);
                }
            }
            mTransport->endOfRemoteCandidates();
            
            // Wait for connection
            {
                std::unique_lock<std::mutex> lock(mState.mtx);
                if (!mState.cv.wait_for(lock, CONNECTION_TIMEOUT, [this]() {
                    return mState.serverConnected || mState.testFailed;
                })) {
                    log("Connection timeout!");
                    mState.testFailed = true;
                    mState.failureReason = "Server connection timeout";
                    mState.cv.notify_all();
                    return;
                }
            }
            
            if (mState.testFailed) return;
            
            // Run until stopped
            mRunning = true;
            while (mRunning && !mState.testFailed) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            log("Shutting down... (echoed " + std::to_string(mEchoCount) + " messages)");
            mTransport->close();
            
        } catch (const std::exception& e) {
            log("Exception: " + std::string(e.what()));
            std::lock_guard<std::mutex> lock(mState.mtx);
            mState.testFailed = true;
            mState.failureReason = "Server exception: " + std::string(e.what());
            mState.cv.notify_all();
        }
    }
    
    void stop() { mRunning = false; }
    int getEchoCount() const { return mEchoCount; }
    
private:
    void log(const std::string& msg) {
        std::cout << "[SERVER] " << msg << std::endl;
    }
    
    SharedState& mState;
    std::string mCertPath;
    std::string mKeyPath;
    std::unique_ptr<IceQuicTransport> mTransport;
    std::atomic<bool> mRunning{false};
    std::atomic<int> mEchoCount{0};
};

// Echo client implementation
class EchoClient {
public:
    EchoClient(SharedState& state) : mState(state) {}
    
    void run() {
        try {
            // Configure client
            IceQuicConfig config;
            config.stunServer = "stun.l.google.com";
            config.stunPort = 19302;
            config.isServer = false;
            config.idleTimeoutMs = 30000;
            config.maxStreams = 100;
            config.maxMessageSize = 65536;
            
            // Set up callbacks
            IceQuicCallbacks callbacks;
            
            callbacks.onLocalCandidate = [this](const std::string& candidate) {
                std::lock_guard<std::mutex> lock(mState.mtx);
                mState.clientCandidates.push_back(candidate);
                log("Local candidate: " + candidate);
            };
            
            callbacks.onGatheringComplete = [this]() {
                log("Gathering complete");
                std::lock_guard<std::mutex> lock(mState.mtx);
                mState.clientGatheringDone = true;
                mState.cv.notify_all();
            };
            
            callbacks.onConnected = [this]() {
                log("Connected!");
                std::lock_guard<std::mutex> lock(mState.mtx);
                mState.clientConnected = true;
                mState.cv.notify_all();
            };
            
            callbacks.onDisconnected = [this]() {
                log("Disconnected");
                mRunning = false;
            };
            
            callbacks.onFailed = [this](const std::string& error) {
                log("Failed: " + error);
                std::lock_guard<std::mutex> lock(mState.mtx);
                mState.testFailed = true;
                mState.failureReason = "Client: " + error;
                mState.cv.notify_all();
                mRunning = false;
            };
            
            callbacks.onData = [this](uint64_t streamId, const uint8_t* data, size_t len) {
                std::string msg(reinterpret_cast<const char*>(data), len);
                log("Echo received: " + msg);
                
                std::lock_guard<std::mutex> lock(mReceivedMtx);
                mReceivedMessages.push_back(msg);
                mReceivedCv.notify_all();
            };
            
            // Create transport
            log("Initializing...");
            mTransport = std::make_unique<IceQuicTransport>(config, callbacks);
            
            // Get credentials
            auto [ufrag, pwd] = mTransport->getLocalCredentials();
            {
                std::lock_guard<std::mutex> lock(mState.mtx);
                mState.clientUfrag = ufrag;
                mState.clientPwd = pwd;
            }
            log("Credentials: ufrag=" + ufrag);
            
            // Start gathering
            log("Starting candidate gathering...");
            mTransport->gatherCandidates();
            
            // Wait for gathering to complete
            {
                std::unique_lock<std::mutex> lock(mState.mtx);
                if (!mState.cv.wait_for(lock, GATHERING_TIMEOUT, [this]() {
                    return mState.clientGatheringDone || mState.testFailed;
                })) {
                    log("Gathering timeout!");
                    mState.testFailed = true;
                    mState.failureReason = "Client gathering timeout";
                    mState.cv.notify_all();
                    return;
                }
            }
            
            if (mState.testFailed) return;
            
            // Wait for server to be ready
            {
                std::unique_lock<std::mutex> lock(mState.mtx);
                mState.cv.wait(lock, [this]() {
                    return mState.serverGatheringDone || mState.testFailed;
                });
            }
            
            if (mState.testFailed) return;
            
            // Set remote credentials and candidates
            log("Setting remote credentials...");
            mTransport->setRemoteCredentials(mState.serverUfrag, mState.serverPwd);
            
            log("Adding remote candidates...");
            for (const auto& candidate : mState.serverCandidates) {
                if (mTransport->addRemoteCandidate(candidate)) {
                    log("Added: " + candidate);
                }
            }
            mTransport->endOfRemoteCandidates();
            
            // Wait for connection
            {
                std::unique_lock<std::mutex> lock(mState.mtx);
                if (!mState.cv.wait_for(lock, CONNECTION_TIMEOUT, [this]() {
                    return mState.clientConnected || mState.testFailed;
                })) {
                    log("Connection timeout!");
                    mState.testFailed = true;
                    mState.failureReason = "Client connection timeout";
                    mState.cv.notify_all();
                    return;
                }
            }
            
            if (mState.testFailed) return;
            
            // Open stream and send test messages
            log("Opening stream...");
            uint64_t streamId = mTransport->openStream();
            log("Stream " + std::to_string(streamId) + " opened");
            
            mRunning = true;
            
            // Send test messages
            for (int i = 1; i <= NUM_TEST_MESSAGES && mRunning && !mState.testFailed; i++) {
                std::string msg = "Test message #" + std::to_string(i);
                
                if (mTransport->send(streamId, 
                                    reinterpret_cast<const uint8_t*>(msg.c_str()),
                                    msg.length())) {
                    log("Sent: " + msg);
                    mSentMessages.push_back(msg);
                } else {
                    log("Failed to send: " + msg);
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            // Wait for echo responses
            log("Waiting for echo responses...");
            {
                std::unique_lock<std::mutex> lock(mReceivedMtx);
                mReceivedCv.wait_for(lock, MESSAGE_TIMEOUT, [this]() {
                    return mReceivedMessages.size() >= mSentMessages.size() || mState.testFailed;
                });
            }
            
            // Verify results
            mRunning = false;
            
            log("Shutting down...");
            mTransport->closeStream(streamId);
            mTransport->close();
            
        } catch (const std::exception& e) {
            log("Exception: " + std::string(e.what()));
            std::lock_guard<std::mutex> lock(mState.mtx);
            mState.testFailed = true;
            mState.failureReason = "Client exception: " + std::string(e.what());
            mState.cv.notify_all();
        }
    }
    
    void stop() { mRunning = false; }
    
    const std::vector<std::string>& getSentMessages() const { return mSentMessages; }
    const std::vector<std::string>& getReceivedMessages() const { return mReceivedMessages; }
    
private:
    void log(const std::string& msg) {
        std::cout << "[CLIENT] " << msg << std::endl;
    }
    
    SharedState& mState;
    std::unique_ptr<IceQuicTransport> mTransport;
    std::atomic<bool> mRunning{false};
    
    std::vector<std::string> mSentMessages;
    std::vector<std::string> mReceivedMessages;
    std::mutex mReceivedMtx;
    std::condition_variable mReceivedCv;
};

void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " <cert_path> <key_path>" << std::endl;
    std::cerr << std::endl;
    std::cerr << "To generate self-signed certificates:" << std::endl;
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
    
    std::cout << "================================================" << std::endl;
    std::cout << "  ICE+QUIC Echo Server/Client Automated Test" << std::endl;
    std::cout << "================================================" << std::endl;
    std::cout << std::endl;
    std::cout << "This test runs server and client in separate threads," << std::endl;
    std::cout << "exchanges ICE credentials/candidates, and verifies" << std::endl;
    std::cout << "bidirectional echo communication." << std::endl;
    std::cout << std::endl;
    std::cout << "Test configuration:" << std::endl;
    std::cout << "  Messages to send: " << NUM_TEST_MESSAGES << std::endl;
    std::cout << "  Connection timeout: " << CONNECTION_TIMEOUT.count() << "s" << std::endl;
    std::cout << std::endl;
    
    SharedState state;
    EchoServer server(state, certPath, keyPath);
    EchoClient client(state);
    
    // Start server and client in separate threads
    std::cout << "--- Starting Server and Client ---" << std::endl;
    std::thread serverThread([&server]() { server.run(); });
    
    // Small delay to let server start first
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::thread clientThread([&client]() { client.run(); });
    
    // Wait for client to finish (it drives the test)
    clientThread.join();
    
    // Stop server
    server.stop();
    serverThread.join();
    
    std::cout << std::endl;
    std::cout << "--- Test Results ---" << std::endl;
    
    if (state.testFailed) {
        std::cout << "FAILURE: " << state.failureReason << std::endl;
        return 1;
    }
    
    const auto& sent = client.getSentMessages();
    const auto& received = client.getReceivedMessages();
    
    std::cout << "Messages sent: " << sent.size() << std::endl;
    std::cout << "Messages received: " << received.size() << std::endl;
    std::cout << "Server echo count: " << server.getEchoCount() << std::endl;
    std::cout << std::endl;
    
    // Verify messages
    int matchCount = 0;
    for (size_t i = 0; i < sent.size() && i < received.size(); i++) {
        if (sent[i] == received[i]) {
            matchCount++;
            std::cout << "  Message " << (i + 1) << ": OK" << std::endl;
        } else {
            std::cout << "  Message " << (i + 1) << ": MISMATCH" << std::endl;
            std::cout << "    Sent: " << sent[i] << std::endl;
            std::cout << "    Received: " << received[i] << std::endl;
        }
    }
    
    std::cout << std::endl;
    std::cout << "================================================" << std::endl;
    
    if (matchCount == NUM_TEST_MESSAGES && received.size() == sent.size()) {
        std::cout << "SUCCESS: All " << NUM_TEST_MESSAGES << " messages echoed correctly!" << std::endl;
        std::cout << "================================================" << std::endl;
        return 0;
    } else {
        std::cout << "FAILURE: Only " << matchCount << "/" << NUM_TEST_MESSAGES << " messages matched" << std::endl;
        std::cout << "================================================" << std::endl;
        return 1;
    }
}
