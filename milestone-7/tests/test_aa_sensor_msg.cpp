// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for src/aa_sensor_msg.hpp: the LOCATION, GPS_SATELLITE, CAR_SPEED, GEAR and PARKING_BRAKE event encodings and their
// age limits.
#include "aa_sensor_msg.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>

using namespace nbt;

// A macro so that a failure names the line of the check. want is an array.
#define ASSERT_BYTES(got, want) assert((got).size() == sizeof(want) && memcmp(&(got)[0], (want), sizeof(want)) == 0)

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
    // The made-up position of the frame in test_car_gps.
    {
        LocationFix f;
        f.lat_e7 = 525000000; f.lon_e7 = -45000000; f.accuracy_e3 = 10000; f.rx_ms = 1;
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
    // A fix with nothing optional set: exactly the fields it always had, so the bytes on the wire do not change.
    {
        LocationFix f;
        assert(!f.has_altitude && !f.has_speed && !f.has_bearing);
        f.lat_e7 = 1; f.lon_e7 = 2; f.accuracy_e3 = 3;
        f.altitude_e2 = 99; f.speed_e3 = 99; f.bearing_e6 = 99;     // values alone do not put a field on the wire
        std::vector<uint8_t> ev;
        buildLocationEvent(f, ev);
        const uint8_t want[] = { 0x80, 0x03, 0x0A, 0x06, 0x10, 0x01, 0x18, 0x02, 0x20, 0x03 };
        ASSERT_BYTES(ev, want);
    }
    // Each optional field on its own, with its tag, in field order, and written even when 0.
    {
        LocationFix f;
        f.lat_e7 = 1; f.lon_e7 = 2; f.accuracy_e3 = 3;
        f.has_speed = true;
        f.speed_e3 = 0;
        std::vector<uint8_t> ev;
        buildLocationEvent(f, ev);
        const uint8_t speed0[] = { 0x80, 0x03, 0x0A, 0x08, 0x10, 0x01, 0x18, 0x02, 0x20, 0x03, 0x30, 0x00 };
        ASSERT_BYTES(ev, speed0);

        f.has_altitude = true; f.altitude_e2 = 5;
        f.speed_e3 = 6;
        f.has_bearing = true; f.bearing_e6 = 7;
        buildLocationEvent(f, ev);
        const uint8_t all[] = { 0x80, 0x03, 0x0A, 0x0C, 0x10, 0x01, 0x18, 0x02, 0x20, 0x03, 0x28, 0x05, 0x30, 0x06, 0x38, 0x07 };
        ASSERT_BYTES(ev, all);
    }
    // Real values round-trip: below sea level is a 10-byte varint, and a bearing near 360 degrees needs 5 bytes.
    {
        LocationFix f;
        f.lat_e7 = 525000000; f.lon_e7 = -45000000; f.accuracy_e3 = 10000;
        f.has_altitude = true; f.altitude_e2 = -2300;
        f.has_speed = true; f.speed_e3 = 13889;         // 50 km/h
        f.has_bearing = true; f.bearing_e6 = 359999999;
        std::vector<uint8_t> ev;
        buildLocationEvent(f, ev);
        assert(ev[0] == 0x80 && ev[1] == 0x03 && ev[2] == 0x0A);
        size_t i = 3;
        const size_t len = (size_t)var(ev, i);
        assert(i + len == ev.size());
        int seen = 0;
        while (i < ev.size()) {
            const uint64_t tag = var(ev, i);
            const uint64_t v = var(ev, i);
            if (tag == 0x28) { assert((int32_t)(int64_t)v == -2300); ++seen; }
            else if (tag == 0x30) { assert((int32_t)v == 13889); ++seen; }
            else if (tag == 0x38) { assert((int32_t)v == 359999999); ++seen; }
        }
        assert(seen == 3);
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
    // GPS_SATELLITE: field 21 of the batch, {1: used}. Exact bytes, and the buffer's old content goes.
    {
        std::vector<uint8_t> ev(100, 0xEE);
        buildSatelliteEvent(7, ev);
        const uint8_t want[] = { 0x80, 0x03, 0xAA, 0x01, 0x02, 0x08, 0x07 };
        ASSERT_BYTES(ev, want);
        // The tag is field 21, length-delimited.
        size_t i = 2;
        assert(var(ev, i) == ((21u << 3) | 2u));
        // Zero is written, not left out: the field is required.
        buildSatelliteEvent(0, ev);
        assert(ev.size() == sizeof(want) && ev[5] == 0x08 && ev[6] == 0x00);
    }
    // The count to send: nothing posted, fresh, at the age limit, past it, and across the 32-bit wrap.
    {
        SatelliteStatus s;
        s.used = 7;
        s.rx_ms = 1000;
        assert(satellitesUsedNow(false, s, 1000) == 0);
        assert(satellitesUsedNow(true, s, 1000) == 7);
        assert(satellitesUsedNow(true, s, 1000 + kSatelliteMaxAgeMs) == 7);
        assert(satellitesUsedNow(true, s, 1000 + kSatelliteMaxAgeMs + 1) == 0);
        s.rx_ms = 0xFFFFFF00u;
        assert(satellitesUsedNow(true, s, 100) == 7);
        assert(satellitesUsedNow(true, s, 10000) == 0);
    }
    // COMPASS: field 2 of the batch, {1: bearing_e6}. Exact bytes for 90 degrees, and the buffer's old content goes.
    {
        std::vector<uint8_t> ev(100, 0xEE);
        buildCompassEvent(90000000, ev);
        const uint8_t want[] = { 0x80, 0x03, 0x12, 0x05, 0x08, 0x80, 0x95, 0xF5, 0x2A };
        assert(ev.size() == sizeof(want));
        for (size_t i = 0; i < sizeof(want); ++i) assert(ev[i] == want[i]);
        // North is written, not left out: the field is required.
        buildCompassEvent(0, ev);
        const uint8_t north[] = { 0x80, 0x03, 0x12, 0x02, 0x08, 0x00 };
        assert(ev.size() == sizeof(north));
        for (size_t i = 0; i < sizeof(north); ++i) assert(ev[i] == north[i]);
    }
    // When a compass repeat is due: not on the car's own 1 s beat, from the limit on, and across the 32-bit wrap.
    {
        assert(!compassRepeatDue(1000, 1000));
        assert(!compassRepeatDue(1000, 2010));                       // a message late by 10 ms is not beaten to it
        assert(!compassRepeatDue(1000, 1000 + kCompassRepeatMs - 1));
        assert(compassRepeatDue(1000, 1000 + kCompassRepeatMs));
        assert(!compassRepeatDue(0xFFFFFF00u, 100));                 // wrapped, 356 ms
        assert(compassRepeatDue(0xFFFFFF00u, 2000));                 // wrapped, past the limit
        // The car's beat must not reach the limit, and a repeat must stay inside what Google Maps counts as current.
        assert(kCompassRepeatMs > 1000 && kCompassRepeatMs < 1500);
    }
    // CAR_SPEED: field 3 of the batch, {1: speed_e3}. Exact bytes for a made-up speed, and the buffer's old content goes.
    {
        std::vector<uint8_t> ev(100, 0xEE);
        buildCarSpeedEvent(6944, ev);
        const uint8_t want[] = { 0x80, 0x03, 0x1A, 0x03, 0x08, 0xA0, 0x36 };
        ASSERT_BYTES(ev, want);
        // Zero is written, not left out: it is the reading that says the car has stopped.
        buildCarSpeedEvent(0, ev);
        const uint8_t zero[] = { 0x80, 0x03, 0x1A, 0x02, 0x08, 0x00 };
        ASSERT_BYTES(ev, zero);
        // Negative: a 10-byte varint, sign-extended, as protobuf writes an int32.
        buildCarSpeedEvent(-1, ev);
        assert(ev.size() == 15 && ev[2] == 0x1A && ev[3] == 11 && ev[4] == 0x08 && ev[14] == 0x01);
        size_t i = 5;
        assert((int32_t)(int64_t)var(ev, i) == -1 && i == ev.size());
    }
    // When it may go out: a moving speed has an age limit, and a 0 does not.
    {
        CarSpeedSample s = { 6944, 1000 };
        assert(carSpeedSendable(s, 1000));
        assert(carSpeedSendable(s, 1000 + kCarSpeedMaxAgeMs));
        assert(!carSpeedSendable(s, 1000 + kCarSpeedMaxAgeMs + 1));
        s.speed_e3 = -6944;
        assert(!carSpeedSendable(s, 1000 + kCarSpeedMaxAgeMs + 1));
        s.rx_ms = 0xFFFFFF00u;
        assert(carSpeedSendable(s, 100));               // wrapped, 356 ms old
        s.speed_e3 = 0;
        s.rx_ms = 1000;
        assert(carSpeedSendable(s, 1000 + 3600000));
    }
    // GEAR: field 8 of the batch, {1: gear}. Exact bytes, and NEUTRAL is written: the field is required and 0 is its value.
    {
        std::vector<uint8_t> ev(100, 0xEE);
        const struct { int gear; uint8_t wire; } gears[] = {
            { AA_GEAR_NEUTRAL, 0x00 }, { AA_GEAR_DRIVE, 0x64 }, { AA_GEAR_PARK, 0x65 }, { AA_GEAR_REVERSE, 0x66 }
        };
        for (size_t g = 0; g < sizeof(gears) / sizeof(gears[0]); ++g) {
            buildGearEvent(gears[g].gear, ev);
            const uint8_t want[] = { 0x80, 0x03, 0x42, 0x02, 0x08, gears[g].wire };
            ASSERT_BYTES(ev, want);
        }
    }
    // PARKING_BRAKE: field 7 of the batch, {1: on}. Exact bytes, and released is written, for the same reason.
    {
        std::vector<uint8_t> ev(100, 0xEE);
        buildParkingBrakeEvent(true, ev);
        const uint8_t on[] = { 0x80, 0x03, 0x3A, 0x02, 0x08, 0x01 };
        ASSERT_BYTES(ev, on);
        buildParkingBrakeEvent(false, ev);
        const uint8_t released[] = { 0x80, 0x03, 0x3A, 0x02, 0x08, 0x00 };
        ASSERT_BYTES(ev, released);
    }
    printf("test_aa_sensor_msg OK\n");
    return 0;
}
