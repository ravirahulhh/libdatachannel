/**
 * QUIC DataChannel 基础示例
 * 
 * 这是一个简化的示例，演示如何使用 libdatachannel 创建 DataChannel
 * 注意：完整的 QUIC 传输支持正在开发中
 */

#include "rtc/rtc.hpp"
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

using namespace rtc;
using namespace std;

// 简单的信令通道模拟（用于本地测试）
class LocalSignaling {
public:
    function<void(string)> onMessage;
    
    void send(const string& message) {
        if (peer && peer->onMessage) {
            // 模拟异步传输
            thread([this, message]() {
                this_thread::sleep_for(chrono::milliseconds(10));
                if (peer && peer->onMessage) {
                    peer->onMessage(message);
                }
            }).detach();
        }
    }
    
    void connect(shared_ptr<LocalSignaling> other) {
        peer = other;
    }
    
private:
    shared_ptr<LocalSignaling> peer;
};

class Peer {
public:
    Peer(const string& name) : mName(name) {
        // 基本配置
        Configuration config;
        config.iceServers.emplace_back("stun:stun.l.google.com:19302");
        
        cout << "[" << mName << "] Creating PeerConnection" << endl;
        
        // 创建 PeerConnection
        mPc = make_shared<PeerConnection>(config);
        mSignaling = make_shared<LocalSignaling>();
        
        setupCallbacks();
    }
    
    void setupCallbacks() {
        // 本地描述生成
        mPc->onLocalDescription([this](Description desc) {
            cout << "[" << mName << "] Local description ready (type: " 
                 << desc.typeString() << ")" << endl;
            mSignaling->send(string(desc));
        });
        
        // 本地候选生成
        mPc->onLocalCandidate([this](Candidate cand) {
            cout << "[" << mName << "] Local candidate: " 
                 << cand.candidate().substr(0, 50) << "..." << endl;
            mSignaling->send("candidate:" + cand.candidate() + "|" + cand.mid());
        });
        
        // 连接状态变化
        mPc->onStateChange([this](PeerConnection::State state) {
            cout << "[" << mName << "] Connection state: ";
            switch(state) {
                case PeerConnection::State::New: cout << "New"; break;
                case PeerConnection::State::Connecting: cout << "Connecting"; break;
                case PeerConnection::State::Connected: cout << "Connected"; break;
                case PeerConnection::State::Disconnected: cout << "Disconnected"; break;
                case PeerConnection::State::Failed: cout << "Failed"; break;
                case PeerConnection::State::Closed: cout << "Closed"; break;
            }
            cout << endl;
        });
        
        // ICE 收集状态
        mPc->onGatheringStateChange([this](PeerConnection::GatheringState state) {
            cout << "[" << mName << "] Gathering state: ";
            switch(state) {
                case PeerConnection::GatheringState::New: cout << "New"; break;
                case PeerConnection::GatheringState::InProgress: cout << "InProgress"; break;
                case PeerConnection::GatheringState::Complete: cout << "Complete"; break;
            }
            cout << endl;
        });
        
        // 信令消息处理
        mSignaling->onMessage = [this](string message) {
            if (message.find("candidate:") == 0) {
                // ICE 候选
                size_t pos = message.find('|');
                if (pos != string::npos) {
                    string cand = message.substr(10, pos - 10); // 跳过 "candidate:"
                    string mid = message.substr(pos + 1);
                    try {
                        mPc->addRemoteCandidate(Candidate(cand, mid));
                    } catch (const exception& e) {
                        cout << "[" << mName << "] Failed to add candidate: " << e.what() << endl;
                    }
                }
            } else {
                // SDP 描述
                try {
                    Description desc(message);
                    cout << "[" << mName << "] Received remote description (type: " 
                         << desc.typeString() << ")" << endl;
                    mPc->setRemoteDescription(desc);
                } catch (const exception& e) {
                    cout << "[" << mName << "] Failed to set remote description: " << e.what() << endl;
                }
            }
        };
    }
    
