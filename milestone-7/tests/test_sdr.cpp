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
static bool containsBytes(const std::vector<uint8_t>& d, const uint8_t* s, size_t n) {
    for (size_t i = 0; i + n <= d.size(); ++i) if (memcmp(&d[i], s, n) == 0) return true;
    return false;
}
int main() {
    // The reference manifest is byte for byte what Google's Desktop Head Unit sends.
    {
        const std::vector<uint8_t> dhu(kDhuSdpProtobuf, kDhuSdpProtobuf + kDhuSdpProtobufLen);
        assert(serializeManifest(referenceManifest(dhuIdentity())) == dhu);
        printf("reference manifest matches the DHU (%u bytes)\n", (unsigned)dhu.size());
    }
    SdrIdentity id;
    std::vector<uint8_t> a = serializeManifest(referenceManifest(id));
    bool bt; int na = countChannels(a, bt);
    std::vector<uint8_t> base(kDhuSdpProtobuf, kDhuSdpProtobuf + kDhuSdpProtobufLen);
    bool bt0; int n0 = countChannels(base, bt0);
    printf("base=%u bytes %d channels; rebuilt=%u bytes %d channels\n", (unsigned)base.size(), n0, (unsigned)a.size(), na);
    assert(n0 == na && !bt);
    assert(contains(a, "Toyota") && contains(a, "GR Supra"));
    assert(!contains(a, "Desktop Head Unit") && !contains(a, "Google"));
    id.btAdapterAddress = "AA:BB:CC:DD:EE:FF";
    std::vector<uint8_t> b = serializeManifest(referenceManifest(id));
    int nb = countChannels(b, bt);
    assert(nb == n0 + 1 && bt && contains(b, "AA:BB:CC:DD:EE:FF"));
    // main video override
    {
        ServiceDiscovery v = referenceManifest(dhuIdentity());
        setenv("NBT_VID_INSETS", "off", 1);
        setenv("NBT_VID_DPI", "0", 1);                               // DPI 140 is a default since v1.5.x
        assert(!configureMainVideo(v));                                  // off + no other env: untouched
        setenv("NBT_VID_MH", "240", 1);
        setenv("NBT_VID_INSETS", "0,240,0,0", 1); setenv("NBT_VID_DPI", "140", 1);
        const size_t channelsBefore = v.channels.size();
        bool ch = configureMainVideo(v);
        assert(ch && v.channels.size() == channelsBefore);
        const std::vector<uint8_t> bytes = serializeManifest(v);
        const uint8_t margin240[] = { 0x20, 0xf0, 0x01 };
        const uint8_t insets[] = { 0x5a, 0x11, 0x0a, 0x09, 0x08, 0x00, 0x10, 0xf0, 0x01, 0x18, 0x00, 0x20, 0x00, 0x12, 0x00, 0x1a, 0x00, 0x20, 0x00 };
        assert(containsBytes(bytes, margin240, sizeof(margin240)));
        assert(containsBytes(bytes, insets, sizeof(insets)));
        printf("video override OK (%u bytes)\n", (unsigned)bytes.size());
    }
    {
        ServiceDiscovery v = referenceManifest(dhuIdentity());
        assert(configureTouchScreen(v));
        const std::vector<uint8_t> bytes = serializeManifest(v);
        const uint8_t t720[] = { 0x08, 0x80, 0x0a, 0x10, 0xd0, 0x05 };
        assert(containsBytes(bytes, t720, sizeof(t720)));
        assert(!configureTouchScreen(v));                            // already 720
        printf("touch config 720 OK\n");
    }
    {
        // Exact sensor channel: sensors 1,3,5,6,7,8,10,13,19,21, raw GPS location, unleaded, no connector.
        const uint8_t sensors[] = {
            0x0a, 0x31, 0x08, 0x01, 0x12, 0x2d,
            0x0a, 0x02, 0x08, 0x01, 0x0a, 0x02, 0x08, 0x03, 0x0a, 0x02, 0x08, 0x05, 0x0a, 0x02, 0x08, 0x06,
            0x0a, 0x02, 0x08, 0x07, 0x0a, 0x02, 0x08, 0x08, 0x0a, 0x02, 0x08, 0x0a, 0x0a, 0x02, 0x08, 0x0d,
            0x0a, 0x02, 0x08, 0x13, 0x0a, 0x02, 0x08, 0x15,
            0x10, 0x80, 0x02, 0x18, 0x01
        };
        const size_t dhuSensorChannelLen = 67;                       // 0a 41 + 65 bytes
        ServiceDiscovery v = referenceManifest(dhuIdentity());
        assert(configureSensors(v));
        const std::vector<uint8_t> bytes = serializeManifest(v);
        assert(bytes.size() == kDhuSdpProtobufLen - dhuSensorChannelLen + sizeof(sensors));
        assert(memcmp(&bytes[0], sensors, sizeof(sensors)) == 0);
        // Everything after the sensor channel is the DHU's, byte for byte.
        assert(memcmp(&bytes[sizeof(sensors)], kDhuSdpProtobuf + dhuSensorChannelLen, kDhuSdpProtobufLen - dhuSensorChannelLen) == 0);
        assert(!configureSensors(v));                                // a second run changes nothing
        // A session gets the corrected channel too.
        const std::vector<uint8_t> session = serializeManifest(sessionManifest(dhuIdentity()));
        assert(memcmp(&session[0], sensors, sizeof(sensors)) == 0);
        printf("sensor channel OK (%u bytes)\n", (unsigned)bytes.size());
    }
    {
        // NBT_RAIL_HIDE -> ServiceDiscoveryResponse field 13 (session_configuration)
        assert(parseRailHide(NULL) == 0 && parseRailHide("") == 0 && parseRailHide("  ") == 0);
        assert(parseRailHide("clock") == SESSION_HIDE_CLOCK);
        assert(parseRailHide("signal") == SESSION_HIDE_PHONE_SIGNAL);
        assert(parseRailHide("battery") == SESSION_HIDE_BATTERY_LEVEL);
        assert(parseRailHide("Clock, Battery") == (SESSION_HIDE_CLOCK | SESSION_HIDE_BATTERY_LEVEL));
        assert(parseRailHide("signal+clock;battery") == SESSION_HIDE_ALL);
        assert(parseRailHide("all") == SESSION_HIDE_ALL && parseRailHide("ALL") == 7);
        assert(parseRailHide("none") == 0 && parseRailHide("off") == 0 && parseRailHide("0") == 0);
        assert(parseRailHide("5") == 5 && parseRailHide("0x3") == 3 && parseRailHide(" 7 ") == 7);
        assert(parseRailHide("8") == -1 && parseRailHide("-1") == -1 && parseRailHide("3x") == -1);
        assert(parseRailHide("clock,wifi") == -1 && parseRailHide("averyveryverylongword") == -1);

        const uint8_t f13zero[] = { 0x68, 0x00 }, f13all[] = { 0x68, 0x07 }, f13clock[] = { 0x68, 0x01 };
        ServiceDiscovery v = referenceManifest(dhuIdentity());
        const std::vector<uint8_t> before = serializeManifest(v);
        assert(containsBytes(before, f13zero, 2) && !containsBytes(before, f13all, 2));
        unsetenv("NBT_RAIL_HIDE");
        assert(!configureRailHide(v) && serializeManifest(v) == before);      // unset: the DHU's 0, byte for byte
        setenv("NBT_RAIL_HIDE", "wifi", 1);
        assert(!configureRailHide(v) && v.sessionConfiguration == 0);         // not understood: ignored
        setenv("NBT_RAIL_HIDE", "all", 1);
        assert(configureRailHide(v) && v.sessionConfiguration == 7);
        std::vector<uint8_t> after = serializeManifest(v);
        assert(after.size() == before.size() && containsBytes(after, f13all, 2));
        assert(!configureRailHide(v));                                       // a second run changes nothing
        v.sessionConfiguration = 8;                                          // other bits are kept
        setenv("NBT_RAIL_HIDE", "clock", 1);
        assert(configureRailHide(v) && v.sessionConfiguration == 9);
        setenv("NBT_RAIL_HIDE", "none", 1);
        assert(configureRailHide(v) && v.sessionConfiguration == 8);
        // A session gets it too.
        setenv("NBT_RAIL_HIDE", "clock", 1);
        assert(containsBytes(serializeManifest(sessionManifest(dhuIdentity())), f13clock, 2));
        unsetenv("NBT_RAIL_HIDE");
        printf("rail hide OK\n");
    }
    printf("test_sdr OK\n");
    return 0;
}
