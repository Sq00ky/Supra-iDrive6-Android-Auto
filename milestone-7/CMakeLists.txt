# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
cmake_minimum_required(VERSION 3.10)
project(nbt_aad C CXX)

# Keep the same profile selection and legacy alias as the native Makefile.
option(G30_PIP "Legacy alias for CLUSTER_PROFILE=g30" OFF)
if(G30_PIP AND NOT DEFINED CLUSTER_PROFILE)
    set(CLUSTER_PROFILE "g30" CACHE STRING "Cluster hardware profile")
else()
    set(CLUSTER_PROFILE "j29" CACHE STRING "Cluster hardware profile")
endif()
set_property(CACHE CLUSTER_PROFILE PROPERTY STRINGS j29 g30)
if(NOT CLUSTER_PROFILE STREQUAL "j29" AND NOT CLUSTER_PROFILE STREQUAL "g30")
    message(FATAL_ERROR "Unsupported CLUSTER_PROFILE '${CLUSTER_PROFILE}'; choose j29 or g30")
endif()
if(G30_PIP AND NOT CLUSTER_PROFILE STREQUAL "g30")
    message(FATAL_ERROR "G30_PIP=ON requires CLUSTER_PROFILE=g30")
endif()

add_library(cluster-profile INTERFACE)
if(CLUSTER_PROFILE STREQUAL "g30")
    target_compile_definitions(cluster-profile INTERFACE NBT_CLUSTER_VIDEO NBT_CLUSTER_PROFILE_G30)
endif()

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
    src/usb_accessory.cpp
    src/usb_direct.cpp
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
    src/cluster/guidance/client.cpp
    src/cluster/guidance/display.cpp
    src/cluster/nav_core.cpp
    src/cluster/hud_terminal.cpp
    src/cluster/hud6wa_display.cpp
    src/volume_osd.cpp
    src/audio/speex.c
)
if(CLUSTER_PROFILE STREQUAL "g30")
    list(APPEND SOURCES src/cluster/video/bridge.cpp)
endif()

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
    cluster-profile
    a2i_client
    socket
    ssl
    crypto
    screen
    usbdi
    m
)

# The shared video implementation is available only for a tested hardware profile.
# CMake tracks included profile/graph headers and compiler definitions directly;
# reconfiguring CLUSTER_PROFILE therefore rebuilds the affected objects.
if(CLUSTER_PROFILE STREQUAL "g30")
    set(VIDEO_SOURCE src/cluster/video)
    set(OPENH264 third_party/openh264)
    file(GLOB OPENH264_COMMON "${OPENH264}/codec/common/src/*.cpp")
    list(FILTER OPENH264_COMMON EXCLUDE REGEX "/Wels[^/]*\\.cpp$")
    file(GLOB OPENH264_CORE "${OPENH264}/codec/decoder/core/src/*.cpp")
    list(REMOVE_ITEM OPENH264_CORE
        "${CMAKE_CURRENT_SOURCE_DIR}/${OPENH264}/codec/decoder/core/src/wels_decoder_thread.cpp")
    set(OPENH264_INCLUDES
        ${OPENH264}/codec/api
        ${OPENH264}/codec/api/wels
        ${OPENH264}/codec/common/inc
        ${OPENH264}/codec/decoder/core/inc
        ${OPENH264}/codec/decoder/plus/inc
    )
    add_library(cluster-video-decoder STATIC
        ${OPENH264_COMMON}
        ${OPENH264_CORE}
        ${OPENH264}/codec/decoder/plus/src/welsDecoderExt.cpp
        ${VIDEO_SOURCE}/decoder_port.cpp
    )
    target_include_directories(cluster-video-decoder PRIVATE ${OPENH264_INCLUDES})
    target_compile_options(cluster-video-decoder PRIVATE
        -O2 -Wall -D_QNX_SOURCE -fno-exceptions -fno-rtti -fno-strict-aliasing)
    target_link_libraries(cluster-video-decoder PRIVATE cluster-profile)
    set_target_properties(cluster-video-decoder PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/cluster/${CLUSTER_PROFILE}")

    add_executable(kombi-player ${VIDEO_SOURCE}/player.cpp ${VIDEO_SOURCE}/supervisor.cpp)
    target_include_directories(kombi-player PRIVATE ${OPENH264_INCLUDES})
    target_compile_options(kombi-player PRIVATE -O2 -Wall -D_QNX_SOURCE -fno-rtti)
    target_link_libraries(kombi-player PRIVATE cluster-profile cluster-video-decoder screen socket m)

    add_executable(cluster-inspect ${VIDEO_SOURCE}/inspect.cpp)
    target_compile_options(cluster-inspect PRIVATE -O2 -Wall -D_QNX_SOURCE)
    target_link_libraries(cluster-inspect PRIVATE cluster-profile screen)
    set_target_properties(kombi-player cluster-inspect PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/cluster/${CLUSTER_PROFILE}"
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "/lib;/usr/lib")
    set_target_properties(cluster-inspect PROPERTIES OUTPUT_NAME inspect)
    add_custom_target(g30-player DEPENDS kombi-player)
    add_custom_target(g30-inspect DEPENDS cluster-inspect)
endif()
