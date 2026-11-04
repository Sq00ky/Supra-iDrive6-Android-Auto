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
//   [9]      HDOP       UINT8   capped at 80 by the sender: taken as 0.1 units (parked: 8 = 0.8)
//   [10]     VDOP       UINT8   same
//   [11..12] Speed      INT16   sent as the internal value; unit unknown (m/s based)
//   [13]     Heading    UINT8   unit unknown (not whole degrees; 360/256 deg?)
//   [14..15] Height     INT16   internal value / 10; unit unknown (parked: 23, maybe 10 m units)
//   [16..17] Year UINT16, [18] Month, [19] Day, [20] Hour, [21] Minute, [22] Second (UINT8, UTC fix time)
// 0xFFFFFFFF in a coordinate means no fix. HDOP sets the accuracy sent to the phone (accuracyFromHdopE3). Speed, heading,
// height and VDOP are decoded and logged but not sent until a drive log confirms their units.
// The same FBlock also sends fkt 0x243 and FBlock 0xCD/01 fkt 0x20B with a second position about 5 m away (probably the
// map-matched / dead-reckoned one). The service discovery says location_characterization RAW_GPS_ONLY (256), so the raw
// GNSS fix (0x440) is the one to send.
//
// Second source: MOST FBlock 0xCD inst 01, fkt 0xC00, OpType Status, NavigationInfo.GPS_RawDataCAN in the firmware (sender in
// NavNBTEvoController, its setters in this order). Payload (10 bytes, big-endian):
//   [0..1]   Speed       UINT16  the raw speed of fkt 0x440 * 7.2; unit unknown; 0xFFFE when over the limit
//   [2], [3] Error in latitude, longitude  UINT8  unit unknown (parked: 25)
//   [4]      Receiver    UINT8   the engine's FIX_NO, FIX_TIME, FIX_2D, FIX_3D (0..3) as 3, 0, 0, 1
//   [5]      Quality     UINT8   meaning unknown (parked: 2)
//   [6]      Satellites  UINT8   used in the fix; over 12 is sent as 14
//   [7]      HDOP        UINT8   same source as fkt 0x440; over 25 is sent as 254
//   [8..9]   Time        UINT16  (hh * 3600 + mm * 60 + ss) mod 4095: the car capture matches the UTC time of fkt 0x440
//
// The decode and the filters are pure: host-tested in tests/test_car_gps.cpp.
//   NBT_GPS_ACCURACY_M=n   a fixed accuracy for every fix, in metres, 2..499, instead of the one from HDOP

#include "aa_sensor_msg.hpp"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
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
    enum { kLogEvery = 5 };     // parked: log the fields every 5th fix, about 5 s. Moving: every fix, to calibrate the units.
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
        r.logFields = fixes_ % kLogEvery == 0 || r.fixChanged || (out.ext && out.speed != 0);
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

struct CarGpsStatus {
    uint16_t speed;             // raw
    uint8_t errLat, errLon;     // raw
    uint8_t receiver;           // kReceiver3dFix, kReceiverNoFix or 0 (time only or 2D)
    uint8_t quality;            // raw
    uint8_t satsUsed;
    uint8_t hdop;               // raw
    uint16_t timeOfDay;
};

const uint8_t kCarStatusFBlock = 0xCD, kCarStatusInst = 0x01;
const uint16_t kCarStatusFkt = 0xC00;
const uint8_t kReceiver3dFix = 1, kReceiverNoFix = 3;

// Decode a fkt 0xC00 Status payload. False when it is too short.
inline bool decodeCarGpsStatus(const uint8_t* p, size_t n, CarGpsStatus& out) {
    if (!p || n < 10) return false;
    out.speed = (uint16_t)((p[0] << 8) | p[1]);
    out.errLat = p[2];
    out.errLon = p[3];
    out.receiver = p[4];
    out.quality = p[5];
    out.satsUsed = p[6];
    out.hdop = p[7];
    out.timeOfDay = (uint16_t)((p[8] << 8) | p[9]);
    return true;
}

