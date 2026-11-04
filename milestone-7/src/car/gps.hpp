// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CAR_GPS_HPP
#define CAR_GPS_HPP

// The car's GPS position, read from the head unit's own MOST traffic.
//
// Source: MOST FBlock 0x52 inst 00, fkt 0x440, OpType Status (0xC), sent about once a second by the head unit's
// navigation (device 0x2021) to its HMI clients. The MOST observe client gets a copy per receiver.
// Payload (23 bytes, big-endian; found by logging the MOST traffic with the test_gps probe, field names and types
// from the sender in NavNBTEvoController, which logs "[MOST_GPS_RawData] Longitude = .., Latitude = .., GPSFix .."):
//   [0..3]   Longitude  INT32   360/2^32 deg per unit
//   [4..7]   Latitude   INT32   same unit
//   [8]      GPSFix     UINT8   (parked with a fix: 4; firmware names FIX_NO/FIX_TIME/FIX_2D/FIX_3D, values unknown)
//   [9]      HDOP       UINT8   capped at 80 by the sender: probably 0.1 units (parked: 8 = 0.8)
//   [10]     VDOP       UINT8   same
//   [11..12] Speed      INT16   sent as the internal value; unit unknown (m/s based)
//   [13]     Heading    UINT8   unit unknown (not whole degrees; 360/256 deg?)
//   [14..15] Height     INT16   internal value / 10; unit unknown (maybe 10 m units)
//   [16..17] Year UINT16, [18] Month, [19] Day, [20] Hour, [21] Minute, [22] Second (UINT8, UTC fix time)
// 0xFFFFFFFF in a coordinate means no fix. Speed, heading, height and the DOPs are decoded and logged until a drive log
// confirms their units.
// The same FBlock also sends fkt 0x243 and FBlock 0xCD/01 fkt 0x20B with a second position about 5 m away (probably the
// map-matched / dead-reckoned one). This is the raw GNSS fix.
//
// The decode and CarGpsFilter are pure: host-tested in tests/test_car_gps.cpp.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace nbt {

namespace most {
class Client;
struct View;
}

namespace car {

struct CarFix {
    int32_t lat_e7, lon_e7;     // degrees * 1e7
    uint16_t year;              // GPS UTC fix time; year 0 = not present
    uint8_t mon, day, hh, mm, ss;
    bool ext;                   // the fields below are present (23-byte payload)
    uint8_t gpsFix, hdop, vdop; // raw, see the layout above
    int16_t speed;              // raw
    uint8_t heading;            // raw
    int16_t height;             // raw
};

const uint8_t kCarGpsFBlock = 0x52, kCarGpsInst = 0x00;
const uint16_t kCarGpsFkt = 0x440;

inline uint32_t carGpsRdBe32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// 360/2^32 deg units -> 1e-7 deg units, rounded half away from zero (no libm: nbt-aad does not link it).
inline int32_t semi32ToE7(uint32_t raw) {
    double d = (double)(int32_t)raw * (3600000000.0 / 4294967296.0);
    return (int32_t)(d < 0 ? d - 0.5 : d + 0.5);
}

// Decode a fkt 0x440 Status payload. False when it is too short, carries the no-fix filler, or is out of range.
inline bool decodeCarGps(const uint8_t* p, size_t n, CarFix& out) {
    if (!p || n < 8) return false;
    uint32_t lon = carGpsRdBe32(p), lat = carGpsRdBe32(p + 4);
    if (lon == 0xFFFFFFFFu || lat == 0xFFFFFFFFu) return false;
    if (lon == 0 && lat == 0) return false;
    out.lon_e7 = semi32ToE7(lon);
    out.lat_e7 = semi32ToE7(lat);
    if (out.lat_e7 > 900000000 || out.lat_e7 < -900000000) return false;
    out.year = 0;
    out.mon = out.day = out.hh = out.mm = out.ss = 0;
    out.ext = false;
    out.gpsFix = out.hdop = out.vdop = out.heading = 0;
    out.speed = out.height = 0;
    if (n >= 23) {
        out.ext = true;
        out.gpsFix = p[8];
        out.hdop = p[9];
        out.vdop = p[10];
        out.speed = (int16_t)((p[11] << 8) | p[12]);
        out.heading = p[13];
        out.height = (int16_t)((p[14] << 8) | p[15]);
        uint16_t y = (uint16_t)((p[16] << 8) | p[17]);
        if (y >= 2000 && y < 2200 && p[18] >= 1 && p[18] <= 12 && p[19] >= 1 && p[19] <= 31 && p[20] < 24 &&
            p[21] < 60 && p[22] < 61) {
            out.year = y; out.mon = p[18]; out.day = p[19]; out.hh = p[20]; out.mm = p[21]; out.ss = p[22];
        }
    }
    return true;
}

// Decides what one received fkt 0x440 message means. The HMI gets the fix at several addresses, so the same payload
// reaches us up to 4 times a second, and the router does not dedupe. A payload is only a repeat of the one just before
// it: the UTC seconds in it tick every second, so a new fix always differs.
class CarGpsFilter {
public:
    enum { kLogEvery = 5 };     // log the fields every 5th fix, about 5 s
    struct Result {
        bool repeat;            // a copy of the last payload: the other fields are not set, ignore the message
        bool ok;                // a fix: the CarFix is valid
        bool stateChanged;      // the first payload seen, or fix <-> no fix
        bool fixChanged;        // GPSFix differs from the last fix
        bool logFields;         // time to log this fix
        unsigned fixNo;         // fixes so far, this one included
    };

