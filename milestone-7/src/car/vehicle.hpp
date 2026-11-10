// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CAR_VEHICLE_HPP
#define CAR_VEHICLE_HPP

// Vehicle signals from the head unit's own MOST traffic, on FBlock 0x05 inst 00, OpType Status (0xC). Observe only.
//   fkt 0x621 CarSpeed2           2 bytes, big-endian, 1/64 km/h, 0xFFFF invalid. About 2 Hz while moving. A copy arrives per
//                                 receiver, about 1 ms apart. Nothing is sent before the first movement, and the car
//                                 appears to go quiet while it stands (inferred: the MOST log skips unchanged values).
//   fkt 0x626 DrivingCondition2   byte 0: 0 stopped, 1 forward, 2 reverse, 3 unclear (at low speed, in either direction)
//   fkt 0x228 gear                byte 0: 1 N, 2 R, 3 P, 4 and up driving (5 and up follow the forward gear in an automatic),
//                                 0 and 15 say nothing. Nothing is sent before the first shift. The firmware's speed lock
//                                 reads this function, 1 as N and 3 as P. Manual gearbox values are not known.
//   fkt 0xCB5 hand brake          byte 0: 1 released, 2 on, others say nothing. The firmware's speed lock reads this function
//                                 as statusContactHandBrake and falls back to fkt 0xD01 on the others; 0xD01 is not read
//                                 here. Nothing is sent before the first report, so the state at start is not known.
// The function names are the firmware's. The scale was fitted against the car's GPS speed.

#include "aa_sensor_msg.hpp"
#include "most/client.hpp"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace nbt {
namespace car {

const uint8_t kVehicleFBlock = 0x05, kVehicleInst = 0x00;
const uint16_t kCarSpeedFkt = 0x621, kDrivingConditionFkt = 0x626, kVehicleGearFkt = 0x228, kParkingBrakeFkt = 0xCB5;

const uint16_t kCarSpeedInvalid = 0xFFFF;
const uint8_t kConditionForward = 1, kConditionReverse = 2;
const uint8_t kVehicleGearNeutral = 1, kVehicleGearReverse = 2, kVehicleGearPark = 3;
const uint8_t kVehicleGearDriveFirst = 4, kVehicleGearDriveLast = 14;
const uint8_t kParkingBrakeReleased = 1, kParkingBrakeOn = 2;

// Copies of one message arrive about 1 ms apart and new readings about 500 ms apart, so this tells them apart.
const uint32_t kCarSpeedCopyWindowMs = 100;
// A silence or a stop shorter than this is ordinary and not logged.
const uint32_t kCarSpeedLogAfterMs = 2000;

// 1/64 km/h to mm/s: raw * 1000 / (64 * 3.6). The largest raw value is well inside 32 bits at each step.
inline int32_t carSpeedE3(uint16_t raw, bool reverse) {
    const int32_t v = (int32_t)((uint32_t)raw * 625u / 144u);
    return reverse ? -v : v;
}

// Decodes fkt 621 and keeps what its sign depends on. Pure: host-tested in tests/test_car_vehicle.cpp.
class CarSpeedFilter {
public:
    struct Result {
        bool ok;                // out is a new reading
        bool copy;              // another receiver's copy of the last message
        bool logBadLength;      // the first frame of a length other than 2
        bool first;             // the first reading since start
        bool directionChanged;  // this reading's direction differs from the last non-zero one
        uint16_t raw;           // when ok
        uint32_t silentMs;      // over kCarSpeedLogAfterMs: this long since the last frame, with lastRaw non-zero
        uint16_t lastRaw;       // the reading before this one
        uint32_t stoppedMs;     // over kCarSpeedLogAfterMs: moving again after this long at 0
        uint32_t zeroFrames;    // how many 0 frames that stop held: 1 means the car went quiet, many that it kept sending
    };

    CarSpeedFilter()
        : condition_(-1), gear_(-1), haveFrame_(false), lastMs_(0), lastRaw_(0), haveReading_(false), lastReverse_(false),
          haveDirection_(false), stopped_(false), stoppedSinceMs_(0), zeroFrames_(0), badLengthLogged_(false) {}

