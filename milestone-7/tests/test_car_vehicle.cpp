// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for car/vehicle: the wheel speed scale, copies, invalid frames and direction, the gear and parking brake codes and
// their changes, then CarVehicle on most::Client with the fake DsiClient, from DSI frame to the callback. All values are made up.
#include "car/vehicle.hpp"
#include "fake_dsi_client.hpp"
#include "most/ids.hpp"
#include "most/msg.hpp"

#include <assert.h>
#include <stdio.h>
#include <vector>

using namespace nbt;
using namespace nbt::car;
using namespace nbt::most;

typedef CarSpeedFilter::Result Result;

static Result feed(CarSpeedFilter& f, uint16_t raw, uint32_t ms, CarSpeedSample& out) {
    const uint8_t p[2] = { (uint8_t)(raw >> 8), (uint8_t)raw };
    return f.feedSpeed(p, sizeof(p), ms, out);
}

static void condition(CarSpeedFilter& f, uint8_t v) { f.feedCondition(&v, 1); }
static void gear(CarSpeedFilter& f, uint8_t v) { f.feedGear(&v, 1); }

static void testScale() {
    assert(carSpeedE3(0, false) == 0);
    assert(carSpeedE3(0, true) == 0);
    assert(carSpeedE3(1600, false) == 6944);            // 25 km/h
    assert(carSpeedE3(1600, true) == -6944);
    assert(carSpeedE3(6400, false) == 27777);           // 100 km/h
    assert(carSpeedE3(0xFFFE, false) == 284435);        // the largest valid value does not overflow
}

// Big-endian, and the time goes with the reading.
static void testDecode() {
    CarSpeedFilter f;
    CarSpeedSample s = { 123, 456 };
    const uint8_t p[2] = { 0x06, 0x40 };
    Result r = f.feedSpeed(p, 2, 1000, s);
    assert(r.ok && r.first && r.raw == 1600 && s.speed_e3 == 6944 && s.rx_ms == 1000);
    assert(feed(f, 1600, 1500, s).ok && !feed(f, 1600, 2000, s).first);
}

// Copies are dropped by time and not by value: a steady speed is still a new reading each time.
static void testCopies() {
    CarSpeedFilter f;
    CarSpeedSample s = { 0, 0 };
    assert(feed(f, 1600, 1000, s).ok);
    Result r = feed(f, 1600, 1001, s);
    assert(!r.ok && r.copy);
    r = feed(f, 1600, 1000 + kCarSpeedCopyWindowMs - 1, s);
    assert(!r.ok && r.copy && s.rx_ms == 1000);
    r = feed(f, 1600, 1500, s);
    assert(r.ok && !r.copy && s.rx_ms == 1500);
    // Across the 32-bit wrap.
    CarSpeedFilter w;
    assert(feed(w, 1600, 0xFFFFFFFFu, s).ok);
    assert(feed(w, 1600, 10, s).copy);
    assert(feed(w, 1600, 500, s).ok);
}

// An invalid or malformed frame gives no reading and does not change the last one.
static void testInvalid() {
    CarSpeedFilter f;
    CarSpeedSample s = { 7, 8 };
    Result r = feed(f, kCarSpeedInvalid, 1000, s);
    assert(!r.ok && !r.copy && s.speed_e3 == 7 && s.rx_ms == 8);
    const uint8_t p[3] = { 1, 2, 3 };
    r = f.feedSpeed(p, 3, 2000, s);
    assert(!r.ok && r.logBadLength && s.speed_e3 == 7);
    r = f.feedSpeed(p, 1, 3000, s);
    assert(!r.ok && !r.logBadLength);                   // logged once
    r = f.feedSpeed(NULL, 2, 4000, s);
    assert(!r.ok);
    assert(feed(f, 1600, 5000, s).ok && s.speed_e3 == 6944);
}

