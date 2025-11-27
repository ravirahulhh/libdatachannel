#ifndef ICE_QUIC_CONFIG_HPP
#define ICE_QUIC_CONFIG_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace ice_quic {

struct IceQuicConfig {
    std::vector<std::string> stunServers = {"stun.l.google.com:19302"};
    
    uint32_t idleTimeoutMs = 30000;
    
    uint32_t maxStreams = 100;
    
    bool isServer = false;
    
    std::string certPath;
    
    std::string keyPath;
    
    bool validate() const {
        if (stunServers.empty()) {
            throw std::invalid_argument("At least one STUN server must be specified");
        }
        
        for (const auto& server : stunServers) {
            if (server.empty()) {
                throw std::invalid_argument("STUN server address cannot be empty");
            }
            auto colonPos = server.rfind(':');
            if (colonPos == std::string::npos || colonPos == 0 || colonPos == server.length() - 1) {
                throw std::invalid_argument("STUN server must be in 'host:port' format: " + server);
            }
        }
        
        if (idleTimeoutMs == 0) {
            throw std::invalid_argument("Idle timeout cannot be zero");
        }
        
        if (maxStreams == 0) {
            throw std::invalid_argument("Max streams cannot be zero");
        }
        
        if (isServer) {
            if (certPath.empty()) {
                throw std::invalid_argument("Certificate path is required for server mode");
            }
            if (keyPath.empty()) {
                throw std::invalid_argument("Private key path is required for server mode");
            }
        }
        
        return true;
    }
    
    bool isValid() const noexcept {
        try {
            return validate();
        } catch (...) {
            return false;
        }
    }
};

}

#endif
