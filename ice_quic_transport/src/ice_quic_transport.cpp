#include "ice_quic_transport.hpp"

#include <nice.h>
#include <gio/gnetworking.h>
#include <lsquic.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>

#include <sstream>
#include <cstring>
#include <algorithm>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace ice_quic {
static std::once_flag s_lsquicInitFlag;

IceQuicTransport::IceQuicTransport(const IceQuicConfig& config, 
                                   const IceQuicCallbacks& callbacks)
    : mMainContext(nullptr)
    , mMainLoop(nullptr)
    , mIceAgent(nullptr)
    , mIceStreamId(0)
    , mQuicEngine(nullptr)
    , mQuicConn(nullptr)
    , mSslCtx(nullptr)
    , mQuicRunning(false)
    , mAddressesSet(false)
    , mNextStreamId(1)
    , mState(TransportState::Disconnected)
    , mConfig(config)
    , mCallbacks(callbacks)
    , mBytesSent(0)
    , mBytesReceived(0)
{
    try {
        config.validate();
    } catch (const std::exception& e) {
        throw IceQuicException(IceQuicException::ErrorCode::InvalidArgument, 
                               std::string("Invalid configuration: ") + e.what());
    }
    
    std::memset(&mLocalAddr, 0, sizeof(mLocalAddr));
    std::memset(&mPeerAddr, 0, sizeof(mPeerAddr));
    
    initIce();
    
    initQuic();
}

IceQuicTransport::~IceQuicTransport() {
    close();
    cleanupQuic();
    cleanupIce();
}

void IceQuicTransport::initIce() {
    // Initialize GLib networking
    g_networking_init();
    
    // Create a new GMainContext for this transport
    mMainContext = g_main_context_new();
    if (!mMainContext) {
        throw IceQuicException(IceQuicException::ErrorCode::InitializationFailed,
                               "Failed to create GMainContext");
    }
    
    // Create the main loop
    mMainLoop = g_main_loop_new(mMainContext, FALSE);
    if (!mMainLoop) {
        g_main_context_unref(mMainContext);
        mMainContext = nullptr;
        throw IceQuicException(IceQuicException::ErrorCode::InitializationFailed,
                               "Failed to create GMainLoop");
    }
    
    // Create the ICE agent with RFC5245 compatibility
    mIceAgent = nice_agent_new(mMainContext, NICE_COMPATIBILITY_RFC5245);
    if (!mIceAgent) {
        g_main_loop_unref(mMainLoop);
        g_main_context_unref(mMainContext);
        mMainLoop = nullptr;
        mMainContext = nullptr;
        throw IceQuicException(IceQuicException::ErrorCode::InitializationFailed,
                               "Failed to create NiceAgent");
    }
    
    if (!mConfig.stunServers.empty()) {
        for (size_t i = 0; i < mConfig.stunServers.size(); ++i) {
            const std::string& server = mConfig.stunServers[i];
            auto colonPos = server.rfind(':');
            if (colonPos != std::string::npos && colonPos > 0 && colonPos < server.length() - 1) {
                std::string host = server.substr(0, colonPos);
                uint16_t port = static_cast<uint16_t>(std::stoi(server.substr(colonPos + 1)));
                if (i == 0) {
                    g_object_set(mIceAgent, 
                                 "stun-server", host.c_str(),
                                 "stun-server-port", static_cast<guint>(port),
                                 nullptr);
                }
            }
        }
    }
    
    // Set controlling mode based on role (server = controlling)
    g_object_set(mIceAgent, 
                 "controlling-mode", mConfig.isServer ? TRUE : FALSE,
                 nullptr);
    
    // Enable ICE trickle for faster candidate exchange
    g_object_set(mIceAgent,
                 "ice-trickle", TRUE,
                 nullptr);
    
    // Connect signals
    g_signal_connect(mIceAgent, "candidate-gathering-done",
                     G_CALLBACK(onIceCandidateGatheringDone), this);
    g_signal_connect(mIceAgent, "component-state-changed",
                     G_CALLBACK(onIceComponentStateChanged), this);
    g_signal_connect(mIceAgent, "new-candidate-full",
                     G_CALLBACK(onIceNewCandidate), this);
    
    // Add a stream with one component (for data)
    mIceStreamId = nice_agent_add_stream(mIceAgent, 1);
    if (mIceStreamId == 0) {
        g_object_unref(mIceAgent);
        g_main_loop_unref(mMainLoop);
        g_main_context_unref(mMainContext);
        mIceAgent = nullptr;
        mMainLoop = nullptr;
        mMainContext = nullptr;
        throw IceQuicException(IceQuicException::ErrorCode::InitializationFailed,
                               "Failed to add ICE stream");
    }
    
    // Set stream name (must be one of: audio, video, text, application, image, message)
    nice_agent_set_stream_name(mIceAgent, mIceStreamId, "application");
    
    // Attach receive callback - this is required for ICE to work
    nice_agent_attach_recv(mIceAgent, mIceStreamId, 1,
                           mMainContext, onIceRecv, this);
    
    // Get local credentials
    gchar* ufrag = nullptr;
    gchar* pwd = nullptr;
    if (nice_agent_get_local_credentials(mIceAgent, mIceStreamId, &ufrag, &pwd)) {
        mLocalUfrag = ufrag;
        mLocalPwd = pwd;
        g_free(ufrag);
        g_free(pwd);
    }
    
    // Start the main loop thread
    mMainLoopThread = std::thread(&IceQuicTransport::runMainLoop, this);
}

