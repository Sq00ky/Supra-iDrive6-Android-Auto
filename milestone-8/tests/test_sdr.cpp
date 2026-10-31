// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "sdr_builder.hpp"
#include "dhu_sdp.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
using namespace nbt;
static int countChannels(const std::vector<uint8_t>& d, bool& hasBt) {
    size_t pos = 0; int n = 0; hasBt = false;
    while (pos < d.size()) {
        uint64_t key = 0; int sh = 0;
        while (true) { uint8_t b = d[pos++]; key |= (uint64_t)(b & 0x7f) << sh; sh += 7; if (!(b & 0x80)) break; }
        int f = (int)(key >> 3), wt = (int)(key & 7);
        if (wt == 0) { while (d[pos++] & 0x80) {} }
        else if (wt == 2) {
            uint64_t l = 0; sh = 0;
            while (true) { uint8_t b = d[pos++]; l |= (uint64_t)(b & 0x7f) << sh; sh += 7; if (!(b & 0x80)) break; }
            if (f == 1) { ++n; for (size_t i = pos; i + 1 < pos + l; ++i) if (d[i] == 0x32) hasBt = hasBt || false; if (d[pos] == 0x08 && d[pos+1] == 15) hasBt = true; }
            pos += (size_t)l;
        } else if (wt == 5) pos += 4; else pos += 8;
    }
    return n;
}
static bool contains(const std::vector<uint8_t>& d, const char* s) {
    size_t n = strlen(s);
    for (size_t i = 0; i + n <= d.size(); ++i) if (memcmp(&d[i], s, n) == 0) return true;
    return false;
}
int main() {
    SdrIdentity id;
    std::vector<uint8_t> a = buildServiceDiscovery(kDhuSdpProtobuf, kDhuSdpProtobufLen, id);
    bool bt; int na = countChannels(a, bt);
    std::vector<uint8_t> base(kDhuSdpProtobuf, kDhuSdpProtobuf + kDhuSdpProtobufLen);
    bool bt0; int n0 = countChannels(base, bt0);
    printf("base=%u bytes %d channels; rebuilt=%u bytes %d channels\n", (unsigned)base.size(), n0, (unsigned)a.size(), na);
    assert(n0 == na && !bt);
    assert(contains(a, "Toyota") && contains(a, "GR Supra"));
    assert(!contains(a, "Desktop Head Unit") && !contains(a, "Google"));
    id.btAdapterAddress = "AA:BB:CC:DD:EE:FF";
    std::vector<uint8_t> b = buildServiceDiscovery(kDhuSdpProtobuf, kDhuSdpProtobufLen, id);
    int nb = countChannels(b, bt);
    assert(nb == n0 + 1 && bt && contains(b, "AA:BB:CC:DD:EE:FF"));
    // main video override
    {
        std::vector<uint8_t> v(kDhuSdpProtobuf, kDhuSdpProtobuf + kDhuSdpProtobufLen);
        setenv("NBT_VID_INSETS", "off", 1);
        setenv("NBT_VID_DPI", "0", 1);                               // DPI 140 is a default since v1.5.x
        assert(!patchMainVideo(v));                                  // off + no other env: untouched
        setenv("NBT_VID_MH", "240", 1);
        setenv("NBT_VID_INSETS", "0,240,0,0", 1); setenv("NBT_VID_DPI", "140", 1);
        int nb0 = countChannels(v, bt);
        bool ch = patchMainVideo(v);
        assert(ch && countChannels(v, bt) == nb0);
        const uint8_t margin240[] = { 0x20, 0xf0, 0x01 };
        const uint8_t insets[] = { 0x5a, 0x11, 0x0a, 0x09, 0x08, 0x00, 0x10, 0xf0, 0x01, 0x18, 0x00, 0x20, 0x00, 0x12, 0x00, 0x1a, 0x00, 0x20, 0x00 };
        bool m = false, ii = false;
        for (size_t i = 0; i + sizeof(insets) <= v.size(); ++i) {
            if (memcmp(&v[i], margin240, sizeof(margin240)) == 0) m = true;
            if (memcmp(&v[i], insets, sizeof(insets)) == 0) ii = true;
        }
        assert(m && ii);
        printf("video override OK (%u bytes)\n", (unsigned)v.size());
    }
    {
        std::vector<uint8_t> v(kDhuSdpProtobuf, kDhuSdpProtobuf + kDhuSdpProtobufLen);
        assert(patchTouchConfig(v));
        const uint8_t t720[] = { 0x08, 0x80, 0x0a, 0x10, 0xd0, 0x05 };
        bool f = false;
        for (size_t i = 0; i + sizeof(t720) <= v.size(); ++i) if (memcmp(&v[i], t720, sizeof(t720)) == 0) f = true;
        assert(f);
        printf("touch config 720 OK\n");
    }
    printf("test_sdr OK\n");
    return 0;
}