// 626 when it says 1 or 2, then the gear, then forward.
static void testDirection() {
    CarSpeedFilter f;
    assert(f.condition() == -1 && f.gear() == -1 && !f.reverse());
    condition(f, 3);
    assert(!f.reverse());                               // unclear, with no gear seen
    gear(f, kVehicleGearReverse);
    assert(f.reverse());                                // unclear: the gear decides
    condition(f, 0);
    assert(f.reverse());                                // stopped: the gear decides
    condition(f, kConditionForward);
    assert(!f.reverse());                               // 626 wins over the gear
    gear(f, 5);
    condition(f, kConditionReverse);
    assert(f.reverse());                                // and the other way
    condition(f, 3);
    assert(!f.reverse());
    // An empty payload changes nothing.
    f.feedCondition(NULL, 0);
    f.feedGear(NULL, 0);
    assert(f.condition() == 3 && f.gear() == 5);
}

static void testSignedSample() {
    CarSpeedFilter f;
    CarSpeedSample s = { 0, 0 };
    condition(f, kConditionReverse);
    assert(feed(f, 320, 1000, s).ok && s.speed_e3 == -1388);
    assert(!feed(f, 330, 1500, s).directionChanged);
    condition(f, kConditionForward);
    Result r = feed(f, 320, 2000, s);
    assert(r.ok && r.directionChanged && s.speed_e3 == 1388);
    // A 0 in between does not hide the change, and has no direction of its own.
    condition(f, kConditionReverse);
    r = feed(f, 0, 2500, s);
    assert(r.ok && !r.directionChanged && s.speed_e3 == 0);
    r = feed(f, 320, 3000, s);
    assert(r.ok && r.directionChanged && s.speed_e3 == -1388);
}

// What the log needs to answer: does the car go quiet at a stop, or keep sending 0?
static void testStopReports() {
    CarSpeedSample s = { 0, 0 };
    // Quiet: one 0, then nothing.
    {
        CarSpeedFilter f;
        assert(feed(f, 400, 1000, s).ok);
        Result r = feed(f, 0, 1500, s);
        assert(r.ok && !r.stoppedMs && !r.silentMs);
        r = feed(f, 30, 31500, s);
        assert(r.ok && r.stoppedMs == 30000 && r.zeroFrames == 1);
        assert(!r.silentMs);                            // silence after a 0 is a stop, not a lost signal
        assert(!feed(f, 40, 32000, s).stoppedMs);
    }
    // Still sending.
    {
        CarSpeedFilter f;
        uint32_t t = 1000;
        for (int i = 0; i < 10; ++i, t += 500) assert(feed(f, 0, t, s).ok);
        Result r = feed(f, 30, t, s);
        assert(r.stoppedMs == 5000 && r.zeroFrames == 10);
    }
    // A brief 0 is not a stop worth a line.
    {
        CarSpeedFilter f;
        assert(feed(f, 0, 1000, s).ok);
        assert(!feed(f, 30, 1500, s).stoppedMs);
    }
    // Silence with a moving value before it is reported as silence.
    {
        CarSpeedFilter f;
        assert(feed(f, 400, 1000, s).ok);
        assert(!feed(f, 410, 1500, s).silentMs);
        Result r = feed(f, 420, 6500, s);
        assert(r.silentMs == 5000 && r.lastRaw == 410 && !r.stoppedMs);
    }
}

// Every code the byte can hold: only the four known groups say anything.
static void testGearCodes() {
    assert(gearToAa(1) == AA_GEAR_NEUTRAL);
    assert(gearToAa(2) == AA_GEAR_REVERSE);
    assert(gearToAa(3) == AA_GEAR_PARK);
    for (int raw = 4; raw <= 14; ++raw) assert(gearToAa(raw) == AA_GEAR_DRIVE);
    assert(gearToAa(0) == -1);
    for (int raw = 15; raw <= 255; ++raw) assert(gearToAa(raw) == -1);
    assert(gearToAa(-1) == -1);     // the iDrive app's value when its text has no number
}

