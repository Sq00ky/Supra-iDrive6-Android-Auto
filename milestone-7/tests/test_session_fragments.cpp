// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Exercise the real session's per-channel reassembly with plaintext frames.
#include "aa_session.hpp"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

struct Received {
    pthread_mutex_t mutex;
    unsigned count;
    Received() : count(0) { pthread_mutex_init(&mutex, NULL); }
    ~Received() { pthread_mutex_destroy(&mutex); }
};

static void navigation(uint16_t id, const uint8_t* body, size_t n, void* user) {
    assert(id == 0x8006 && n == 2 && body[0] == 8 && body[1] == 1);
    Received& received = *static_cast<Received*>(user);
    pthread_mutex_lock(&received.mutex);
    ++received.count;
    pthread_mutex_unlock(&received.mutex);
}

static void readExact(int fd, unsigned char* data, size_t n) {
    size_t got = 0;
    while (got < n) {
        const ssize_t result = read(fd, data + got, n - got);
        assert(result > 0);
        got += (size_t)result;
    }
}

static void frame(int fd, unsigned flags, const unsigned char* data, size_t n) {
    unsigned char buf[32] = { 10, (unsigned char)flags, 0, (unsigned char)n };
    size_t off = 4;
    if (flags == 1) { buf[4] = buf[5] = buf[6] = 0; buf[7] = 4; off = 8; }
    assert(off + n <= sizeof(buf));
    memcpy(buf + off, data, n);
    assert(write(fd, buf, off + n) == (ssize_t)(off + n));
}

static void checkpoint(int fd, Received& received, unsigned expected) {
    // PingResponse is sent by the session thread after all preceding frames were
    // processed. Unlike waiting for a counter, this also detects extra callbacks.
    const unsigned char ping[] = { 0, 3, 0, 4, 0, 0x0b, 8, 1 };
    const unsigned char pong[] = { 0, 3, 0, 4, 0, 0x0c, 8, 1 };
    assert(write(fd, ping, sizeof(ping)) == (ssize_t)sizeof(ping));
    unsigned char reply[sizeof(pong)];
    readExact(fd, reply, sizeof(reply));
    assert(memcmp(reply, pong, sizeof(pong)) == 0);
    pthread_mutex_lock(&received.mutex);
    const unsigned actual = received.count;
    pthread_mutex_unlock(&received.mutex);
    if (actual != expected)
        fprintf(stderr, "navigation callbacks: expected %u, got %u\n", expected, actual);
    assert(actual == expected);
}

static void run(const char* scenario) {
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    Received received;
    nbt::AASession session;
    session.setNavStatusCallback(navigation, &received);
    assert(session.start(sv[0]));
    unsigned char version[12];
    readExact(sv[1], version, sizeof(version));
    const unsigned char head[] = { 0x80, 6 }, tail[] = { 8, 1 }, whole[] = { 0x80, 6, 8, 1 };

    if (!strcmp(scenario, "orphan")) {
        // MIDDLE must not manufacture a FIRST for the following LAST.
        frame(sv[1], 0, head, 2);
        frame(sv[1], 2, tail, 2);
        checkpoint(sv[1], received, 0);
        // A standalone LAST is also ignored, and normal traffic still works.
        frame(sv[1], 2, whole, 4);
        frame(sv[1], 3, whole, 4);
        checkpoint(sv[1], received, 1);
    } else if (!strcmp(scenario, "bulk")) {
        // BULK replaces an unfinished message; its old LAST must not deliver it.
        frame(sv[1], 1, head, 2);
        frame(sv[1], 3, whole, 4);
        frame(sv[1], 2, tail, 2);
        checkpoint(sv[1], received, 1);
        frame(sv[1], 3, whole, 4);
        checkpoint(sv[1], received, 2);
    } else {
        // Both FIRST/LAST and FIRST/MIDDLE/LAST still deliver once.
        frame(sv[1], 1, head, 2);
        frame(sv[1], 2, tail, 2);
        checkpoint(sv[1], received, 1);
        frame(sv[1], 1, head, 1);
        frame(sv[1], 0, head + 1, 1);
        frame(sv[1], 2, tail, 2);
        checkpoint(sv[1], received, 2);
    }
    session.stop();
    close(sv[1]);
    printf("test_session_fragments %s OK\n", scenario);
}

int main(int argc, char** argv) {
    alarm(20);
    signal(SIGPIPE, SIG_IGN);
    const char* const scenarios[] = { "orphan", "bulk", "valid" };
    for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i) {
        if (argc == 1 || (argc == 2 && !strcmp(argv[1], scenarios[i]))) {
            run(scenarios[i]);
            if (argc == 2) return 0;
        }
    }
    return argc == 1 ? 0 : 2;
}