    Result feedSpeed(const uint8_t* p, size_t n, uint32_t rxMs, CarSpeedSample& out) {
        Result r = Result();
        if (n != 2 || !p) {
            r.logBadLength = !badLengthLogged_;
            badLengthLogged_ = true;
            return r;
        }
        if (haveFrame_ && rxMs - lastMs_ < kCarSpeedCopyWindowMs) {
            r.copy = true;
            return r;
        }
        const uint32_t gap = rxMs - lastMs_;
        const bool hadFrame = haveFrame_;
        haveFrame_ = true;
        lastMs_ = rxMs;
        const uint16_t raw = (uint16_t)((p[0] << 8) | p[1]);
        if (raw == kCarSpeedInvalid) return r;

        const bool rev = reverse();
        r.ok = true;
        r.raw = raw;
        r.lastRaw = lastRaw_;
        r.first = !haveReading_;
        if (hadFrame && haveReading_ && lastRaw_ != 0 && gap >= kCarSpeedLogAfterMs) r.silentMs = gap;
        if (raw == 0) {
            if (!stopped_) {
                stopped_ = true;
                stoppedSinceMs_ = rxMs;
                zeroFrames_ = 0;
            }
            ++zeroFrames_;
        } else {
            if (stopped_ && rxMs - stoppedSinceMs_ >= kCarSpeedLogAfterMs) {
                r.stoppedMs = rxMs - stoppedSinceMs_;
                r.zeroFrames = zeroFrames_;
            }
            stopped_ = false;
            r.directionChanged = haveDirection_ && rev != lastReverse_;
            haveDirection_ = true;
            lastReverse_ = rev;
        }
        haveReading_ = true;
        lastRaw_ = raw;
        out.speed_e3 = carSpeedE3(raw, rev);
        out.rx_ms = rxMs;
        return r;
    }

    void feedCondition(const uint8_t* p, size_t n) {
        if (p && n >= 1) condition_ = p[0];
    }
    void feedGear(const uint8_t* p, size_t n) {
        if (p && n >= 1) gear_ = p[0];
    }

    // fkt 626 when it says which way, otherwise the gear. Forward when neither has said anything.
    bool reverse() const {
        if (condition_ == kConditionForward) return false;
        if (condition_ == kConditionReverse) return true;
        return gear_ == kVehicleGearReverse;
    }
    int condition() const { return condition_; }    // -1 before the first message
    int gear() const { return gear_; }              // -1 before the first message

private:
    int condition_, gear_;
    bool haveFrame_;
    uint32_t lastMs_;
    uint16_t lastRaw_;
    bool haveReading_, lastReverse_, haveDirection_, stopped_;
    uint32_t stoppedSinceMs_, zeroFrames_;
    bool badLengthLogged_;
};

// The car's raw gear as the value Android Auto takes, or -1 for one that says nothing.
inline int gearToAa(int raw) {
    if (raw == kVehicleGearNeutral) return AA_GEAR_NEUTRAL;
    if (raw == kVehicleGearReverse) return AA_GEAR_REVERSE;
    if (raw == kVehicleGearPark) return AA_GEAR_PARK;
    if (raw >= kVehicleGearDriveFirst && raw <= kVehicleGearDriveLast) return AA_GEAR_DRIVE;
    return -1;
}

// The car's raw hand brake as 0 released or 1 on, or -1 for one that says nothing.
inline int parkingBrakeToAa(int raw) {
    if (raw == kParkingBrakeReleased) return 0;
    if (raw == kParkingBrakeOn) return 1;
    return -1;
}

// A state the car reports in one byte and Android Auto wants on change. Compares what would be sent, not the raw value or
// the time: that drops the copies per receiver, and a shift from one forward gear to the next, which is still DRIVE. A raw
// value that says nothing leaves the last state as it was. Pure: host-tested in tests/test_car_vehicle.cpp.
class CarStateFilter {
public:
    typedef int (*Decode)(int raw);     // the value to send, or -1 for a raw value that says nothing