    CarGpsFilter() : seen_(false), lastLen_(0), lastOk_(false), haveGpsFix_(false), lastGpsFix_(0), fixes_(0) {}

    Result feed(const uint8_t* p, size_t n, CarFix& out) {
        Result r;
        r.repeat = false;
        r.ok = false;
        r.stateChanged = false;
        r.fixChanged = false;
        r.logFields = false;
        r.fixNo = fixes_;
        if (!p) n = 0;
        // Compares the part it keeps. The payload is 23 bytes, so anything past kKeep is a message this code cannot read.
        size_t keep = n < (size_t)kKeep ? n : (size_t)kKeep;
        if (seen_ && n == lastLen_ && (keep == 0 || memcmp(p, last_, keep) == 0)) {
            r.repeat = true;
            return r;
        }
        if (keep) memcpy(last_, p, keep);
        lastLen_ = n;

        r.ok = decodeCarGps(p, n, out);
        r.stateChanged = !seen_ || r.ok != lastOk_;
        seen_ = true;
        lastOk_ = r.ok;
        if (!r.ok) return r;

        r.fixChanged = out.ext && haveGpsFix_ && out.gpsFix != lastGpsFix_;
        if (out.ext) { haveGpsFix_ = true; lastGpsFix_ = out.gpsFix; }
        r.logFields = fixes_ % kLogEvery == 0 || r.fixChanged;
        r.fixNo = ++fixes_;
        return r;
    }

private:
    enum { kKeep = 32 };
    bool seen_;
    uint8_t last_[kKeep];
    size_t lastLen_;
    bool lastOk_;
    bool haveGpsFix_;
    uint8_t lastGpsFix_;
    unsigned fixes_;
};

// A consumer of the observe-only MOST client: routes 52/00 fkt 0x440 Status and logs the fix under [GPS]. It adds no
// device-controller keys: the client's subscriptions switch delivery on, and GPS arrives with no key.
class CarGps {
public:
    // Before the client's start(). False: the client has no room for the route.
    bool attach(most::Client& client);

private:
    static void statusTramp(const most::View& m, void* self);
    void onStatus(const most::View& m);

    CarGpsFilter filter_;
};

} // namespace car
} // namespace nbt

#endif
