// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for the dead-phone timeout in aa_session: the session ends when the phone has sent nothing for NBT_RX_TIMEOUT_S,
// however often other threads wake it, and stays up while the phone sends anything at all. A program of its own because
// rxTimeoutMs() caches its value and test_session_sensors runs with the timeout off. All values are made up.
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

enum { kLimitMs = 2000, kLateMs = 1500 };   // the limit set in main, and how late past it the session may end

static uint32_t testNowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000);
}

// Reads and drops whatever the session has sent, for up to ms.
static void discardFor(int fd, int ms) {
    const uint32_t start = testNowMs();
    uint8_t junk[512];
    for (;;) {
        const int left = ms - (int)(testNowMs() - start);
        if (left <= 0) return;
        struct pollfd pf;
        pf.fd = fd;
        pf.events = POLLIN;
        pf.revents = 0;
        if (poll(&pf, 1, left) <= 0) return;
        if (read(fd, junk, sizeof(junk)) <= 0) return;
    }
}

// A plain SensorStartRequest on channel 1, as a bulk frame with the encryption flag off.
static const uint8_t kStartSpeed[] = { 0x01, 0x03, 0x00, 0x04, 0x80, 0x01, 0x08, 0x03 };

static void writeAll(int fd, const uint8_t* p, size_t n) {
    assert(write(fd, p, n) == (ssize_t)n);
}

static AASession* newSession(int sv[2]) {
    AASession* s = new AASession();
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    assert(s->start(sv[0]));
    return s;
}

static void endSession(AASession* s, int sv[2]) {
    s->stop();
    close(sv[1]);
    delete s;
}

static CarSpeedSample movingNow() {
    CarSpeedSample v;
    v.speed_e3 = 6944;
    v.rx_ms = testNowMs();
    return v;
}

// How long the session lasted from now, reading what it sends and posting a speed every postEveryMs (0: none). Gives up at
// maxMs, returning maxMs.
static uint32_t lifetime(AASession* s, int fd, int postEveryMs, uint32_t maxMs) {
    const uint32_t start = testNowMs();
    while (s->isActive() && testNowMs() - start < maxMs) {
        if (postEveryMs) s->postCarSpeed(movingNow());
        discardFor(fd, postEveryMs ? postEveryMs : 50);
    }
    return s->isActive() ? maxMs : testNowMs() - start;
}

// A silent phone from the moment the session starts.
static void testSilentPhone() {
    int sv[2];
    AASession* s = newSession(sv);
    const uint32_t took = lifetime(s, sv[1], 0, kLimitMs + 4000);
    printf("silent phone: ended after %u ms\n", took);
    assert(took >= kLimitMs - 100 && took <= kLimitMs + kLateMs);
    endSession(s, sv);
}

// The same with the car moving: each post wakes the session, and that must not count as hearing from the phone.
static void testSilentPhoneCarMoving() {
    int sv[2];
    AASession* s = newSession(sv);
    writeAll(sv[1], kStartSpeed, sizeof(kStartSpeed));
    discardFor(sv[1], 100);
    const uint32_t took = lifetime(s, sv[1], 300, kLimitMs + 4000);
    printf("silent phone, car moving: ended after %u ms\n", took);
    assert(took <= kLimitMs + kLateMs);
    endSession(s, sv);
}

// Half a header and then nothing: part of a frame is the last the phone said.
static void testSilentAfterHalfAHeader() {
    int sv[2];
    AASession* s = newSession(sv);
    writeAll(sv[1], kStartSpeed, 2);
    const uint32_t took = lifetime(s, sv[1], 0, kLimitMs + 4000);
    printf("half a header: ended after %u ms\n", took);
    assert(took <= kLimitMs + kLateMs);
    endSession(s, sv);
}

// A phone that keeps talking stays connected well past the limit, with the car moving.
static void testTalkingPhoneStays() {
    int sv[2];
    AASession* s = newSession(sv);
    writeAll(sv[1], kStartSpeed, sizeof(kStartSpeed));
    const uint32_t start = testNowMs();
    while (testNowMs() - start < kLimitMs * 3u) {
        s->postCarSpeed(movingNow());
        writeAll(sv[1], kStartSpeed, sizeof(kStartSpeed));  // answered, and otherwise harmless
        discardFor(sv[1], 500);
        assert(s->isActive());
    }
    endSession(s, sv);
}

// One byte at a time still counts: the frame takes longer than the limit, and each byte shows the phone is there.
static void testSlowBytesStay() {
    int sv[2];
    AASession* s = newSession(sv);
    for (size_t i = 0; i < sizeof(kStartSpeed); ++i) {
        writeAll(sv[1], kStartSpeed + i, 1);
        discardFor(sv[1], 500);
        assert(s->isActive());
    }
    assert(sizeof(kStartSpeed) * 500u > (unsigned)kLimitMs);
    endSession(s, sv);
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    setenv("NBT_RX_TIMEOUT_S", "2", 1);
    testSilentPhone();
    testSilentPhoneCarMoving();
    testSilentAfterHalfAHeader();
    testTalkingPhoneStays();
    testSlowBytesStay();
    printf("test_session_rx_timeout OK\n");
    return 0;
}