static void testParkingBrakeCodes() {
    assert(parkingBrakeToAa(1) == 0);
    assert(parkingBrakeToAa(2) == 1);
    assert(parkingBrakeToAa(0) == -1);
    assert(parkingBrakeToAa(3) == -1);
    assert(parkingBrakeToAa(255) == -1);
}

// Copies and forward gears are not changes, an unknown value keeps the state, and each log line comes once.
static void testStateFilter() {
    CarStateFilter f(&gearToAa);
    typedef CarStateFilter::Result R;
    uint8_t b = 3;
    R r = f.feed(&b, 1);
    assert(r.known && r.changed && r.value == AA_GEAR_PARK && r.raw == 3);
    r = f.feed(&b, 1);                  // another receiver's copy
    assert(r.known && !r.changed);
    b = 5;
    assert(f.feed(&b, 1).changed);
    b = 6;
    r = f.feed(&b, 1);                  // a forward gear to the next is still DRIVE
    assert(r.known && !r.changed && r.value == AA_GEAR_DRIVE);
    b = 15;
    r = f.feed(&b, 1);
    assert(!r.known && !r.changed && r.logUnknown && r.raw == 15);
    r = f.feed(&b, 1);
    assert(!r.known && !r.logUnknown);  // logged once for this value
    b = 0;
    assert(f.feed(&b, 1).logUnknown);   // and once for another
    b = 7;
    r = f.feed(&b, 1);                  // the unknown ones did not lose the state
    assert(r.known && !r.changed);
    // A malformed frame says nothing, and is logged once.
    const uint8_t two[2] = { 2, 2 };
    r = f.feed(two, 2);
    assert(!r.known && !r.changed && r.logBadLength);
    r = f.feed(two, 0);
    assert(!r.known && !r.logBadLength);
    r = f.feed(NULL, 1);
    assert(!r.known && !r.logBadLength);
    b = 2;
    r = f.feed(&b, 1);
    assert(r.changed && r.value == AA_GEAR_REVERSE);
    // The first value is always a change, even one whose value is 0.
    CarStateFilter n(&gearToAa);
    assert(n.feedRaw(1).changed);
    CarStateFilter brake(&parkingBrakeToAa);
    r = brake.feedRaw(1);
    assert(r.changed && r.value == 0);
    assert(!brake.feedRaw(1).changed);
    assert(brake.feedRaw(2).changed);
    r = brake.feedRaw(3);               // invalid: still on
    assert(!r.known && !r.changed);
    r = brake.feedRaw(2);
    assert(r.known && !r.changed);
}

// ---- CarVehicle on the client ------------------------------------------------------------------------------------------

static std::vector<CarSpeedSample> g_speeds;
static std::vector<int> g_gears;
static std::vector<bool> g_brakes;

static void onSpeed(const CarSpeedSample& s, void* user) {
    assert(user == &g_speeds);
    g_speeds.push_back(s);
}

static void onGear(int gear, void* user) {
    assert(user == &g_speeds);
    g_gears.push_back(gear);
}

static void onBrake(bool on, void* user) {
    assert(user == &g_speeds);
    g_brakes.push_back(on);
}

static CarVehicleCallbacks allCallbacks() {
    CarVehicleCallbacks cb;
    cb.speed = &onSpeed;
    cb.gear = &onGear;
    cb.parkingBrake = &onBrake;
    cb.user = &g_speeds;
    return cb;
}

static void resetCaptured() {
    fake_dsi::reset();
    g_speeds.clear();
    g_gears.clear();
    g_brakes.clear();
}

static void send(uint16_t fkt, uint8_t op, const uint8_t* p, size_t n, uint8_t fblock = kVehicleFBlock, uint8_t inst = kVehicleInst) {
    Msg m;
    m.txType = 0; m.varTag = 2; m.varVal = 0; m.dst = 0x5001; m.src = 0x2021;
    m.fblock = fblock; m.inst = inst; m.fkt = fkt; m.opType = op; m.hdrTag = 0;
    m.data.assign(p, p + n);
    std::vector<uint8_t> f = buildMessage(m);
    fake_dsi::data(kInfoMessage, &f[0], (uint32_t)f.size());
}

