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

static void testFullConfiguration() {
    fake_dsi::reset();
    most::Client c;
    hudterm::Config config;
    hudterm::HudTerminal hud(config);
    car::CcsInput knob;
    car::InputActivity activity;
    car::VolumeInput volume;
    car::CarGps gps;
    car::CarVehicle vehicle;
    car::CcsConfig ccs;
    for (int i = 0; i < 128; ++i) assert(c.addRoute(0x77, 0, i, most::kOpStatus, &onFavourite, NULL));
    assert(c.requireConsumer("HUD", hud.attach(c, true)));
    assert(c.requireConsumer("knob", knob.attach(c, ccs, NULL, NULL)));
    assert(c.requireConsumer("favourites", c.addRoute(0x15, 0x0c, 0xc02, most::kOpStatus, &onFavourite, NULL)));
    assert(c.requireConsumer("input", activity.attach(c, ccs, NULL, NULL)));
    assert(c.requireConsumer("volume", volume.attach(c, NULL, NULL)));
    assert(c.requireConsumer("GPS", gps.attach(c, 0, NULL, NULL, NULL, NULL)));
    car::CarVehicleCallbacks cb;
    cb.speed = &onSpeed; cb.gear = &onGear; cb.parkingBrake = &onBrake;
    assert(c.requireConsumer("vehicle", vehicle.attach(c, cb)));
    most_fixture::startClient(c);
    most::Msg command;
    command.txType = 0; command.varTag = 1; command.varVal = 0; command.dst = 0x0101; command.src = 0;
    command.fblock = hudterm::kFBlock; command.inst = hudterm::kInst;
    command.fkt = hudterm::kSetVisibility; command.opType = most::kOpStartResultAck;
    command.hdrTag = 0;
    command.data = hudterm::setVisibility(1, true);
    assert(c.send(command));    // normal HUD setup must include its send permission
    const uint8_t speed[] = { 0x06, 0x40 }, gear = 3, brake = 2, favourite = 1;
    send(car::kVehicleFBlock, car::kVehicleInst, car::kCarSpeedFkt, speed, sizeof(speed));
    send(car::kVehicleFBlock, car::kVehicleInst, car::kVehicleGearFkt, &gear, 1);
    send(car::kVehicleFBlock, car::kVehicleInst, car::kParkingBrakeFkt, &brake, 1);
    send(0x15, 0x0c, 0xc02, &favourite, 1);
    assert(speeds == 1 && gears == 1 && brakes == 1 && favourites == 1);
    fake_dsi::detach();
}

static void testPartialFailure() {
    fake_dsi::reset();
    speeds = gears = brakes = 0;
    most::Client missing;
    for (int i = 0; i < most::Client::kMaxKeys; ++i) assert(missing.addDevCtrl((uint16_t)(0x4000 + i)));
    car::CcsInput failedKnob;
    car::CcsConfig ccs;
    assert(!missing.requireConsumer("knob", failedKnob.attach(missing, ccs, NULL, NULL)));
    car::CarVehicle vehicle;
    car::CarVehicleCallbacks cb;
    cb.speed = &onSpeed; cb.gear = &onGear; cb.parkingBrake = &onBrake;
    assert(missing.requireConsumer("vehicle", vehicle.attach(missing, cb)));
    most_fixture::startClient(missing);
    const uint8_t speed[] = { 0x06, 0x40 }, gear = 3, brake = 2;
    send(car::kVehicleFBlock, car::kVehicleInst, car::kCarSpeedFkt, speed, sizeof(speed));
    send(car::kVehicleFBlock, car::kVehicleInst, car::kVehicleGearFkt, &gear, 1);
    send(car::kVehicleFBlock, car::kVehicleInst, car::kParkingBrakeFkt, &brake, 1);
    assert(speeds == 1 && gears == 1 && brakes == 1);   // the failed knob must not block healthy consumers
    fake_dsi::detach();
}

int main() {
    testFullConfiguration();
    testPartialFailure();
    printf("test_most_consumers OK\n");
}
