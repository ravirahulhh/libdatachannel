#ifndef ICE_QUIC_TYPES_HPP
#define ICE_QUIC_TYPES_HPP

#include <functional>
#include <string>
#include <vector>
#include <cstdint>

namespace ice_quic {

struct IceDescription {
    std::string ufrag;
    std::string pwd;
    std::vector<std::string> candidates;
};

enum class TransportState {
    Disconnected = 0,
    Gathering = 1,
    Connecting = 2,
    Connected = 3,
    Failed = 4
};

inline const char* transportStateToString(TransportState state) {
    switch (state) {
        case TransportState::Disconnected: return "Disconnected";
        case TransportState::Gathering:    return "Gathering";
        case TransportState::Connecting:   return "Connecting";
        case TransportState::Connected:    return "Connected";
        case TransportState::Failed:       return "Failed";
        default:                           return "Unknown";
    }
}

struct IceQuicCallbacks {
    std::function<void(const std::string& candidateSdp)> onLocalCandidate;
    std::function<void()> onGatheringComplete;
    std::function<void()> onConnected;
    std::function<void()> onDisconnected;
    std::function<void(const std::string& error)> onFailed;
    std::function<void(uint64_t streamId)> onStreamOpened;
    std::function<void(uint64_t streamId)> onStreamClosed;
    std::function<void(uint64_t streamId, const uint8_t* data, size_t len)> onData;
};

}

#endif