    void createDataChannel(const string& label) {
        cout << "[" << mName << "] Creating DataChannel: " << label << endl;
        auto dc = mPc->createDataChannel(label);
        
        dc->onOpen([this, label, dc]() {
            cout << "[" << mName << "] DataChannel '" << label << "' opened!" << endl;
            
            // 发送测试消息
            string msg = "Hello from " + mName;
            dc->send(msg);
            cout << "[" << mName << "] Sent: " << msg << endl;
        });
        
        dc->onMessage([this, label](variant<binary, string> message) {
            if (holds_alternative<string>(message)) {
                cout << "[" << mName << "] Received: " << get<string>(message) << endl;
            }
        });
        
        dc->onClosed([this, label]() {
            cout << "[" << mName << "] DataChannel '" << label << "' closed" << endl;
        });
        
        dc->onError([this, label](string error) {
            cout << "[" << mName << "] DataChannel error: " << error << endl;
        });
        
        mDataChannel = dc;
    }
    
    void onDataChannel(function<void(shared_ptr<DataChannel>)> callback) {
        mPc->onDataChannel([this, callback](shared_ptr<DataChannel> dc) {
            cout << "[" << mName << "] Received DataChannel: " << dc->label() << endl;
            
            dc->onOpen([this, dc]() {
                cout << "[" << mName << "] DataChannel '" << dc->label() << "' opened!" << endl;
            });
            
            dc->onMessage([this, dc](variant<binary, string> message) {
                if (holds_alternative<string>(message)) {
                    string msg = get<string>(message);
                    cout << "[" << mName << "] Received: " << msg << endl;
                    
                    // 回复消息
                    string reply = "Reply from " + mName;
                    dc->send(reply);
                    cout << "[" << mName << "] Sent: " << reply << endl;
                }
            });
            
            mDataChannel = dc;
            if (callback) callback(dc);
        });
    }
    
    shared_ptr<LocalSignaling> signaling() { return mSignaling; }
    shared_ptr<DataChannel> dataChannel() { return mDataChannel; }
    
private:
    string mName;
    shared_ptr<LocalSignaling> mSignaling;
    shared_ptr<PeerConnection> mPc;
    shared_ptr<DataChannel> mDataChannel;
};

int main() {
    try {
        cout << "\n=== libdatachannel DataChannel Example ===" << endl;
        cout << "This example demonstrates basic DataChannel usage\n" << endl;
        
        // 初始化日志
        InitLogger(LogLevel::Warning);
        
        // 创建两个对等端
        auto peer1 = make_shared<Peer>("Peer1");
        auto peer2 = make_shared<Peer>("Peer2");
        
        // 连接信令通道
        peer1->signaling()->connect(peer2->signaling());
        peer2->signaling()->connect(peer1->signaling());
        
        // Peer1 创建 DataChannel (主动方)
        peer1->createDataChannel("test-channel");
        
        // Peer2 等待接收 DataChannel (被动方)
        peer2->onDataChannel([](shared_ptr<DataChannel> dc) {
            cout << "Peer2 successfully received DataChannel!" << endl;
        });
        
        // 等待连接建立和数据传输
        cout << "\nWaiting for connection..." << endl;
        this_thread::sleep_for(chrono::seconds(5));
        
        // 发送更多测试数据
        if (peer1->dataChannel() && peer1->dataChannel()->isOpen()) {
            cout << "\nSending additional messages..." << endl;
            for (int i = 1; i <= 3; i++) {
                string msg = "Message #" + to_string(i);
                peer1->dataChannel()->send(msg);
                cout << "[Peer1] Sent: " << msg << endl;
                this_thread::sleep_for(chrono::milliseconds(500));
            }
        }
        
        // 等待消息传输完成
        this_thread::sleep_for(chrono::seconds(2));
        
        cout << "\n=== Example completed ===" << endl;
        cout << "\nNote: Full QUIC transport support is under development." << endl;
        cout << "This example uses the default DTLS+SCTP transport." << endl;
        
        return 0;
        
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }
}
