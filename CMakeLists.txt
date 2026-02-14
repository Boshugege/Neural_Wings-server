cmake_minimum_required(VERSION 3.11)
project(Neural_Wings_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options("/utf-8")
    set(CMAKE_CXX_FLAGS_DEBUG   "/MDd /Od /Z7 /EHsc /FS")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "/DEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "/MD /O2 /DNDEBUG /EHsc /FS")
endif()

# ── Sources ──────────────────────────────────────────────────────
set(SERVER_SOURCES
    src/main.cpp
    src/GameServer.cpp
    src/nbnet_server_impl.c
)

add_executable(${PROJECT_NAME} ${SERVER_SOURCES})

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "Neural_Wings-server"
)

# ── nbnet ────────────────────────────────────────────────────────
set(NBNET_ROOT "${CMAKE_SOURCE_DIR}/third_party/nbnet")

if(NOT EXISTS "${NBNET_ROOT}/nbnet.h")
    message(FATAL_ERROR
        "nbnet not found!  Please clone/download nbnet into:\n"
        "  ${NBNET_ROOT}\n"
        "  (needs nbnet.h and net_drivers/ folder)\n"
        "  https://github.com/nathhB/nbnet")
endif()

# ── Include paths ────────────────────────────────────────────────
# "shared/" mirrors the Engine/Network include structure so existing
# #include "Engine/Network/..." paths work unchanged.
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/shared
    ${CMAKE_SOURCE_DIR}/src
    ${NBNET_ROOT}
)

# ── WebRTC Support ───────────────────────────────────
# Requires libdatachannel + OpenSSL via vcpkg.
find_package(LibDataChannel CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    LibDataChannel::LibDataChannel
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_compile_definitions(${PROJECT_NAME} PRIVATE NW_ENABLE_WEBRTC_C)
message(STATUS "Server WebRTC_C driver: ENABLED (Mandatory)")

# On Windows, nbnet UDP driver needs ws2_32.
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 winmm)
endif()
