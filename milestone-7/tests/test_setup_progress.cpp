// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "../src/setup_progress.hpp"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); exit(1); } } while (0)

static void failedAttempts() {
    nbt::SetupProgress s;
    CHECK(s.failures() == 0 && s.cycle() == 0);
    CHECK(!s.canRecover(0, false));
    CHECK(s.failed(0));
    CHECK(!s.canRecover(0, false));
    CHECK(s.failed(0));
    CHECK(s.canRecover(0, false));
    CHECK(s.canRecover(0, false));
    CHECK(!s.canRecover(0, true));
    CHECK(!s.canRecover(1, false));
    s.beginTcp();
    CHECK(s.cycle() == 1 && s.failures() == 0);
    CHECK(!s.failed(0));
    CHECK(!s.failed(1));
    CHECK(!s.canRecover(0, false));
    s.finishTcp(true);
    CHECK(!s.failed(1));
    s.endTcp();
    CHECK(!s.failed(0)); // a very short TCP session still invalidates old attempts
    CHECK(s.failed(1));
    CHECK(s.failed(1));
    CHECK(s.failed(1));
    CHECK(s.canRecover(1, false));
}

static void previousSessionCallbackDuringStart() {
    nbt::SetupProgress s;
    s.beginTcp();
    s.finishTcp(true);
    s.beginTcp(); // new accept arrives while AASession::start joins old thread
    const unsigned cycle = s.cycle();
    s.endTcp(); // old session's disconnect callback
    CHECK(!s.failed(cycle)); // pending new connection is still protected
    CHECK(!s.failed(cycle));
    CHECK(!s.failed(cycle));
    CHECK(s.failures() == 0);
    CHECK(!s.canRecover(cycle, false));
    s.finishTcp(true);
    CHECK(!s.failed(cycle));
    CHECK(!s.canRecover(cycle, false));
    s.endTcp();
    CHECK(s.failed(cycle));

    s.beginTcp();
    s.endTcp();
    s.finishTcp(false); // failed new initialization clears the pending guard
    CHECK(s.failed(s.cycle()));
    CHECK(s.failed(s.cycle()));
    CHECK(s.failed(s.cycle()));
    CHECK(s.canRecover(s.cycle(), false));
}

static void saturatedCounter() {
    nbt::SetupProgress s;
    for (unsigned i = 0; i != 1000005u; ++i) s.failed(0);
    CHECK(s.failures() == 1000000u);
    CHECK(!s.failed(0));
    CHECK(s.canRecover(0, false));
    s.beginTcp();
    CHECK(s.failures() == 0);
}

// Exercise the actual critical-section protocol used by the daemon: recovery
// checks and commits running=false under the same mutex that accept checks
// before beginTcp(). Whichever thread wins, an accepted connection is never
// subsequently interrupted by recovery of the old setup cycle.
struct Race {
    nbt::SetupProgress s;
    pthread_mutex_t mutex, gateMutex;
    pthread_cond_t gate;
    int ready;
    bool go, running, accepted, recovered;
    unsigned requestCycle;
};

static void awaitGate(Race* r) {
    pthread_mutex_lock(&r->gateMutex);
    ++r->ready;
    pthread_cond_broadcast(&r->gate);
    while (!r->go) pthread_cond_wait(&r->gate, &r->gateMutex);
    pthread_mutex_unlock(&r->gateMutex);
}

static void* acceptThread(void* arg) {
    Race* r = static_cast<Race*>(arg);
    awaitGate(r);
    pthread_mutex_lock(&r->mutex);
    if (r->running) {
        r->s.beginTcp();
        r->accepted = true;
    }
    pthread_mutex_unlock(&r->mutex);
    if (r->accepted) {
        // Old disconnect callback occurs while the new start is pending.
        pthread_mutex_lock(&r->mutex);
        r->s.endTcp();
        pthread_mutex_unlock(&r->mutex);
        pthread_mutex_lock(&r->mutex);
        r->s.finishTcp(true);
        pthread_mutex_unlock(&r->mutex);
    }
    return NULL;
}

