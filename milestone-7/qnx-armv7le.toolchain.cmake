# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
# CMake Toolchain File for QNX SDP 6.5.0 / 6.6.0 (ARMv7LE)
# Target: Cortex-A15 Little Endian (BMW NBT Evo)

set(CMAKE_SYSTEM_NAME QNX)
set(CMAKE_SYSTEM_VERSION 6.5.0)
set(CMAKE_SYSTEM_PROCESSOR armv7le)

if(DEFINED ENV{QNX_HOST} AND NOT "$ENV{QNX_HOST}" STREQUAL "")
    set(QNX_HOST $ENV{QNX_HOST})
else()
    set(QNX_HOST "C:/Program Files (x86)/QNX6.5/host/win32/x86")
endif()

if(DEFINED ENV{QNX_TARGET} AND NOT "$ENV{QNX_TARGET}" STREQUAL "")
    set(QNX_TARGET $ENV{QNX_TARGET})
else()
    set(QNX_TARGET "C:/Program Files (x86)/QNX6.5/target/qnx6")
endif()

if(CMAKE_HOST_WIN32)
    set(QNX_COMPILER "${QNX_HOST}/usr/bin/qcc.exe")
else()
    set(QNX_COMPILER "${QNX_HOST}/usr/bin/qcc")
endif()
set(CMAKE_C_COMPILER "${QNX_COMPILER}")
set(CMAKE_CXX_COMPILER "${QNX_COMPILER}")

# Match the QNX SDP 6.5 Makefile, including GCC's gnu++0x spelling.
set(CMAKE_C_FLAGS_INIT "-Vgcc_ntoarmv7le -march=armv7-a -mfpu=vfpv3 -mfloat-abi=softfp")
set(CMAKE_CXX_FLAGS_INIT "-Vgcc_ntoarmv7le_cpp -Wc,-std=gnu++0x -march=armv7-a -mfpu=vfpv3 -mfloat-abi=softfp")

set(CMAKE_FIND_ROOT_PATH "${QNX_TARGET}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
