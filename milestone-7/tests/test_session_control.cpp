// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Control-channel frames sent to a real session over a socket, without TLS. PingResponse is the one reply that goes
// out unencrypted, so it also shows the session is still reading after a bad message.
#include "aa_session.hpp"

#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

using namespace nbt;

typedef std::vector<uint8_t> Bytes;

template <size_t N>
static Bytes bytes(const uint8_t (&v)[N]) { return Bytes(v, v + N); }

static void readExact(int fd, uint8_t* p, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        assert(r > 0);
        got += (size_t)r;
    }
}

// Channel 0, unencrypted, not fragmented.
static void sendPlain(int fd, const Bytes& payload) {
    uint8_t head[4] = { 0x00, 0x03, (uint8_t)(payload.size() >> 8), (uint8_t)payload.size() };
    assert(write(fd, head, 4) == 4);
    assert(write(fd, &payload[0], payload.size()) == (ssize_t)payload.size());
}

static Bytes readReply(int fd) {
    uint8_t head[4];
    readExact(fd, head, 4);
    assert(head[0] == 0x00 && head[1] == 0x03);
    Bytes body((head[2] << 8) | head[3]);
    readExact(fd, &body[0], body.size());
    return body;
}

int main() {
    alarm(20);
    signal(SIGPIPE, SIG_IGN);
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    AASession session;
    assert(session.start(sv[0]));
    const int phone = sv[1];

    uint8_t version[12];
    readExact(phone, version, sizeof(version));
    assert(version[5] == 1);

    // The timestamp comes back in field 1.
    {
        const uint8_t ping[] = { 0x00, 0x0B, 0x08, 0x96, 0x01 };
        const uint8_t want[] = { 0x00, 0x0C, 0x08, 0x96, 0x01 };
        sendPlain(phone, bytes(ping));
        assert(readReply(phone) == bytes(want));
    }
    // A 9-byte timestamp in nanoseconds.
    {
        const uint8_t ping[] = { 0x00, 0x0B, 0x08, 0x80, 0xB0, 0xD1, 0xE6, 0x8F, 0xFB, 0xFD, 0xE3, 0x18 };
        const uint8_t want[] = { 0x00, 0x0C, 0x08, 0x80, 0xB0, 0xD1, 0xE6, 0x8F, 0xFB, 0xFD, 0xE3, 0x18 };
        sendPlain(phone, bytes(ping));
        assert(readReply(phone) == bytes(want));
    }
    // A field in front of the timestamp doesn't hide it.
    {
        const uint8_t ping[] = { 0x00, 0x0B, 0x10, 0x01, 0x08, 0x05 };
        const uint8_t want[] = { 0x00, 0x0C, 0x08, 0x05 };
        sendPlain(phone, bytes(ping));
        assert(readReply(phone) == bytes(want));
    }
    // No timestamp, or one cut off: the response has none.
    {
        const uint8_t ping[] = { 0x00, 0x0B };
        const uint8_t cut[] = { 0x00, 0x0B, 0x08, 0x96 };
        const uint8_t want[] = { 0x00, 0x0C };
        sendPlain(phone, bytes(ping));
        assert(readReply(phone) == bytes(want));
        sendPlain(phone, bytes(cut));
        assert(readReply(phone) == bytes(want));
    }
    // Bad lengths in voice, navigation focus and audio focus requests. The session must still answer the next ping.
    {
        const uint8_t voice[] = { 0x00, 0x11, 0x12, 0xFA, 0xFF, 0xFF, 0xFF, 0x0F };
        const uint8_t nav[] = { 0x00, 0x0D, 0x12, 0xF5, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01 };
        const uint8_t focus[] = { 0x00, 0x12, 0x12, 0xFA, 0xFF, 0xFF, 0xFF, 0x0F };
        const uint8_t ping[] = { 0x00, 0x0B, 0x08, 0x07 };
        const uint8_t want[] = { 0x00, 0x0C, 0x08, 0x07 };
        sendPlain(phone, bytes(voice));
        sendPlain(phone, bytes(nav));
        sendPlain(phone, bytes(focus));
        sendPlain(phone, bytes(ping));
        assert(readReply(phone) == bytes(want));
    }

    session.stop();
    close(phone);
    printf("test_session_control OK\n");
    return 0;
}
