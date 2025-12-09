# CMakeLists.txt for ICE+QUIC P2P Transport Layer
# This is an independent library using libnice for ICE and lsquic for QUIC

cmake_minimum_required(VERSION 3.13)
project(ice_quic_transport
    VERSION 0.1.0
    LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)

# Options
option(ICE_QUIC_BUILD_EXAMPLES "Build ice_quic_transport examples" ON)
option(ICE_QUIC_USE_SYSTEM_LIBNICE "Use system libnice instead of building from source" ON)
option(ICE_QUIC_USE_SYSTEM_LSQUIC "Use system lsquic instead of building from source" OFF)

# Find required packages
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# =============================================================================
# libnice dependency (ICE layer)
# =============================================================================
if(ICE_QUIC_USE_SYSTEM_LIBNICE)
    # Use system libnice via pkg-config
    pkg_check_modules(LIBNICE nice)
    
    if(NOT LIBNICE_FOUND)
        message(FATAL_ERROR 
            "libnice not found. Please install libnice development package:\n"
            "  Ubuntu/Debian: sudo apt install libnice-dev\n"
            "  macOS: brew install libnice\n"
            "  Fedora: sudo dnf install libnice-devel\n"
            "Or set ICE_QUIC_USE_SYSTEM_LIBNICE=OFF to skip (not yet supported)")
    endif()
    
    # Find GLIB (required by libnice)
    pkg_check_modules(GLIB glib-2.0 gio-2.0 gobject-2.0)
    
    if(NOT GLIB_FOUND)
        message(FATAL_ERROR 
            "GLib not found. Please install GLib development package:\n"
            "  Ubuntu/Debian: sudo apt install libglib2.0-dev\n"
            "  macOS: brew install glib\n"
            "  Fedora: sudo dnf install glib2-devel")
    endif()
    
    if(NOT TARGET LibNice::LibNice)
        add_library(LibNice::LibNice INTERFACE IMPORTED)
        set_target_properties(LibNice::LibNice PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBNICE_INCLUDE_DIRS};${GLIB_INCLUDE_DIRS}"
            INTERFACE_LINK_DIRECTORIES "${LIBNICE_LIBRARY_DIRS};${GLIB_LIBRARY_DIRS}"
            INTERFACE_LINK_LIBRARIES "${LIBNICE_LIBRARIES};${GLIB_LIBRARIES}"
            INTERFACE_COMPILE_OPTIONS "${LIBNICE_CFLAGS_OTHER};${GLIB_CFLAGS_OTHER}")
    endif()
    
    message(STATUS "Using system libnice: ${LIBNICE_LIBRARIES}")
else()
    message(FATAL_ERROR "Building libnice from source is not yet supported. Please install libnice system package.")
endif()

# =============================================================================
# lsquic dependency (QUIC layer)
# =============================================================================
# lsquic requires BoringSSL for QUIC support
# We use the BoringSSL from deps/boringssl

