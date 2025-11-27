# Requirements Document

## Introduction

本文档定义了一个独立的基于 ICE + QUIC 的 P2P 传输层库的需求规格。该传输层使用 libnice 进行 ICE（Interactive Connectivity Establishment）NAT 穿透，使用 lsquic 进行 QUIC 协议传输。

核心设计思路：
1. libnice 创建 ICE agent 进行 NAT 穿透
2. ICE 完成连接后获得 UDP socket fd
3. 将 socket fd 传递给 lsquic 进行 QUIC 通信

该库是完全独立的，拥有自己的配置系统和 API，不依赖 libdatachannel 的任何组件。

## Glossary

- **IceQuicTransport**: 基于 libnice 和 lsquic 的独立 P2P 传输层组件
- **IceQuicConfig**: 传输层配置结构，包含 ICE 和 QUIC 相关设置
- **ICE**: Interactive Connectivity Establishment，用于 NAT 穿透的协议（RFC 8445）
- **QUIC**: 基于 UDP 的多路复用安全传输协议（RFC 9000）
- **libnice**: GNOME 项目维护的 ICE 实现库，支持 STUN、TURN、ICE punch
- **lsquic**: LiteSpeed QUIC 库，完整实现 IETF QUIC（RFC 9000）和 Google QUIC
- **STUN**: Session Traversal Utilities for NAT，用于发现公网地址（RFC 5389）

- **Stream**: QUIC 协议中的逻辑数据流，支持多路复用
- **Socket_FD**: 操作系统级别的 UDP socket 文件描述符
- **Candidate**: ICE 候选地址，包括 host、srflx（server reflexive）类型
- **TransportState**: 传输层状态枚举（Disconnected、Connecting、Connected、Failed）

## Requirements

### Requirement 1

**User Story:** As a developer, I want to configure the ICE+QUIC transport with my own settings, so that I can customize the behavior for my application.

#### Acceptance Criteria

1. THE IceQuicConfig SHALL provide fields for STUN server addresses (hostname and port)
2. THE IceQuicConfig SHALL provide fields for QUIC settings including idle timeout, max streams, and max message size
3. THE IceQuicConfig SHALL provide a field to specify client or server role
4. THE IceQuicConfig SHALL provide optional fields for TLS certificate and private key paths for server mode

### Requirement 2

**User Story:** As a developer, I want to initialize the ICE+QUIC transport layer, so that I can establish P2P connections through NAT.

#### Acceptance Criteria

1. WHEN IceQuicTransport is constructed with an IceQuicConfig THEN the IceQuicTransport SHALL initialize libnice ICE agent with the provided STUN servers
2. WHEN IceQuicTransport is constructed THEN the IceQuicTransport SHALL initialize lsquic engine in the appropriate mode based on the configured role
3. WHEN IceQuicTransport is destroyed THEN the IceQuicTransport SHALL release all libnice and lsquic resources without memory leaks
4. IF libnice initialization fails THEN the IceQuicTransport SHALL throw an exception with a descriptive error message
5. IF lsquic initialization fails THEN the IceQuicTransport SHALL throw an exception with a descriptive error message

### Requirement 3

**User Story:** As a developer, I want to perform ICE candidate gathering and exchange, so that peers can discover connectivity paths.

#### Acceptance Criteria

1. WHEN gatherCandidates is called THEN the IceQuicTransport SHALL trigger libnice to gather host and server-reflexive candidates
2. WHEN a new ICE candidate is discovered THEN the IceQuicTransport SHALL invoke the onLocalCandidate callback with the candidate SDP string
3. WHEN candidate gathering completes THEN the IceQuicTransport SHALL invoke the onGatheringComplete callback
4. WHEN addRemoteCandidate is called with a valid candidate SDP string THEN the IceQuicTransport SHALL add the candidate to libnice for connectivity checks
5. WHEN setRemoteCredentials is called THEN the IceQuicTransport SHALL configure libnice with the remote ICE ufrag and password
6. WHEN getLocalCredentials is called THEN the IceQuicTransport SHALL return the local ICE ufrag and password as a pair of strings

### Requirement 4

**User Story:** As a developer, I want the ICE connection to complete and hand off the socket to QUIC, so that secure multiplexed communication can begin.

#### Acceptance Criteria