// Decides what one received fkt 0xC00 message means. Copies of a message count as nothing new. Its time field advances, so
// a payload that repeats later is not news either: gapMs measures from the last payload that changed.
class CarGpsStatusFilter {
public:
    enum { kLogEvery = 5 };     // log the fields every 5th status
    struct Result {
        bool repeat;            // the same payload as the last one: the other fields are not set, ignore the message
        bool ok;                // the CarGpsStatus is valid
        bool logBad;            // not ok, and not said yet
        bool receiverChanged;   // differs from the last status; the first one counts
        bool logFields;         // time to log this status
        unsigned statusNo;      // statuses so far, this one included
        uint32_t gapMs;         // longest time between two new payloads since the last log
    };

    CarGpsStatusFilter() : seen_(false), lastLen_(0), statuses_(0), lastMs_(0), maxGapMs_(0), haveReceiver_(false),
                           lastReceiver_(0), badLogged_(false) {}

    Result feed(const uint8_t* p, size_t n, uint32_t rxMs, CarGpsStatus& out) {
        Result r;
        r.repeat = false;
        r.ok = false;
        r.logBad = false;
        r.receiverChanged = false;
        r.logFields = false;
        r.statusNo = statuses_;
        r.gapMs = 0;
        if (!p) n = 0;
        size_t keep = n < (size_t)kKeep ? n : (size_t)kKeep;
        if (seen_ && n == lastLen_ && (keep == 0 || memcmp(p, last_, keep) == 0)) {
            r.repeat = true;
            return r;
        }
        if (keep) memcpy(last_, p, keep);
        lastLen_ = n;
        r.ok = decodeCarGpsStatus(p, n, out);
        if (!r.ok) {
            r.logBad = !badLogged_;
            badLogged_ = true;
            seen_ = true;
            return r;
        }
        if (seen_ && statuses_ > 0) {
            uint32_t gap = rxMs - lastMs_;
            if (gap > maxGapMs_) maxGapMs_ = gap;
        }
        seen_ = true;
        lastMs_ = rxMs;
        r.receiverChanged = !haveReceiver_ || out.receiver != lastReceiver_;
        haveReceiver_ = true;
        lastReceiver_ = out.receiver;
        r.logFields = statuses_ % kLogEvery == 0 || r.receiverChanged;
        r.statusNo = ++statuses_;
        if (r.logFields) {
            r.gapMs = maxGapMs_;
            maxGapMs_ = 0;
        }
        return r;
    }

private:
    enum { kKeep = 16 };
    bool seen_;
    uint8_t last_[kKeep];
    size_t lastLen_;
    unsigned statuses_;
    uint32_t lastMs_;
    uint32_t maxGapMs_;
    bool haveReceiver_;
    uint8_t lastReceiver_;
    bool badLogged_;
};

// LocationFix::accuracy_e3 is a radius holding 68% of fixes, in mm. Android Auto 17.9's location code treats 1 m or less and
// 500 m or more as an integration error and stops using the accuracy for the session, so everything here stays inside that.
const uint32_t kUereE3 = 5000;              // the receiver's range error, 5 m
const uint32_t kAccuracyMinE3 = 2000;       // a low HDOP does not make the fix better than this
const uint32_t kAccuracyNoHdopE3 = 10000;   // no usable HDOP on this fix
const uint8_t kHdopRawMax = 80;             // the sender's cap

// HDOP * UERE, the usual estimate of horizontal error. UERE is a constant from published single-frequency error budgets, not
// a measurement of this receiver: they come to about 5.25 m, or 5.6 m once scaled from the 63% radius the formula gives to
// the 68% wanted, and about 4.7 m with better satellite clocks and orbits. HDOP does not see multipath, so the figure is
// optimistic among buildings.
inline uint32_t accuracyFromHdopE3(uint8_t rawHdop) {
    if (rawHdop == 0 || rawHdop > kHdopRawMax) return kAccuracyNoHdopE3;
    const uint32_t a = (uint32_t)rawHdop * kUereE3 / 10;
    return a < kAccuracyMinE3 ? kAccuracyMinE3 : a;
}

