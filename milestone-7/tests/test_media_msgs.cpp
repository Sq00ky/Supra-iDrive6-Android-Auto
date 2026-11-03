// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: AA media-channel messages, byte for byte against the layout of Google's receiver library (DHU r2.1).
#include "aa_media_msgs.hpp"
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static bool same(const std::vector<uint8_t>& v, const uint8_t* want, size_t n) {
    return v.size() == n && memcmp(&v[0], want, n) == 0;
}

int main() {
    // AudioUnderflowNotification 0x800b { session = 1 }
    { const uint8_t w[] = { 0x80, 0x0B, 0x08, 0x00 }; CHECK(same(aaUnderflowNotification(0), w, sizeof(w))); }
    { const uint8_t w[] = { 0x80, 0x0B, 0x08, 0x05 }; CHECK(same(aaUnderflowNotification(5), w, sizeof(w))); }
    { const uint8_t w[] = { 0x80, 0x0B, 0x08, 0xAC, 0x02 }; CHECK(same(aaUnderflowNotification(300), w, sizeof(w))); }

    printf("test_media_msgs: %s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
