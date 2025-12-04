# MsQuic 速度测试程序 CMakeLists.txt
cmake_minimum_required(VERSION 3.13)
project(msquic_speed_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 MsQuic
find_library(MSQUIC_LIB msquic REQUIRED)
find_path(MSQUIC_INCLUDE msquic.h PATH_SUFFIXES msquic REQUIRED)

# 服务端
add_executable(speed_test_server speed_test_server.cpp)
target_include_directories(speed_test_server PRIVATE ${MSQUIC_INCLUDE})
target_link_libraries(speed_test_server ${MSQUIC_LIB} pthread)

# 客户端
add_executable(speed_test_client speed_test_client.cpp)
target_include_directories(speed_test_client PRIVATE ${MSQUIC_INCLUDE})
target_link_libraries(speed_test_client ${MSQUIC_LIB} pthread)

message(STATUS "MsQuic library: ${MSQUIC_LIB}")
message(STATUS "MsQuic include: ${MSQUIC_INCLUDE}")
