# Requirements Document

## Introduction

本文档定义了对现有 ICE+QUIC P2P 传输层库的优化需求。优化目标是：
1. 充分利用 libnice 的 SDP 解析能力，简化候选地址处理
2. 精简代码逻辑，专注于 P2P 场景
3. 明确使用 QUIC 层的 stream 进行多路复用，ICE 层只负责单一 UDP 通道

核心设计原则：
- libnice 负责 ICE 协商和 NAT 穿透，提供单一可靠的 UDP 通道
- lsquic 负责 QUIC 协议，在该 UDP 通道上提供多路复用的 stream
- 简化 API，移除冗余功能

## Glossary

- **IceQuicTransport**: 基于 libnice 和 lsquic 的 P2P 传输层组件
- **IceQuicConfig**: 传输层配置结构
- **ICE**: Interactive Connectivity Establishment，NAT 穿透协议（RFC 8445）
- **QUIC**: 基于 UDP 的多路复用安全传输协议（RFC 9000）
- **libnice**: GNOME 项目的 ICE 实现库，支持完整的 SDP 解析
- **lsquic**: LiteSpeed QUIC 库
- **SDP**: Session Description Protocol，用于描述 ICE 候选地址
- **Stream**: QUIC 协议中的逻辑数据流
- **Component**: ICE 中的数据通道，本设计只使用单个 component
- **TransportState**: 传输层状态枚举

## Requirements

### Requirement 1

**User Story:** As a developer, I want to use libnice's native SDP parsing capabilities, so that I can simplify candidate handling and reduce code complexity.

#### Acceptance Criteria

1. WHEN addRemoteCandidate is called with an SDP string THEN the IceQuicTransport SHALL use nice_agent_parse_remote_candidate_sdp to parse the candidate
2. WHEN generating local candidates THEN the IceQuicTransport SHALL use nice_agent_generate_local_candidate_sdp to format candidates
3. WHEN setRemoteSdp is called with a complete SDP offer/answer THEN the IceQuicTransport SHALL use nice_agent_parse_remote_sdp to parse all candidates at once
4. WHEN getLocalSdp is called THEN the IceQuicTransport SHALL use nice_agent_generate_local_sdp to generate a complete SDP description

### Requirement 2

**User Story:** As a developer, I want a simplified configuration structure, so that I can quickly set up P2P connections without unnecessary options.

#### Acceptance Criteria

1. THE IceQuicConfig SHALL provide a single stunServers field as a vector of server addresses
2. THE IceQuicConfig SHALL provide idleTimeoutMs for QUIC connection timeout
3. THE IceQuicConfig SHALL provide maxStreams for maximum concurrent QUIC streams
4. THE IceQuicConfig SHALL provide isServer to specify client or server role
5. THE IceQuicConfig SHALL provide optional certPath and keyPath for server TLS configuration

### Requirement 3

**User Story:** As a developer, I want the ICE layer to provide only a single UDP channel, so that all multiplexing is handled by QUIC streams.

#### Acceptance Criteria

1. WHEN IceQuicTransport is initialized THEN the IceQuicTransport SHALL create exactly one ICE stream with one component
2. THE IceQuicTransport SHALL NOT expose ICE stream or component IDs to the application
3. WHEN ICE connection is established THEN the IceQuicTransport SHALL route all QUIC packets through the single ICE channel
4. THE IceQuicTransport SHALL use QUIC streams for all application-level multiplexing

### Requirement 4

**User Story:** As a developer, I want simplified ICE credential exchange, so that I can easily integrate with signaling systems.

#### Acceptance Criteria

1. WHEN getLocalDescription is called THEN the IceQuicTransport SHALL return a structure containing ufrag, pwd, and candidates
2. WHEN setRemoteDescription is called THEN the IceQuicTransport SHALL configure ICE with the provided ufrag, pwd, and candidates
3. THE IceQuicTransport SHALL support trickle ICE by allowing candidates to be added incrementally via addRemoteCandidate

### Requirement 5

**User Story:** As a developer, I want to send and receive data over QUIC streams, so that I can exchange application data with peers.

#### Acceptance Criteria

1. WHEN openStream is called THEN the IceQuicTransport SHALL create a new bidirectional QUIC stream and return its ID
2. WHEN send is called with stream ID and data THEN the IceQuicTransport SHALL transmit the data on the specified stream
3. WHEN data is received on a stream THEN the IceQuicTransport SHALL invoke the onData callback with stream ID and data
4. WHEN closeStream is called THEN the IceQuicTransport SHALL gracefully close the specified stream

### Requirement 6

**User Story:** As a developer, I want clear connection lifecycle management, so that I can handle connection states properly.

#### Acceptance Criteria

1. WHEN gatherCandidates is called THEN the IceQuicTransport SHALL transition to Gathering state
2. WHEN candidate gathering completes THEN the IceQuicTransport SHALL invoke onGatheringComplete callback
3. WHEN ICE and QUIC handshakes complete THEN the IceQuicTransport SHALL transition to Connected state and invoke onConnected callback
4. WHEN close is called THEN the IceQuicTransport SHALL send QUIC CONNECTION_CLOSE and transition to Disconnected state
5. IF ICE or QUIC fails THEN the IceQuicTransport SHALL transition to Failed state and invoke onFailed callback

### Requirement 7

**User Story:** As a developer, I want to monitor connection statistics, so that I can assess connection quality.

#### Acceptance Criteria

1. WHEN getRtt is called THEN the IceQuicTransport SHALL return the current round-trip time in milliseconds
2. WHEN getBytesSent is called THEN the IceQuicTransport SHALL return the total bytes sent
3. WHEN getBytesReceived is called THEN the IceQuicTransport SHALL return the total bytes received
4. WHEN getState is called THEN the IceQuicTransport SHALL return the current TransportState

### Requirement 8

**User Story:** As a developer, I want working examples demonstrating the optimized API, so that I can understand how to use the library.

#### Acceptance Criteria

1. THE library SHALL provide an echo server example using the simplified API
2. THE library SHALL provide an echo client example using the simplified API
3. THE library SHALL provide a loopback test demonstrating in-process P2P connection
4. THE examples SHALL demonstrate SDP-based candidate exchange

