// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for car/gps: MOST 52/00 fkt 440 and CD/01 fkt C00 decode (car captures), their filters, and CarGps on
// most::Client with the fake DsiClient, from DSI frame to LocationFix.
#include "car/gps.hpp"
#include "fake_dsi_client.hpp"
#include "most/client.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <vector>

using namespace nbt;
using namespace nbt::car;
using namespace nbt::most;

// Car capture: 2021 -> 2022, FB 52/00 fkt 440 Status, parked at 53.355370,-1.526293, 2026-11-03 23:08:38 UTC.
static const uint8_t kCapture[] = { 0xfe, 0xea, 0x25, 0x92, 0x25, 0xf1, 0x0c, 0x71, 0x04, 0x08, 0x08, 0x00, 0x00, 0xd7,
                                    0x00, 0x17, 0x07, 0xea, 0x0b, 0x03, 0x17, 0x08, 0x26 };

static void testDecode() {
    {
        CarFix f;
        assert(decodeCarGps(kCapture, sizeof(kCapture), f));
        assert(f.lat_e7 > 533553600 && f.lat_e7 < 533553800);     // 53.35537
        assert(f.lon_e7 < -15262800 && f.lon_e7 > -15263000);     // -1.526293
        assert(f.year == 2026 && f.mon == 11 && f.day == 3 && f.hh == 23 && f.mm == 8 && f.ss == 38);
        // bytes 8..15: GPSFix, HDOP, VDOP, Speed (INT16), Heading, Height (INT16)
        assert(f.ext && f.gpsFix == 4 && f.hdop == 8 && f.vdop == 8 && f.speed == 0 && f.heading == 215 && f.height == 23);
    }
    // No fix: 0xFFFFFFFF filler, zeros, short payload, no payload.
    {
        uint8_t p[23];
        memset(p, 0xFF, sizeof(p));
        CarFix f;
        assert(!decodeCarGps(p, sizeof(p), f));
        memset(p, 0, sizeof(p));
        assert(!decodeCarGps(p, sizeof(p), f));
        assert(!decodeCarGps(kCapture, 7, f));
        assert(!decodeCarGps(NULL, 23, f));
    }
    // A latitude past the pole: 2^30 units is +90 deg, 0x40000002 is just over.
    {
        const uint8_t p[] = { 0x00, 0x00, 0x00, 0x01, 0x40, 0x00, 0x00, 0x02 };
        CarFix f;
        assert(!decodeCarGps(p, sizeof(p), f));
    }
    // 8-byte payload (no time): position only.
    {
        CarFix f;
        assert(decodeCarGps(kCapture, 8, f) && f.year == 0 && !f.ext);
    }
    // Signed 16-bit fields: a negative height and a large speed.
    {
        const uint8_t p[] = { 0xfe, 0xea, 0x25, 0x92, 0x25, 0xf1, 0x0c, 0x71, 0x03, 0x0c, 0x11, 0x01, 0x2c, 0x80,
                              0xff, 0xfe, 0x07, 0xea, 0x0b, 0x03, 0x17, 0x08, 0x26 };
        CarFix f;
        assert(decodeCarGps(p, sizeof(p), f));
        assert(f.gpsFix == 3 && f.hdop == 12 && f.vdop == 17 && f.speed == 300 && f.heading == 128 && f.height == -2);
    }
    // A time that makes no sense is left out; the position is still good.
    {
        uint8_t p[sizeof(kCapture)];
        memcpy(p, kCapture, sizeof(p));
        p[18] = 13;
        CarFix f;
        assert(decodeCarGps(p, sizeof(p), f) && f.ext && f.year == 0 && f.mon == 0);
    }
}

// kCapture with the UTC seconds changed: what the next fix looks like.
static std::vector<uint8_t> tick(uint8_t ss, uint8_t gpsFix = 4) {
    std::vector<uint8_t> p(kCapture, kCapture + sizeof(kCapture));
    p[22] = ss;
    p[8] = gpsFix;
    return p;
}

