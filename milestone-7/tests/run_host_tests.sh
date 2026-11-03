#!/usr/bin/env bash
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT

cxx=${CXX:-g++}
flags=(-std=gnu++11 -Wall -Wextra -DNBT_HOST_TEST -Iinclude -Isrc -Itests -pthread)

for test in \
    test_ap_candidates test_audio_mixer test_audio_route test_bcl_detect \
    test_bt_play test_car_config test_ccs_decoder test_ccs_push test_cds_parse \
    test_ctl_wizard test_kombi_graph test_kombi_nav test_kombi_nav_msg \
    test_lm_geometry test_mmr_pps test_most_msg test_neutral_resume test_pcm \
    test_sdr test_sdr_drop test_session_lifecycle test_touch test_touch_frame \
    test_usb_log test_voice_scenario test_wpp
do
    sources=()
    extra_flags=()
    case "$test" in
        test_ap_candidates|test_bcl_detect|test_wpp)
            sources=(src/wireless_setup.cpp) ;;
        test_audio_mixer) sources=(src/audio_mixer.cpp) ;;
        test_audio_route) sources=(src/audio_route.cpp) ;;
        test_bt_play) sources=(src/bt_play.cpp) ;;
        test_car_config|test_sdr|test_sdr_drop)
            sources=(src/sdr_builder.cpp) ;;
        test_kombi_nav) sources=(src/kombi_nav.cpp src/kombi_client.cpp) ;;
        test_pcm) sources=(src/pcm_utils.cpp) ;;
        test_session_lifecycle)
            sources=(src/aa_session.cpp src/sdr_builder.cpp src/crash_handler.cpp)
            extra_flags=(-include tests/stub_cryptor.hpp) ;;
        test_touch) sources=(src/touch_probe.cpp) ;;
        test_voice_scenario) sources=(src/voice_scenario.cpp) ;;
    esac
    echo "[BUILD] $test"
    "$cxx" "${flags[@]}" "${extra_flags[@]}" "tests/$test.cpp" "${sources[@]}" -o "$build_dir/$test"
    echo "[TEST] $test"
    if [[ "$test" == test_touch ]]; then
        # Replay decoding only: avoid car peer persistence and connected-UDP routing.
        NBT_TOUCH_CONNECT=0 timeout 120 "$build_dir/$test"
        echo "[TEST] $test (split datagrams)"
        NBT_TOUCH_CONNECT=0 TOUCH_SPLIT=1 timeout 120 "$build_dir/$test"
    elif [[ "$test" == test_wpp ]]; then
        # This test writes t_iap2_wifi.cfg in its working directory.
        (cd "$build_dir" && timeout 120 "$build_dir/$test")
    else
        timeout 120 "$build_dir/$test"
    fi
done

# The old fake-framework integration harness does not model the current RHMI ABI.
# Still compile the real app on Linux to catch C++ errors without the QNX SDK.
echo "[BUILD] rhmiapp/libNbtAad.so (host)"
"$cxx" "${flags[@]}" -fPIC -shared rhmiapp/nbt_aad_app.cpp -o "$build_dir/libNbtAad.so"

echo "[TEST] test_media_payload.py"
timeout 120 python3 tests/test_media_payload.py