static void* recoveryThread(void* arg) {
    Race* r = static_cast<Race*>(arg);
    awaitGate(r);
    pthread_mutex_lock(&r->mutex);
    if (r->running && r->s.canRecover(r->requestCycle, false)) {
        r->running = false;
        r->recovered = true;
    }
    pthread_mutex_unlock(&r->mutex);
    return NULL;
}

static void serializedTcpVersusRecovery() {
    for (unsigned i = 0; i != 128; ++i) {
        Race r;
        CHECK(pthread_mutex_init(&r.mutex, NULL) == 0);
        CHECK(pthread_mutex_init(&r.gateMutex, NULL) == 0);
        CHECK(pthread_cond_init(&r.gate, NULL) == 0);
        r.ready = 0;
        r.go = r.accepted = r.recovered = false;
        r.running = true;
        r.s.failed(0); r.s.failed(0);
        r.requestCycle = r.s.cycle();
        pthread_t a, b;
        CHECK(pthread_create(&a, NULL, &acceptThread, &r) == 0);
        CHECK(pthread_create(&b, NULL, &recoveryThread, &r) == 0);
        pthread_mutex_lock(&r.gateMutex);
        while (r.ready != 2) pthread_cond_wait(&r.gate, &r.gateMutex);
        r.go = true;
        pthread_cond_broadcast(&r.gate);
        pthread_mutex_unlock(&r.gateMutex);
        CHECK(pthread_join(a, NULL) == 0);
        CHECK(pthread_join(b, NULL) == 0);
        CHECK(r.accepted != r.recovered);
        CHECK(r.accepted == r.running);
        CHECK(!r.s.canRecover(r.s.cycle(), false) || !r.running);
        CHECK(pthread_cond_destroy(&r.gate) == 0);
        CHECK(pthread_mutex_destroy(&r.gateMutex) == 0);
        CHECK(pthread_mutex_destroy(&r.mutex) == 0);
    }
}

static void noSppBeforeAnyHandshake() {
    nbt::SetupProgress s;
    CHECK(s.sppWaitSeconds(100) == 0);
    s.waitForSpp(true, 100);
    CHECK(s.sppWaitSeconds(100) == 0);
    s.waitForSpp(true, 102); // repeated poll must not reset the timer
    CHECK(s.sppWaitSeconds(102) == 2);
    CHECK(!s.canRecover(0, false, 102));
    CHECK(s.canRecover(0, false, 103));
    CHECK(!s.canRecover(0, true, 103));
    CHECK(!s.canRecover(1, false, 103));
    s.waitForSpp(false, 111); // SPP opened while the request was in flight
    CHECK(s.sppWaitSeconds(111) == 0);
    CHECK(!s.canRecover(0, false, 111));
    s.waitForSpp(true, 200);
    CHECK(s.canRecover(0, false, 210));
    s.beginTcp(); // a late TCP connection must win the same mutex race
    CHECK(s.sppWaitSeconds(220) == 0);
    CHECK(!s.canRecover(s.cycle(), false, 220));
    s.waitForSpp(true, 220); // active/pending TCP suppresses wireless idle reports
    CHECK(s.sppWaitSeconds(230) == 0);
    s.endTcp(); // previous callback during start leaves the pending guard intact
    s.waitForSpp(true, 230);
    CHECK(s.sppWaitSeconds(240) == 0);
    s.finishTcp(false);
    s.waitForSpp(true, 300);
    CHECK(s.canRecover(s.cycle(), false, 310));
    CHECK(s.sppWaitSeconds(10) == 0);
    s.waitForSpp(true, 10); // reversed clock starts a fresh wait
    CHECK(!s.canRecover(s.cycle(), false, 12));
    CHECK(s.canRecover(s.cycle(), false, 13));
    CHECK(s.sppWaitSeconds(2000000) == 1000000u);
}

int main() {
    failedAttempts();
    previousSessionCallbackDuringStart();
    saturatedCounter();
    serializedTcpVersusRecovery();
    noSppBeforeAnyHandshake();
    printf("setup progress OK (%u checks, 128 concurrent races)\n", checks);
    return 0;
}