    struct Result {
        bool known;             // raw says something: value is the state now
        bool changed;           // and it differs from the last known one
        bool logBadLength;      // the first frame of a length other than 1
        bool logUnknown;        // the first time this raw value said nothing
        int raw;
        int value;
    };

    explicit CarStateFilter(Decode decode) : decode_(decode), have_(false), last_(-1), badLengthLogged_(false) {
        memset(unknownLogged_, 0, sizeof(unknownLogged_));
    }

    Result feed(const uint8_t* p, size_t n) {
        if (n != 1 || !p) {
            Result r = Result();
            r.logBadLength = !badLengthLogged_;
            badLengthLogged_ = true;
            return r;
        }
        return feedRaw(p[0]);
    }

    Result feedRaw(int raw) {
        Result r = Result();
        r.raw = raw;
        r.value = decode_(raw);
        if (r.value < 0) {
            bool& logged = unknownLogged_[(uint8_t)raw];
            r.logUnknown = !logged;
            logged = true;
            return r;
        }
        r.known = true;
        r.changed = !have_ || r.value != last_;
        have_ = true;
        last_ = r.value;
        return r;
    }

private:
    Decode decode_;
    bool have_;
    int last_;
    bool badLengthLogged_;
    bool unknownLogged_[256];
};

typedef void (*CarSpeedCb)(const CarSpeedSample& speed, void* user);
typedef void (*CarGearCb)(int aaGear, void* user);              // AaGear
typedef void (*CarParkingBrakeCb)(bool on, void* user);

// A NULL callback turns that signal off.
struct CarVehicleCallbacks {
    CarSpeedCb speed;
    CarGearCb gear;
    CarParkingBrakeCb parkingBrake;
    void* user;
    CarVehicleCallbacks() : speed(NULL), gear(NULL), parkingBrake(NULL), user(NULL) {}
};

// A consumer of the observe-only MOST client: up to four routes and no device-controller keys, like car GPS. It hands on each
// new valid speed, and the gear and parking brake when they change. A silent or invalid signal hands on nothing, so no value
// is ever made up here. The callbacks run on the DSI callback thread, or for the gear on the thread calling gearFromApp: see
// the handler rules in most/client.hpp.
class CarVehicle {
public:
    CarVehicle();
    ~CarVehicle();

    // Before the client's start(). False: the client has no room for the routes, and every callback stays silent.
    bool attach(most::Client& client, const CarVehicleCallbacks& callbacks);

    // The gear from the iDrive app (raw, same codes as fkt 228). It fills in until fkt 228 has said something, which gives a
    // gear at start, before the first shift, and on a car without that function. Ignored after that. Thread-safe, and safe
    // before attach.
    void gearFromApp(int raw);

private:
    static void speedTramp(const most::View& m, void* self);
    static void conditionTramp(const most::View& m, void* self);
    static void gearTramp(const most::View& m, void* self);
    static void parkingBrakeTramp(const most::View& m, void* self);
    void onSpeed(const most::View& m);
    void onGear(const most::View& m);
    void onParkingBrake(const most::View& m);
    void reportGear(const CarStateFilter::Result& r, const char* source);   // with gearMutex_ held

    CarSpeedFilter filter_;         // only touched on the DSI callback thread
    CarStateFilter brake_;          // only touched on the DSI callback thread
    // Held across the callback, so the gears are posted in the order they were decided: a stale gear from the app must not
    // land after a newer one from the car, because nothing would correct it before the next shift.
    pthread_mutex_t gearMutex_;
    CarStateFilter gear_;           // guarded by gearMutex_
    bool mostGearHeard_;            // guarded by gearMutex_: fkt 228 has said something, so the app's gear no longer counts
    CarVehicleCallbacks callbacks_; // gear guarded by gearMutex_, as gearFromApp can run before attach. The rest is set before start.
};

} // namespace car
} // namespace nbt

#endif
