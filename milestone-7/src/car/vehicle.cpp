// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "car/vehicle.hpp"

#include <stdio.h>

namespace nbt {
namespace car {

static const char* aaGearName(int aaGear) {
    switch (aaGear) {
    case AA_GEAR_NEUTRAL: return "NEUTRAL";
    case AA_GEAR_DRIVE:   return "DRIVE";
    case AA_GEAR_PARK:    return "PARK";
    case AA_GEAR_REVERSE: return "REVERSE";
    }
    return "?";
}

CarVehicle::CarVehicle() : brake_(&parkingBrakeToAa), gear_(&gearToAa), mostGearHeard_(false) {
    pthread_mutex_init(&gearMutex_, NULL);
}

CarVehicle::~CarVehicle() {
    pthread_mutex_destroy(&gearMutex_);
}

bool CarVehicle::attach(most::Client& client, const CarVehicleCallbacks& callbacks) {
    pthread_mutex_lock(&gearMutex_);
    callbacks_ = callbacks;
    pthread_mutex_unlock(&gearMutex_);

    bool ok = true;
    if (callbacks.speed)
        ok = client.addRoute(kVehicleFBlock, kVehicleInst, kCarSpeedFkt, most::kOpStatus, &CarVehicle::speedTramp, this) &&
             client.addRoute(kVehicleFBlock, kVehicleInst, kDrivingConditionFkt, most::kOpStatus, &CarVehicle::conditionTramp, this);
    if (ok && (callbacks.speed || callbacks.gear))      // speed takes its direction from the gear too
        ok = client.addRoute(kVehicleFBlock, kVehicleInst, kVehicleGearFkt, most::kOpStatus, &CarVehicle::gearTramp, this);
    if (ok && callbacks.parkingBrake)
        ok = client.addRoute(kVehicleFBlock, kVehicleInst, kParkingBrakeFkt, most::kOpStatus, &CarVehicle::parkingBrakeTramp, this);
    if (!ok) {
        printf("[Vehicle] no room on the MOST client: car speed, gear and parking brake disabled\n");
        pthread_mutex_lock(&gearMutex_);
        callbacks_ = CarVehicleCallbacks();     // some routes may have fitted
        pthread_mutex_unlock(&gearMutex_);
        return false;
    }
    if (callbacks.speed)
        printf("[Vehicle] car speed: MOST %02X/%02X fkt %03X -> Android Auto CAR_SPEED, direction from fkt %03X and %03X\n",
               kVehicleFBlock, kVehicleInst, kCarSpeedFkt, kDrivingConditionFkt, kVehicleGearFkt);
    if (callbacks.gear)
        printf("[Vehicle] gear: MOST %02X/%02X fkt %03X -> Android Auto GEAR, from the iDrive app until that function reports\n",
               kVehicleFBlock, kVehicleInst, kVehicleGearFkt);
    if (callbacks.parkingBrake)
        printf("[Vehicle] parking brake: MOST %02X/%02X fkt %03X -> Android Auto PARKING_BRAKE\n", kVehicleFBlock, kVehicleInst,
               kParkingBrakeFkt);
    return true;
}

void CarVehicle::speedTramp(const most::View& m, void* self) { static_cast<CarVehicle*>(self)->onSpeed(m); }

void CarVehicle::conditionTramp(const most::View& m, void* self) {
    static_cast<CarVehicle*>(self)->filter_.feedCondition(m.data, m.len);
}

void CarVehicle::gearTramp(const most::View& m, void* self) { static_cast<CarVehicle*>(self)->onGear(m); }

void CarVehicle::parkingBrakeTramp(const most::View& m, void* self) { static_cast<CarVehicle*>(self)->onParkingBrake(m); }

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
    if (callbacks_.speed) callbacks_.speed(s, callbacks_.user);
}

void CarVehicle::reportGear(const CarStateFilter::Result& r, const char* source) {
    if (r.logBadLength) printf("[Vehicle] gear: payload of another length on fkt %03X, expected 1\n", kVehicleGearFkt);
    if (r.logUnknown) printf("[Vehicle] gear: raw %d from %s says nothing, keeping the last gear\n", r.raw, source);
    if (!r.changed) return;
    printf("[Vehicle] gear: %s (raw %d, from %s)\n", aaGearName(r.value), r.raw, source);
    callbacks_.gear(r.value, callbacks_.user);
}

void CarVehicle::onGear(const most::View& m) {
    filter_.feedGear(m.data, m.len);
    pthread_mutex_lock(&gearMutex_);
    if (callbacks_.gear) {
        const CarStateFilter::Result r = gear_.feed(m.data, m.len);
        if (r.known) mostGearHeard_ = true;
        reportGear(r, "MOST");
    }
    pthread_mutex_unlock(&gearMutex_);
}

void CarVehicle::gearFromApp(int raw) {
    pthread_mutex_lock(&gearMutex_);
    if (callbacks_.gear && !mostGearHeard_) reportGear(gear_.feedRaw(raw), "the iDrive app");
    pthread_mutex_unlock(&gearMutex_);
}

void CarVehicle::onParkingBrake(const most::View& m) {
    const CarStateFilter::Result r = brake_.feed(m.data, m.len);
    if (r.logBadLength)
        printf("[Vehicle] parking brake: %u-byte payload on %02X/%02X fkt %03X, expected 1\n", (unsigned)m.len, m.fblock, m.inst, m.fkt);
    if (r.logUnknown) printf("[Vehicle] parking brake: raw %d says nothing, keeping the last state\n", r.raw);
    if (!r.changed) return;
    printf("[Vehicle] parking brake: %s (raw %d)\n", r.value ? "on" : "released", r.raw);
    if (callbacks_.parkingBrake) callbacks_.parkingBrake(r.value != 0, callbacks_.user);
}

} // namespace car
} // namespace nbt
