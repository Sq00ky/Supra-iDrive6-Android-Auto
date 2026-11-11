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
    src/pf_aa_embed.cpp
    src/audio/a2i_audio.cpp
    src/audio/config.cpp
    src/audio/bt_audio.cpp
    src/audio/speech_out.cpp
    src/audio/mic_uplink.cpp
    src/audio/a2i_stream.cpp
    src/audio/audio_route.cpp
    src/audio/mixer.cpp
    src/audio/playout.cpp
    src/audio/drift.cpp
    src/audio/stream_rate.cpp
    src/audio/browser_audio.cpp
    src/audio/pcm_ring.cpp
    src/audio/resampler.cpp
    src/audio/asound_device.cpp
    src/video_manager.cpp
    src/mmr_launch.cpp
    src/tcp_server.cpp
    src/aa_session.cpp
    src/audio/audio_acks.cpp
    src/aa_cryptor.cpp
    src/bluetooth_manager.cpp
    src/wireless_setup.cpp
    src/touch_probe.cpp
    src/dsi_client.cpp
    src/audio/voice_scenario.cpp
    src/audio/bt_play.cpp
    src/most/client.cpp
    src/most/trace.cpp
    src/car/ccs_input.cpp
    src/car/volume_input.cpp
    src/car/input_activity.cpp
    src/car/geoid.cpp
    src/car/gps.cpp
    src/car/vehicle.cpp
    src/sdr_builder.cpp
    src/crash_handler.cpp
    src/cluster/j29_client.cpp
    src/cluster/j29_display.cpp
    src/cluster/nav_core.cpp
    src/cluster/hud_terminal.cpp
    src/cluster/hud6wa_display.cpp
    src/cluster/hud6wb_display.cpp
    src/cluster/hud6wb_codec.cpp
    src/cluster/hud6wb_artwork.cpp
    src/audio/speex.c
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
