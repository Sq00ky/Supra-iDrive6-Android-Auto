// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: NBT_SDR_DROP / NBT_AUDIO_BT channel removal on the real DHU template.
#include "sdr_builder.hpp"
#include "dhu_sdp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static bool getV(const std::vector<uint8_t>& p, size_t& pos, unsigned long long& v) {
    v = 0; int sh = 0;
    while (pos < p.size() && sh < 64) { uint8_t b = p[pos++]; v |= (unsigned long long)(b & 0x7f) << sh; if (!(b & 0x80)) return true; sh += 7; }
    return false;
}

// Collect channel ids of the top-level ChannelDescriptors; returns false on a parse error.
static bool channels(const std::vector<uint8_t>& p, std::vector<int>& ids) {
    size_t pos = 0;
    while (pos < p.size()) {
        unsigned long long k, v;
        if (!getV(p, pos, k)) return false;
        int f = (int)(k >> 3), wt = (int)(k & 7);
        if (wt == 0) { if (!getV(p, pos, v)) return false; }
        else if (wt == 2) {
            if (!getV(p, pos, v) || pos + v > p.size()) return false;
            if (f == 1) { size_t q = pos; unsigned long long k2, c; if (getV(p, q, k2) && k2 == 8 && getV(p, q, c)) ids.push_back((int)c); }
            pos += (size_t)v;
        } else if (wt == 5) pos += 4; else if (wt == 1) pos += 8; else return false;
    }
    return pos == p.size();
}

static bool has(const std::vector<int>& v, int x) { for (size_t i = 0; i < v.size(); ++i) if (v[i] == x) return true; return false; }

int main() {
    int ids[16];
    CHECK(parseChannelList("8,9,14", ids, 16) == 3 && ids[0] == 8 && ids[1] == 9 && ids[2] == 14);
    CHECK(parseChannelList(" 9 ; 0x0e", ids, 16) == 2 && ids[0] == 9 && ids[1] == 14);
    CHECK(parseChannelList("", ids, 16) == 0);
    CHECK(parseChannelList("x", ids, 16) == 0);

    std::vector<uint8_t> base(kDhuSdpProtobuf, kDhuSdpProtobuf + kDhuSdpProtobufLen);
    std::vector<int> before;
    CHECK(channels(base, before));
    CHECK(has(before, 8) && has(before, 9) && has(before, 14) && has(before, 7));

    // NBT_AUDIO_BT=0 (A2I mode): nothing changes
    unsetenv("NBT_SDR_DROP"); setenv("NBT_AUDIO_BT", "0", 1);
    std::vector<uint8_t> a = base;
    CHECK(dropSdrChannels(a) == 0 && a == base);

    // unset = Bluetooth audio mode (default since v1.7.5): channel 9 dropped
    unsetenv("NBT_AUDIO_BT");
    std::vector<uint8_t> a2 = base;
    CHECK(dropSdrChannels(a2) == 1);

    // NBT_AUDIO_BT=1 -> only 9 removed (8 + 14 are required by the phone), rest intact and parseable
    setenv("NBT_AUDIO_BT", "1", 1);
    std::vector<uint8_t> b = base;
    CHECK(dropSdrChannels(b) == 1);
    std::vector<int> after;
    CHECK(channels(b, after));
    CHECK(!has(after, 9) && has(after, 8) && has(after, 14));
    CHECK(after.size() + 1 == before.size());
    CHECK(has(after, 6) && has(after, 7) && has(after, 13));

    // explicit full drop still possible
    setenv("NBT_SDR_DROP", "8,9,14", 1);
    std::vector<uint8_t> f = base;
    CHECK(dropSdrChannels(f) == 3);
    unsetenv("NBT_SDR_DROP");

    // explicit list wins; "none" keeps everything even with BT on
    setenv("NBT_SDR_DROP", "9", 1);
    std::vector<uint8_t> c = base;
    CHECK(dropSdrChannels(c) == 1);
    setenv("NBT_SDR_DROP", "none", 1);
    std::vector<uint8_t> d = base;
    CHECK(dropSdrChannels(d) == 0 && d == base);
    unsetenv("NBT_SDR_DROP"); unsetenv("NBT_AUDIO_BT");

    // identity rewrite + drop compose
    SdrIdentity id;
    std::vector<uint8_t> e = buildServiceDiscovery(base.empty() ? NULL : &base[0], base.size(), id);
    setenv("NBT_AUDIO_BT", "on", 1);
    CHECK(dropSdrChannels(e) == 1);
    std::vector<int> ce; CHECK(channels(e, ce));
    unsetenv("NBT_AUDIO_BT");

    printf(fails ? "test_sdr_drop: %d FAILED\n" : "test_sdr_drop: all passed\n", fails);
    return fails ? 1 : 0;
}