static gboolean quitMainLoopCallback(gpointer userData) {
    GMainLoop* loop = static_cast<GMainLoop*>(userData);
    if (loop && g_main_loop_is_running(loop)) {
        g_main_loop_quit(loop);
    }
    return G_SOURCE_REMOVE;
}

void IceQuicTransport::cleanupIce() {
    if (mMainLoop && mMainContext) {
        GSource* source = g_idle_source_new();
        g_source_set_callback(source, quitMainLoopCallback, mMainLoop, nullptr);
        g_source_attach(source, mMainContext);
        g_source_unref(source);
    }
    
    if (mMainLoopThread.joinable()) {
        mMainLoopThread.join();
    }
    
    if (mIceAgent) {
        g_object_unref(mIceAgent);
        mIceAgent = nullptr;
    }
    
    if (mMainLoop) {
        g_main_loop_unref(mMainLoop);
        mMainLoop = nullptr;
    }
    
    if (mMainContext) {
        g_main_context_unref(mMainContext);
        mMainContext = nullptr;
    }
}

void IceQuicTransport::runMainLoop() {
    g_main_context_push_thread_default(mMainContext);
    
    g_main_loop_run(mMainLoop);
    
    g_main_context_pop_thread_default(mMainContext);
}

void IceQuicTransport::initQuic() {
    // Initialize liblsquic once global
    std::call_once(s_lsquicInitFlag, []() {
        if (lsquic_global_init(LSQUIC_GLOBAL_SERVER | LSQUIC_GLOBAL_CLIENT) != 0) {
            throw IceQuicException(IceQuicException::ErrorCode::InitializationFailed,
                                   "Failed to initialize lsquic library");
        }

    });
    
    // Create SSL context
    mSslCtx = SSL_CTX_new(TLS_method());
    if (!mSslCtx) {
        throw IceQuicException(IceQuicException::ErrorCode::InitializationFailed,
                               "Failed to create SSL context");
    }
    
    // Configure SSL context
    SSL_CTX_set_min_proto_version(mSslCtx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(mSslCtx, TLS1_3_VERSION);
    
    // Set ALPN for QUIC - use "echo" as our custom protocol
    static const unsigned char alpn[] = {0x04, 'e', 'c', 'h', 'o'};  // Length-prefixed ALPN
    SSL_CTX_set_alpn_protos(mSslCtx, alpn, sizeof(alpn));
    
    // For server mode, load certificate and key, and set ALPN select callback
    if (mConfig.isServer) {
        if (!mConfig.certPath.empty() && !mConfig.keyPath.empty()) {
            if (SSL_CTX_use_certificate_file(mSslCtx, mConfig.certPath.c_str(), 
                                             SSL_FILETYPE_PEM) != 1) {
                SSL_CTX_free(mSslCtx);
                mSslCtx = nullptr;
                throw IceQuicException(IceQuicException::ErrorCode::InitializationFailed,
                                       "Failed to load TLS certificate");
            }
            if (SSL_CTX_use_PrivateKey_file(mSslCtx, mConfig.keyPath.c_str(), 
                                            SSL_FILETYPE_PEM) != 1) {
                SSL_CTX_free(mSslCtx);
                mSslCtx = nullptr;
                throw IceQuicException(IceQuicException::ErrorCode::InitializationFailed,
                                       "Failed to load TLS private key");
            }
        }
        
        // Set ALPN select callback for server
        SSL_CTX_set_alpn_select_cb(mSslCtx, 
            [](SSL* ssl, const unsigned char** out, unsigned char* outlen,
               const unsigned char* in, unsigned int inlen, void* arg) -> int {
                (void)ssl;
                (void)arg;
                // Our ALPN is "echo"
                static const unsigned char server_alpn[] = {0x04, 'e', 'c', 'h', 'o'};
                int r = SSL_select_next_proto((unsigned char**)out, outlen,
                                              in, inlen, server_alpn, sizeof(server_alpn));
                if (r == OPENSSL_NPN_NEGOTIATED) {
                    return SSL_TLSEXT_ERR_OK;
                }
                return SSL_TLSEXT_ERR_ALERT_FATAL;
            }, nullptr);
        
        // Enable early data (0-RTT)
        SSL_CTX_set_early_data_enabled(mSslCtx, 1);
    }
    
    // Configure lsquic engine settings
    struct lsquic_engine_settings settings;
    unsigned flags = mConfig.isServer ? LSENG_SERVER : 0;
    lsquic_engine_init_settings(&settings, flags);
    
    // Apply configuration
    settings.es_idle_timeout = mConfig.idleTimeoutMs / 1000;  // Convert to seconds
    settings.es_init_max_streams_bidi = mConfig.maxStreams;
    // Use reasonable defaults for flow control (64KB per stream, 640KB connection-level)
    constexpr size_t kDefaultMaxMessageSize = 65536;
    settings.es_init_max_data = kDefaultMaxMessageSize * 10;  // Connection-level flow control
    settings.es_init_max_stream_data_bidi_local = kDefaultMaxMessageSize;
    settings.es_init_max_stream_data_bidi_remote = kDefaultMaxMessageSize;
    
    // Enable IETF QUIC versions - support both v1
    settings.es_versions = 1 << LSQVER_I001;
    
    // Disable version negotiation and stateless retry for P2P use case
    settings.es_send_verneg = 0;
    settings.es_support_srej = 0;
    

    
    // Verify settings
    char err_buf[256];
    if (lsquic_engine_check_settings(&settings, flags, err_buf, sizeof(err_buf)) != 0) {
        SSL_CTX_free(mSslCtx);
        mSslCtx = nullptr;
        throw IceQuicException(IceQuicException::ErrorCode::InitializationFailed,
                               std::string("Invalid lsquic settings: ") + err_buf);
    }
    
    // Set up stream interface callbacks
    static const struct lsquic_stream_if stream_if = {
        .on_new_conn = onNewConn,
        .on_conn_closed = onConnClosed,
        .on_new_stream = onNewStream,
        .on_read = onStreamRead,
        .on_write = onStreamWrite,
        .on_close = onStreamClose,
        .on_hsk_done = onHskDone,
    };
    
    // Set up engine API
    struct lsquic_engine_api api;
    std::memset(&api, 0, sizeof(api));
    api.ea_settings = &settings;
    api.ea_stream_if = &stream_if;
    api.ea_stream_if_ctx = this;
    api.ea_packets_out = onPacketsOut;
    api.ea_packets_out_ctx = this;
    api.ea_get_ssl_ctx = onGetSslCtx;
    api.ea_alpn = "echo";  // Custom ALPN for P2P transport
    
    // For server mode, set up certificate lookup
    if (mConfig.isServer) {
        api.ea_lookup_cert = onLookupCert;
        api.ea_cert_lu_ctx = this;
    }
    
    // Create the engine
    mQuicEngine = lsquic_engine_new(flags, &api);
    if (!mQuicEngine) {
        SSL_CTX_free(mSslCtx);
        mSslCtx = nullptr;
        throw IceQuicException(IceQuicException::ErrorCode::InitializationFailed,
                               "Failed to create lsquic engine");
    }
}

void IceQuicTransport::cleanupQuic() {
    mQuicRunning = false;
    
    if (mQuicEventThread.joinable()) {
        mQuicEventThread.join();
    }
    
    if (mQuicConn) {
        lsquic_conn_close(mQuicConn);
        mQuicConn = nullptr;
    }
    
    if (mQuicEngine) {
        lsquic_engine_destroy(mQuicEngine);
        mQuicEngine = nullptr;
    }
    
    if (mSslCtx) {
        SSL_CTX_free(mSslCtx);
        mSslCtx = nullptr;
    }
}

void IceQuicTransport::runQuicEventLoop() {
    while (mQuicRunning) {
        int sleepMs = 10;
        
        {
            std::lock_guard<std::mutex> lock(mQuicEngineMutex);
            
            if (mQuicEngine) {
                // Process connections
                lsquic_engine_process_conns(mQuicEngine);
                
                // Send unsent packets
                if (lsquic_engine_has_unsent_packets(mQuicEngine)) {
                    lsquic_engine_send_unsent_packets(mQuicEngine);
                }
                
                // Determine when to process next
                int diff = 0;
                if (lsquic_engine_earliest_adv_tick(mQuicEngine, &diff)) {
                    if (diff > 0) {
                        sleepMs = std::min(diff / 1000, 10);
                        if (sleepMs < 1) sleepMs = 1;
                    } else {
                        sleepMs = 0;
                    }
                }
            }
        }
        
        if (sleepMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }
    }
}

void IceQuicTransport::processQuicEvents() {}

void IceQuicTransport::startQuicConnection() {
    if (!mQuicEngine || !mAddressesSet) return;
    
    if (!mConfig.isServer) {
        std::lock_guard<std::mutex> lock(mQuicEngineMutex);
        
        const struct sockaddr* localSa = reinterpret_cast<const struct sockaddr*>(&mLocalAddr);
        const struct sockaddr* peerSa = reinterpret_cast<const struct sockaddr*>(&mPeerAddr);
        
        // Use IETF QUIC v1 explicitly
        mQuicConn = lsquic_engine_connect(
            mQuicEngine,
            LSQVER_I001,  // Use IETF QUIC v1 explicitly
            localSa,
            peerSa,
            this,      // peer_ctx
            nullptr,   // conn_ctx
            "localhost",  // hostname SNI
            0,         // base_plpmtu
            nullptr, 0,  // session resumption
            nullptr, 0   // token
        );
        
        if (!mQuicConn) {
            if (mCallbacks.onFailed) {
                mCallbacks.onFailed("Failed to initiate QUIC connection");
            }
            return;
        }
        lsquic_engine_process_conns(mQuicEngine);
    }
    
    if (!mQuicRunning.exchange(true)) {
        if (mQuicEventThread.joinable()) {
            mQuicEventThread.join();
        }
        mQuicEventThread = std::thread(&IceQuicTransport::runQuicEventLoop, this);
    }
}

void IceQuicTransport::gatherCandidates() {
    if (!mIceAgent || mIceStreamId == 0) {
        throw IceQuicException(IceQuicException::ErrorCode::InvalidState,
                               "ICE agent not initialized");
    }
    
    mState = TransportState::Gathering;
    
    if (!nice_agent_gather_candidates(mIceAgent, mIceStreamId)) {
        mState = TransportState::Failed;
        throw IceQuicException(IceQuicException::ErrorCode::IceError,
                               "Failed to start candidate gathering");
    }
}

bool IceQuicTransport::addRemoteCandidate(const std::string& candidateSdp) {
    if (!mIceAgent || mIceStreamId == 0) {
        return false;
    }
    
    if (candidateSdp.empty()) {
        return false;
    }
    
    NiceCandidate* candidate = nice_agent_parse_remote_candidate_sdp(
        mIceAgent, 
        mIceStreamId, 
        candidateSdp.c_str()
    );
    
    if (!candidate) {
        return false;
    }
    
    GSList* candidates = g_slist_append(nullptr, candidate);
    int result = nice_agent_set_remote_candidates(mIceAgent, mIceStreamId, 1, candidates);
    
    g_slist_free_full(candidates, (GDestroyNotify)nice_candidate_free);
    
    if (result > 0 && mState == TransportState::Gathering) {
        mState = TransportState::Connecting;
    }
    
    return result > 0;
}

void IceQuicTransport::setRemoteCredentials(const std::string& ufrag, const std::string& pwd) {
    if (!mIceAgent || mIceStreamId == 0) {
        throw IceQuicException(IceQuicException::ErrorCode::InvalidState,
                               "ICE agent not initialized");
    }
    
    if (!nice_agent_set_remote_credentials(mIceAgent, mIceStreamId, 
                                           ufrag.c_str(), pwd.c_str())) {
        throw IceQuicException(IceQuicException::ErrorCode::IceError,
                               "Failed to set remote credentials");
    }
}

void IceQuicTransport::setRemoteDescription(const IceDescription& desc) {
    if (!mIceAgent || mIceStreamId == 0) {
        throw IceQuicException(IceQuicException::ErrorCode::InvalidState,
                               "ICE agent not initialized");
    }
    
    if (!desc.ufrag.empty() && !desc.pwd.empty()) {
        if (!nice_agent_set_remote_credentials(mIceAgent, mIceStreamId, 
                                               desc.ufrag.c_str(), desc.pwd.c_str())) {
            throw IceQuicException(IceQuicException::ErrorCode::IceError,
                                   "Failed to set remote credentials");
        }
    }
    
    for (const auto& candidateSdp : desc.candidates) {
        addRemoteCandidate(candidateSdp);
    }
}

std::pair<std::string, std::string> IceQuicTransport::getLocalCredentials() const {
    return {mLocalUfrag, mLocalPwd};
}

IceDescription IceQuicTransport::getLocalDescription() const {
    IceDescription desc;
    
    if (mIceAgent && mIceStreamId != 0) {
        gchar* ufrag = nullptr;
        gchar* pwd = nullptr;
        if (nice_agent_get_local_credentials(mIceAgent, mIceStreamId, &ufrag, &pwd)) {
            if (ufrag) {
                desc.ufrag = ufrag;
                g_free(ufrag);
            }
            if (pwd) {
                desc.pwd = pwd;
                g_free(pwd);
            }
        }
    }
    
    if (desc.ufrag.empty()) {
        desc.ufrag = mLocalUfrag;
    }
    if (desc.pwd.empty()) {
        desc.pwd = mLocalPwd;
    }
    
    {
        std::lock_guard<std::mutex> lock(mCandidatesMutex);
        desc.candidates = mLocalCandidates;
    }
    
    return desc;
}

void IceQuicTransport::endOfRemoteCandidates() {
    if (!mIceAgent || mIceStreamId == 0) {
        return;
    }
    nice_agent_peer_candidate_gathering_done(mIceAgent, mIceStreamId);
}

uint64_t IceQuicTransport::openStream() {
    if (mState != TransportState::Connected) {
        return 0;
    }
    
    if (!mQuicConn) {
        return 0;
    }
    
    {
        std::lock_guard<std::mutex> lock(mStreamsMutex);
        if (mStreams.size() >= mConfig.maxStreams) {
            return 0;
        }
    }
    
    uint64_t expectedStreamId;
    {
        std::lock_guard<std::mutex> lock(mStreamsMutex);
        size_t currentStreamCount = mStreams.size();
        if (mConfig.isServer) {
            expectedStreamId = 1 + (currentStreamCount * 4);
        } else {
            expectedStreamId = currentStreamCount * 4;
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(mQuicEngineMutex);
        lsquic_conn_make_stream(mQuicConn);
        
        lsquic_engine_process_conns(mQuicEngine);
    }
    
    return expectedStreamId;
}

void IceQuicTransport::closeStream(uint64_t streamId) {
    lsquic_stream_t* stream = nullptr;
    
    {
        std::lock_guard<std::mutex> lock(mStreamsMutex);
        auto it = mStreams.find(streamId);
        if (it == mStreams.end()) {
            return;
        }
        stream = it->second;
        mStreams.erase(it);
    }
    
    if (stream) {
        std::lock_guard<std::mutex> lock(mQuicEngineMutex);
        lsquic_stream_close(stream);
    }
}

uint32_t IceQuicTransport::getMaxStreams() const {
    return mConfig.maxStreams;
}

bool IceQuicTransport::send(uint64_t streamId, const uint8_t* data, size_t len) {
    if (len == 0) {
        return true;
    }
    
    if (mState != TransportState::Connected) {
        return false;
    }
    
    // Validate data size (64KB max message size)
    constexpr size_t kMaxMessageSize = 65536;
    if (len > kMaxMessageSize) {
        return false;
    }
    
    // Find and validate stream
    lsquic_stream_t* stream = nullptr;
    {
        std::lock_guard<std::mutex> lock(mStreamsMutex);
        auto it = mStreams.find(streamId);
        if (it == mStreams.end() || !it->second) {
            return false;
        }
        stream = it->second;
    }
    
    // Write data to stream
    ssize_t written = lsquic_stream_write(stream, data, len);
    if (written < 0) {
        return false;
    }
    
    // Flush to ensure data is sent
    lsquic_stream_flush(stream);
    
    mBytesSent += static_cast<size_t>(written);
    
    return static_cast<size_t>(written) == len;
}

void IceQuicTransport::close() {
    TransportState currentState = mState.load();
    
    if (currentState == TransportState::Disconnected) {
        return;
    }
    
    if (mQuicConn) {
        lsquic_conn_close(mQuicConn);
    }
    
    {
        std::lock_guard<std::mutex> lock(mStreamsMutex);
        for (auto& [streamId, stream] : mStreams) {
            if (stream) {
                lsquic_stream_close(stream);
            }
        }
        mStreams.clear();
    }
    
    processQuicEvents();
    
    TransportState expected = currentState;
    if (mState.compare_exchange_strong(expected, TransportState::Disconnected)) {
        if (mCallbacks.onDisconnected) {
            mCallbacks.onDisconnected();
        }
    }
}

TransportState IceQuicTransport::getState() const {
    return mState.load();
}

uint32_t IceQuicTransport::getRtt() const {
    if (!mQuicConn) {
        return 0;
    }
    
    struct lsquic_conn_info info;
    if (lsquic_conn_get_info(mQuicConn, &info) != 0) {
        return 0;
    }
    
    return info.lci_rtt / 1000;
}

size_t IceQuicTransport::getBytesSent() const {
    return mBytesSent.load();
}

size_t IceQuicTransport::getBytesReceived() const {
    return mBytesReceived.load();
}

bool IceQuicTransport::isServerMode() const {
    return mConfig.isServer;
}

// libnice Callbacks

void IceQuicTransport::onIceCandidateGatheringDone(NiceAgent* agent, guint streamId, 
                                                   gpointer userData) {
    (void)agent;
    (void)streamId;
    
    auto* transport = static_cast<IceQuicTransport*>(userData);
    if (transport && transport->mCallbacks.onGatheringComplete) {
        transport->mCallbacks.onGatheringComplete();
    }
}

void IceQuicTransport::onIceComponentStateChanged(NiceAgent* agent, guint streamId,
                                                  guint componentId, guint state,
                                                  gpointer userData) {
    (void)streamId;
    (void)componentId;
    
    auto* transport = static_cast<IceQuicTransport*>(userData);
    if (!transport) return;
    
    switch (state) {
        case NICE_COMPONENT_STATE_CONNECTED:
        case NICE_COMPONENT_STATE_READY: {
            // ICE connection established - get the selected candidate pair addresses
            NiceCandidate* localCandidate = nullptr;
            NiceCandidate* remoteCandidate = nullptr;
            
            if (nice_agent_get_selected_pair(agent, transport->mIceStreamId, 1,
                                             &localCandidate, &remoteCandidate)) {
                // Extract addresses for QUIC
                if (localCandidate && remoteCandidate) {
                    char localAddrStr[INET6_ADDRSTRLEN];
                    char peerAddrStr[INET6_ADDRSTRLEN];
                    nice_address_to_string(&localCandidate->addr, localAddrStr);
                    nice_address_to_string(&remoteCandidate->addr, peerAddrStr);
                    
                    // Check if addresses are IPv6 (contains ':')
                    bool isIPv6 = (strchr(localAddrStr, ':') != nullptr);
                    
                    if (isIPv6) {
                        // Set up IPv6 local address
                        struct sockaddr_in6* localAddr = 
                            reinterpret_cast<struct sockaddr_in6*>(&transport->mLocalAddr);
                        std::memset(localAddr, 0, sizeof(*localAddr));
                        localAddr->sin6_family = AF_INET6;
                        inet_pton(AF_INET6, localAddrStr, &localAddr->sin6_addr);
                        localAddr->sin6_port = htons(nice_address_get_port(&localCandidate->addr));
                        
                        // Set up IPv6 peer address
                        struct sockaddr_in6* peerAddr = 
                            reinterpret_cast<struct sockaddr_in6*>(&transport->mPeerAddr);
                        std::memset(peerAddr, 0, sizeof(*peerAddr));
                        peerAddr->sin6_family = AF_INET6;
                        inet_pton(AF_INET6, peerAddrStr, &peerAddr->sin6_addr);
                        peerAddr->sin6_port = htons(nice_address_get_port(&remoteCandidate->addr));
                    } else {
                        // Set up IPv4 local address
                        struct sockaddr_in* localAddr = 
                            reinterpret_cast<struct sockaddr_in*>(&transport->mLocalAddr);
                        std::memset(localAddr, 0, sizeof(*localAddr));
                        localAddr->sin_family = AF_INET;
                        inet_pton(AF_INET, localAddrStr, &localAddr->sin_addr);
                        localAddr->sin_port = htons(nice_address_get_port(&localCandidate->addr));
                        
                        // Set up IPv4 peer address
                        struct sockaddr_in* peerAddr = 
                            reinterpret_cast<struct sockaddr_in*>(&transport->mPeerAddr);
                        std::memset(peerAddr, 0, sizeof(*peerAddr));
                        peerAddr->sin_family = AF_INET;
                        inet_pton(AF_INET, peerAddrStr, &peerAddr->sin_addr);
                        peerAddr->sin_port = htons(nice_address_get_port(&remoteCandidate->addr));
                    }
                    
                    transport->mAddressesSet = true;
                }
            }
            
            // Start QUIC connection over the ICE-established path
            transport->startQuicConnection();
            break;
        }
            
        case NICE_COMPONENT_STATE_FAILED:
            // ICE connection failed
            transport->mState = TransportState::Failed;
            if (transport->mCallbacks.onFailed) {
                transport->mCallbacks.onFailed("ICE connectivity check failed");
            }
            break;
            
        case NICE_COMPONENT_STATE_DISCONNECTED:
            // ICE disconnected
            if (transport->mState == TransportState::Connected) {
                transport->mState = TransportState::Disconnected;
                if (transport->mCallbacks.onDisconnected) {
                    transport->mCallbacks.onDisconnected();
                }
            }
            break;
            
        default:
            // Other states (gathering, connecting) - no action needed
            break;
    }
}

void IceQuicTransport::onIceNewCandidate(NiceAgent* agent, NiceCandidate* candidate,
                                         gpointer userData) {
    auto* transport = static_cast<IceQuicTransport*>(userData);
    if (!transport || !candidate) return;
    
    gchar* candidateStr = nice_agent_generate_local_candidate_sdp(agent, candidate);
    if (candidateStr) {
        std::string sdp(candidateStr);
        
        {
            std::lock_guard<std::mutex> lock(transport->mCandidatesMutex);
            transport->mLocalCandidates.push_back(sdp);
        }
        
        if (transport->mCallbacks.onLocalCandidate) {
            transport->mCallbacks.onLocalCandidate(sdp);
        }
        g_free(candidateStr);
    }
}

void IceQuicTransport::onIceRecv(NiceAgent* agent, guint streamId, guint componentId,
                                 guint len, gchar* buf, gpointer userData) {
    (void)agent;
    (void)streamId;
    (void)componentId;
    
    auto* transport = static_cast<IceQuicTransport*>(userData);
    if (!transport) return;
    
    if (!transport->mQuicEngine || !transport->mAddressesSet) {
        return;
    }
    
    std::unique_lock<std::mutex> lock(transport->mQuicEngineMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        return;
    }
    
    transport->mBytesReceived += len;
    
    int ecn = 0;
    
    int result = lsquic_engine_packet_in(
        transport->mQuicEngine,
        reinterpret_cast<const unsigned char*>(buf),
        len,
        reinterpret_cast<const struct sockaddr*>(&transport->mLocalAddr),
        reinterpret_cast<const struct sockaddr*>(&transport->mPeerAddr),
        transport,  // peer_ctx
        ecn
    );
    
    lsquic_engine_process_conns(transport->mQuicEngine);
    
    if (lsquic_engine_has_unsent_packets(transport->mQuicEngine)) {
        lsquic_engine_send_unsent_packets(transport->mQuicEngine);
    }
}

// lsquic Callbacks

int IceQuicTransport::onPacketsOut(void* ctx, const lsquic_out_spec* specs, 
                                   unsigned count) {
    auto* transport = static_cast<IceQuicTransport*>(ctx);
    if (!transport || !transport->mIceAgent) return -1;
    

    
    unsigned sent = 0;
    for (unsigned i = 0; i < count; ++i) {
        const lsquic_out_spec& spec = specs[i];
        
        size_t totalLen = 0;
        for (size_t j = 0; j < spec.iovlen; ++j) {
            totalLen += spec.iov[j].iov_len;
        }
        
        std::vector<char> buffer(totalLen);
        size_t offset = 0;
        for (size_t j = 0; j < spec.iovlen; ++j) {
            std::memcpy(buffer.data() + offset, spec.iov[j].iov_base, spec.iov[j].iov_len);
            offset += spec.iov[j].iov_len;
        }
        
        // Send via libnice
        int result = nice_agent_send(transport->mIceAgent, 
                                     transport->mIceStreamId, 
                                     1,  // component ID
                                     buffer.size(),
                                     buffer.data());
        
        if (result < 0) {
            break;
        }
        
        transport->mBytesSent += result;
        ++sent;
    }
    
    return sent > 0 ? static_cast<int>(sent) : -1;
}

lsquic_conn_ctx_t* IceQuicTransport::onNewConn(void* ctx, lsquic_conn_t* conn) {
    auto* transport = static_cast<IceQuicTransport*>(ctx);
    if (!transport) return nullptr;
    
    transport->mQuicConn = conn;

    if (transport->mConfig.isServer && 
        transport->mState == TransportState::Connecting) {
        transport->mState = TransportState::Connected;
        if (transport->mCallbacks.onConnected) {
            transport->mCallbacks.onConnected();
        }
    }
    
    return reinterpret_cast<lsquic_conn_ctx_t*>(transport);
}

void IceQuicTransport::onConnClosed(lsquic_conn_t* conn) {
    lsquic_conn_ctx_t* ctx = lsquic_conn_get_ctx(conn);
    auto* transport = reinterpret_cast<IceQuicTransport*>(ctx);
    
    lsquic_conn_set_ctx(conn, nullptr);
    
    if (!transport) return;
    
    transport->mQuicConn = nullptr;
    
    {
        std::lock_guard<std::mutex> lock(transport->mStreamsMutex);
        transport->mStreams.clear();
    }
    
    TransportState expected = TransportState::Connected;
    if (transport->mState.compare_exchange_strong(expected, TransportState::Disconnected)) {
        if (transport->mCallbacks.onDisconnected) {
            transport->mCallbacks.onDisconnected();
        }
    } else {
        expected = TransportState::Connecting;
        if (transport->mState.compare_exchange_strong(expected, TransportState::Disconnected)) {
            if (transport->mCallbacks.onDisconnected) {
                transport->mCallbacks.onDisconnected();
            }
        }
    }
}

lsquic_stream_ctx_t* IceQuicTransport::onNewStream(void* ctx, lsquic_stream_t* stream) {
    auto* transport = static_cast<IceQuicTransport*>(ctx);
    if (!transport) return nullptr;
    
    uint64_t streamId = lsquic_stream_id(stream);
    
    {
        std::lock_guard<std::mutex> lock(transport->mStreamsMutex);
        transport->mStreams[streamId] = stream;
    }
    
    if (transport->mCallbacks.onStreamOpened) {
        transport->mCallbacks.onStreamOpened(streamId);
    }
    
    lsquic_stream_wantread(stream, 1);
    
    return reinterpret_cast<lsquic_stream_ctx_t*>(transport);
}

void IceQuicTransport::onStreamRead(lsquic_stream_t* stream, lsquic_stream_ctx_t* ctx) {
    auto* transport = reinterpret_cast<IceQuicTransport*>(ctx);
    if (!transport) return;
    
    uint64_t streamId = lsquic_stream_id(stream);
    
    unsigned char buf[65536];
    ssize_t nread = lsquic_stream_read(stream, buf, sizeof(buf));
    
    if (nread > 0) {
        transport->mBytesReceived += nread;
        
        if (transport->mCallbacks.onData) {
            transport->mCallbacks.onData(streamId, buf, static_cast<size_t>(nread));
        }
    } else if (nread == 0) {
        lsquic_stream_shutdown(stream, 0);
    }
}

void IceQuicTransport::onStreamWrite(lsquic_stream_t* stream, lsquic_stream_ctx_t* ctx) {
    (void)stream;
    (void)ctx;
}

void IceQuicTransport::onStreamClose(lsquic_stream_t* stream, lsquic_stream_ctx_t* ctx) {
    auto* transport = reinterpret_cast<IceQuicTransport*>(ctx);
    if (!transport) return;
    
    uint64_t streamId = lsquic_stream_id(stream);
    
    bool wasInMap = false;
    {
        std::lock_guard<std::mutex> lock(transport->mStreamsMutex);
        auto it = transport->mStreams.find(streamId);
        if (it != transport->mStreams.end()) {
            transport->mStreams.erase(it);
            wasInMap = true;
        }
    }
    
    if (wasInMap && transport->mCallbacks.onStreamClosed) {
        transport->mCallbacks.onStreamClosed(streamId);
    }
}

void IceQuicTransport::onHskDone(lsquic_conn_t* conn, enum lsquic_hsk_status status) {
    lsquic_conn_ctx_t* ctx = lsquic_conn_get_ctx(conn);
    auto* transport = reinterpret_cast<IceQuicTransport*>(ctx);
    if (!transport) return;
    
    if (status == LSQ_HSK_OK || status == LSQ_HSK_RESUMED_OK) {
        // QUIC handshake completed successfully
        transport->mState = TransportState::Connected;
        if (transport->mCallbacks.onConnected) {
            transport->mCallbacks.onConnected();
        }
    } else {
        // Handshake failed
        transport->mState = TransportState::Failed;
        if (transport->mCallbacks.onFailed) {
            transport->mCallbacks.onFailed("QUIC handshake failed");
        }
    }
}

struct ssl_ctx_st* IceQuicTransport::onGetSslCtx(void* peer_ctx, const struct sockaddr* local) {
    (void)local;
    auto* transport = static_cast<IceQuicTransport*>(peer_ctx);
    if (!transport) return nullptr;
    return transport->mSslCtx;
}

struct ssl_ctx_st* IceQuicTransport::onLookupCert(void* cert_lu_ctx, 
                                                   const struct sockaddr* local,
                                                   const char* sni) {
    (void)local;
    (void)sni;
    auto* transport = static_cast<IceQuicTransport*>(cert_lu_ctx);
    if (!transport) return nullptr;
    

    
    // Return the SSL context with our certificate
    return transport->mSslCtx;
}

} // namespace ice_quic
