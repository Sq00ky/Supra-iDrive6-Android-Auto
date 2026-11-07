// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for car/vehicle: the wheel speed scale, copies, invalid frames and direction, then CarVehicle on most::Client with
// the fake DsiClient, from DSI frame to the callback. All values are made up.
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

// ---- CarVehicle on the client ------------------------------------------------------------------------------------------

static std::vector<CarSpeedSample> g_speeds;
static void onSpeed(const CarSpeedSample& s, void* user) {
    assert(user == &g_speeds);
    g_speeds.push_back(s);
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
    fake_dsi::reset();
    g_speeds.clear();
    Client c;
    CarVehicle v;
    assert(v.attach(c, &onSpeed, &g_speeds));
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

    fake_dsi::detach();
}

// With no room, the callback stays silent even for the routes that fitted.
static void testNoRoom() {
    fake_dsi::reset();
    g_speeds.clear();
    Client c;
    static int dummy;
    struct H { static void none(const View&, void*) {} };
    while (c.addRoute(0x77, kAny, kAny, kAny, &H::none, &dummy)) {
        if (dummy++ == Router::kMaxRoutes - 2) break;   // leaves room for one of the three
    }
    CarVehicle v;
    assert(!v.attach(c, &onSpeed, &g_speeds));
    assert(c.start("NBTAAD_MOST"));
    fake_dsi::attach();
    respond();
    const uint8_t forward[2] = { 0x06, 0x40 };
    send(kCarSpeedFkt, kOpStatus, forward, 2);
    assert(g_speeds.empty());
    fake_dsi::detach();
}

int main() {
    testScale();
    testDecode();
    testCopies();
    testInvalid();
    testDirection();
    testSignedSample();
    testStopReports();
    testConsumer();
    testNoRoom();
    printf("test_car_vehicle OK\n");
    return 0;
}
