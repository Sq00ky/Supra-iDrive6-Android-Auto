// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for src/aa_sensor_msg.hpp: the LOCATION event encoding and the age limit.
#include "aa_sensor_msg.hpp"
#include <assert.h>
#include <stdio.h>

using namespace nbt;

// Reads one protobuf varint at d[i], advancing i.
static uint64_t var(const std::vector<uint8_t>& d, size_t& i) {
    uint64_t v = 0; int sh = 0;
    while (i < d.size()) { uint8_t b = d[i++]; v |= (uint64_t)(b & 0x7F) << sh; sh += 7; if (!(b & 0x80)) break; }
    return v;
}

static void decode(const std::vector<uint8_t>& ev, int32_t& lat, int32_t& lon, uint32_t& acc, size_t& len) {
    assert(ev[0] == 0x80 && ev[1] == 0x03 && ev[2] == 0x0A);
    size_t i = 3;
    len = (size_t)var(ev, i);
    assert(i + len == ev.size());
    while (i < ev.size()) {
        uint64_t tag = var(ev, i);
        uint64_t v = var(ev, i);
        if (tag == 0x10) lat = (int32_t)(int64_t)v;
        else if (tag == 0x18) lon = (int32_t)(int64_t)v;
        else if (tag == 0x20) acc = (uint32_t)v;
        else assert(!"unexpected field");
    }
}

int main() {
    // The parked position from the car capture in test_car_gps.
    {
        LocationFix f;
        f.lat_e7 = 533553700; f.lon_e7 = -15262930; f.accuracy_e3 = 10000; f.rx_ms = 1;
        std::vector<uint8_t> ev;
        buildLocationEvent(f, ev);
        int32_t lat = 0, lon = 0; uint32_t acc = 0; size_t len = 0;
        decode(ev, lat, lon, acc, len);
        assert(lat == f.lat_e7 && lon == f.lon_e7 && acc == 10000);
        // A negative longitude is a 10-byte varint (protobuf int32 sign extension). Latitude > 2^28: 5 bytes.
        assert(len == 1 + 5 + 1 + 10 + 1 + 2);
    }
    // Both negative, and the event replaces what the buffer held.
    {
        LocationFix f;
        f.lat_e7 = -339000000; f.lon_e7 = -1800000000; f.accuracy_e3 = 1; f.rx_ms = 1;
        std::vector<uint8_t> ev(100, 0xEE);
        buildLocationEvent(f, ev);
        int32_t lat = 0, lon = 0; uint32_t acc = 0; size_t len = 0;
        decode(ev, lat, lon, acc, len);
        assert(lat == f.lat_e7 && lon == f.lon_e7 && acc == 1);
        assert(len == 1 + 10 + 1 + 10 + 1 + 1);
    }
    // The age limit, at its edge and across the 32-bit wrap of the clock.
    {
        LocationFix f;
        f.lat_e7 = f.lon_e7 = 1; f.accuracy_e3 = 1;
        f.rx_ms = 1000;
        assert(locationFresh(f, 1000));
        assert(locationFresh(f, 1000 + kLocationMaxAgeMs));
        assert(!locationFresh(f, 1000 + kLocationMaxAgeMs + 1));
        f.rx_ms = 0xFFFFFF00u;
        assert(locationFresh(f, 0xFFFFFF00u + 100));
        assert(locationFresh(f, 100));          // wrapped, 356 ms old
        assert(!locationFresh(f, 10000));       // wrapped, past the limit
        assert(!locationFresh(f, 0x7FFFFFFFu)); // 24 days old
    }
    printf("test_aa_sensor_msg OK\n");
    return 0;
}