static void testFilter() {
    // Copies, one per receiver: only the first counts.
    {
        CarGpsFilter flt;
        CarFix f;
        CarGpsFilter::Result r = flt.feed(kCapture, sizeof(kCapture), f);
        assert(!r.repeat && r.ok && r.stateChanged && !r.fixChanged && r.logFields && r.fixNo == 1);
        for (int i = 0; i < 3; ++i) assert(flt.feed(kCapture, sizeof(kCapture), f).repeat);
        std::vector<uint8_t> p = tick(39);
        r = flt.feed(&p[0], p.size(), f);
        assert(!r.repeat && r.ok && !r.stateChanged && r.fixNo == 2);
        assert(f.ss == 39);
    }
    // The log cadence: every 5th fix, counting from the first.
    {
        CarGpsFilter flt;
        CarFix f;
        int logged = 0;
        for (int i = 0; i < 10; ++i) {
            std::vector<uint8_t> p = tick((uint8_t)i);
            CarGpsFilter::Result r = flt.feed(&p[0], p.size(), f);
            assert((unsigned)(i + 1) == r.fixNo);
            assert(r.logFields == (i % 5 == 0));
            if (r.logFields) ++logged;
        }
        assert(logged == 2);
    }
    // Moving: every fix logs, and back to the cadence when the speed is 0 again. The first and sixth fix would log anyway.
    {
        CarGpsFilter flt;
        CarFix f;
        for (int i = 0; i < 12; ++i) {
            std::vector<uint8_t> p = tick((uint8_t)i);
            const bool moving = i >= 2 && i < 8;
            if (moving) p[12] = 0x2c;       // speed 44, raw
            CarGpsFilter::Result r = flt.feed(&p[0], p.size(), f);
            assert(r.logFields == (moving || i % 5 == 0));
        }
    }
    // Reversing, if the speed is negative then, counts as moving too.
    {
        CarGpsFilter flt;
        CarFix f;
        std::vector<uint8_t> p = tick(1);
        flt.feed(&p[0], p.size(), f);
        p = tick(2);
        p[11] = 0xff; p[12] = 0xf0;
        assert(flt.feed(&p[0], p.size(), f).logFields);
        assert(f.speed == -16);
    }
    // GPSFix changing logs at once, off the cadence. The first fix is not a change.
    {
        CarGpsFilter flt;
        CarFix f;
        std::vector<uint8_t> p = tick(1, 4);
        assert(!flt.feed(&p[0], p.size(), f).fixChanged);
        p = tick(2, 4);
        CarGpsFilter::Result r = flt.feed(&p[0], p.size(), f);
        assert(!r.fixChanged && !r.logFields);
        p = tick(3, 3);
        r = flt.feed(&p[0], p.size(), f);
        assert(r.fixChanged && r.logFields && !r.stateChanged);
    }
    // Fix <-> no fix. The filler repeats without saying so again, and a fix after it counts as a change.
    {
        uint8_t none[23];
        memset(none, 0xFF, sizeof(none));
        CarGpsFilter flt;
        CarFix f;
        CarGpsFilter::Result r = flt.feed(none, sizeof(none), f);
        assert(!r.repeat && !r.ok && r.stateChanged && r.fixNo == 0);
        assert(flt.feed(none, sizeof(none), f).repeat);
        r = flt.feed(kCapture, sizeof(kCapture), f);
        assert(r.ok && r.stateChanged && r.fixNo == 1);
        r = flt.feed(none, sizeof(none), f);
        assert(!r.ok && r.stateChanged && r.fixNo == 1);
    }
    // An empty payload is a payload too: seen once, then a repeat.
    {
        CarGpsFilter flt;
        CarFix f;
        const uint8_t any = 0;
        CarGpsFilter::Result r = flt.feed(&any, 0, f);
        assert(!r.repeat && !r.ok && r.stateChanged);
        assert(flt.feed(NULL, 0, f).repeat);
    }
    // Longer than the part it keeps: bounded, and the same message still reads as a repeat.
    {
        uint8_t big[64];
        memset(big, 0x11, sizeof(big));
        CarGpsFilter flt;
        CarFix f;
        assert(!flt.feed(big, sizeof(big), f).repeat);
        assert(flt.feed(big, sizeof(big), f).repeat);
    }
}

