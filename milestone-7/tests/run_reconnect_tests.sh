#!/bin/sh
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
set -eu
cd "$(dirname "$0")/.."
nbt_test_dir=$(mktemp -d "${TMPDIR:-/tmp}/nbt-reconnect-tests.XXXXXX")
trap 'rm -rf "$nbt_test_dir"' EXIT HUP INT TERM
nbt_test_cxx=${CXX:-c++}
"$nbt_test_cxx" -std=c++98 -Wall -Wextra -pedantic -Werror tests/test_reconnect_policy.cpp -o "$nbt_test_dir/policy"
"$nbt_test_dir/policy"
"$nbt_test_cxx" -std=c++98 -Wall -Wextra -pedantic -Werror -pthread tests/test_setup_progress.cpp -o "$nbt_test_dir/progress"
"$nbt_test_dir/progress"
"$nbt_test_cxx" -std=c++98 -Wall -Wextra -Isrc tests/test_ap_candidates.cpp src/wireless_setup.cpp -o "$nbt_test_dir/ap"
"$nbt_test_dir/ap"
"$nbt_test_cxx" -std=c++98 -Wall -Wextra -Isrc tests/test_bcl_detect.cpp src/wireless_setup.cpp -o "$nbt_test_dir/bcl"
"$nbt_test_dir/bcl"
