# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
# QNX Momentics common.mk for nbt-aad
# Target: armv7le (Cortex-A15, BMW NBT Evo)

ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

NAME=nbt-aad

# Compiler and Linker Flags
CCFLAGS += -O2 -Wall -D_QNX_SOURCE -march=armv7-a -mfpu=vfpv3 -mfloat-abi=softfp
CXXFLAGS += -O2 -Wall -D_QNX_SOURCE -Wc,-std=gnu++0x -march=armv7-a -mfpu=vfpv3 -mfloat-abi=softfp

# Include directories
EXTRA_INCVPATH += $(PROJECT_ROOT)/include $(PROJECT_ROOT)/src

# Library dependencies
LIBS += socket screen a2i_client

include $(MKFILES_ROOT)/qtargets.mk