// Car capture: 2021 -> 5000, FB CD/01 fkt C00 Status, in the same second as kCapture.
static const uint8_t kStatusCapture[] = { 0x00, 0x00, 0x19, 0x19, 0x01, 0x02, 0x07, 0x08, 0x05, 0x8a };

static void testStatusDecode() {
    CarGpsStatus s;
    assert(decodeCarGpsStatus(kStatusCapture, sizeof(kStatusCapture), s));
    assert(s.speed == 0 && s.errLat == 25 && s.errLon == 25 && s.receiver == kReceiver3dFix && s.quality == 2);
    assert(s.satsUsed == 7 && s.hdop == 8 && s.timeOfDay == 0x058a);

    // The check on the layout: the time field against the UTC time of the fix, and HDOP against the same field there.
    CarFix f;
    assert(decodeCarGps(kCapture, sizeof(kCapture), f));
    assert(((unsigned)f.hh * 3600 + (unsigned)f.mm * 60 + f.ss) % 4095 == s.timeOfDay);
    assert(f.hdop == s.hdop);

    // Big-endian, and each byte in its place.
    const uint8_t p[] = { 0x12, 0x34, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0xab, 0xcd };
    assert(decodeCarGpsStatus(p, sizeof(p), s));
    assert(s.speed == 0x1234 && s.errLat == 1 && s.errLon == 2 && s.receiver == 3 && s.quality == 4 && s.satsUsed == 5);
    assert(s.hdop == 6 && s.timeOfDay == 0xabcd);

    assert(!decodeCarGpsStatus(kStatusCapture, 9, s));
    assert(!decodeCarGpsStatus(NULL, 10, s));
}

static std::vector<uint8_t> statusAt(uint16_t time, uint8_t receiver = kReceiver3dFix, uint8_t used = 7) {
    std::vector<uint8_t> p(kStatusCapture, kStatusCapture + sizeof(kStatusCapture));
    p[4] = receiver;
    p[6] = used;
    p[8] = (uint8_t)(time >> 8);
    p[9] = (uint8_t)time;
    return p;
}

static void testStatusFilter() {
    // Copies are repeats. The first status logs, the next four do not.
    {
        CarGpsStatusFilter flt;
        CarGpsStatus s;
        CarGpsStatusFilter::Result r = flt.feed(kStatusCapture, sizeof(kStatusCapture), 1000, s);
        assert(!r.repeat && r.ok && r.receiverChanged && r.logFields && r.statusNo == 1 && r.gapMs == 0);
        assert(flt.feed(kStatusCapture, sizeof(kStatusCapture), 1002, s).repeat);
        for (int i = 1; i < CarGpsStatusFilter::kLogEvery; ++i) {
            std::vector<uint8_t> p = statusAt((uint16_t)(1000 + i));
            r = flt.feed(&p[0], p.size(), 1000 + i * 1000u, s);
            assert(!r.repeat && r.ok && !r.logFields && !r.receiverChanged && r.statusNo == (unsigned)(i + 1));
        }
        // The longest gap since the last log comes out with the next log, then starts again.
        std::vector<uint8_t> p = statusAt(2000);
        r = flt.feed(&p[0], p.size(), 9000, s);
        assert(r.logFields && r.gapMs == 4000);
        p = statusAt(2001);
        r = flt.feed(&p[0], p.size(), 10000, s);
        assert(!r.logFields && r.gapMs == 0);
    }
    // A receiver change logs at once, off the cadence.
    {
        CarGpsStatusFilter flt;
        CarGpsStatus s;
        std::vector<uint8_t> p = statusAt(1);
        flt.feed(&p[0], p.size(), 0, s);
        p = statusAt(2);
        assert(!flt.feed(&p[0], p.size(), 1000, s).logFields);
        p = statusAt(3, kReceiverNoFix);
        CarGpsStatusFilter::Result r = flt.feed(&p[0], p.size(), 2000, s);
        assert(r.receiverChanged && r.logFields && s.receiver == kReceiverNoFix);
    }
    // A short payload says so once, however it varies, and does not count.
    {
        CarGpsStatusFilter flt;
        CarGpsStatus s;
        const uint8_t a[] = { 1, 2 }, b[] = { 3, 4 };
        CarGpsStatusFilter::Result r = flt.feed(a, sizeof(a), 0, s);
        assert(!r.repeat && !r.ok && r.logBad && r.statusNo == 0);
        r = flt.feed(b, sizeof(b), 1, s);
        assert(!r.repeat && !r.ok && !r.logBad);
        r = flt.feed(kStatusCapture, sizeof(kStatusCapture), 2, s);
        assert(r.ok && r.statusNo == 1 && r.gapMs == 0);
    }
    // The gap holds across the 32-bit wrap of the clock.
    {
        CarGpsStatusFilter flt;
        CarGpsStatus s;
        std::vector<uint8_t> p = statusAt(1);
        flt.feed(&p[0], p.size(), 0xFFFFFF00u, s);
        p = statusAt(2, kReceiverNoFix);
        CarGpsStatusFilter::Result r = flt.feed(&p[0], p.size(), 100, s);
        assert(r.logFields && r.gapMs == 356);
    }
}

