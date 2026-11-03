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

    // Ack 0x8004 { session = 1, ack = 2, receive_time = 3 (repeated, unpacked) }
    {
        const uint8_t w[] = { 0x80, 0x04, 0x08, 0x00, 0x10, 0x01 };   // what nbt-aad always sent: no times
        CHECK(same(aaMediaAck(0, 1, NULL, 0), w, sizeof(w)));
    }
    {
        const uint8_t w[] = { 0x80, 0x04, 0x08, 0x03, 0x10, 0x01, 0x18, 0x96, 0x01 };   // 150 ns
        const uint64_t t = 150;
        CHECK(same(aaMediaAck(3, 1, &t, 1), w, sizeof(w)));
    }
    {
        // 2026-11-03 12:00:00 UTC in ns (a 9-byte varint), two values: each gets its own 0x18 tag
        const uint64_t t[2] = { 1793707200000000000ull, 1793707200042666000ull };
        std::vector<uint8_t> m = aaMediaAck(300, 2, t, 2);
        CHECK(m.size() == 2 + 3 + 2 + 2 * (1 + 9));
        CHECK(m[2] == 0x08 && m[3] == 0xAC && m[4] == 0x02 && m[5] == 0x10 && m[6] == 0x02);
        CHECK(m[7] == 0x18 && m[17] == 0x18);
        // decode the first value back
        uint64_t v = 0; int sh = 0; size_t i = 8;
        while (i < m.size()) { uint8_t b = m[i++]; v |= (uint64_t)(b & 0x7F) << sh; sh += 7; if (!(b & 0x80)) break; }
        CHECK(v == t[0] && i == 17);
    }

    printf("test_media_msgs: %s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
