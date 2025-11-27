/**
 * Copyright (c) 2025 Your Name
 * QUIC Transport Implementation using Microsoft MsQuic
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "quictransport.hpp"
#include "icetransport.hpp"
#include "internals.hpp"
#include "logcounter.hpp"
#include "message.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <cstring>

using namespace std::chrono_literals;
using namespace std::chrono;

namespace rtc::impl {

// MsQuic 全局 API 表
static const QUIC_API_TABLE* g_MsQuic = nullptr;
static std::atomic<int> g_MsQuicRefCount = 0;
static std::mutex g_MsQuicMutex;

void QuicTransport::Init() {
	std::lock_guard<std::mutex> lock(g_MsQuicMutex);
	
	if (g_MsQuicRefCount++ == 0) {
		PLOG_DEBUG << "Initializing MsQuic";
		
		QUIC_STATUS status = MsQuicOpen2(&g_MsQuic);
		if (QUIC_FAILED(status)) {
			PLOG_ERROR << "MsQuicOpen2 failed: 0x" << std::hex << status;
			throw std::runtime_error("Failed to initialize MsQuic");
		}
		
		PLOG_INFO << "MsQuic initialized successfully";
	}
}

void QuicTransport::Cleanup() {
	std::lock_guard<std::mutex> lock(g_MsQuicMutex);
	
	if (--g_MsQuicRefCount == 0) {
		PLOG_DEBUG << "Cleaning up MsQuic";
		
		if (g_MsQuic) {
			MsQuicClose(g_MsQuic);
			g_MsQuic = nullptr;
		}
	}
}

QuicTransport::QuicTransport(shared_ptr<IceTransport> lower, const Configuration &config,
                             message_callback recvCallback, amount_callback bufferedAmountCallback,
                             state_callback stateChangeCallback)
    : Transport(lower, std::move(stateChangeCallback)),
      mMaxMessageSize(config.maxMessageSize.value_or(DEFAULT_LOCAL_MAX_MESSAGE_SIZE)),
      mIsClient(true), // 根据 ICE 角色确定
      mSendQueue(0, message_size_func),
      mBufferedAmountCallback(std::move(bufferedAmountCallback)),
      mLastStatsLog(std::chrono::steady_clock::now()),
      mMsQuic(g_MsQuic) {
	
	onRecv(std::move(recvCallback));

	PLOG_DEBUG << "Initializing QUIC transport with MsQuic";

	if (!mMsQuic) {
		throw std::runtime_error("MsQuic not initialized. Call QuicTransport::Init() first.");
	}

	// 创建 Registration
	const QUIC_REGISTRATION_CONFIG regConfig = {
		"libdatachannel-quic",
		QUIC_EXECUTION_PROFILE_LOW_LATENCY
	};
	
	QUIC_STATUS status = mMsQuic->RegistrationOpen(&regConfig, &mRegistration);
	if (QUIC_FAILED(status)) {
		throw std::runtime_error("Failed to create MsQuic registration");
	}

	// 配置 ALPN
	const QUIC_BUFFER alpn = { sizeof("webrtc-quic") - 1, (uint8_t*)"webrtc-quic" };

	// 创建 Configuration
	QUIC_SETTINGS settings = {};
	settings.IdleTimeoutMs = 30000;  // 30秒空闲超时
	settings.IsSet.IdleTimeoutMs = TRUE;
	settings.PeerBidiStreamCount = 100;
	settings.IsSet.PeerBidiStreamCount = TRUE;
	settings.PeerUnidiStreamCount = 100;
	settings.IsSet.PeerUnidiStreamCount = TRUE;
	
	// TODO: 设置拥塞控制算法
	// 需要在 Configuration 中添加 quicSettings
	// if (config.quicSettings.congestionControl == QuicSettings::CongestionControl::BBR) {
	// 	settings.CongestionControlAlgorithm = QUIC_CONGESTION_CONTROL_ALGORITHM_BBR;
	// 	settings.IsSet.CongestionControlAlgorithm = TRUE;
	// }
	
	// 默认使用 Cubic (BBR 可能不可用)
	settings.CongestionControlAlgorithm = QUIC_CONGESTION_CONTROL_ALGORITHM_CUBIC;
	settings.IsSet.CongestionControlAlgorithm = TRUE;
	
	// 初始拥塞窗口
	settings.InitialWindowPackets = 10;
	settings.IsSet.InitialWindowPackets = TRUE;

	status = mMsQuic->ConfigurationOpen(mRegistration, &alpn, 1, &settings, 
	                                    sizeof(settings), nullptr, &mConfiguration);
	if (QUIC_FAILED(status)) {
		mMsQuic->RegistrationClose(mRegistration);
		throw std::runtime_error("Failed to create MsQuic configuration");
	}

	// 配置证书（自签名用于测试）
	mCredConfig.Type = QUIC_CREDENTIAL_TYPE_NONE;
	mCredConfig.Flags = QUIC_CREDENTIAL_FLAG_CLIENT;
	if (!mIsClient) {
		mCredConfig.Flags = QUIC_CREDENTIAL_FLAG_NONE;
		// 服务端需要证书，这里简化处理
		// 实际应用中应该使用真实证书
	}

	status = mMsQuic->ConfigurationLoadCredential(mConfiguration, &mCredConfig);
	if (QUIC_FAILED(status)) {
		PLOG_WARNING << "Failed to load credentials: 0x" << std::hex << status;
	}

	PLOG_INFO << "MsQuic transport initialized";
}

QuicTransport::~QuicTransport() {
	PLOG_DEBUG << "Destroying QUIC transport";

	mProcessor.join();

	mWrittenOnce = true;
	mWrittenCondition.notify_all();

	unregisterIncoming();

	// 清理 MsQuic 资源
	if (mConnection) {
		mMsQuic->ConnectionClose(mConnection);
		mConnection = nullptr;
	}
	
	if (mListener) {
		mMsQuic->ListenerClose(mListener);
		mListener = nullptr;
	}

	if (mConfiguration) {
		mMsQuic->ConfigurationClose(mConfiguration);
		mConfiguration = nullptr;
	}

	if (mRegistration) {
		mMsQuic->RegistrationClose(mRegistration);
		mRegistration = nullptr;
	}
}

void QuicTransport::onBufferedAmount(amount_callback callback) {
	mBufferedAmountCallback = std::move(callback);
}

void QuicTransport::start() {
	PLOG_DEBUG << "Starting QUIC transport";
	registerIncoming();
	connect();
}

void QuicTransport::stop() {
	PLOG_DEBUG << "Stopping QUIC transport";
	shutdown();
}

void QuicTransport::connect() {
	PLOG_DEBUG << "QUIC connecting over ICE";
	changeState(State::Connecting);

	// 创建 QUIC 连接
	QUIC_STATUS status = mMsQuic->ConnectionOpen(mRegistration, ConnectionCallback, 
	                                             this, &mConnection);
	if (QUIC_FAILED(status)) {
		PLOG_ERROR << "Failed to create connection: 0x" << std::hex << status;
		changeState(State::Failed);
		return;
	}

	// 设置自定义发送回调（通过 ICE 发送）
	// MsQuic 默认使用系统 UDP socket，我们需要拦截发送
	// 这里使用 QUIC_PARAM_CONN_DISABLE_1RTT_ENCRYPTION 等参数来控制
	
	// 启动连接
	if (mIsClient) {
		// 客户端模式：主动连接
		status = mMsQuic->ConnectionStart(mConnection, mConfiguration, 
		                                  QUIC_ADDRESS_FAMILY_UNSPEC, 
		                                  "localhost", 4433);
		if (QUIC_FAILED(status)) {
			PLOG_ERROR << "Failed to start connection: 0x" << std::hex << status;
			changeState(State::Failed);
			return;
		}
	} else {
		// 服务端模式：创建监听器
		QUIC_ADDR addr = {};
		QuicAddrSetFamily(&addr, QUIC_ADDRESS_FAMILY_UNSPEC);
		QuicAddrSetPort(&addr, 4433);
		
		status = mMsQuic->ListenerOpen(mRegistration, ListenerCallback, 
		                               this, &mListener);
		if (QUIC_FAILED(status)) {
			PLOG_ERROR << "Failed to create listener: 0x" << std::hex << status;
			changeState(State::Failed);
			return;
		}
		
		// 重新定义 alpn（在 if 块内）
		const QUIC_BUFFER alpnListener = { sizeof("webrtc-quic") - 1, (uint8_t*)"webrtc-quic" };
		status = mMsQuic->ListenerStart(mListener, &alpnListener, 1, &addr);
		if (QUIC_FAILED(status)) {
			PLOG_ERROR << "Failed to start listener: 0x" << std::hex << status;
			changeState(State::Failed);
			return;
		}
	}
}

void QuicTransport::shutdown() {
	mSendQueue.stop();
	
	if (state() == State::Connected && mConnection) {
		PLOG_DEBUG << "QUIC graceful shutdown";
		
		// 发送 CONNECTION_CLOSE
		mMsQuic->ConnectionShutdown(mConnection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
		
		changeState(State::Disconnected);
	}
}

bool QuicTransport::send(message_ptr message) {
	std::lock_guard lock(mSendMutex);
	if (state() != State::Connected)
		return false;

	if (!message)
		return trySendQueue();

	PLOG_VERBOSE << "QUIC send size=" << message->size();

	if (message->size() > mMaxMessageSize)
		throw std::invalid_argument("Message is too large");

	// 尝试直接发送
	if (trySendQueue() && trySendMessage(message))
		return true;

	// 加入发送队列
	mSendQueue.push(message);
	updateBufferedAmount(message->stream, ptrdiff_t(message_size_func(message)));
	return false;
}

bool QuicTransport::flush() {
	try {
		std::lock_guard lock(mSendMutex);
		if (state() != State::Connected)
			return false;

		trySendQueue();
		return true;

	} catch (const std::exception &e) {
		PLOG_WARNING << "QUIC flush: " << e.what();
		return false;
	}
}

uint64_t QuicTransport::createStream() {
	std::lock_guard lock(mSendMutex);
	
	uint64_t streamId = mNextStreamId;
	mNextStreamId += 4; // 双向流
	
	PLOG_DEBUG << "Created QUIC stream " << streamId;
	return streamId;
}

void QuicTransport::closeStream(uint64_t streamId) {
	std::lock_guard lock(mSendMutex);
	std::lock_guard streamLock(mStreamMutex);
	
	PLOG_DEBUG << "Closing QUIC stream " << streamId;
	
	auto it = mStreamIdMap.find(streamId);
	if (it != mStreamIdMap.end()) {
		HQUIC stream = it->second;
		mMsQuic->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL, 0);
	}
	
	mProcessor.enqueue(&QuicTransport::flush, shared_from_this());
}

uint64_t QuicTransport::maxStream() const {
	return 100; // 可以从 MsQuic 配置中获取
}

void QuicTransport::incoming(message_ptr message) {
	// 从 ICE 接收 UDP 数据包
	// 注意：MsQuic 默认使用自己的 UDP socket
	// 要通过 ICE 发送/接收，需要使用 MsQuic 的自定义数据路径功能
	// 这需要使用 QUIC_PARAM_CONN_SEND_CALLBACK 等参数
	
	if (!mWrittenOnce) {
		std::unique_lock lock(mWriteMutex);
		mWrittenCondition.wait(lock, [&]() { 
			return mWrittenOnce || state() == State::Failed; 
		});
	}

	if (state() == State::Failed)
		return;

	if (!message) {
		PLOG_INFO << "QUIC disconnected (ICE closed)";
		changeState(State::Disconnected);
		recv(nullptr);
		return;
	}

	PLOG_VERBOSE << "QUIC incoming packet size=" << message->size();
	++mPacketsReceived;

	// TODO: 将 UDP 包注入到 MsQuic
	// 这需要使用 MsQuic 的自定义数据路径 API
	// 目前 MsQuic 主要设计为使用系统 UDP socket
	// 需要研究 QUIC_PARAM_CONN_SEND_CALLBACK 等高级功能
}

bool QuicTransport::outgoing(message_ptr message) {
	// 发送到 ICE（UDP 包）
	message->dscp = 10; // AF11
	return Transport::outgoing(std::move(message));
}

void QuicTransport::doRecv() {
	std::lock_guard lock(mRecvMutex);
	--mPendingRecvCount;
	
	try {
		processQuicEvents();
	} catch (const std::exception &e) {
		PLOG_WARNING << "QUIC recv: " << e.what();
	}
}

void QuicTransport::doFlush() {
	std::lock_guard lock(mSendMutex);
	--mPendingFlushCount;
	
	try {
		trySendQueue();
		processQuicEvents();
	} catch (const std::exception &e) {
		PLOG_WARNING << "QUIC flush: " << e.what();
	}
}

void QuicTransport::enqueueRecv() {
	if (mPendingRecvCount > 0)
		return;

	if (auto shared_this = weak_from_this().lock()) {
		++mPendingRecvCount;
		mProcessor.enqueue(&QuicTransport::doRecv, std::move(shared_this));
	}
}

void QuicTransport::enqueueFlush() {
	if (mPendingFlushCount > 0)
		return;

	if (auto shared_this = weak_from_this().lock()) {
		++mPendingFlushCount;
		mProcessor.enqueue(&QuicTransport::doFlush, std::move(shared_this));
	}
}

bool QuicTransport::trySendQueue() {
	// 需要持有 mSendMutex
	while (auto next = mSendQueue.peek()) {
		message_ptr message = std::move(*next);
		if (!trySendMessage(message))
			return false;

		mSendQueue.pop();
		updateBufferedAmount(message->stream, -ptrdiff_t(message_size_func(message)));
	}

	if (!mSendQueue.running() && !std::exchange(mSendShutdown, true)) {
		shutdown();
	}

	return true;
}

bool QuicTransport::trySendMessage(message_ptr message) {
	// 需要持有 mSendMutex
	if (state() != State::Connected || !mConnection)
		return false;

	PLOG_VERBOSE << "QUIC try send stream=" << message->stream 
	             << " size=" << message->size();

	// 获取或创建 MsQuic 流
	HQUIC stream = createMsQuicStream(message->stream);
	if (!stream) {
		PLOG_ERROR << "Failed to create MsQuic stream";
		return false;
	}

	// 发送数据
	QUIC_BUFFER buffer;
	buffer.Length = static_cast<uint32_t>(message->size());
	buffer.Buffer = reinterpret_cast<uint8_t*>(message->data());

	QUIC_SEND_FLAGS flags = QUIC_SEND_FLAG_NONE;
	if (message->type == Message::Reset) {
		flags = QUIC_SEND_FLAG_FIN;
	}

	QUIC_STATUS status = mMsQuic->StreamSend(stream, &buffer, 1, flags, message.get());
	if (QUIC_FAILED(status)) {
		PLOG_ERROR << "StreamSend failed: 0x" << std::hex << status;
		return false;
	}

	mBytesSent += message->size();
	return true;
}

HQUIC QuicTransport::createMsQuicStream(uint64_t streamId) {
	std::lock_guard lock(mStreamMutex);
	
	// 检查是否已存在
	auto it = mStreamIdMap.find(streamId);
	if (it != mStreamIdMap.end()) {
		return it->second;
	}

	// 创建新流
	HQUIC stream = nullptr;
	QUIC_STATUS status = mMsQuic->StreamOpen(mConnection, QUIC_STREAM_OPEN_FLAG_NONE,
	                                         StreamCallback, this, &stream);
	if (QUIC_FAILED(status)) {
		PLOG_ERROR << "Failed to open stream: 0x" << std::hex << status;
		return nullptr;
	}

	// 启动流
	status = mMsQuic->StreamStart(stream, QUIC_STREAM_START_FLAG_NONE);
	if (QUIC_FAILED(status)) {
		PLOG_ERROR << "Failed to start stream: 0x" << std::hex << status;
		mMsQuic->StreamClose(stream);
		return nullptr;
	}

	// 保存映射
	mStreamMap[stream] = streamId;
	mStreamIdMap[streamId] = stream;

	PLOG_DEBUG << "Created MsQuic stream " << streamId;
	return stream;
}

void QuicTransport::processQuicEvents() {
	// MsQuic 使用事件驱动模型，不需要显式的事件循环
	// 所有事件通过回调函数处理
	
	// 定期记录统计信息
	logStats();
}

void QuicTransport::handleQuicTimeout() {
	// MsQuic 内部处理超时
}

void QuicTransport::updateBufferedAmount(uint64_t streamId, ptrdiff_t delta) {
	// 需要持有 mSendMutex
	if (delta == 0)
		return;

	auto it = mBufferedAmount.insert(std::make_pair(streamId, 0)).first;
	size_t amount = size_t(std::max(ptrdiff_t(it->second) + delta, ptrdiff_t(0)));
	
	if (amount == 0)
		mBufferedAmount.erase(it);
	else
		it->second = amount;

	triggerBufferedAmount(streamId, amount);
}

void QuicTransport::triggerBufferedAmount(uint64_t streamId, size_t amount) {
	try {
		mBufferedAmountCallback(streamId, amount);
	} catch (const std::exception &e) {
		PLOG_WARNING << "QUIC buffered amount callback: " << e.what();
	}
}

void QuicTransport::clearStats() {
	mBytesReceived = 0;
	mBytesSent = 0;
	mPacketsSent = 0;
	mPacketsReceived = 0;
	mPacketsLost = 0;
	mLastStatsLog = std::chrono::steady_clock::now();
}

size_t QuicTransport::bytesSent() { 
	return mBytesSent; 
}

size_t QuicTransport::bytesReceived() { 
	return mBytesReceived; 
}

optional<milliseconds> QuicTransport::rtt() {
	if (state() != State::Connected || !mConnection)
		return nullopt;

	// 获取 MsQuic 统计信息
	QUIC_STATISTICS stats = {};
	uint32_t statsSize = sizeof(stats);
	
	QUIC_STATUS status = mMsQuic->GetParam(mConnection, QUIC_PARAM_CONN_STATISTICS,
	                                       &statsSize, &stats);
	if (QUIC_FAILED(status)) {
		return nullopt;
	}

	return milliseconds(stats.Rtt / 1000); // 转换为毫秒
}

QuicTransport::PacketLossStats QuicTransport::getPacketLossStats() {
	PacketLossStats stats;
	
	stats.packetsSent = mPacketsSent;
	stats.packetsReceived = mPacketsReceived;
	
	if (mConnection) {
		QUIC_STATISTICS quicStats = {};
		uint32_t statsSize = sizeof(quicStats);
		
		QUIC_STATUS status = mMsQuic->GetParam(mConnection, QUIC_PARAM_CONN_STATISTICS,
		                                       &statsSize, &quicStats);
		if (QUIC_SUCCEEDED(status)) {
			stats.packetsLost = quicStats.Send.TotalPackets - quicStats.Send.SuspectedLostPackets;
			mPacketsLost = stats.packetsLost;
		}
	}
	
	stats.packetsLost = mPacketsLost;
	
	if (stats.packetsSent > 0) {
		stats.lossRate = (static_cast<double>(stats.packetsLost) / 
		                  static_cast<double>(stats.packetsSent)) * 100.0;
	} else {
		stats.lossRate = 0.0;
	}
	
	return stats;
}

void QuicTransport::logStats() {
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - mLastStatsLog);
	
	if (elapsed < mStatsLogInterval)
		return;
	
	mLastStatsLog = now;
	
	auto stats = getPacketLossStats();
	auto currentRtt = rtt();
	
	PLOG_INFO << "QUIC Stats (MsQuic): "
	          << "Sent=" << stats.packetsSent << " pkts, "
	          << "Recv=" << stats.packetsReceived << " pkts, "
	          << "Lost=" << stats.packetsLost << " pkts, "
	          << "Loss=" << std::fixed << std::setprecision(2) << stats.lossRate << "%, "
	          << "RTT=" << (currentRtt ? std::to_string(currentRtt->count()) + "ms" : "N/A")
	          << ", BytesSent=" << mBytesSent << ", BytesRecv=" << mBytesReceived;
}

// ============================================================================
// MsQuic 回调函数
// ============================================================================

QUIC_STATUS QUIC_API QuicTransport::ConnectionCallback(HQUIC /* Connection */, void* Context, 
                                                        QUIC_CONNECTION_EVENT* Event) {
	auto* transport = static_cast<QuicTransport*>(Context);
	if (!transport) {
		return QUIC_STATUS_INVALID_STATE;
	}

	transport->handleConnectionEvent(Event);
	return QUIC_STATUS_SUCCESS;
}

