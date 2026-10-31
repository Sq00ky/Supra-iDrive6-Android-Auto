# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
cmake_minimum_required(VERSION 3.10)
project(nbt_aad C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Source files
set(SOURCES
    src/main.cpp
    src/audio_manager.cpp
    src/a2i_stream.cpp
    src/audio_route.cpp
    src/pcm_utils.cpp
    src/video_manager.cpp
    src/input_manager.cpp
    src/tcp_server.cpp
)

add_executable(nbt-aad ${SOURCES})

# Target QNX compilation flags
target_compile_options(nbt-aad PRIVATE
    -Wall
    -Wextra
    -O2
    -fPIC
    -D_QNX_SOURCE
)

# System libraries available in QNX sysroot
# When linking against target sysroot:
# -lscreen (Graphics)
# -lsocket (Networking)
# -la2i_client (Audio/Mic)
# -lsys_dsi_servicebroker.cpp-ne (DSI Input)
# -lssl -lcrypto (OpenSSL)
target_link_libraries(nbt-aad PRIVATE
    socket
)
