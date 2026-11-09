// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for the sensor channel in aa_session: from SensorStartRequest on a socket to the events on it, with the stub
// cryptor. Frames with the encryption flag off go straight to the dispatcher, so no TLS is needed in either direction.
#include "stub_cryptor.hpp"
#include "aa_session.cpp"
#include <assert.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace nbt;

static uint32_t testNowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000);
}

static bool readAll(int fd, uint8_t* p, size_t n, int timeoutMs) {
    size_t got = 0;
    const uint32_t start = testNowMs();
    while (got < n) {
        const int left = timeoutMs - (int)(testNowMs() - start);
        if (left <= 0) return false;
        struct pollfd pf;
        pf.fd = fd;
        pf.events = POLLIN;
        pf.revents = 0;
        if (poll(&pf, 1, left) <= 0) return false;
        ssize_t r = read(fd, p + got, n - got);
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

// One frame from the session: channel and payload. False on a timeout.
static bool readFrame(int fd, int timeoutMs, uint8_t& channel, std::vector<uint8_t>& payload) {
    uint8_t h[4];
    if (!readAll(fd, h, 4, timeoutMs)) return false;
    channel = h[0];
    payload.resize((size_t)((h[2] << 8) | h[3]));
    return payload.empty() || readAll(fd, &payload[0], payload.size(), timeoutMs);
}

// A plain SensorStartRequest on channel 1: {1: type, 2: period}, as a bulk frame with the encryption flag off.
static void sendStart(int fd, uint8_t type, const std::vector<uint8_t>& periodVarint) {
    std::vector<uint8_t> body;
    body.push_back(0x80); body.push_back(0x01);
    body.push_back(0x08); body.push_back(type);
    if (!periodVarint.empty()) {
        body.push_back(0x10);
        body.insert(body.end(), periodVarint.begin(), periodVarint.end());
    }
    std::vector<uint8_t> frame;
    frame.push_back(0x01);
    frame.push_back(0x03);
    frame.push_back(0x00);
    frame.push_back((uint8_t)body.size());
    frame.insert(frame.end(), body.begin(), body.end());
    assert(write(fd, &frame[0], frame.size()) == (ssize_t)frame.size());
}

static std::vector<uint8_t> period0() { return std::vector<uint8_t>(1, 0x00); }

static bool isStartResponse(const std::vector<uint8_t>& p) {
    return p.size() == 4 && p[0] == 0x80 && p[1] == 0x02 && p[2] == 0x08 && p[3] == 0x00;
}

// Satellite event bytes for a count.
static std::vector<uint8_t> satEvent(uint32_t used) {
    std::vector<uint8_t> want;
    buildSatelliteEvent(used, want);
    return want;
}

// The next frame on channel 1, skipping anything else.
static bool nextSensorFrame(int fd, int timeoutMs, std::vector<uint8_t>& p) {
    const uint32_t start = testNowMs();
    for (;;) {
        const int left = timeoutMs - (int)(testNowMs() - start);
        if (left <= 0) return false;
        uint8_t ch = 0;
        if (!readFrame(fd, left, ch, p)) return false;
        if (ch == 1) return true;
    }
}

static AASession* newSession(int sv[2]) {
    AASession* s = new AASession();
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    assert(s->start(sv[0]));
    uint8_t ch = 0xFF;
    std::vector<uint8_t> p;
    assert(readFrame(sv[1], 2000, ch, p));      // the VersionRequest
    assert(ch == 0 && p.size() >= 2 && p[1] == 0x01);
    return s;
}

static void endSession(AASession* s, int sv[2]) {
    s->stop();
    close(sv[1]);
    delete s;
}

// Sensor 21 with nothing from the car: started, answered, and 0 at once. Then it keeps coming with the phone silent, because
// the guide wants no silent periods.
static void testSatellitesNoData() {
    int sv[2];
    AASession* s = newSession(sv);
    std::vector<uint8_t> p;
    assert(!nextSensorFrame(sv[1], 1300, p));   // not before the phone starts it
    sendStart(sv[1], 21, period0());
    assert(nextSensorFrame(sv[1], 2000, p) && isStartResponse(p));
    assert(nextSensorFrame(sv[1], 2000, p) && p == satEvent(0));
    uint32_t t0 = testNowMs();
    for (int i = 0; i < 2; ++i) {
        assert(nextSensorFrame(sv[1], 2500, p) && p == satEvent(0));
        uint32_t t1 = testNowMs();
        assert(t1 - t0 >= 800);                 // about once a second, not on every pass of the loop
        t0 = t1;
    }
    endSession(s, sv);
}

// A posted count goes out, and one past the age limit counts as none.
static void testSatellitesPosted() {
    int sv[2];
    AASession* s = newSession(sv);
    SatelliteStatus st;
    st.used = 7;
    st.rx_ms = testNowMs();
    s->postCarSatellites(st);       // before the phone starts the sensor: its first event carries it
    sendStart(sv[1], 21, period0());
    std::vector<uint8_t> p;
    assert(nextSensorFrame(sv[1], 2000, p) && isStartResponse(p));
    assert(nextSensorFrame(sv[1], 2000, p) && p == satEvent(7));
    st.used = 9;
    st.rx_ms = testNowMs();
    s->postCarSatellites(st);
    assert(nextSensorFrame(sv[1], 2500, p) && p == satEvent(9));
    st.rx_ms = testNowMs() - kSatelliteMaxAgeMs - 1;
    s->postCarSatellites(st);
    assert(nextSensorFrame(sv[1], 2500, p) && p == satEvent(0));
    endSession(s, sv);
}

// The location path on the same socket: nothing until sensor 1 starts, then a fresh position on start and on each post. An
// old position does not go out.
static void testLocation() {
    int sv[2];
    AASession* s = newSession(sv);
    LocationFix f;
    f.lat_e7 = 525000000; f.lon_e7 = -45000000; f.accuracy_e3 = 10000; f.rx_ms = testNowMs();
    s->postCarLocation(f);
    std::vector<uint8_t> p, want;
    assert(!nextSensorFrame(sv[1], 300, p));
    sendStart(sv[1], 1, period0());
    assert(nextSensorFrame(sv[1], 2000, p) && isStartResponse(p));
    buildLocationEvent(f, want);
    assert(nextSensorFrame(sv[1], 2000, p) && p == want);
    f.lat_e7 += 100;
    f.rx_ms = testNowMs();
    s->postCarLocation(f);
    buildLocationEvent(f, want);
    assert(nextSensorFrame(sv[1], 2000, p) && p == want);
    f.lat_e7 += 100;
    f.rx_ms = testNowMs() - kLocationMaxAgeMs - 1;
    s->postCarLocation(f);
    assert(!nextSensorFrame(sv[1], 500, p));
    endSession(s, sv);
}

static std::vector<uint8_t> compassEvent(int32_t bearing_e6) {
    std::vector<uint8_t> want;
    buildCompassEvent(bearing_e6, want);
    return want;
}

// Sensor 2 with nothing from the car: started and answered, but no made-up direction. The first heading goes out on arrival,
// without waiting for a timer.
static void testCompassNoData() {
    int sv[2];
    AASession* s = newSession(sv);
    std::vector<uint8_t> p;
    sendStart(sv[1], 2, period0());
    assert(nextSensorFrame(sv[1], 2000, p) && isStartResponse(p));
    assert(!nextSensorFrame(sv[1], 1300, p));
    const uint32_t t0 = testNowMs();
    s->postCarCompass(90000000);
    assert(nextSensorFrame(sv[1], 900, p) && p == compassEvent(90000000));
    assert(testNowMs() - t0 < 500);
    endSession(s, sv);
}

// A heading posted before the start is not sent until the phone starts the sensor, and its first event carries it. Each new
// heading then goes out on arrival, and it puts the repeat back by a full interval.
static void testCompassPosted() {
    int sv[2];
    AASession* s = newSession(sv);
    std::vector<uint8_t> p;
    s->postCarCompass(90000000);
    assert(!nextSensorFrame(sv[1], 1300, p));
    sendStart(sv[1], 2, period0());
    assert(nextSensorFrame(sv[1], 2000, p) && isStartResponse(p));
    assert(nextSensorFrame(sv[1], 2000, p) && p == compassEvent(90000000));
    const uint32_t t0 = testNowMs();
    s->postCarCompass(180000000);
    assert(nextSensorFrame(sv[1], 900, p) && p == compassEvent(180000000));
    assert(testNowMs() - t0 < 500);
    assert(!nextSensorFrame(sv[1], kCompassRepeatMs - 200, p));
    assert(nextSensorFrame(sv[1], 1000, p) && p == compassEvent(180000000));
    endSession(s, sv);
}

// With nothing new and the phone silent, the last heading keeps going out: not on every pass of the loop, and not stretched to
// the 2 s that 1 s select() ticks would give.
static void testCompassRepeats() {
    int sv[2];
    AASession* s = newSession(sv);
    std::vector<uint8_t> p;
    s->postCarCompass(270000000);
    sendStart(sv[1], 2, period0());
    assert(nextSensorFrame(sv[1], 2000, p) && isStartResponse(p));
    assert(nextSensorFrame(sv[1], 2000, p) && p == compassEvent(270000000));
    uint32_t t0 = testNowMs();
    for (int i = 0; i < 3; ++i) {
        assert(nextSensorFrame(sv[1], 2500, p) && p == compassEvent(270000000));
        const uint32_t t1 = testNowMs();
        assert(t1 - t0 >= kCompassRepeatMs - 100);
        assert(t1 - t0 < 1900);
        t0 = t1;
    }
    endSession(s, sv);
}

// Wakes restart the select() tick, so wakes faster than the tick mean it never times out. The repeat must still go out. An old
// position makes a wake that sends nothing.
static void testCompassRepeatsThroughWakes() {
    int sv[2];
    AASession* s = newSession(sv);
    std::vector<uint8_t> p;
    sendStart(sv[1], 1, period0());
    assert(nextSensorFrame(sv[1], 2000, p) && isStartResponse(p));
    s->postCarCompass(90000000);
    sendStart(sv[1], 2, period0());
    assert(nextSensorFrame(sv[1], 2000, p) && isStartResponse(p));
    assert(nextSensorFrame(sv[1], 2000, p) && p == compassEvent(90000000));
    LocationFix old;
    old.lat_e7 = 525000000; old.lon_e7 = -45000000; old.accuracy_e3 = 10000;
    const uint32_t start = testNowMs();
    uint32_t last = start;
    int repeats = 0;
    while (testNowMs() - start < 3000) {
        old.rx_ms = testNowMs() - kLocationMaxAgeMs - 1;
        s->postCarLocation(old);
        if (nextSensorFrame(sv[1], 100, p)) {
            assert(p == compassEvent(90000000));
            assert(testNowMs() - last < 1900);
            last = testNowMs();
            ++repeats;
        }
    }
    assert(repeats >= 2);
    endSession(s, sv);
}

// The heading is kept when a session ends, including one that arrives between sessions, and nothing goes out until the next
// session's phone starts the sensor.
static void testCompassAcrossSessions() {
    int sv[2];
    AASession* s = newSession(sv);
    std::vector<uint8_t> p;
    s->postCarCompass(90000000);
    sendStart(sv[1], 2, period0());
    assert(nextSensorFrame(sv[1], 2000, p) && isStartResponse(p));
    assert(nextSensorFrame(sv[1], 2000, p) && p == compassEvent(90000000));
    s->stop();
    close(sv[1]);
    s->postCarCompass(180000000);

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    assert(s->start(sv[0]));
    uint8_t ch = 0xFF;
    assert(readFrame(sv[1], 2000, ch, p));      // the VersionRequest
    assert(!nextSensorFrame(sv[1], 1300, p));
    sendStart(sv[1], 2, period0());
    assert(nextSensorFrame(sv[1], 2000, p) && isStartResponse(p));
    assert(nextSensorFrame(sv[1], 2000, p) && p == compassEvent(180000000));
    endSession(s, sv);
}

static std::vector<uint8_t> speedEvent(int32_t speed_e3) {
    std::vector<uint8_t> want;
    buildCarSpeedEvent(speed_e3, want);
    return want;
}

static CarSpeedSample speedNow(int32_t speed_e3, uint32_t ageMs = 0) {
    CarSpeedSample s;
    s.speed_e3 = speed_e3;
    s.rx_ms = testNowMs() - ageMs;
    return s;
}

// Sensor 3 with nothing from the car: answered, and silent. That also covers NBT_CAR_SPEED=0, which posts nothing.
static void testSpeedNoData() {
    int sv[2];
    AASession* s = newSession(sv);
    std::vector<uint8_t> p;
    sendStart(sv[1], 3, period0());
    assert(nextSensorFrame(sv[1], 2000, p) && isStartResponse(p));
    assert(!nextSensorFrame(sv[1], 1300, p));
    endSession(s, sv);
}

// Nothing until the phone starts it. Then the newest on start and on each post, with the sign. An old moving speed stays home.
static void testSpeedPosted() {
    int sv[2];
    AASession* s = newSession(sv);
    std::vector<uint8_t> p;
    s->postCarSpeed(speedNow(6944));
    assert(!nextSensorFrame(sv[1], 300, p));
    sendStart(sv[1], 3, period0());
    assert(nextSensorFrame(sv[1], 2000, p) && isStartResponse(p));
    assert(nextSensorFrame(sv[1], 2000, p) && p == speedEvent(6944));
    s->postCarSpeed(speedNow(-1388));
    assert(nextSensorFrame(sv[1], 2000, p) && p == speedEvent(-1388));
    // A moving speed is not repeated: silence does not become a claim about now.
    assert(!nextSensorFrame(sv[1], 1300, p));
    s->postCarSpeed(speedNow(6944, kCarSpeedMaxAgeMs + 1));
    assert(!nextSensorFrame(sv[1], 500, p));
    endSession(s, sv);
}

// A start with an old moving speed sends nothing, and the next reading goes out as usual.
static void testSpeedStaleAtStart() {
    int sv[2];
    AASession* s = newSession(sv);
    std::vector<uint8_t> p;
    s->postCarSpeed(speedNow(6944, kCarSpeedMaxAgeMs + 1));
    sendStart(sv[1], 3, period0());
    assert(nextSensorFrame(sv[1], 2000, p) && isStartResponse(p));
    assert(!nextSensorFrame(sv[1], 1300, p));
    s->postCarSpeed(speedNow(7000));
    assert(nextSensorFrame(sv[1], 2000, p) && p == speedEvent(7000));
    endSession(s, sv);
}

// A 0 has no age limit and repeats about once a second with the car silent, until the car moves again.
static void testSpeedZeroRepeats() {
    int sv[2];
    AASession* s = newSession(sv);
    std::vector<uint8_t> p;
    s->postCarSpeed(speedNow(0, 60000));
    sendStart(sv[1], 3, period0());
    assert(nextSensorFrame(sv[1], 2000, p) && isStartResponse(p));
    assert(nextSensorFrame(sv[1], 2000, p) && p == speedEvent(0));
    uint32_t t0 = testNowMs();
    for (int i = 0; i < 2; ++i) {
        assert(nextSensorFrame(sv[1], 2500, p) && p == speedEvent(0));
        uint32_t t1 = testNowMs();
        assert(t1 - t0 >= 800);                 // about once a second, not on every pass of the loop
        t0 = t1;
    }
    s->postCarSpeed(speedNow(6944));
    assert(nextSensorFrame(sv[1], 2000, p) && p == speedEvent(6944));
    assert(!nextSensorFrame(sv[1], 1300, p));
    endSession(s, sv);
}

// The speed survives a new session, but the phone must start the sensor again first.
static void testSpeedNewSession() {
    int sv[2];
    AASession* s = newSession(sv);
    std::vector<uint8_t> p;
    s->postCarSpeed(speedNow(0));
    sendStart(sv[1], 3, period0());
    assert(nextSensorFrame(sv[1], 2000, p) && isStartResponse(p));
    assert(nextSensorFrame(sv[1], 2000, p) && p == speedEvent(0));
    s->stop();
    close(sv[1]);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    assert(s->start(sv[0]));
    uint8_t ch = 0xFF;
    assert(readFrame(sv[1], 2000, ch, p));      // the VersionRequest
    assert(!nextSensorFrame(sv[1], 1300, p));   // no repeat left over from the last session
    sendStart(sv[1], 3, period0());
    assert(nextSensorFrame(sv[1], 2000, p) && isStartResponse(p));
    assert(nextSensorFrame(sv[1], 2000, p) && p == speedEvent(0));
    endSession(s, sv);
}

// Satellites keep their timer while speed posts keep waking the session and the phone says nothing.
static void testSatellitesWhileSpeedWakes() {
    int sv[2];
    AASession* s = newSession(sv);
    std::vector<uint8_t> p;
    sendStart(sv[1], 21, period0());
    assert(nextSensorFrame(sv[1], 2000, p) && isStartResponse(p));
    assert(nextSensorFrame(sv[1], 2000, p) && p == satEvent(0));
    sendStart(sv[1], 3, period0());
    assert(nextSensorFrame(sv[1], 2000, p) && isStartResponse(p));
    int sats = 0, speeds = 0;
    const uint32_t start = testNowMs();
    while (testNowMs() - start < 3500) {
        s->postCarSpeed(speedNow(6944));
        const uint32_t until = testNowMs() + 300;
        while ((int)(until - testNowMs()) > 0 && nextSensorFrame(sv[1], (int)(until - testNowMs()), p)) {
            if (p == satEvent(0)) ++sats;
            else if (p == speedEvent(6944)) ++speeds;
        }
    }
    printf("satellites while speed wakes: %d satellite and %d speed events in 3.5 s\n", sats, speeds);
    assert(sats >= 2 && sats <= 5);             // about once a second, not once per wake
    assert(speeds >= 8);
    endSession(s, sv);
}

// The period parser: present, the -1 of a 10-byte varint, absent, and a message it should stop on.
static void testPeriod() {
    const uint8_t a[] = { 0x80, 0x01, 0x08, 0x01, 0x10, 0xE8, 0x07 };
    std::vector<uint8_t> v(a, a + sizeof(a));
    int64_t period = 0;
    assert(pbSensorPeriod(v, 2, period) && period == 1000);
    const uint8_t b[] = { 0x80, 0x01, 0x08, 0x01, 0x10, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01 };
    v.assign(b, b + sizeof(b));
    assert(pbSensorPeriod(v, 2, period) && period == -1);
    const uint8_t c[] = { 0x80, 0x01, 0x08, 0x01 };
    v.assign(c, c + sizeof(c));
    assert(!pbSensorPeriod(v, 2, period));
    const uint8_t d[] = { 0x80, 0x01, 0x0A, 0x01, 0x10, 0x05 };   // field 1 length-delimited: not this message
    v.assign(d, d + sizeof(d));
    assert(!pbSensorPeriod(v, 2, period));
}

// A start with a -1 period is still answered, and reaches the log rather than the wire: only the type drives what is sent.
static void testStartWithMinusOne() {
    int sv[2];
    AASession* s = newSession(sv);
    const uint8_t m1[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01 };
    sendStart(sv[1], 21, std::vector<uint8_t>(m1, m1 + sizeof(m1)));
    std::vector<uint8_t> p;
    assert(nextSensorFrame(sv[1], 2000, p) && isStartResponse(p));
    assert(nextSensorFrame(sv[1], 2000, p) && p == satEvent(0));
    endSession(s, sv);
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    setenv("NBT_RX_TIMEOUT_S", "0", 1);
    testPeriod();
    testSatellitesNoData();
    testSatellitesPosted();
    testLocation();
    testCompassNoData();
    testCompassPosted();
    testCompassRepeats();
    testCompassRepeatsThroughWakes();
    testCompassAcrossSessions();
    testStartWithMinusOne();
    testSpeedNoData();
    testSpeedPosted();
    testSpeedStaleAtStart();
    testSpeedZeroRepeats();
    testSpeedNewSession();
    testSatellitesWhileSpeedWakes();
    printf("test_session_sensors OK\n");
    return 0;
}
