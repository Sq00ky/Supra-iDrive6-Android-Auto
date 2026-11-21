# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
# The native suite stays on the host architecture so TSAN runs without CPU emulation.
FROM gcc:13@sha256:8c0fe724784c6d617cb582f79ddaf5dd2233aaba2fb7a14e0b9d03348fea8088 AS native
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
        libssl-dev protobuf-compiler && rm -rf /var/lib/apt/lists/*

# Build this stage with --platform linux/amd64. It supplies the mandatory 32-bit test on Apple Silicon too.
FROM ubuntu:24.04@sha256:9c7f200c07e3d4a03e7249d2c16bf66a51120fb876aa2242cf31451412543289 AS multilib
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
        make g++-multilib && rm -rf /var/lib/apt/lists/*
RUN printf '#include <string>\nint main(){std::string s;return s.size();}\n' | \
        g++-13 -m32 -x c++ -o /tmp/probe32 - && /tmp/probe32 && rm /tmp/probe32
