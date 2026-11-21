#!/bin/sh
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
# The complete host suite for Linux and Docker Desktop on Apple Silicon; also used by CI.
set -eu
cd "$(dirname "$0")/.."

docker build --target native -t nbt-aad-tests:native - < milestone-7/tests/Dockerfile
docker build --platform linux/amd64 --target multilib -t nbt-aad-tests:multilib - < milestone-7/tests/Dockerfile

# setarch -R in the TSAN test needs personality(2), which Docker's default seccomp profile can block.
docker run --rm --user "$(id -u):$(id -g)" --security-opt seccomp=unconfined \
    -v "$PWD/milestone-7":/src -w /src nbt-aad-tests:native \
    timeout 900 make -k check-native OUT=build/native
docker run --rm --platform linux/amd64 --user "$(id -u):$(id -g)" \
    -v "$PWD/milestone-7":/src -w /src nbt-aad-tests:multilib \
    timeout 300 make -k -C tests run-test_aa_proto32 HOST_CXX=g++-13 OUT=build/multilib
printf '[CHECK] complete host suite passed: native, protobuf schema, TSAN and 32-bit\n'