void QuicTransport::handleConnectionEvent(QUIC_CONNECTION_EVENT* Event) {
	switch (Event->Type) {
	case QUIC_CONNECTION_EVENT_CONNECTED:
		PLOG_INFO << "QUIC connection established";
		changeState(State::Connected);
		mWrittenOnce = true;
		mWrittenCondition.notify_all();
		break;

	case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
		PLOG_INFO << "QUIC connection shutdown by transport, status=0x" 
		          << std::hex << Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status;
		changeState(State::Disconnected);
		recv(nullptr);
		break;

	case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
		PLOG_INFO << "QUIC connection shutdown by peer, error=" 
		          << Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode;
		changeState(State::Disconnected);
		recv(nullptr);
		break;

	case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
		PLOG_DEBUG << "QUIC connection shutdown complete";
		changeState(State::Disconnected);
		break;

	case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
		PLOG_DEBUG << "Peer started stream";
		// 对端创建了新流
		mMsQuic->SetCallbackHandler(Event->PEER_STREAM_STARTED.Stream, 
		                            (void*)StreamCallback, this);
		break;

	case QUIC_CONNECTION_EVENT_RESUMED:
		PLOG_DEBUG << "QUIC connection resumed (0-RTT)";
		break;

	case QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED:
		PLOG_VERBOSE << "Datagram state changed";
		break;

	case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED:
		PLOG_VERBOSE << "Datagram received";
		// 可以用于实现不可靠传输
		break;

	case QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED:
		PLOG_VERBOSE << "Datagram send state changed";
		break;

	case QUIC_CONNECTION_EVENT_STREAMS_AVAILABLE:
		PLOG_VERBOSE << "Streams available: bidi=" 
		             << Event->STREAMS_AVAILABLE.BidirectionalCount
		             << ", unidi=" << Event->STREAMS_AVAILABLE.UnidirectionalCount;
		break;

	default:
		PLOG_VERBOSE << "Unhandled connection event: " << Event->Type;
		break;
	}
}