// The car sends fkt C00 with variant tag 1, and fkt 440 with tag 2.
static void sendStatusTag(uint32_t varTag, uint8_t fblock, uint8_t inst, uint16_t fkt, uint8_t op, const uint8_t* p, size_t n,
                          uint16_t dst) {
    Msg m;
    m.txType = 0; m.varTag = varTag; m.varVal = 0; m.dst = dst; m.src = 0x2021;
    m.fblock = fblock; m.inst = inst; m.fkt = fkt; m.opType = op; m.hdrTag = 0;
    m.data.assign(p, p + n);
    std::vector<uint8_t> f = buildMessage(m);
    fake_dsi::data(kInfoMessage, &f[0], (uint32_t)f.size());
}

static void sendStatus(uint8_t fblock, uint8_t inst, uint16_t fkt, uint8_t op, const uint8_t* p, size_t n, uint16_t dst) {
    sendStatusTag(2, fblock, inst, fkt, op, p, n, dst);
}

static void respond() {
    std::vector<uint8_t> resp;
    putLe32(resp, 93);
    putLe16(resp, 0x205D);
    fake_dsi::data(kInfoClientResponse, &resp[0], (uint32_t)resp.size());
}

static std::vector<LocationFix> g_fixes;
static void onFix(const LocationFix& f, void* user) {
    assert(user == &g_fixes);
    g_fixes.push_back(f);
}

static std::vector<SatelliteStatus> g_sats;
static void onSats(const SatelliteStatus& s, void* user) {
    assert(user == &g_fixes);
    g_sats.push_back(s);
}

static uint32_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000);
}

static void sendCarStatus(const uint8_t* p, size_t n, uint16_t dst = 0x5000) {
    sendStatusTag(1, kCarStatusFBlock, kCarStatusInst, kCarStatusFkt, kOpStatus, p, n, dst);
}