// The accuracy override from NBT_GPS_ACCURACY_M: 2..499 m. 0 means none: use HDOP.
inline uint32_t parseGpsAccuracyOverrideE3(const char* metres) {
    int m = metres ? atoi(metres) : 0;
    return (m >= 2 && m <= 499) ? (uint32_t)m * 1000u : 0;
}
inline uint32_t readGpsAccuracyOverrideE3() { return parseGpsAccuracyOverrideE3(getenv("NBT_GPS_ACCURACY_M")); }

// The accuracy to send with a fix. A fix without the extra fields has HDOP 0.
inline uint32_t fixAccuracyE3(const CarFix& f, uint32_t overrideE3) {
    return overrideE3 ? overrideE3 : accuracyFromHdopE3(f.hdop);
}

// The fix in Android Auto units, stamped with the time it was received.
inline LocationFix toLocationFix(const CarFix& f, uint32_t accuracyE3, uint32_t rxMs) {
    LocationFix l;
    l.lat_e7 = f.lat_e7;
    l.lon_e7 = f.lon_e7;
    l.accuracy_e3 = accuracyE3;
    l.rx_ms = rxMs;
    return l;
}

// The age limit on the status behind a position, the same as the one on a position.
const uint32_t kCarStatusMaxAgeMs = kLocationMaxAgeMs;

// Whether a position received at now_ms may go to the phone: the newest status must be recent and report a 3D fix. fkt 0x440
// may keep coming with a held position when the signal is lost. Status 0 covers both time-only and 2D, and only 2D has a
// position, so it does not pass. Sending nothing does not hand over to the phone's GPS everywhere: Android Auto 17.9's own
// features keep the last position they got while location is advertised. An old position that stops updating is still
// better than one sent as new.
inline bool positionUsable(bool haveStatus, const CarGpsStatus& s, uint32_t statusMs, uint32_t nowMs) {
    return haveStatus && nowMs - statusMs <= kCarStatusMaxAgeMs && s.receiver == kReceiver3dFix;
}

// SatelliteStatus::used from a status: what the car says, except that no fix means none. A value the sender cannot produce
// (it caps at 14; a negative count would come out as a large byte) counts as none too.
inline uint32_t satellitesUsed(const CarGpsStatus& s) {
    return s.receiver == kReceiverNoFix || s.satsUsed > 14 ? 0 : s.satsUsed;
}

typedef void (*LocationCb)(const LocationFix& fix, void* user);
typedef void (*SatellitesCb)(const SatelliteStatus& status, void* user);

// A consumer of the observe-only MOST client: routes 52/00 fkt 0x440 and CD/01 fkt 0xC00 Status, logs them under [GPS] and
// hands on each new status, and each new fix that positionUsable() lets through. It adds no device-controller keys: the
// client's subscriptions switch delivery on, and GPS arrives with no key.
// The callbacks run on the DSI callback thread: see the handler rules in most/client.hpp.
class CarGps {
public:
    CarGps();

    // Before the client's start(). accuracyOverrideE3 0: the accuracy comes from each fix's HDOP. False: the client has no
    // room for the route.
    bool attach(most::Client& client, uint32_t accuracyOverrideE3, LocationCb cb, SatellitesCb satCb, void* user);

private:
    static void fixTramp(const most::View& m, void* self);
    static void statusTramp(const most::View& m, void* self);
    void onFix(const most::View& m);
    void onStatus(const most::View& m);

    CarGpsFilter filter_;
    CarGpsStatusFilter statusFilter_;
    // Both handlers run on the DSI callback thread, so these need no lock.
    bool haveStatus_;
    CarGpsStatus status_;
    uint32_t statusMs_;         // when the newest status changed
    int usableLogged_;          // -1 not yet, else the last positionUsable() result logged
    uint32_t accuracyOverrideE3_;
    LocationCb cb_;
    SatellitesCb satCb_;
    void* user_;
};

} // namespace car
} // namespace nbt

#endif
