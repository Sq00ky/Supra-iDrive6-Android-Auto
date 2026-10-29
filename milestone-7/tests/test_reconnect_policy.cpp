// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "../src/reconnect_policy.hpp"
#include <stdio.h>
#include <stdlib.h>

static unsigned checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); exit(1); } } while (0)

static bool poll(nbt::ReconnectPolicy& p, uint64_t now, unsigned failures,
                 int pid = 100, bool connected = false, unsigned cycle = 0) {
    return p.evaluate(now, true, false, false, pid, connected, failures, cycle);
}

static void fastFailureAndOneRestart() {
    nbt::ReconnectPolicy p;
    CHECK(!poll(p, 0, 0));
    CHECK(!poll(p, 3600, 0)); // phone absent: elapsed time alone never restarts
    CHECK(!poll(p, 3601, 1));
    CHECK(!poll(p, 9999, 1)); // one failed attempt is not enough
    CHECK(poll(p, 9999, 2)); // same clock tick: no extra fixed delay
    CHECK(!poll(p, 10000, 3));
    CHECK(!poll(p, 20000, 300));
    CHECK(!poll(p, 20001, 0, 0));
    CHECK(!poll(p, 20002, 0, 200));
    CHECK(!poll(p, 20003, 2, 200)); // new process does not reset the budget
}

static void normalConnectionAndLateSuccess() {
    nbt::ReconnectPolicy p;
    CHECK(!poll(p, 0, 1));
    CHECK(!poll(p, 1, 0, 100, true, 1)); // second attempt connected normally
    CHECK(!poll(p, 2, 0, 100, true, 1));
    CHECK(!poll(p, 40, 0, 100, true, 1));
    CHECK(!poll(p, 41, 1, 100, false, 1));
    CHECK(!poll(p, 42, 2, 100, true, 1)); // connection wins even if stale count remains
    CHECK(!poll(p, 43, 0, 100, true, 2));
    CHECK(!poll(p, 44, 1, 100, false, 2));
    CHECK(poll(p, 45, 2, 100, false, 2));
}

static void stableConnectionRearmsWithoutDelayingConnection() {
    nbt::ReconnectPolicy p;
    CHECK(poll(p, 0, 2));
    CHECK(!poll(p, 1, 0, 200, true, 1));
    CHECK(!poll(p, 30, 0, 200, true, 1)); // only 29 seconds: still one episode
    CHECK(!poll(p, 31, 2, 200, false, 1)); // flapping does not replenish budget
    CHECK(!poll(p, 32, 0, 200, true, 2));
    CHECK(!poll(p, 61, 0, 200, true, 2));
    CHECK(!poll(p, 62, 0, 200, true, 2)); // 30 continuous seconds rearms
    CHECK(!poll(p, 63, 1, 200, false, 2));
    CHECK(poll(p, 64, 2, 200, false, 2));
}

static void guardsAndExplicitUserAction() {
    nbt::ReconnectPolicy p;
    CHECK(!p.evaluate(0, false, false, false, 100, false, 2, 0));
    CHECK(!p.evaluate(1, true, true, false, 100, false, 2, 0));
    CHECK(!p.evaluate(2, true, false, true, 100, false, 2, 0));
    CHECK(!p.evaluate(3, true, false, false, -1, false, 2, 0));
    CHECK(!p.evaluate(4, true, false, false, 0, false, 2, 0));
    CHECK(poll(p, 5, 2));
    CHECK(!p.evaluate(6, false, false, false, 100, true, 0, 1));
    CHECK(!p.evaluate(100, false, false, false, 100, true, 0, 1));
    CHECK(!poll(p, 101, 2, 200)); // OFF does not replenish budget
    CHECK(!p.evaluate(102, true, true, false, 200, false, 2, 0));
    CHECK(!poll(p, 103, 2, 200)); // manual Stop does not replenish budget
    p.manualStart();
    CHECK(!poll(p, 104, 1, 300));
    CHECK(poll(p, 105, 2, 300)); // explicit user action rearms
}

static void clockPidAndCycleChangesPreserveBudget() {
    nbt::ReconnectPolicy p;
    CHECK(poll(p, 1000, 2));
    CHECK(!poll(p, 1001, 0, 100, true, 1));
    CHECK(!poll(p, 1030, 0, 200, true, 1)); // PID change interrupts stability
    CHECK(!poll(p, 1031, 2, 200, false, 1));
    CHECK(!poll(p, 1032, 0, 200, true, 2));
    CHECK(!poll(p, 1061, 0, 200, true, 3)); // session cycle change interrupts stability
    CHECK(!poll(p, 1062, 2, 200, false, 3));
    CHECK(!poll(p, 1, 2, 200)); // reversed clock does not replenish budget
    CHECK(!poll(p, 2, 0, 200, true, 4));
    CHECK(!poll(p, 31, 0, 200, true, 4));
    CHECK(!poll(p, 3, 0, 200, true, 4)); // reverse resets continuous stability timer
    CHECK(!poll(p, 4, 2, 200, false, 4));
    p.manualStart();
    const uint64_t big = (uint64_t)0xffffffffu + 100;
    CHECK(!poll(p, big, 1));
    CHECK(poll(p, big, 2));
}

static bool missing(nbt::ReconnectPolicy& p, uint64_t now, unsigned age) {
    return p.evaluate(now, true, false, false, 532590, false, 0, 0, age);
}