static void testConsumer() {
    fake_dsi::reset();
    g_fixes.clear();
    g_sats.clear();
    Client c;
    CarGps gps;
    assert(gps.attach(c, 25000, &onFix, &onSats, &g_fixes));
    assert(c.start("NBTAAD_MOST"));     // GPS alone is enough for the client to start (NBT_KNOB=0)
    assert(fake_dsi::opened());
    fake_dsi::attach();
    respond();

    // It adds no keys, so request 1 is skipped: the server logs an error for an empty list.
    for (size_t i = 0; i < fake_dsi::calls.size(); ++i)
        assert(!(fake_dsi::calls[i].kind == fake_dsi::Call::kRequest && fake_dsi::calls[i].id == kReqRegisterDevCtrls));

    // Before any status, a fix is held back.
    std::vector<uint8_t> early = tick(37);
    sendStatus(0x52, 0x00, 0x440, kOpStatus, &early[0], early.size(), 0x2022);
    assert(c.stats().routed == 1);
    assert(g_fixes.empty());

    // CD/01 fkt C00 Status reaches it with the tag the car uses, in the noinfo class. Other functions there do not.
    sendCarStatus(kStatusCapture, sizeof(kStatusCapture));
    assert(c.stats().routed == 2);
    assert(c.stats().noInfo == 1 && c.stats().routedNonNormal == 1);
    sendStatusTag(1, kCarStatusFBlock, kCarStatusInst, 0x20B, kOpStatus, kStatusCapture, sizeof(kStatusCapture), 0x5000);
    assert(c.stats().routed == 2);

    // One count per new status, with a receive time on the clock of the session. A copy does not refresh it.
    assert(g_sats.size() == 1 && g_sats[0].used == 7);
    assert(nowMs() - g_sats[0].rx_ms <= kSatelliteMaxAgeMs);
    sendCarStatus(kStatusCapture, sizeof(kStatusCapture), 0x2035);
    assert(c.stats().routed == 3);
    assert(g_sats.size() == 1);

    // 52/00 fkt 440 Status reaches it, each receiver's copy.
    sendStatus(0x52, 0x00, 0x440, kOpStatus, kCapture, sizeof(kCapture), 0x2022);
    sendStatus(0x52, 0x00, 0x440, kOpStatus, kCapture, sizeof(kCapture), 0x204D);
    assert(c.stats().routed == 5);
    // Other instance, function or op: not routed.
    sendStatus(0x52, 0x01, 0x440, kOpStatus, kCapture, sizeof(kCapture), 0x2022);
    sendStatus(0x52, 0x00, 0x243, kOpStatus, kCapture, sizeof(kCapture), 0x2022);
    sendStatus(0x52, 0x00, 0x440, kOpGet, kCapture, 0, 0x2022);
    assert(c.stats().routed == 5);

    // Two copies of one fix: one LocationFix, in Android Auto units, with a receive time on the clock of the session.
    assert(g_fixes.size() == 1);
    assert(g_fixes[0].lat_e7 > 533553600 && g_fixes[0].lat_e7 < 533553800);
    assert(g_fixes[0].lon_e7 < -15262800 && g_fixes[0].lon_e7 > -15263000);
    assert(g_fixes[0].accuracy_e3 == 25000);    // attached with an override
    assert(locationFresh(g_fixes[0], nowMs()));
    // Parked: a speed of 0 is sent, the held heading is not, and the height never is.
    assert(g_fixes[0].has_speed && g_fixes[0].speed_e3 == 0);
    assert(!g_fixes[0].has_bearing && !g_fixes[0].has_altitude);

    // The next second's fix goes on. A no-fix payload does not, so the last fix ages out on the session's side.
    std::vector<uint8_t> next = tick(39);
    sendStatus(0x52, 0x00, 0x440, kOpStatus, &next[0], next.size(), 0x2022);
    assert(g_fixes.size() == 2);
    uint8_t none[23];
    memset(none, 0xFF, sizeof(none));
    sendStatus(0x52, 0x00, 0x440, kOpStatus, none, sizeof(none), 0x2022);
    assert(g_fixes.size() == 2);

    // The receiver loses the fix and fkt 440 keeps coming with its position: the count drops to 0 whatever the byte says,
    // and the positions stop.
    std::vector<uint8_t> lost = statusAt(0x058b, kReceiverNoFix, 3);
    sendCarStatus(&lost[0], lost.size());
    assert(g_sats.size() == 2 && g_sats[1].used == 0);
    next = tick(40);
    sendStatus(0x52, 0x00, 0x440, kOpStatus, &next[0], next.size(), 0x2022);
    assert(g_fixes.size() == 2);

    // Status 0 is time-only or 2D, so positions stay back, but the count stands.
    std::vector<uint8_t> flat = statusAt(0x058c, 0, 3);
    sendCarStatus(&flat[0], flat.size());
    assert(g_sats.size() == 3 && g_sats[2].used == 3);
    next = tick(41);
    sendStatus(0x52, 0x00, 0x440, kOpStatus, &next[0], next.size(), 0x2022);
    assert(g_fixes.size() == 2);

    // Back to a 3D fix: positions go on.
    std::vector<uint8_t> back = statusAt(0x058d);
    sendCarStatus(&back[0], back.size());
    next = tick(42);
    sendStatus(0x52, 0x00, 0x440, kOpStatus, &next[0], next.size(), 0x2022);
    assert(g_fixes.size() == 3);
}

