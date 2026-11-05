// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "aa_session.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
using namespace nbt;

static volatile int g_ended = 0;
static void onEnded(void*) { __sync_fetch_and_add(&g_ended, 1); }

static int countFds() {
    int n = 0;
    DIR* d = opendir("/proc/self/fd");
    if (!d) return -1;
    while (readdir(d)) ++n;
    closedir(d);
    return n;
}

static bool readVersionRequest(int fd) {
    uint8_t buf[12];
    size_t got = 0;
    while (got < sizeof(buf)) {
        ssize_t n = read(fd, buf + got, sizeof(buf) - got);
        if (n <= 0) return false;
        got += (size_t)n;
    }
    return buf[0] == 0 && buf[1] == 3 && buf[5] == 1; // ch0, flags 3, msg id 0x0001
}

struct Ctx { AASession* s; int fd; };
static void* restarter(void* a) { Ctx* c = (Ctx*)a; c->s->start(c->fd); return NULL; }
static void* stopper(void* a) { ((AASession*)a)->stop(); return NULL; }

// The MOST callback thread, posting the car's position and satellites across every session start and stop.
static volatile int g_stopPosting = 0;
static void* poster(void* a) {
    LocationFix f;
    f.lat_e7 = 525000000; f.lon_e7 = -45000000; f.accuracy_e3 = 10000; f.rx_ms = 1;
    SatelliteStatus st;
    st.used = 7; st.rx_ms = 1;
    while (!__sync_fetch_and_add(&g_stopPosting, 0)) {
        ((AASession*)a)->postCarLocation(f);
        ((AASession*)a)->postCarSatellites(st);
        ++f.rx_ms;
        ++st.rx_ms;
        usleep(200);
    }
    return NULL;
}

// What a phone still in an earlier session does: encrypted frames, no answer to the VersionRequest. No handshake ran
// here, so the real cryptor can't decrypt them either.
static void sendEncryptedFrames(int fd, int n) {
    const uint8_t frame[4 + 8] = { 6, 0x0B, 0, 8, 0x17, 3, 3, 0, 3, 1, 2, 3 };
    for (int i = 0; i < n; ++i) assert(write(fd, frame, sizeof(frame)) == (ssize_t)sizeof(frame));
}

static void waitEnded(int before) {
    for (int w = 0; w < 400 && __sync_fetch_and_add(&g_ended, 0) <= before; ++w) usleep(5000);
    assert(__sync_fetch_and_add(&g_ended, 0) > before);
}

static void testOutcomes() {
    AASession s;
    s.setDisconnectCallback(&onEnded, NULL);
    int sv[2];

    // Encrypted frames that never decrypt end the session by themselves, while the phone keeps the socket open.
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    int before = __sync_fetch_and_add(&g_ended, 0);
    assert(s.start(sv[0]));
    assert(readVersionRequest(sv[1]));
    sendEncryptedFrames(sv[1], 16);
    waitEnded(before);
    s.stop();
    assert(s.outcome() == AASession::kStale);
    close(sv[1]);

    // Fewer than the limit, then the phone goes away: the session waited, and it still counts as stale.
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    before = __sync_fetch_and_add(&g_ended, 0);
    assert(s.start(sv[0]));
    assert(readVersionRequest(sv[1]));
    sendEncryptedFrames(sv[1], 3);
    usleep(50000);
    assert(s.isActive());   // under the limit: the session waits
    close(sv[1]);
    waitEnded(before);
    s.stop();
    assert(s.outcome() == AASession::kStale);

    // The phone says nothing at all.
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    before = __sync_fetch_and_add(&g_ended, 0);
    assert(s.start(sv[0]));
    assert(readVersionRequest(sv[1]));
    close(sv[1]);
    waitEnded(before);
    s.stop();
    assert(s.outcome() == AASession::kNoAnswer);
    printf("outcomes: ok\n");
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    AASession s;
    s.setDisconnectCallback(&onEnded, NULL);
    int fds_before = countFds();
    const int N = 400;
    int selfEnded = 0;
    pthread_t post;
    assert(pthread_create(&post, NULL, poster, &s) == 0);
    for (int i = 0; i < N; ++i) {
        int sv[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
        assert(s.start(sv[0]));
        assert(readVersionRequest(sv[1]));
        switch (i % 4) {
        case 0: { // phone leaves: session ends on its own
            close(sv[1]);
            for (int w = 0; w < 200 && __sync_fetch_and_add(&g_ended, 0) <= selfEnded; ++w) usleep(5000);
            assert(__sync_fetch_and_add(&g_ended, 0) > selfEnded);
            ++selfEnded;
            break; }
        case 1: { // we stop while phone still connected
            s.stop();
            close(sv[1]);
            break; }
        case 2: { // phone reconnects while the old session is still active (start() replaces it)
            int sv2[2];
            assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv2) == 0);
            Ctx c = { &s, sv2[0] };
            pthread_t t; pthread_create(&t, NULL, restarter, &c);
            pthread_join(t, NULL);
            assert(readVersionRequest(sv2[1]));
            close(sv[1]);
            s.stop();
            close(sv2[1]);
            break; }
        case 3: { // stop() racing with the phone closing
            pthread_t t; pthread_create(&t, NULL, stopper, &s);
            close(sv[1]);
            pthread_join(t, NULL);
            break; }
        }
        s.stop();   // must be idempotent and never hang
    }
    __sync_fetch_and_add(&g_stopPosting, 1);
    pthread_join(post, NULL);
    // let detached callbacks settle
    usleep(100000);
    int fds_after = countFds();
    printf("cycles=%d ended_callbacks=%d fds before=%d after=%d\n", N, (int)__sync_fetch_and_add(&g_ended, 0), fds_before, fds_after);
    assert(fds_after <= fds_before + 1);
    testOutcomes();
    printf("test_session_lifecycle OK\n");
    return 0;
}