QUIC_STATUS QUIC_API QuicTransport::StreamCallback(HQUIC Stream, void* Context, 
                                                    QUIC_STREAM_EVENT* Event) {
	auto* transport = static_cast<QuicTransport*>(Context);
	if (!transport) {
		return QUIC_STATUS_INVALID_STATE;
	}
	(void)Stream; // 标记为已使用

	transport->handleStreamEvent(Stream, Event);
	return QUIC_STATUS_SUCCESS;
}

void QuicTransport::handleStreamEvent(HQUIC Stream, QUIC_STREAM_EVENT* Event) {
	std::lock_guard lock(mStreamMutex);
	
	// 获取流 ID
	uint64_t streamId = 0;
	auto it = mStreamMap.find(Stream);
	if (it != mStreamMap.end()) {
		streamId = it->second;
	}

	switch (Event->Type) {
	case QUIC_STREAM_EVENT_START_COMPLETE:
		PLOG_VERBOSE << "Stream " << streamId << " start complete";
		break;

	case QUIC_STREAM_EVENT_RECEIVE: {
		PLOG_VERBOSE << "Stream " << streamId << " received " 
		             << Event->RECEIVE.TotalBufferLength << " bytes";
		
		// 接收数据
		for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; i++) {
			const QUIC_BUFFER* buffer = &Event->RECEIVE.Buffers[i];
			
			// 创建消息并传递给上层
			auto msg = make_message(reinterpret_cast<byte*>(buffer->Buffer), 
			                       reinterpret_cast<byte*>(buffer->Buffer) + buffer->Length,
			                       Message::Binary, streamId);
			mBytesReceived += buffer->Length;
			recv(std::move(msg));
		}
		break;
	}

	case QUIC_STREAM_EVENT_SEND_COMPLETE: {
		PLOG_VERBOSE << "Stream " << streamId << " send complete";
		// 发送完成，可以更新缓冲区统计
		// ClientContext 包含发送的消息指针，这里不需要处理
		break;
	}

	case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
		PLOG_DEBUG << "Stream " << streamId << " peer send shutdown";
		break;

	case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
		PLOG_WARNING << "Stream " << streamId << " peer send aborted, error=" 
		             << Event->PEER_SEND_ABORTED.ErrorCode;
		break;

	case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
		PLOG_WARNING << "Stream " << streamId << " peer receive aborted, error=" 
		             << Event->PEER_RECEIVE_ABORTED.ErrorCode;
		break;

	case QUIC_STREAM_EVENT_SEND_SHUTDOWN_COMPLETE:
		PLOG_DEBUG << "Stream " << streamId << " send shutdown complete";
		break;

	case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE: {
		PLOG_DEBUG << "Stream " << streamId << " shutdown complete";
		
		// 清理流映射
		mStreamMap.erase(Stream);
		if (streamId != 0) {
			mStreamIdMap.erase(streamId);
		}
		
		// 关闭流
		mMsQuic->StreamClose(Stream);
		
		// 通知上层流已关闭
		auto msg = make_message(0, Message::Reset, streamId);
		recv(std::move(msg));
		break;
	}

	case QUIC_STREAM_EVENT_IDEAL_SEND_BUFFER_SIZE: {
		PLOG_VERBOSE << "Stream " << streamId << " ideal send buffer size=" 
		             << Event->IDEAL_SEND_BUFFER_SIZE.ByteCount;
		break;
	}

	default:
		PLOG_VERBOSE << "Unhandled stream event: " << Event->Type;
		break;
	}
}

QUIC_STATUS QUIC_API QuicTransport::ListenerCallback(HQUIC /* Listener */, void* Context, 
                                                      QUIC_LISTENER_EVENT* Event) {
	auto* transport = static_cast<QuicTransport*>(Context);
	if (!transport) {
		return QUIC_STATUS_INVALID_STATE;
	}

	switch (Event->Type) {
	case QUIC_LISTENER_EVENT_NEW_CONNECTION:
		PLOG_INFO << "New QUIC connection incoming";
		
		// 接受连接
		transport->mConnection = Event->NEW_CONNECTION.Connection;
		transport->mMsQuic->SetCallbackHandler(transport->mConnection, 
		                                       (void*)ConnectionCallback, transport);
		
		// 配置连接
		transport->mMsQuic->ConnectionSetConfiguration(transport->mConnection, 
		                                               transport->mConfiguration);
		return QUIC_STATUS_SUCCESS;

	case QUIC_LISTENER_EVENT_STOP_COMPLETE:
		PLOG_DEBUG << "Listener stopped";
		break;

	default:
		PLOG_VERBOSE << "Unhandled listener event: " << Event->Type;
		break;
	}

	return QUIC_STATUS_SUCCESS;
}

} // namespace rtc::impl