static void testPositionUsable() {
    CarGpsStatus s;
    assert(decodeCarGpsStatus(kStatusCapture, sizeof(kStatusCapture), s));
    assert(s.receiver == kReceiver3dFix);
    assert(!positionUsable(false, s, 1000, 1000));
    assert(positionUsable(true, s, 1000, 1000));
    // At the age limit, past it, and across the 32-bit wrap.
    assert(positionUsable(true, s, 1000, 1000 + kCarStatusMaxAgeMs));
    assert(!positionUsable(true, s, 1000, 1000 + kCarStatusMaxAgeMs + 1));
    assert(positionUsable(true, s, 0xFFFFFF00u, 100));
    assert(!positionUsable(true, s, 0xFFFFFF00u, 10000));
    // Only a 3D fix passes: no fix, time only or 2D, and anything the sender's table cannot produce do not.
    s.receiver = kReceiverNoFix;
    assert(!positionUsable(true, s, 1000, 1000));
    s.receiver = 0;
    assert(!positionUsable(true, s, 1000, 1000));
    s.receiver = 2;
    assert(!positionUsable(true, s, 1000, 1000));
}

static void testSatellitesUsed() {
    CarGpsStatus s;
    assert(decodeCarGpsStatus(kStatusCapture, sizeof(kStatusCapture), s));
    assert(satellitesUsed(s) == 7);
    s.receiver = 0;             // time only or 2D: the count stands
    assert(satellitesUsed(s) == 7);
    s.receiver = kReceiverNoFix;
    assert(satellitesUsed(s) == 0);
    s.receiver = kReceiver3dFix;
    s.satsUsed = 14;            // the sender's "over 12"
    assert(satellitesUsed(s) == 14);
    s.satsUsed = 255;           // a negative count, through the sender's byte
    assert(satellitesUsed(s) == 0);
}

static void testAccuracyOverride() {
    assert(parseGpsAccuracyOverrideE3(NULL) == 0);
    assert(parseGpsAccuracyOverrideE3("") == 0);
    assert(parseGpsAccuracyOverrideE3("0") == 0);
    assert(parseGpsAccuracyOverrideE3("-5") == 0);
    assert(parseGpsAccuracyOverrideE3("10000") == 0);
    assert(parseGpsAccuracyOverrideE3("rubbish") == 0);
    // Inside the range Android Auto accepts, and at its edges: 1 m and 500 m would disable the accuracy on the phone.
    assert(parseGpsAccuracyOverrideE3("1") == 0);
    assert(parseGpsAccuracyOverrideE3("2") == 2000);
    assert(parseGpsAccuracyOverrideE3("25") == 25000);
    assert(parseGpsAccuracyOverrideE3("499") == 499000);
    assert(parseGpsAccuracyOverrideE3("500") == 0);
}

static void testAccuracyFromHdop() {
    assert(accuracyFromHdopE3(8) == 4000);          // the parked capture: HDOP 0.8
    assert(accuracyFromHdopE3(5) == 2500);          // half a metre per step is kept
    assert(accuracyFromHdopE3(4) == 2000);          // the floor, reached exactly
    assert(accuracyFromHdopE3(1) == 2000);          // below it
    assert(accuracyFromHdopE3(30) == 15000);
    assert(accuracyFromHdopE3(80) == 40000);        // the sender's cap
    // Not a reading: no HDOP, or more than the sender can send.
    assert(accuracyFromHdopE3(0) == 10000);
    assert(accuracyFromHdopE3(81) == 10000);
    assert(accuracyFromHdopE3(255) == 10000);
    // Whatever the byte, the phone keeps using the accuracy: above 1 m and below 500 m.
    for (int raw = 0; raw <= 255; ++raw) {
        const uint32_t a = accuracyFromHdopE3((uint8_t)raw);
        assert(a > 1000 && a < 500000);
    }

    CarFix f;
    assert(decodeCarGps(kCapture, sizeof(kCapture), f));
    assert(fixAccuracyE3(f, 0) == 4000);
    assert(fixAccuracyE3(f, 25000) == 25000);       // an override wins
    assert(decodeCarGps(kCapture, 8, f) && !f.ext); // a fix with no extra fields has no HDOP
    assert(fixAccuracyE3(f, 0) == 10000);
}

