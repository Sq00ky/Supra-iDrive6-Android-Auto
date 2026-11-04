// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AA_SENSOR_MSG_HPP
#define AA_SENSOR_MSG_HPP

// Android Auto sensor channel: the LOCATION and GPS_SATELLITE events, in Android Auto units. It knows nothing about where
// the data came from. Pure: host-tested in tests/test_aa_sensor_msg.cpp.

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace nbt {

struct LocationFix {
    int32_t lat_e7, lon_e7;     // degrees * 1e7
    uint32_t accuracy_e3;       // metres * 1000 (LocationData field 4, optional: only fields 2 and 3 are required)
    uint32_t rx_ms;             // CLOCK_MONOTONIC ms, truncated to 32 bits, when the fix was received
    // Optional, and off until set: nothing sets them yet, because the units of the car's values are not confirmed.
    bool has_altitude, has_speed, has_bearing;
    int32_t altitude_e2;        // metres * 100 (field 5)
    int32_t speed_e3;           // metres per second * 1000 (field 6)
    int32_t bearing_e6;         // degrees * 1e6 (field 7)

    LocationFix()
        : lat_e7(0), lon_e7(0), accuracy_e3(0), rx_ms(0), has_altitude(false), has_speed(false), has_bearing(false),
          altitude_e2(0), speed_e3(0), bearing_e6(0) {}
};

// An older fix counts as no fix: the phone stamps each event with its arrival time, so an old position would read as
// the car being there now.
const uint32_t kLocationMaxAgeMs = 5000;

// now_ms must be read after the fix was received. Holds across the 32-bit wrap.
inline bool locationFresh(const LocationFix& f, uint32_t now_ms) {
    return now_ms - f.rx_ms <= kLocationMaxAgeMs;
}

struct SatelliteStatus {
    uint32_t used;              // satellites used in the fix
    uint32_t rx_ms;             // CLOCK_MONOTONIC ms, truncated to 32 bits, when the status was received
};

// The integration guide asks for this sensor with or without a fix and with no silent periods, so it is sent on a timer:
// about once a second, and an old status counts as no satellites.
const uint32_t kSatelliteMaxAgeMs = 5000;
const uint32_t kSatelliteSendEveryMs = 1000;

// What to tell the phone now. now_ms must be read after the status was received. Holds across the 32-bit wrap.
inline uint32_t satellitesUsedNow(bool have, const SatelliteStatus& s, uint32_t now_ms) {
    return have && now_ms - s.rx_ms <= kSatelliteMaxAgeMs ? s.used : 0;
}

inline void sensorPutVar(std::vector<uint8_t>& v, uint64_t x) {
    while (x >= 0x80) { v.push_back((uint8_t)(x | 0x80)); x >>= 7; }
    v.push_back((uint8_t)x);
}

// Plain SensorEventIndication (0x8003) with one LocationData (SensorBatch field 1):
//   {2: latitude_e7, 3: longitude_e7, 4: accuracy_e3, 5: altitude_e2, 6: speed_e3, 7: bearing_e6}, with 5 to 7 only when set.
//   Types and scales are the same in DHU r2.1 and Android Auto 17.9. Negative int32 values go out as 10-byte varints
//   (protobuf int32 sign-extends to 64 bits). There is no fix time: DHU r2.1 writes field 1 as 0, and Android Auto 17.9 has no
//   such field and stamps the arrival time.
inline void buildLocationEvent(const LocationFix& f, std::vector<uint8_t>& out) {
    std::vector<uint8_t> loc;
    loc.push_back(0x10); sensorPutVar(loc, (uint64_t)(int64_t)f.lat_e7);
    loc.push_back(0x18); sensorPutVar(loc, (uint64_t)(int64_t)f.lon_e7);
    loc.push_back(0x20); sensorPutVar(loc, f.accuracy_e3);
    if (f.has_altitude) { loc.push_back(0x28); sensorPutVar(loc, (uint64_t)(int64_t)f.altitude_e2); }
    if (f.has_speed) { loc.push_back(0x30); sensorPutVar(loc, (uint64_t)(int64_t)f.speed_e3); }
    if (f.has_bearing) { loc.push_back(0x38); sensorPutVar(loc, (uint64_t)(int64_t)f.bearing_e6); }
    out.clear();
    out.push_back(0x80); out.push_back(0x03);
    out.push_back(0x0A); sensorPutVar(out, loc.size());
    out.insert(out.end(), loc.begin(), loc.end());
}

// Plain SensorEventIndication (0x8003) with one GpsSatelliteData (SensorBatch field 21, tag 0xAA 0x01): {1: used}, the only
// field the message requires. Field 1 is the number used and field 2 the number in view: Android Auto 17.9 reads at most
// field 2 per-satellite entries, and those include satellites not in use. Field 2 is optional and left out, as nothing in
// that app reads this sensor. Per-satellite entries need an in-use flag the car's list does not have.
inline void buildSatelliteEvent(uint32_t used, std::vector<uint8_t>& out) {
    std::vector<uint8_t> sat;
    sat.push_back(0x08); sensorPutVar(sat, used);
    out.clear();
    out.push_back(0x80); out.push_back(0x03);
    out.push_back(0xAA); out.push_back(0x01); sensorPutVar(out, sat.size());
    out.insert(out.end(), sat.begin(), sat.end());
}

} // namespace nbt

#endif
