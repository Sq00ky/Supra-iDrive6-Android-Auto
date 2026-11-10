// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// AASession::byeAndWait() against a scripted phone. Built with stub_cryptor.hpp in place of the real cryptor (see
// tests/Makefile): encrypted frames are plain text here, so the test can read and write them without a TLS handshake.
#include "aa_session.hpp"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
using namespace nbt;

static volatile int g_ended = 0;
static void onEnded(void*) { __sync_fetch_and_add(&g_ended, 1); }

static double now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void readN(int fd, uint8_t* out, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, out + got, n - got);
        assert(r > 0);
        got += (size_t)r;
    }
}

// Past the VersionRequest, and with one frame decrypted: an established session.
static void establish(AASession& s, int* phone) {
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    assert(s.start(sv[0]));
    uint8_t version[12];
    readN(sv[1], version, sizeof(version));
    const uint8_t frame[6] = { 0, 0x0B, 0, 2, 0x7F, 0x7F };   // an encrypted control message nobody handles
    assert(write(sv[1], frame, sizeof(frame)) == (ssize_t)sizeof(frame));
    *phone = sv[1];
}

// The ByeByeRequest as the phone gets it: encrypted control frame, id 0x000F, reason 1.
static void expectBye(int phone) {
    uint8_t got[8];
    readN(phone, got, sizeof(got));
    const uint8_t want[8] = { 0, 0x0B, 0, 4, 0x00, 0x0F, 0x08, 0x01 };
    assert(memcmp(got, want, sizeof(want)) == 0);
}

struct ByeCtx { AASession* s; uint32_t wait_ms; bool result; double took_s; };
static void* byeThread(void* a) {
    ByeCtx* c = reinterpret_cast<ByeCtx*>(a);
    const double t0 = now();
    c->result = c->s->byeAndWait(c->wait_ms);
    c->took_s = now() - t0;
    return NULL;
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    AASession s;
    s.setDisconnectCallback(&onEnded, NULL);
    int phone = -1;

    // The phone answers: the session ends at once, well inside the wait.
    establish(s, &phone);
    ByeCtx c = { &s, 5000, false, 0 };
    pthread_t t;
    assert(pthread_create(&t, NULL, &byeThread, &c) == 0);
    expectBye(phone);
    const uint8_t resp[6] = { 0, 0x0B, 0, 2, 0x00, 0x10 };
    assert(write(phone, resp, sizeof(resp)) == (ssize_t)sizeof(resp));
    pthread_join(t, NULL);
    assert(c.result);
    assert(c.took_s < 2.0);
    assert(!s.isActive());
    assert(__sync_fetch_and_add(&g_ended, 0) == 1);
    assert(s.outcome() == AASession::kEstablished);   // a goodbye is not a failed session: no USB reset
    s.stop();
    close(phone);
    printf("answered: ok\n");

    // The phone stays silent: back after the wait, the session still runs and stop() ends it as before.
    establish(s, &phone);
    usleep(50000);
    ByeCtx c2 = { &s, 200, true, 0 };
    assert(pthread_create(&t, NULL, &byeThread, &c2) == 0);
    expectBye(phone);
    pthread_join(t, NULL);
    assert(!c2.result);
    assert(c2.took_s >= 0.15 && c2.took_s < 2.0);
    assert(s.isActive());
    s.stop();
    close(phone);
    printf("no answer: ok\n");

    // No session: nothing to wait for.
    const double t0 = now();
    assert(s.byeAndWait(5000));
    assert(now() - t0 < 1.0);
    printf("no session: ok\n");

    // A request left over from one session is not sent in the next.
    establish(s, &phone);
    usleep(50000);
    s.byeAndWait(0);
    s.stop();
    close(phone);
    establish(s, &phone);
    usleep(1200000);   // past a select tick: nothing may have been queued for the phone
    uint8_t b;
    assert(recv(phone, &b, 1, MSG_DONTWAIT) == -1);
    s.stop();
    close(phone);
    printf("next session: ok\n");

    // A broken encrypted stream ends an established session: 16 frames in a row that do not decrypt. Fewer, then one
    // that does, start the count again.
    establish(s, &phone);
    const uint8_t bad[6] = { 0, 0x0B, 0, 2, 0xEE, 0xEE };    // stub_cryptor.hpp refuses a payload that starts 0xEE
    const uint8_t good[6] = { 0, 0x0B, 0, 2, 0x7F, 0x7F };
    const int before = __sync_fetch_and_add(&g_ended, 0);
    for (int i = 0; i < 15; ++i) assert(write(phone, bad, sizeof(bad)) == (ssize_t)sizeof(bad));
    assert(write(phone, good, sizeof(good)) == (ssize_t)sizeof(good));
    for (int i = 0; i < 15; ++i) assert(write(phone, bad, sizeof(bad)) == (ssize_t)sizeof(bad));
    usleep(100000);
    assert(s.isActive());                                     // 15, one good, 15: never 16 in a row
    assert(__sync_fetch_and_add(&g_ended, 0) == before);
    assert(write(phone, bad, sizeof(bad)) == (ssize_t)sizeof(bad));
    for (int w = 0; w < 400 && __sync_fetch_and_add(&g_ended, 0) == before; ++w) usleep(5000);
    assert(__sync_fetch_and_add(&g_ended, 0) == before + 1);
    assert(!s.isActive());
    assert(s.outcome() == AASession::kEstablished);           // it was up: not a stale phone, no USB reset
    s.stop();
    close(phone);
    printf("broken stream: ok\n");

    printf("test_session_bye OK\n");
    return 0;
}