static void respond() {
    std::vector<uint8_t> resp;
    putLe32(resp, 93);
    putLe16(resp, 0x205D);
    fake_dsi::data(kInfoClientResponse, &resp[0], (uint32_t)resp.size());
}

static void testConsumer() {
    resetCaptured();
    Client c;
    CarVehicle v;
    CarVehicleCallbacks speedOnly;
    speedOnly.speed = &onSpeed;
    speedOnly.user = &g_speeds;
    assert(v.attach(c, speedOnly));
    assert(c.start("NBTAAD_MOST"));     // speed alone is enough for the client to start
    fake_dsi::attach();
    respond();
    // Observe only: no keys, so nothing but the client's own subscriptions and setup goes out.
    for (size_t i = 0; i < fake_dsi::calls.size(); ++i)
        assert(!(fake_dsi::calls[i].kind == fake_dsi::Call::kRequest && fake_dsi::calls[i].id == kReqRegisterDevCtrls));

    const uint8_t forward[2] = { 0x06, 0x40 };
    send(kCarSpeedFkt, kOpStatus, forward, 2);
    assert(g_speeds.size() == 1 && g_speeds[0].speed_e3 == 6944);
    send(kCarSpeedFkt, kOpStatus, forward, 2);          // another receiver's copy
    assert(g_speeds.size() == 1);

    // Not ours: another op, instance, FBlock or function.
    const uint32_t before = (uint32_t)g_speeds.size();
    send(kCarSpeedFkt, kOpGet, forward, 2);
    send(kCarSpeedFkt, kOpStatus, forward, 2, kVehicleFBlock, 0x01);
    send(kCarSpeedFkt, kOpStatus, forward, 2, 0x06);
    send(0x622, kOpStatus, forward, 2);
    assert(g_speeds.size() == before);

    // The direction reaches the sample through its own routes.
    const uint8_t rev = kConditionReverse;
    send(kDrivingConditionFkt, kOpStatus, &rev, 1);
    struct timespec nap = { 0, (long)(kCarSpeedCopyWindowMs + 20) * 1000000L };
    nanosleep(&nap, NULL);
    send(kCarSpeedFkt, kOpStatus, forward, 2);
    assert(g_speeds.size() == before + 1 && g_speeds.back().speed_e3 == -6944);
    const uint8_t unclear = 3, inReverse = kVehicleGearReverse, inDrive = 5;
    send(kDrivingConditionFkt, kOpStatus, &unclear, 1);
    send(kVehicleGearFkt, kOpStatus, &inDrive, 1);
    nanosleep(&nap, NULL);
    send(kCarSpeedFkt, kOpStatus, forward, 2);
    assert(g_speeds.back().speed_e3 == 6944);
    send(kVehicleGearFkt, kOpStatus, &inReverse, 1);
    nanosleep(&nap, NULL);
    send(kCarSpeedFkt, kOpStatus, forward, 2);
    assert(g_speeds.back().speed_e3 == -6944);
    assert(g_gears.empty() && g_brakes.empty());        // both off: the gear route is there for the speed's direction only

    fake_dsi::detach();
}

static void sendByte(uint16_t fkt, uint8_t value) { send(fkt, kOpStatus, &value, 1); }

