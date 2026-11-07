// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CAR_VEHICLE_HPP
#define CAR_VEHICLE_HPP

// Vehicle signals from the head unit's own MOST traffic, on FBlock 0x05 inst 00, OpType Status (0xC). Observe only.
//   fkt 0x621 CarSpeed2           2 bytes, big-endian, 1/64 km/h, 0xFFFF invalid. About 2 Hz while moving. A copy arrives per
//                                 receiver, about 1 ms apart. Nothing is sent before the first movement, and the car
//                                 appears to go quiet while it stands (inferred: the MOST log skips unchanged values).
//   fkt 0x626 DrivingCondition2   byte 0: 0 stopped, 1 forward, 2 reverse, 3 unclear (at low speed, in either direction)
//   fkt 0x228 gear                byte 0: 2 is reverse. Nothing is sent before the first shift.
// The function names are the firmware's. The scale was fitted against the car's GPS speed.

#include "aa_sensor_msg.hpp"
#include "most/client.hpp"

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace car {

const uint8_t kVehicleFBlock = 0x05, kVehicleInst = 0x00;
const uint16_t kCarSpeedFkt = 0x621, kDrivingConditionFkt = 0x626, kVehicleGearFkt = 0x228;

const uint16_t kCarSpeedInvalid = 0xFFFF;
const uint8_t kConditionForward = 1, kConditionReverse = 2;
const uint8_t kVehicleGearReverse = 2;

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

typedef void (*CarSpeedCb)(const CarSpeedSample& speed, void* user);

// A consumer of the observe-only MOST client: three routes and no device-controller keys, like car GPS. It hands on each new
// valid speed. A silent or invalid signal hands on nothing, so no 0 is ever made up here. The callback runs on the DSI
// callback thread: see the handler rules in most/client.hpp.
class CarVehicle {
public:
    CarVehicle();

    // Before the client's start(). False: the client has no room for the routes.
    bool attach(most::Client& client, CarSpeedCb cb, void* user);

private:
    static void speedTramp(const most::View& m, void* self);
    static void conditionTramp(const most::View& m, void* self);
    static void gearTramp(const most::View& m, void* self);
    void onSpeed(const most::View& m);

    CarSpeedFilter filter_;     // only touched on the DSI callback thread
    CarSpeedCb cb_;
    void* user_;
};

} // namespace car
} // namespace nbt

#endif