1. WHEN ICE connectivity checks succeed THEN the IceQuicTransport SHALL transition to Connected state
2. WHEN ICE connectivity succeeds THEN the IceQuicTransport SHALL configure lsquic to use the ICE-established path for QUIC communication
3. WHEN lsquic QUIC handshake completes THEN the IceQuicTransport SHALL invoke the onConnected callback
4. IF ICE connectivity checks fail THEN the IceQuicTransport SHALL transition to Failed state and invoke onFailed callback
5. IF QUIC handshake fails THEN the IceQuicTransport SHALL transition to Failed state and invoke onFailed callback with error description

### Requirement 5

**User Story:** As a developer, I want to create and manage QUIC streams, so that I can multiplex multiple data channels over a single connection.

#### Acceptance Criteria

1. WHEN openStream is called THEN the IceQuicTransport SHALL create a new bidirectional QUIC stream and return its stream ID
2. WHEN closeStream is called with a valid stream ID THEN the IceQuicTransport SHALL gracefully close the specified stream
3. WHEN a peer opens a new stream THEN the IceQuicTransport SHALL invoke the onStreamOpened callback with the stream ID
4. WHEN a stream is closed by the peer THEN the IceQuicTransport SHALL invoke the onStreamClosed callback with the stream ID
5. WHEN getMaxStreams is called THEN the IceQuicTransport SHALL return the maximum number of concurrent bidirectional streams allowed

### Requirement 6

**User Story:** As a developer, I want to send and receive data over QUIC streams, so that I can exchange application data with peers.

#### Acceptance Criteria

1. WHEN send is called with stream ID and data buffer THEN the IceQuicTransport SHALL queue the data for transmission on the specified stream
2. WHEN data is received on a stream THEN the IceQuicTransport SHALL invoke the onData callback with stream ID and data buffer
3. WHEN send is called and the connection is not in Connected state THEN the IceQuicTransport SHALL return false
4. WHEN send is called with data exceeding max message size THEN the IceQuicTransport SHALL return false
5. WHEN send succeeds THEN the IceQuicTransport SHALL return true

### Requirement 7

**User Story:** As a developer, I want to monitor connection statistics, so that I can assess connection quality and debug issues.

#### Acceptance Criteria

1. WHEN getRtt is called THEN the IceQuicTransport SHALL return the current round-trip time in milliseconds from lsquic
2. WHEN getBytesSent is called THEN the IceQuicTransport SHALL return the total bytes sent since connection establishment
3. WHEN getBytesReceived is called THEN the IceQuicTransport SHALL return the total bytes received since connection establishment
4. WHEN getPacketLoss is called THEN the IceQuicTransport SHALL return the packet loss percentage as a floating point value
5. WHEN getState is called THEN the IceQuicTransport SHALL return the current TransportState enum value

### Requirement 8

**User Story:** As a developer, I want to gracefully close the connection, so that resources are properly released and peers are notified.

#### Acceptance Criteria

1. WHEN close is called THEN the IceQuicTransport SHALL send QUIC CONNECTION_CLOSE frame before closing
2. WHEN the peer closes the connection THEN the IceQuicTransport SHALL transition to Disconnected state and invoke onDisconnected callback
3. WHEN the ICE layer disconnects THEN the IceQuicTransport SHALL close the QUIC connection and transition to Disconnected state
4. WHEN close completes THEN the IceQuicTransport SHALL release all stream resources and invoke onDisconnected callback

### Requirement 9

**User Story:** As a developer, I want lsquic to route packets through the ICE-established UDP path, so that QUIC traffic flows through the NAT-traversed connection.

#### Acceptance Criteria

1. WHEN ICE connection is established THEN the IceQuicTransport SHALL use lsquic ea_packets_out callback to route outgoing packets through libnice
2. WHEN UDP packets arrive via libnice THEN the IceQuicTransport SHALL feed packets to lsquic via lsquic_engine_packet_in function
3. WHEN lsquic needs to send packets THEN the IceQuicTransport SHALL use nice_agent_send to transmit through the ICE connection
4. THE IceQuicTransport SHALL handle lsquic timer events by calling lsquic_engine_process_conns at appropriate intervals

### Requirement 10

**User Story:** As a developer, I want comprehensive API documentation and usage examples, so that I can integrate the library into my application.

#### Acceptance Criteria

1. THE library SHALL provide a complete C++ header file with documented public API
2. THE library SHALL provide a simple echo client example demonstrating connection establishment and data exchange
3. THE library SHALL provide a simple echo server example demonstrating connection acceptance and data exchange
4. THE examples SHALL demonstrate ICE candidate exchange via a signaling simulation
5. THE examples SHALL compile and run successfully on Linux and macOS platforms
