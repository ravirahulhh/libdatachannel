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

# 库文件路径
link_directories(
    ${LSQUIC_DIR}/src/liblsquic
    ${BORINGSSL_DIR}/ssl
    ${BORINGSSL_DIR}/crypto
)

# 编译服务端
add_executable(speed_test_server speed_test_server.cpp)
target_link_libraries(speed_test_server
    lsquic
    ssl
    crypto
    event
    z
    pthread
    m
)

# 编译客户端
add_executable(speed_test_client speed_test_client.cpp)
target_link_libraries(speed_test_client
    lsquic
    ssl
    crypto
    event
    z
    pthread
    m
)
