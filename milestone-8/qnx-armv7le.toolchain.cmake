# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
# CMake Toolchain File for QNX SDP 6.5.0 / 6.6.0 (ARMv7LE)
# Target: Cortex-A15 Little Endian (BMW NBT Evo)

set(CMAKE_SYSTEM_NAME QNX)
set(CMAKE_SYSTEM_VERSION 6.5.0)
set(CMAKE_SYSTEM_PROCESSOR armv7le)

if(DEFINED ENV{QNX_HOST})
    set(QNX_HOST $ENV{QNX_HOST})
else()
    set(QNX_HOST "C:/QNX650/host/win32/x86")
endif()

if(DEFINED ENV{QNX_TARGET})
    set(QNX_TARGET $ENV{QNX_TARGET})
else()
    set(QNX_TARGET "C:/QNX650/target/qnx6")
endif()

set(CMAKE_C_COMPILER   "${QNX_HOST}/usr/bin/ntoarmv7-gcc")
set(CMAKE_CXX_COMPILER "${QNX_HOST}/usr/bin/ntoarmv7-g++")

set(CMAKE_C_FLAGS   "-V4.8.2,gcc_ntoarmv7le -march=armv7-a -mfpu=vfpv4 -mfloat-abi=softfp" CACHE STRING "C flags")
set(CMAKE_CXX_FLAGS "-V4.8.2,gcc_ntoarmv7le_cpp -march=armv7-a -mfpu=vfpv4 -mfloat-abi=softfp" CACHE STRING "CXX flags")

set(CMAKE_FIND_ROOT_PATH "${QNX_TARGET}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