# Set BoringSSL paths for lsquic
set(BORINGSSL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../deps/boringssl" CACHE PATH "Path to BoringSSL")
set(BORINGSSL_BUILD_DIR "${CMAKE_BINARY_DIR}/boringssl" CACHE PATH "Path to BoringSSL build directory")

# Check if BoringSSL exists
if(NOT EXISTS "${BORINGSSL_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "BoringSSL not found at ${BORINGSSL_DIR}. Please ensure deps/boringssl is available.")
endif()

# Build BoringSSL if not using system lsquic
if(NOT ICE_QUIC_USE_SYSTEM_LSQUIC)
    # Configure BoringSSL build options
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    
    # Add BoringSSL subdirectory
    add_subdirectory(${BORINGSSL_DIR} ${BORINGSSL_BUILD_DIR} EXCLUDE_FROM_ALL)
    
    # Set variables for lsquic to find BoringSSL
    set(LIBSSL_LIB_ssl ssl CACHE STRING "" FORCE)
    set(LIBSSL_LIB_crypto crypto CACHE STRING "" FORCE)
    set(SSLLIB_INCLUDE "${BORINGSSL_DIR}/include" CACHE PATH "" FORCE)
    set(LSQUIC_LIBSSL "BORINGSSL" CACHE STRING "" FORCE)
    
    message(STATUS "Using BoringSSL from: ${BORINGSSL_DIR}")
    
    # lsquic configuration
    set(LSQUIC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../deps/lsquic" CACHE PATH "Path to lsquic")
    set(LSQUIC_BUILD_DIR "${CMAKE_BINARY_DIR}/lsquic" CACHE PATH "Path to lsquic build directory")
    
    if(NOT EXISTS "${LSQUIC_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "lsquic not found at ${LSQUIC_DIR}. Please ensure deps/lsquic is available.")
    endif()
    
    # Configure lsquic build options
    set(LSQUIC_BIN OFF CACHE BOOL "Disable lsquic binaries" FORCE)
    set(LSQUIC_TESTS OFF CACHE BOOL "Disable lsquic tests" FORCE)
    set(LSQUIC_SHARED_LIB OFF CACHE BOOL "Build lsquic as static library" FORCE)
    
    # Add lsquic subdirectory
    add_subdirectory(${LSQUIC_DIR} ${LSQUIC_BUILD_DIR} EXCLUDE_FROM_ALL)
    
    message(STATUS "Using lsquic from: ${LSQUIC_DIR}")
else()
    # Use system lsquic
    find_library(LSQUIC_LIBRARY NAMES lsquic liblsquic REQUIRED)
    find_path(LSQUIC_INCLUDE_DIR NAMES lsquic.h REQUIRED)
    
    if(NOT TARGET lsquic)
        add_library(lsquic UNKNOWN IMPORTED)
        set_target_properties(lsquic PROPERTIES
            IMPORTED_LOCATION "${LSQUIC_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LSQUIC_INCLUDE_DIR}")
    endif()
    
    message(STATUS "Using system lsquic: ${LSQUIC_LIBRARY}")
endif()

# =============================================================================
# ice_quic_transport library
# =============================================================================

# Source files
set(ICE_QUIC_TRANSPORT_SOURCES
    src/ice_quic_transport.cpp
)

set(ICE_QUIC_TRANSPORT_HEADERS
    include/ice_quic_transport.hpp
    include/ice_quic_config.hpp
    include/ice_quic_types.hpp
    include/ice_quic_exception.hpp
)

# Create the library
add_library(ice_quic_transport STATIC ${ICE_QUIC_TRANSPORT_SOURCES} ${ICE_QUIC_TRANSPORT_HEADERS})

target_include_directories(ice_quic_transport PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# Link dependencies
target_link_libraries(ice_quic_transport PUBLIC
    LibNice::LibNice
    Threads::Threads
    lsquic
    ssl
    crypto)

# Add lsquic include directory
target_include_directories(ice_quic_transport PRIVATE
    ${LSQUIC_DIR}/include
    ${BORINGSSL_DIR}/include)

# Add compile definitions
target_compile_definitions(ice_quic_transport INTERFACE
    ICE_QUIC_TRANSPORT_VERSION="${PROJECT_VERSION}")

# =============================================================================
# Examples
# =============================================================================
if(ICE_QUIC_BUILD_EXAMPLES)
    # Echo Server Example
    add_executable(echo_server examples/echo_server.cpp)
    target_link_libraries(echo_server PRIVATE ice_quic_transport z)
    target_include_directories(echo_server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${LSQUIC_DIR}/include
        ${BORINGSSL_DIR}/include)
    
    # Echo Client Example
    add_executable(echo_client examples/echo_client.cpp)
    target_link_libraries(echo_client PRIVATE ice_quic_transport z)
    target_include_directories(echo_client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${LSQUIC_DIR}/include
        ${BORINGSSL_DIR}/include)
    
    # Loopback Test Example
    add_executable(loopback_test examples/loopback_test.cpp)
    target_link_libraries(loopback_test PRIVATE ice_quic_transport z)
    target_include_directories(loopback_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${LSQUIC_DIR}/include
        ${BORINGSSL_DIR}/include)
    
    # Automated Echo Test
    add_executable(automated_echo_test examples/automated_echo_test.cpp)
    target_link_libraries(automated_echo_test PRIVATE ice_quic_transport z)
    target_include_directories(automated_echo_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${LSQUIC_DIR}/include
        ${BORINGSSL_DIR}/include)
    
    # Speed Test Server
    add_executable(speed_test_server examples/speed_test_server.cpp)
    target_link_libraries(speed_test_server PRIVATE ice_quic_transport z)
    target_include_directories(speed_test_server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${LSQUIC_DIR}/include
        ${BORINGSSL_DIR}/include)
    
    # Speed Test Client
    add_executable(speed_test_client examples/speed_test_client.cpp)
    target_link_libraries(speed_test_client PRIVATE ice_quic_transport z)
    target_include_directories(speed_test_client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${LSQUIC_DIR}/include
        ${BORINGSSL_DIR}/include)
    
    message(STATUS "ice_quic_transport examples enabled")
    message(STATUS "  - echo_server")
    message(STATUS "  - echo_client")
    message(STATUS "  - loopback_test")
    message(STATUS "  - automated_echo_test")
    message(STATUS "  - speed_test_server")
    message(STATUS "  - speed_test_client")
endif()

# =============================================================================
# Installation
# =============================================================================
install(TARGETS ice_quic_transport
    EXPORT IceQuicTransportTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")

# Export targets
install(EXPORT IceQuicTransportTargets
    FILE IceQuicTransportTargets.cmake
    NAMESPACE IceQuicTransport::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/IceQuicTransport)

# Package configuration
include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/IceQuicTransportConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/IceQuicTransportConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/IceQuicTransport)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/IceQuicTransportConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/IceQuicTransportConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/IceQuicTransportConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/IceQuicTransport)

message(STATUS "ice_quic_transport configuration complete")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build examples: ${ICE_QUIC_BUILD_EXAMPLES}")