// From DSI frame to callback: P, R, N, D and the forward gears, with copies, and the hand brake.
static void testGearAndBrake() {
    resetCaptured();
    Client c;
    CarVehicle v;
    assert(v.attach(c, allCallbacks()));
    assert(c.start("NBTAAD_MOST"));
    fake_dsi::attach();
    respond();

    // Nothing is made up before the car has reported.
    assert(g_gears.empty() && g_brakes.empty());

    sendByte(kVehicleGearFkt, 3);
    sendByte(kVehicleGearFkt, 3);                       // another receiver's copy
    assert(g_gears.size() == 1 && g_gears[0] == AA_GEAR_PARK);
    sendByte(kVehicleGearFkt, 2);
    sendByte(kVehicleGearFkt, 1);
    sendByte(kVehicleGearFkt, 5);
    assert(g_gears.size() == 4 && g_gears[1] == AA_GEAR_REVERSE && g_gears[2] == AA_GEAR_NEUTRAL && g_gears[3] == AA_GEAR_DRIVE);
    sendByte(kVehicleGearFkt, 6);                       // up through the forward gears: still DRIVE
    sendByte(kVehicleGearFkt, 11);
    sendByte(kVehicleGearFkt, 15);                      // says nothing
    assert(g_gears.size() == 4);

    sendByte(kParkingBrakeFkt, 2);
    sendByte(kParkingBrakeFkt, 2);                      // a copy
    assert(g_brakes.size() == 1 && g_brakes[0]);
    sendByte(kParkingBrakeFkt, 1);
    assert(g_brakes.size() == 2 && !g_brakes[1]);
    sendByte(kParkingBrakeFkt, 0);                      // not coded, and invalid: released stays
    sendByte(kParkingBrakeFkt, 3);
    assert(g_brakes.size() == 2);

    // Not ours: another op, instance, FBlock or function, and a malformed payload.
    const uint8_t on = 2;
    send(kParkingBrakeFkt, kOpGet, &on, 1);
    send(kParkingBrakeFkt, kOpStatus, &on, 1, kVehicleFBlock, 0x01);
    send(kParkingBrakeFkt, kOpStatus, &on, 1, 0x06);
    send(0x210, kOpStatus, &on, 1);
    const uint8_t two[2] = { 2, 2 };
    send(kParkingBrakeFkt, kOpStatus, two, 2);
    assert(g_brakes.size() == 2);

    // The speed still takes its direction from the same gear route. 626 has said nothing here.
    sendByte(kVehicleGearFkt, 2);
    const uint8_t moving[2] = { 0x06, 0x40 };
    send(kCarSpeedFkt, kOpStatus, moving, 2);
    assert(g_speeds.size() == 1 && g_speeds[0].speed_e3 == -6944);

    fake_dsi::detach();
}

// The app gives a gear before the first shift, and stops counting once the car has said something. A shift the app reports
// after the car does is neither sent twice nor sent late.
static void testGearFromApp() {
    resetCaptured();
    Client c;
    CarVehicle v;
    v.gearFromApp(3);                                   // before attach: the app's file is read again after it
    assert(g_gears.empty());
    assert(v.attach(c, allCallbacks()));
    assert(c.start("NBTAAD_MOST"));
    fake_dsi::attach();
    respond();

    v.gearFromApp(3);
    assert(g_gears.size() == 1 && g_gears[0] == AA_GEAR_PARK);
    v.gearFromApp(3);                                   // no change
    assert(g_gears.size() == 1);
    v.gearFromApp(-1);                                  // its text had no number: says nothing
    v.gearFromApp(2);
    assert(g_gears.size() == 2 && g_gears[1] == AA_GEAR_REVERSE);

    // A value from the car that says nothing does not take over from the app.
    sendByte(kVehicleGearFkt, 15);
    v.gearFromApp(1);
    assert(g_gears.size() == 3 && g_gears[2] == AA_GEAR_NEUTRAL);

    // The car's first value, the same as the app's: no second event, but the car has taken over now.
    sendByte(kVehicleGearFkt, 1);
    assert(g_gears.size() == 3);
    v.gearFromApp(3);
    assert(g_gears.size() == 3);
    sendByte(kVehicleGearFkt, 5);
    assert(g_gears.size() == 4 && g_gears[3] == AA_GEAR_DRIVE);
    v.gearFromApp(5);
    v.gearFromApp(2);
    assert(g_gears.size() == 4);

    fake_dsi::detach();
}

