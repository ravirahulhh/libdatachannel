#ifndef ICE_QUIC_TRANSPORT_HPP
#define ICE_QUIC_TRANSPORT_HPP

#include "ice_quic_config.hpp"
#include "ice_quic_types.hpp"
#include "ice_quic_exception.hpp"

#include <lsquic.h>

#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <map>
#include <thread>
#include <cstdint>
#include <sys/socket.h>
#include <netinet/in.h>

typedef struct _GMainLoop GMainLoop;
typedef struct _GMainContext GMainContext;
typedef struct _NiceAgent NiceAgent;
typedef struct _NiceCandidate NiceCandidate;
typedef void* gpointer;
typedef char gchar;
typedef unsigned int guint;

struct ssl_ctx_st;
typedef struct ssl_ctx_st SSL_CTX;

namespace ice_quic {

class IceQuicTransport {
public:
    explicit IceQuicTransport(const IceQuicConfig& config, 
                              const IceQuicCallbacks& callbacks);
    
    ~IceQuicTransport();
    
    IceQuicTransport(const IceQuicTransport&) = delete;
    
    IceQuicTransport& operator=(const IceQuicTransport&) = delete;
    
    void gatherCandidates();
    
    bool addRemoteCandidate(const std::string& candidateSdp);
    
    void setRemoteCredentials(const std::string& ufrag, const std::string& pwd);
    
    void setRemoteDescription(const IceDescription& desc);
    
    void endOfRemoteCandidates();
    
    std::pair<std::string, std::string> getLocalCredentials() const;
    
    IceDescription getLocalDescription() const;
    
    uint64_t openStream();
    
    void closeStream(uint64_t streamId);
    
    uint32_t getMaxStreams() const;
    
    bool send(uint64_t streamId, const uint8_t* data, size_t len);
    
    void close();
    
    TransportState getState() const;
    
    uint32_t getRtt() const;
    
    size_t getBytesSent() const;
    
    size_t getBytesReceived() const;
    
    bool isServerMode() const;

private:
    GMainContext* mMainContext;
    GMainLoop* mMainLoop;
    NiceAgent* mIceAgent;
    guint mIceStreamId;
    std::string mLocalUfrag;
    std::string mLocalPwd;
    std::vector<std::string> mLocalCandidates;
    mutable std::mutex mCandidatesMutex;
    std::thread mMainLoopThread;
    
    lsquic_engine_t* mQuicEngine;
    lsquic_conn_t* mQuicConn;
    SSL_CTX* mSslCtx;
    std::thread mQuicEventThread;
    std::atomic<bool> mQuicRunning;
    
    struct sockaddr_storage mLocalAddr;
    struct sockaddr_storage mPeerAddr;
    bool mAddressesSet;
    
    std::map<uint64_t, lsquic_stream_t*> mStreams;
    std::mutex mStreamsMutex;
    std::atomic<uint64_t> mNextStreamId;
    
    std::mutex mQuicEngineMutex;
    
    std::atomic<TransportState> mState;
    IceQuicConfig mConfig;
    IceQuicCallbacks mCallbacks;
    
    std::atomic<size_t> mBytesSent;
    std::atomic<size_t> mBytesReceived;
    
    void initIce();
    void cleanupIce();
    void runMainLoop();
    void initQuic();
    void cleanupQuic();
    void runQuicEventLoop();
    void processQuicEvents();
    void startQuicConnection();
    
    static void onIceCandidateGatheringDone(NiceAgent* agent, guint streamId, 
                                            gpointer userData);
    static void onIceComponentStateChanged(NiceAgent* agent, guint streamId,
                                           guint componentId, guint state,
                                           gpointer userData);
    static void onIceNewCandidate(NiceAgent* agent, NiceCandidate* candidate,
                                  gpointer userData);
    static void onIceRecv(NiceAgent* agent, guint streamId, guint componentId,
                          guint len, gchar* buf, gpointer userData);
    
    static int onPacketsOut(void* ctx, const lsquic_out_spec* specs, 
                            unsigned count);
    static lsquic_conn_ctx_t* onNewConn(void* ctx, lsquic_conn_t* conn);
    static void onConnClosed(lsquic_conn_t* conn);
    static lsquic_stream_ctx_t* onNewStream(void* ctx, lsquic_stream_t* stream);
    static void onStreamRead(lsquic_stream_t* stream, lsquic_stream_ctx_t* ctx);
    static void onStreamWrite(lsquic_stream_t* stream, lsquic_stream_ctx_t* ctx);
    static void onStreamClose(lsquic_stream_t* stream, lsquic_stream_ctx_t* ctx);
    static void onHskDone(lsquic_conn_t* conn, enum lsquic_hsk_status status);
    static struct ssl_ctx_st* onGetSslCtx(void* peer_ctx, const struct sockaddr* local);
    static struct ssl_ctx_st* onLookupCert(void* cert_lu_ctx, 
                                           const struct sockaddr* local,
                                           const char* sni);
};

}

#endif
