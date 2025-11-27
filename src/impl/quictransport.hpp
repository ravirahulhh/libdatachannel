/**
 * Copyright (c) 2025 Your Name
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef RTC_IMPL_QUIC_TRANSPORT_H
#define RTC_IMPL_QUIC_TRANSPORT_H

#include "common.hpp"
#include "configuration.hpp"
#include "processor.hpp"
#include "queue.hpp"
#include "transport.hpp"

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>

// MsQuic 头文件
#include <msquic.h>

namespace rtc::impl {

class IceTransport;

class QuicTransport final : public Transport, public std::enable_shared_from_this<QuicTransport> {
public:
	static void Init();
	static void Cleanup();

	using amount_callback = std::function<void(uint64_t streamId, size_t amount)>;

	QuicTransport(shared_ptr<IceTransport> lower, const Configuration &config,
	              message_callback recvCallback, amount_callback bufferedAmountCallback,
	              state_callback stateChangeCallback);
	~QuicTransport();

	void onBufferedAmount(amount_callback callback);

	void start() override;
	void stop() override;
	bool send(message_ptr message) override;
	bool flush();
	
	// QUIC流管理
	uint64_t createStream();
	void closeStream(uint64_t streamId);
	uint64_t maxStream() const;

	// 统计信息
	void clearStats();
	size_t bytesSent();
	size_t bytesReceived();
	optional<std::chrono::milliseconds> rtt();
	
	struct PacketLossStats {
		size_t packetsSent = 0;
		size_t packetsLost = 0;
		size_t packetsReceived = 0;
		double lossRate = 0.0;
	};
	PacketLossStats getPacketLossStats();
	void logStats();

private:
	void connect();
	void shutdown();
	void incoming(message_ptr message) override;  // 从ICE接收UDP数据包
	bool outgoing(message_ptr message) override;  // 发送到ICE

	void doRecv();
	void doFlush();
	void enqueueRecv();
	void enqueueFlush();
	
	bool trySendQueue();
	bool trySendMessage(message_ptr message);
	void updateBufferedAmount(uint64_t streamId, ptrdiff_t delta);
	void triggerBufferedAmount(uint64_t streamId, size_t amount);

	// QUIC事件处理
	void processQuicEvents();
	void handleQuicTimeout();
	
	// MsQuic 回调函数
	static QUIC_STATUS QUIC_API ConnectionCallback(HQUIC Connection, void* Context, 
	                                                QUIC_CONNECTION_EVENT* Event);
	static QUIC_STATUS QUIC_API StreamCallback(HQUIC Stream, void* Context, 
	                                           QUIC_STREAM_EVENT* Event);
	static QUIC_STATUS QUIC_API ListenerCallback(HQUIC Listener, void* Context, 
	                                             QUIC_LISTENER_EVENT* Event);
	
	// 辅助函数
	void handleConnectionEvent(QUIC_CONNECTION_EVENT* Event);
	void handleStreamEvent(HQUIC Stream, QUIC_STREAM_EVENT* Event);
	HQUIC createMsQuicStream(uint64_t streamId);

	const size_t mMaxMessageSize;
	const bool mIsClient;
	
	// MsQuic 实例
	const QUIC_API_TABLE* mMsQuic = nullptr;
	HQUIC mRegistration = nullptr;
	HQUIC mConfiguration = nullptr;
	HQUIC mConnection = nullptr;
	HQUIC mListener = nullptr;  // 用于服务端模式
	
	// 证书配置
	QUIC_CREDENTIAL_CONFIG mCredConfig = {};
	
	// 流管理
	std::map<HQUIC, uint64_t> mStreamMap;  // MsQuic流句柄 -> 内部流ID
	std::map<uint64_t, HQUIC> mStreamIdMap;  // 内部流ID -> MsQuic流句柄
	std::mutex mStreamMutex;

	Processor mProcessor;
	std::atomic<int> mPendingRecvCount = 0;
	std::atomic<int> mPendingFlushCount = 0;
	std::mutex mRecvMutex;
	std::recursive_mutex mSendMutex;
	Queue<message_ptr> mSendQueue;
	bool mSendShutdown = false;
	std::map<uint64_t, size_t> mBufferedAmount;
	amount_callback mBufferedAmountCallback;

	std::mutex mWriteMutex;
	std::condition_variable mWrittenCondition;
	std::atomic<bool> mWritten = false;
	std::atomic<bool> mWrittenOnce = false;

	// 统计信息
	std::atomic<size_t> mBytesSent = 0, mBytesReceived = 0;
	std::atomic<size_t> mPacketsSent = 0;
	std::atomic<size_t> mPacketsReceived = 0;
	std::atomic<size_t> mPacketsLost = 0;
	std::chrono::steady_clock::time_point mLastStatsLog;
	std::chrono::milliseconds mStatsLogInterval{10000};

	// QUIC流管理
	std::atomic<uint64_t> mNextStreamId = 0;
	std::map<uint64_t, binary> mPartialStreamData;
};

} // namespace rtc::impl

#endif
