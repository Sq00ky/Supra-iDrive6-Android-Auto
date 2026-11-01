# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
cmake_minimum_required(VERSION 3.10)
project(nbt_aad C CXX)

# The QNX Makefile passes -Wc,-std=gnu++0x. C++11 matches that. -fPIC is omitted: it
# produces a shared object the head unit will not exec.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Same translation units as the Makefile. The QNX build is the Makefile (qcc); this list is
# so a CMake configure does not drop sources or pass -fPIC, which the readme notes produces ET_DYN.
set(SOURCES
    src/main.cpp
    src/audio_manager.cpp
    src/a2i_stream.cpp
    src/audio_route.cpp
    src/audio_mixer.cpp
    src/browser_audio.cpp
    src/pcm_utils.cpp
    src/video_manager.cpp
    src/tcp_server.cpp
    src/aa_session.cpp
    src/aa_cryptor.cpp
    src/bluetooth_manager.cpp
    src/wireless_setup.cpp
    src/touch_probe.cpp
    src/dsi_client.cpp
    src/voice_scenario.cpp
    src/bt_play.cpp
    src/ccs_input.cpp
    src/sdr_builder.cpp
    src/crash_handler.cpp
)

add_executable(nbt-aad ${SOURCES})

# Target QNX compilation flags
target_compile_options(nbt-aad PRIVATE
    -Wall
    -Wextra
    -O2
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
    a2i_client
    socket
    ssl
    crypto
    screen
)
