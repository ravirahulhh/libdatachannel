#ifndef ICE_QUIC_EXCEPTION_HPP
#define ICE_QUIC_EXCEPTION_HPP

#include <stdexcept>
#include <string>

namespace ice_quic {

class IceQuicException : public std::runtime_error {
public:
    enum class ErrorCode {
        InitializationFailed,
        IceError,
        QuicError,
        InvalidState,
        InvalidArgument,
        ConnectionFailed
    };
    
    IceQuicException(ErrorCode code, const std::string& message)
        : std::runtime_error(formatMessage(code, message))
        , mCode(code)
        , mMessage(message) {}
    
    ErrorCode code() const noexcept {
        return mCode;
    }
    
    const std::string& message() const noexcept {
        return mMessage;
    }
    
    static const char* errorCodeToString(ErrorCode code) noexcept {
        switch (code) {
            case ErrorCode::InitializationFailed: return "InitializationFailed";
            case ErrorCode::IceError:             return "IceError";
            case ErrorCode::QuicError:            return "QuicError";
            case ErrorCode::InvalidState:         return "InvalidState";
            case ErrorCode::InvalidArgument:      return "InvalidArgument";
            case ErrorCode::ConnectionFailed:     return "ConnectionFailed";
            default:                              return "Unknown";
        }
    }
    
private:
    ErrorCode mCode;
    std::string mMessage;
    
    static std::string formatMessage(ErrorCode code, const std::string& message) {
        return std::string("[") + errorCodeToString(code) + "] " + message;
    }
};

}

#endif