// With the gear off, the app's value and the route's go nowhere, and the speed still gets its direction.
static void testSignalsOff() {
    resetCaptured();
    Client c;
    CarVehicle v;
    CarVehicleCallbacks cb = allCallbacks();
    cb.gear = NULL;
    cb.parkingBrake = NULL;
    assert(v.attach(c, cb));
    assert(c.start("NBTAAD_MOST"));
    fake_dsi::attach();
    respond();
    v.gearFromApp(3);
    sendByte(kVehicleGearFkt, kVehicleGearReverse);
    sendByte(kParkingBrakeFkt, 2);
    assert(g_gears.empty() && g_brakes.empty());
    const uint8_t moving[2] = { 0x06, 0x40 };
    send(kCarSpeedFkt, kOpStatus, moving, 2);
    assert(g_speeds.size() == 1 && g_speeds[0].speed_e3 == -6944);
    fake_dsi::detach();

    // Speed off: only the routes the others need.
    resetCaptured();
    Client c2;
    CarVehicle v2;
    cb = allCallbacks();
    cb.speed = NULL;
    assert(v2.attach(c2, cb));
    assert(c2.start("NBTAAD_MOST"));
    fake_dsi::attach();
    respond();
    send(kCarSpeedFkt, kOpStatus, moving, 2);
    assert(g_speeds.empty());
    sendByte(kVehicleGearFkt, 3);
    sendByte(kParkingBrakeFkt, 2);
    assert(g_gears.size() == 1 && g_brakes.size() == 1);
    fake_dsi::detach();

    // All off: nothing to route, and not an error.
    resetCaptured();
    Client c3;
    CarVehicle v3;
    assert(v3.attach(c3, CarVehicleCallbacks()));
}

// How many free routes the three signals together need: speed 621 and 626, gear 228, brake CB5.
enum { kVehicleRoutes = 4 };

// With no room, every callback stays silent even for the routes that fitted. freeRoutes is how many are left for it.
static void checkNoRoom(int freeRoutes) {
    resetCaptured();
    Client c;
    int added = 0;
    struct H { static void none(const View&, void*) {} };
    while (added < Router::kMaxRoutes - freeRoutes) {
        assert(c.addRoute(0x77, kAny, kAny, kAny, &H::none, NULL));
        ++added;
    }
    CarVehicle v;
    assert(!v.attach(c, allCallbacks()));
    assert(c.start("NBTAAD_MOST"));
    fake_dsi::attach();
    respond();
    const uint8_t forward[2] = { 0x06, 0x40 };
    send(kCarSpeedFkt, kOpStatus, forward, 2);
    sendByte(kVehicleGearFkt, 3);
    sendByte(kParkingBrakeFkt, 2);
    v.gearFromApp(3);
    assert(g_speeds.empty() && g_gears.empty() && g_brakes.empty());
    fake_dsi::detach();
}

static void testNoRoom() {
    for (int freeRoutes = 0; freeRoutes < kVehicleRoutes; ++freeRoutes) checkNoRoom(freeRoutes);
}

// Exactly enough room is enough, so the counts above are right.
static void testExactRoom() {
    resetCaptured();
    Client c;
    struct H { static void none(const View&, void*) {} };
    for (int i = 0; i < Router::kMaxRoutes - kVehicleRoutes; ++i) assert(c.addRoute(0x77, kAny, kAny, kAny, &H::none, NULL));
    CarVehicle v;
    assert(v.attach(c, allCallbacks()));
}

int main() {
    testScale();
    testDecode();
    testCopies();
    testInvalid();
    testDirection();
    testSignedSample();
    testStopReports();
    testGearCodes();
    testParkingBrakeCodes();
    testStateFilter();
    testConsumer();
    testGearAndBrake();
    testGearFromApp();
    testSignalsOff();
    testNoRoom();
    testExactRoom();
    printf("test_car_vehicle OK\n");
    return 0;
}