// From DSI frame to LocationFix with no override: each fix carries the accuracy of its own HDOP.
static void testAccuracyOnEachFix() {
    fake_dsi::reset();
    g_fixes.clear();
    g_sats.clear();
    Client c;
    CarGps gps;
    assert(gps.attach(c, 0, &onFix, &onSats, &g_fixes));
    assert(c.start("NBTAAD_MOST"));
    fake_dsi::attach();
    respond();
    sendCarStatus(kStatusCapture, sizeof(kStatusCapture));

    sendStatus(0x52, 0x00, 0x440, kOpStatus, kCapture, sizeof(kCapture), 0x2022);
    assert(g_fixes.size() == 1 && g_fixes[0].accuracy_e3 == 4000);

    std::vector<uint8_t> p = tick(39);
    p[9] = 30;      // HDOP 3.0
    sendStatus(0x52, 0x00, 0x440, kOpStatus, &p[0], p.size(), 0x2022);
    assert(g_fixes.size() == 2 && g_fixes[1].accuracy_e3 == 15000);

    p = tick(40);
    p[9] = 0;       // no HDOP
    sendStatus(0x52, 0x00, 0x440, kOpStatus, &p[0], p.size(), 0x2022);
    assert(g_fixes.size() == 3 && g_fixes[2].accuracy_e3 == 10000);

    sendStatus(0x52, 0x00, 0x440, kOpStatus, kCapture, 8, 0x2022);  // no extra fields
    assert(g_fixes.size() == 4 && g_fixes[3].accuracy_e3 == 10000);
}

static CarFix fixWith(int16_t speed, uint8_t heading) {
    CarFix f;
    assert(decodeCarGps(kCapture, sizeof(kCapture), f));
    f.speed = speed;
    f.heading = heading;
    return f;
}

static void testSpeedAndBearing() {
    // Clockwise from anticlockwise raw values: the drive's own samples, with the course measured between the fixes.
    assert(headingToBearingE6(0) == 0);                 // north is 0, not 360
    assert(headingToBearingE6(255) == 1406250);         // just east of north
    assert(headingToBearingE6(213) == 60468750);        // drive: course 60.5 deg
    assert(headingToBearingE6(188) == 95625000);        // drive: course about 104 deg, pulling forward at 1.7 m/s
    assert(headingToBearingE6(127) == 181406250);       // drive: course 181.8 deg
    assert(headingToBearingE6(74) == 255937500);        // drive: reversing, course 270 deg, the way the car moves
    assert(headingToBearingE6(1) == 358593750);         // just west of north, and inside an int32
    // Every byte gives a bearing the phone's field can hold, in 0 to 360 degrees.
    for (int raw = 0; raw <= 255; ++raw) {
        const int32_t b = headingToBearingE6((uint8_t)raw);
        assert(b >= 0 && b < 360000000);
    }

    // Speed: 0.01 m/s raw to mm/s. The fastest of the drive was 14.09 m/s.
    LocationFix l = toLocationFix(fixWith(1409, 213), 3500, 7);
    assert(l.has_speed && l.speed_e3 == 14090);
    assert(l.has_bearing && l.bearing_e6 == 60468750);
    assert(!l.has_altitude);
    assert(l.accuracy_e3 == 3500 && l.rx_ms == 7);

    // Bearing only from 2 m/s: below that the heading can be old. Speed is sent either way, including 0.
    l = toLocationFix(fixWith(199, 74), 3500, 7);
    assert(l.has_speed && l.speed_e3 == 1990 && !l.has_bearing);
    l = toLocationFix(fixWith(200, 74), 3500, 7);
    assert(l.has_speed && l.speed_e3 == 2000 && l.has_bearing && l.bearing_e6 == 255937500);
    l = toLocationFix(fixWith(0, 74), 3500, 7);
    assert(l.has_speed && l.speed_e3 == 0 && !l.has_bearing);

    // Out of range: neither field goes, and the position still does. The sender has not been seen to go negative.
    l = toLocationFix(fixWith(-1, 74), 3500, 7);
    assert(!l.has_speed && !l.has_bearing && l.lat_e7 != 0);
    l = toLocationFix(fixWith(6999, 74), 3500, 7);
    assert(l.has_speed && l.speed_e3 == 69990 && l.has_bearing);
    l = toLocationFix(fixWith(7000, 74), 3500, 7);
    assert(!l.has_speed && !l.has_bearing);
    l = toLocationFix(fixWith(32767, 74), 3500, 7);
    assert(!l.has_speed && !l.has_bearing);

    // A short payload has no speed or heading to send, rather than a speed of 0.
    CarFix shortFix;
    assert(decodeCarGps(kCapture, 8, shortFix) && !shortFix.ext);
    l = toLocationFix(shortFix, 10000, 7);
    assert(!l.has_speed && !l.has_bearing);
}

