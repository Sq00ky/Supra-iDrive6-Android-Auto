// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Register the daemon's full MOST consumer set together: a new HUD route must not disable the vehicle sensors.
#include "cluster/hud_terminal.hpp"
#include "car/ccs_input.hpp"
#include "car/input_activity.hpp"
#include "car/volume_input.hpp"
#include "car/gps.hpp"
#include "car/vehicle.hpp"
#include "most_fixture.hpp"
#include <assert.h>
#include <stdio.h>

using namespace nbt;
static int speeds, gears, brakes, favourites;
static void onSpeed(const CarSpeedSample& s, void*) { assert(s.speed_e3 == 6944); ++speeds; }
static void onGear(int, void*) { ++gears; }
static void onBrake(bool, void*) { ++brakes; }
static void onFavourite(const most::View&, void*) { ++favourites; }

static void send(uint8_t block, uint8_t inst, uint16_t fkt, const uint8_t* data, size_t len) {
    most::Msg m;
    m.txType = 0; m.varTag = 2; m.varVal = 0; m.dst = 0x5001; m.src = 0x2021;
    m.fblock = block; m.inst = inst; m.fkt = fkt; m.opType = most::kOpStatus; m.hdrTag = 0;
    m.data.assign(data, data + len);
    std::vector<uint8_t> wire = most::buildMessage(m);
    fake_dsi::data(most::kInfoMessage, &wire[0], (uint32_t)wire.size());
}

int main() {
    most::Client c;
    hudterm::Config config;
    hudterm::HudTerminal hud(config);
    car::CcsInput knob;
    car::InputActivity activity;
    car::VolumeInput volume;
    car::CarGps gps;
    car::CarVehicle vehicle;
    car::CcsConfig ccs;
    assert(hud.attach(c, false));
    assert(knob.attach(c, ccs, NULL, NULL));
    assert(c.addRoute(0x15, 0x0c, 0xc02, most::kOpStatus, &onFavourite, NULL));
    assert(activity.attach(c, ccs, NULL, NULL));
    assert(volume.attach(c, NULL, NULL));
    assert(gps.attach(c, 0, NULL, NULL, NULL, NULL));
    car::CarVehicleCallbacks cb;
    cb.speed = &onSpeed; cb.gear = &onGear; cb.parkingBrake = &onBrake;
    assert(vehicle.attach(c, cb));
    most_fixture::startClient(c);
    const uint8_t speed[] = { 0x06, 0x40 }, gear = 3, brake = 2, favourite = 1;
    send(car::kVehicleFBlock, car::kVehicleInst, car::kCarSpeedFkt, speed, sizeof(speed));
    send(car::kVehicleFBlock, car::kVehicleInst, car::kVehicleGearFkt, &gear, 1);
    send(car::kVehicleFBlock, car::kVehicleInst, car::kParkingBrakeFkt, &brake, 1);
    send(0x15, 0x0c, 0xc02, &favourite, 1);
    assert(speeds == 1 && gears == 1 && brakes == 1 && favourites == 1);
    fake_dsi::detach();
    printf("test_most_consumers OK\n");
}
