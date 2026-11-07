// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "car/vehicle.hpp"

#include <stdio.h>

namespace nbt {
namespace car {

CarVehicle::CarVehicle() : cb_(NULL), user_(NULL) {}

bool CarVehicle::attach(most::Client& client, CarSpeedCb cb, void* user) {
    cb_ = cb;
    user_ = user;
    if (!client.addRoute(kVehicleFBlock, kVehicleInst, kCarSpeedFkt, most::kOpStatus, &CarVehicle::speedTramp, this) ||
        !client.addRoute(kVehicleFBlock, kVehicleInst, kDrivingConditionFkt, most::kOpStatus, &CarVehicle::conditionTramp, this) ||
        !client.addRoute(kVehicleFBlock, kVehicleInst, kVehicleGearFkt, most::kOpStatus, &CarVehicle::gearTramp, this)) {
        printf("[Vehicle] no room on the MOST client: car speed disabled\n");
        cb_ = NULL;     // some routes may have fitted
        return false;
    }
    printf("[Vehicle] car speed: MOST %02X/%02X fkt %03X -> Android Auto CAR_SPEED, direction from fkt %03X and %03X\n",
           kVehicleFBlock, kVehicleInst, kCarSpeedFkt, kDrivingConditionFkt, kVehicleGearFkt);
    return true;
}

void CarVehicle::speedTramp(const most::View& m, void* self) { static_cast<CarVehicle*>(self)->onSpeed(m); }

void CarVehicle::conditionTramp(const most::View& m, void* self) {
    static_cast<CarVehicle*>(self)->filter_.feedCondition(m.data, m.len);
}

void CarVehicle::gearTramp(const most::View& m, void* self) {
    static_cast<CarVehicle*>(self)->filter_.feedGear(m.data, m.len);
}

void CarVehicle::onSpeed(const most::View& m) {
    CarSpeedSample s;
    s.speed_e3 = 0;
    s.rx_ms = 0;
    CarSpeedFilter::Result r = filter_.feedSpeed(m.data, m.len, m.rxMs, s);
    if (r.logBadLength)
        printf("[Vehicle] speed: %u-byte payload on %02X/%02X fkt %03X, expected 2\n", (unsigned)m.len, m.fblock, m.inst, m.fkt);
    if (!r.ok) return;
    // Each of these happens at most once per stop or shift, so the log stays bounded.
    if (r.first) printf("[Vehicle] speed: first reading raw=%u\n", r.raw);
    if (r.silentMs)
        printf("[Vehicle] speed: silent for %.1f s after raw=%u, now raw=%u\n", r.silentMs / 1000.0, r.lastRaw, r.raw);
    if (r.stoppedMs)
        printf("[Vehicle] speed: moving again after %.1f s stopped, %u zero frames\n", r.stoppedMs / 1000.0, r.zeroFrames);
    if (r.directionChanged)
        printf("[Vehicle] speed: direction now %s (626=%d, 228=%d)\n", filter_.reverse() ? "reverse" : "forward",
               filter_.condition(), filter_.gear());
    if (cb_) cb_(s, user_);
}

} // namespace car
} // namespace nbt