// From DSI frame to LocationFix, with bytes in the places the car sends them.
static void testSpeedOnEachFix() {
    fake_dsi::reset();
    g_fixes.clear();
    g_sats.clear();
    Client c;
    CarGps gps;
    assert(gps.attach(c, 0, &onFix, &onSats, &g_fixes));
    assert(c.start("NBTAAD_MOST"));
    fake_dsi::attach();
    respond();
    sendCarStatus(kStatusCapture, sizeof(kStatusCapture));

    std::vector<uint8_t> p = tick(39);
    p[11] = 0x05; p[12] = 0x81;     // 1409
    p[13] = 213;
    sendStatus(0x52, 0x00, 0x440, kOpStatus, &p[0], p.size(), 0x2022);
    assert(g_fixes.size() == 1);
    assert(g_fixes[0].has_speed && g_fixes[0].speed_e3 == 14090);
    assert(g_fixes[0].has_bearing && g_fixes[0].bearing_e6 == 60468750);
}

// Full client: attach() says so and the consumer stays out.
static void noRoute(const View&, void*) {}
static void testFull() {
    fake_dsi::reset();
    Client c;
    while (c.addRoute(0x01, kAny, kAny, kAny, &noRoute, NULL)) {}
    CarGps gps;
    assert(!gps.attach(c, 10000, &onFix, &onSats, &g_fixes));
}

// Room for one route only: attach() fails, and what got through stays silent.
static void testOneRouteFits() {
    fake_dsi::reset();
    g_fixes.clear();
    Client c;
    int added = 0;
    while (added < Router::kMaxRoutes - 1 && c.addRoute(0x01, kAny, kAny, kAny, &noRoute, NULL)) ++added;
    assert(added == Router::kMaxRoutes - 1);
    CarGps gps;
    assert(!gps.attach(c, 10000, &onFix, &onSats, &g_fixes));
    assert(c.start("NBTAAD_MOST"));
    fake_dsi::attach();
    respond();
    sendStatus(0x52, 0x00, 0x440, kOpStatus, kCapture, sizeof(kCapture), 0x2022);
    assert(g_fixes.empty());
}

int main() {
    testDecode();
    testFilter();
    testStatusDecode();
    testStatusFilter();
    testConsumer();
    testPositionUsable();
    testSatellitesUsed();
    testAccuracyOverride();
    testAccuracyFromHdop();
    testAccuracyOnEachFix();
    testSpeedAndBearing();
    testSpeedOnEachFix();
    testFull();
    testOneRouteFits();
    printf("test_car_gps OK\n");
    return 0;
}
