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

# 查找 BoringSSL 库 (编译在 build 子目录)
find_library(SSL_LIB ssl PATHS ${BORINGSSL_DIR}/build/ssl NO_DEFAULT_PATH)
if(NOT SSL_LIB)
    set(SSL_LIB ${BORINGSSL_DIR}/build/ssl/libssl.a)
endif()

find_library(CRYPTO_LIB crypto PATHS ${BORINGSSL_DIR}/build/crypto NO_DEFAULT_PATH)
if(NOT CRYPTO_LIB)
    set(CRYPTO_LIB ${BORINGSSL_DIR}/build/crypto/libcrypto.a)
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
