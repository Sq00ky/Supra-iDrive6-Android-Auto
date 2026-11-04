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
    uint32_t accuracy_e3;       // metres * 1000 (LocationData field 4, required by the protocol)
    uint32_t rx_ms;             // CLOCK_MONOTONIC ms, truncated to 32 bits, when the fix was received
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
//   {2: latitude_e7, 3: longitude_e7, 4: accuracy_e3}. Negative int32 values go out as 10-byte varints
//   (protobuf int32 sign-extends to 64 bits). The fix time (field 1, deprecated) is left out: the phone stamps
//   the arrival time.
inline void buildLocationEvent(const LocationFix& f, std::vector<uint8_t>& out) {
    std::vector<uint8_t> loc;
    loc.push_back(0x10); sensorPutVar(loc, (uint64_t)(int64_t)f.lat_e7);
    loc.push_back(0x18); sensorPutVar(loc, (uint64_t)(int64_t)f.lon_e7);
    loc.push_back(0x20); sensorPutVar(loc, f.accuracy_e3);
    out.clear();
    out.push_back(0x80); out.push_back(0x03);
    out.push_back(0x0A); sensorPutVar(out, loc.size());
    out.insert(out.end(), loc.begin(), loc.end());
}

// Plain SensorEventIndication (0x8003) with one GpsSatelliteData (SensorBatch field 21, tag 0xAA 0x01): {1: used}, the only
// field the message requires. The guide says the required count is the number used, while the Open Android Auto notes, whose
// names are inferred from an obfuscated APK, call field 1 the total and field 2 the number used. Field 2 stays out until that
// is settled: this event says nothing contradictory either way, and 0 means the same under both readings. Per-satellite
// entries need an in-use flag the car's list does not have.
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
