#!/usr/bin/env bash
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
exec make -j"$(nproc)" check "$@"
