// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AA_SENSOR_MSG_HPP
#define AA_SENSOR_MSG_HPP

// Android Auto sensor channel: the LOCATION, COMPASS, GPS_SATELLITE, CAR_SPEED, GEAR and PARKING_BRAKE events, in Android Auto
// units. It knows nothing about where the data came from. Pure: host-tested in tests/test_aa_sensor_msg.cpp.

#include "aa_proto.hpp"

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace nbt {

struct LocationFix {
    int32_t lat_e7, lon_e7;     // degrees * 1e7
    uint32_t accuracy_e3;       // metres * 1000 (LocationData field 4, optional: only fields 2 and 3 are required)
    uint32_t rx_ms;             // CLOCK_MONOTONIC ms, truncated to 32 bits, when the fix was received
    // Optional, and off until set.
    bool has_altitude, has_speed, has_bearing;
    int32_t altitude_e2;        // metres above the WGS84 ellipsoid * 100 (field 5)
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

// The compass goes out as each new heading arrives. With nothing new it repeats the last one, as the car's value stays true
// while the car stands still and the integration guide wants about 1 Hz. The car sends once a second, give or take 10 ms, so
// this does not fire between messages, and it stays inside the 1.5 s Google Maps 26.44 seems to allow for a current compass.
const uint32_t kCompassRepeatMs = 1100;

// Whether a repeat is due. Holds across the 32-bit wrap.
inline bool compassRepeatDue(uint32_t last_sent_ms, uint32_t now_ms) {
    return now_ms - last_sent_ms >= kCompassRepeatMs;
}

struct CarSpeedSample {
    int32_t speed_e3;           // metres per second * 1000, negative in reverse
    uint32_t rx_ms;             // CLOCK_MONOTONIC ms, truncated to 32 bits, when it was received
};

// An old moving speed is not sent: nothing says the car still does that speed. A 0 has no age limit, because the car can
// stay silent while it stands, and it is repeated so the phone keeps hearing it. The repeat only ever follows a 0 the car
// reported: silence after a moving speed does not become a 0.
const uint32_t kCarSpeedMaxAgeMs = 2000;
const uint32_t kCarSpeedRepeatZeroEveryMs = 1000;

// now_ms must be read after the sample was received. Holds across the 32-bit wrap.
inline bool carSpeedSendable(const CarSpeedSample& s, uint32_t now_ms) {
    return s.speed_e3 == 0 || now_ms - s.rx_ms <= kCarSpeedMaxAgeMs;
}

// GearData field 1. DHU r2.1 also takes 1 to 10 for a selected gear, which an automatic does not report.
enum AaGear { AA_GEAR_NEUTRAL = 0, AA_GEAR_DRIVE = 100, AA_GEAR_PARK = 101, AA_GEAR_REVERSE = 102 };

enum { AA_MSG_SENSOR_EVENT = 0x8003 };

// Plain SensorEventIndication (0x8003) with one LocationData (SensorBatch field 1):
//   {2: latitude_e7, 3: longitude_e7, 4: accuracy_e3, 5: altitude_e2, 6: speed_e3, 7: bearing_e6}, with 5 to 7 only when set.
//   Types and scales are the same in DHU r2.1 and Android Auto 17.9. There is no fix time: DHU r2.1 writes field 1 as 0, and
//   Android Auto 17.9 has no such field and stamps the arrival time.
inline void buildLocationEvent(const LocationFix& f, std::vector<uint8_t>& out) {
    enum { F_LOCATION = 1 };
    enum { F_LATITUDE = 2, F_LONGITUDE = 3, F_ACCURACY = 4, F_ALTITUDE = 5, F_SPEED = 6, F_BEARING = 7 };
    proto::ProtoWriter location;
    location.integer(F_LATITUDE, f.lat_e7);
    location.integer(F_LONGITUDE, f.lon_e7);
    location.unsignedInteger(F_ACCURACY, f.accuracy_e3);
    if (f.has_altitude) location.integer(F_ALTITUDE, f.altitude_e2);
    if (f.has_speed) location.integer(F_SPEED, f.speed_e3);
    if (f.has_bearing) location.integer(F_BEARING, f.bearing_e6);
    proto::ProtoWriter event(AA_MSG_SENSOR_EVENT);
    event.message(F_LOCATION, location);
    out = event.bytes();
}

// Plain SensorEventIndication (0x8003) with one GpsSatelliteData (SensorBatch field 21): {1: used}, the only field the message
// requires. Field 1 is the number used and field 2 the number in view: Android Auto 17.9 reads at most
// field 2 per-satellite entries, and those include satellites not in use. Field 2 is optional and left out, as nothing in
// that app reads this sensor. Per-satellite entries need an in-use flag the car's list does not have.
inline void buildSatelliteEvent(uint32_t used, std::vector<uint8_t>& out) {
    enum { F_GPS_SATELLITE = 21 };
    enum { F_USED = 1 };
    proto::ProtoWriter satellites;
    satellites.unsignedInteger(F_USED, used);
    proto::ProtoWriter event(AA_MSG_SENSOR_EVENT);
    event.message(F_GPS_SATELLITE, satellites);
    out = event.bytes();
}

// Plain SensorEventIndication (0x8003) with one CompassData (SensorBatch field 2): {1: bearing_e6}, degrees * 1e6, clockwise
// from north. Field and scale are from DHU r2.1, and Android Auto 17.9 has the same scale. Pitch (2) and roll (3) are optional
// and left out, as the car's value has neither.
inline void buildCompassEvent(int32_t bearing_e6, std::vector<uint8_t>& out) {
    enum { F_COMPASS = 2 };
    enum { F_BEARING = 1 };
    proto::ProtoWriter compass;
    compass.integer(F_BEARING, bearing_e6);
    proto::ProtoWriter event(AA_MSG_SENSOR_EVENT);
    event.message(F_COMPASS, compass);
    out = event.bytes();
}

// Plain SensorEventIndication (0x8003) with one speed entry (SensorBatch field 3): {1: speed_e3}, written even when 0.
inline void buildCarSpeedEvent(int32_t speed_e3, std::vector<uint8_t>& out) {
    enum { F_CAR_SPEED = 3 };
    enum { F_SPEED_E3 = 1 };
    proto::ProtoWriter speed;
    speed.integer(F_SPEED_E3, speed_e3);
    proto::ProtoWriter event(AA_MSG_SENSOR_EVENT);
    event.message(F_CAR_SPEED, speed);
    out = event.bytes();
}

// Plain SensorEventIndication (0x8003) with one GearData (SensorBatch field 8): {1: AaGear}, written even for NEUTRAL, as 0.
// DHU r2.1 requires the field.
inline void buildGearEvent(int aaGear, std::vector<uint8_t>& out) {
    enum { F_GEAR_DATA = 8 };
    enum { F_GEAR = 1 };
    proto::ProtoWriter gear;
    gear.integer(F_GEAR, aaGear);
    proto::ProtoWriter event(AA_MSG_SENSOR_EVENT);
    event.message(F_GEAR_DATA, gear);
    out = event.bytes();
}

// Plain SensorEventIndication (0x8003) with one ParkingBrakeData (SensorBatch field 7): {1: on}, written even when released.
// DHU r2.1 requires the field.
inline void buildParkingBrakeEvent(bool on, std::vector<uint8_t>& out) {
    enum { F_PARKING_BRAKE = 7 };
    enum { F_ON = 1 };
    proto::ProtoWriter brake;
    brake.integer(F_ON, on ? 1 : 0);
    proto::ProtoWriter event(AA_MSG_SENSOR_EVENT);
    event.message(F_PARKING_BRAKE, brake);
    out = event.bytes();
}

} // namespace nbt

#endif