static void missingSppAndWarmPowerCycle() {
    nbt::ReconnectPolicy p;
    CHECK(!missing(p, 28, 0)); // automatic launch in the failed real capture
    CHECK(!missing(p, 30, 2));
    CHECK(missing(p, 31, 3)); // zero handshakes must no longer prevent recovery
    CHECK(!missing(p, 280, 252)); // no endless restarts when phone is absent
    CHECK(!p.evaluate(281, true, false, false, 1175662, false, 0, 0, 10));
    p.powerChanged(300); // an OFF transition may occur before the return
    CHECK(!missing(p, 309, 10));
    CHECK(missing(p, 310, 10));
    p.powerChanged(360); // a later ON transition rearms even if OFF spent its budget
    CHECK(!missing(p, 369, 60));
    CHECK(missing(p, 370, 70));
    CHECK(!missing(p, 400, 100));
    p.powerChanged(401);
    CHECK(!p.evaluate(411, true, true, false, 532590, false, 0, 0, 10)); // manual Stop wins
    CHECK(!p.evaluate(412, false, false, false, 532590, false, 0, 0, 10)); // OFF wins
    CHECK(!p.evaluate(413, true, false, true, 532590, false, 0, 0, 10)); // stopping wins
    CHECK(!p.evaluate(414, true, false, false, 532590, true, 0, 1, 10)); // connected wins
}

static void powerDebounceAndQuietRelaunch() {
    nbt::ReconnectPolicy p;
    p.powerChanged(10);
    p.powerChanged(15);
    CHECK(!missing(p, 24, 100));
    CHECK(missing(p, 25, 100)); // settle after the last change, not the first
    nbt::ReconnectLaunchDelay d;
    CHECK(!d.ready(0, true, false, false));
    d.schedule(100);
    CHECK(d.pending());
    CHECK(!d.ready(102, true, false, false));
    CHECK(d.ready(103, true, false, false));
    CHECK(!d.pending());
    CHECK(!d.ready(111, true, false, false));
    d.schedule(200);
    CHECK(!d.ready(201, false, false, false));
    CHECK(!d.pending());
    d.schedule(300);
    CHECK(!d.ready(301, true, true, false));
    CHECK(!d.pending());
    d.schedule(400);
    CHECK(!d.ready(401, true, false, true)); // external/manual launch cancels pending duplicate
    CHECK(!d.pending());
    d.schedule(500);
    d.cancel();
    CHECK(!d.ready(600, true, false, false));
    d.schedule(700);
    d.schedule(705); // another power change extends settling
    CHECK(!d.ready(707, true, false, false));
    CHECK(d.ready(708, true, false, false));
    d.schedule(1000);
    CHECK(!d.ready(1, true, false, false)); // clock reversal restarts the full settle window
    CHECK(!d.ready(3, true, false, false));
    CHECK(d.ready(4, true, false, false));
}

static void selectableTimingKeepsConservativeFallback() {
    nbt::ReconnectTiming fast = nbt::ReconnectTiming::fromProfileName("fast");
    nbt::ReconnectTiming slow = nbt::ReconnectTiming::fromProfileName("conservative");
    CHECK(!nbt::ReconnectTiming::fromProfileName(NULL).conservative);
    CHECK(nbt::ReconnectTiming::fromProfileName("").conservative);
    CHECK(nbt::ReconnectTiming::fromProfileName("unknown").conservative);
    CHECK(fast.autostartSeconds == 3 && fast.quietSeconds == 3);
    CHECK(slow.autostartSeconds == 15 && slow.quietSeconds == 10);
    nbt::ReconnectPolicy p;
    CHECK(!p.evaluate(0, true, false, false, 100, false, 0, 0, 2, fast.missingSppSeconds));
    CHECK(p.evaluate(1, true, false, false, 100, false, 0, 0, 3, fast.missingSppSeconds));
    CHECK(!p.evaluate(2, true, false, false, 200, false, 0, 0, 10, slow.missingSppSeconds)); // budget survives profile/PID changes
    p.manualStart();
    CHECK(!p.evaluate(3, true, false, false, 200, false, 0, 0, 9, slow.missingSppSeconds));
    CHECK(p.evaluate(4, true, false, false, 200, false, 0, 0, 10, slow.missingSppSeconds));
    p.manualStart();
    CHECK(!p.evaluate(5, true, false, false, 200, true, 0, 1, 30, fast.missingSppSeconds)); // connected always wins
    CHECK(!p.evaluate(6, true, true, false, 200, false, 0, 1, 30, fast.missingSppSeconds));
    CHECK(!p.evaluate(7, false, false, false, 200, false, 0, 1, 30, fast.missingSppSeconds));
    CHECK(!p.evaluate(8, true, false, true, 200, false, 0, 1, 30, fast.missingSppSeconds));
    nbt::ReconnectLaunchDelay d;
    d.schedule(100, slow.quietSeconds);
    CHECK(!d.ready(109, true, false, false));
    CHECK(d.ready(110, true, false, false));
    d.schedule(200, slow.quietSeconds);
    CHECK(!d.ready(1, true, false, false));
    CHECK(!d.ready(10, true, false, false)); // clock reversal must keep the selected 10 s
    CHECK(d.ready(11, true, false, false));
    d.schedule(300, fast.quietSeconds);
    CHECK(!d.ready(302, true, false, false));
    CHECK(d.ready(303, true, false, false));
    d.schedule(400, 0); // cannot configure a zero-delay relaunch
    CHECK(!d.ready(402, true, false, false));
    CHECK(d.ready(403, true, false, false));
}

int main() {
    fastFailureAndOneRestart();
    normalConnectionAndLateSuccess();
    stableConnectionRearmsWithoutDelayingConnection();
    guardsAndExplicitUserAction();
    clockPidAndCycleChangesPreserveBudget();
    missingSppAndWarmPowerCycle();
    powerDebounceAndQuietRelaunch();
    selectableTimingKeepsConservativeFallback();
    printf("fast reconnect policy OK (%u checks)\n", checks);
    return 0;
}
