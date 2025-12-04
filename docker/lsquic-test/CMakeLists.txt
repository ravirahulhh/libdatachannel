cmake_minimum_required(VERSION 3.10)
project(lsquic_speed_test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# lsquic 和 BoringSSL 路径
set(LSQUIC_DIR "/src/lsquic" CACHE PATH "lsquic source directory")
set(BORINGSSL_DIR "/src/boringssl" CACHE PATH "BoringSSL source directory")

# 头文件路径
include_directories(
    ${LSQUIC_DIR}/include
    ${BORINGSSL_DIR}/include
)

# 查找 lsquic 库
find_library(LSQUIC_LIB lsquic PATHS ${LSQUIC_DIR}/src/liblsquic NO_DEFAULT_PATH)
if(NOT LSQUIC_LIB)
    set(LSQUIC_LIB ${LSQUIC_DIR}/src/liblsquic/liblsquic.a)
endif()

# BoringSSL 库路径 (直接指定，因为是静态库)
set(SSL_LIB ${BORINGSSL_DIR}/build/ssl/libssl.a)
set(CRYPTO_LIB ${BORINGSSL_DIR}/build/crypto/libcrypto.a)

# 验证文件存在
if(NOT EXISTS ${SSL_LIB})
    message(FATAL_ERROR "SSL library not found: ${SSL_LIB}")
endif()
if(NOT EXISTS ${CRYPTO_LIB})
    message(FATAL_ERROR "Crypto library not found: ${CRYPTO_LIB}")
endif()

message(STATUS "LSQUIC_LIB: ${LSQUIC_LIB}")
message(STATUS "SSL_LIB: ${SSL_LIB}")
message(STATUS "CRYPTO_LIB: ${CRYPTO_LIB}")

# 编译服务端
add_executable(speed_test_server speed_test_server.cpp)
target_link_libraries(speed_test_server
    ${LSQUIC_LIB}
    ${SSL_LIB}
    ${CRYPTO_LIB}
    event
    z
    pthread
    m
)

# 编译客户端
add_executable(speed_test_client speed_test_client.cpp)
target_link_libraries(speed_test_client
    ${LSQUIC_LIB}
    ${SSL_LIB}
    ${CRYPTO_LIB}
    event
    z
    pthread
    m
)
