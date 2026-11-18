// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Wire layouts and RLE32 ABGR algorithm adapted from JayJay3071/nbt-android-auto,
// src/android_auto/knob/nbt_cluster.cpp at 0fb9fd2. No x86 DSI code is used.
#ifndef CLUSTER_HUD6WB_CODEC_HPP
#define CLUSTER_HUD6WB_CODEC_HPP
#include "cluster/hudterm.hpp"
namespace nbt { namespace hudterm {
std::string toLatin15(const std::string& utf8, size_t maxChars);
std::vector<uint8_t> latinText(uint16_t handle, const std::string& utf8, size_t maxChars);
std::vector<uint8_t> distanceBar(uint16_t handle, int value);
std::vector<uint8_t> rle32(const std::vector<uint32_t>& argb);
// Full surface bitmap, one application segment. Oversize streams are refused, not truncated.
std::vector<uint8_t> bitmap(uint16_t handle, uint8_t id, const std::vector<uint32_t>& argb,
                            unsigned width, unsigned height, size_t maxBytes);
struct RoadShield {
    char kind; std::string number;
    RoadShield() : kind(0) {}
};
// Opt-in heuristic: only an explicit A/B route token at the beginning of road text.
// B style is German; do not enable this heuristic for an unverified market/profile.
RoadShield roadShield(const std::string& road);
std::vector<uint32_t> shieldPicture(unsigned width, unsigned height, const RoadShield& shield);
} }
#endif
