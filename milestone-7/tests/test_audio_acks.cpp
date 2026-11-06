// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: the session's deferred audio acks and underflow reports (audio/audio_acks.hpp), through fake senders.
#include "audio/audio_acks.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <vector>

using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

struct Sent {
    std::vector<uint8_t> ch;
    std::vector<uint64_t> rx;
    bool ok;
    Sent() : ok(true) {}
};

static bool sendAck(uint8_t channel, uint64_t rx, void* user) {
    Sent* s = static_cast<Sent*>(user);
    s->ch.push_back(channel);
    s->rx.push_back(rx);
    return s->ok;
}

static bool sendUnderflow(uint8_t channel, void* user) {
    Sent* s = static_cast<Sent*>(user);
    s->ch.push_back(channel);
    return s->ok;
}

int main() {
    unsetenv("NBT_AA_MAX_UNACKED");
    unsetenv("NBT_AA_ACK_FORCE_MS");
    unsetenv("NBT_AA_ACK_TIMES");

    // 1. The window: NBT_AA_MAX_UNACKED, default 10, clamped to 4..64.
    CHECK(AudioAcks::window() == 10);
    setenv("NBT_AA_MAX_UNACKED", "2", 1);
    CHECK(AudioAcks::window() == 4);
    setenv("NBT_AA_MAX_UNACKED", "100", 1);
    CHECK(AudioAcks::window() == 64);
    setenv("NBT_AA_MAX_UNACKED", "12", 1);
    CHECK(AudioAcks::window() == 12);
    unsetenv("NBT_AA_MAX_UNACKED");

    // 2. Deferred acks off (the default): nothing is recorded, the session acks on arrival.
    {
        AudioAcks a;
        a.startSession();
        CHECK(!a.deferred());
        CHECK(a.maxUnacked() == 10);
        CHECK(a.ackTimes());
        CHECK(!a.onFrame(9, 100));
        CHECK(!a.consumed(9, 100));   // no frame recorded: nothing becomes ready
        CHECK(!a.hasPending());
        Sent s;
        a.drain(&sendAck, &s);
        a.onStreamStart(9, &sendAck, &s);
        CHECK(s.ch.empty());
    }

    // 3. On: acked once consumed, sent guidance, media, system (the order the old code sent them in).
    {
        AudioAcks a;
        a.setDeferred(true);
        a.startSession();
        CHECK(a.deferred());
        CHECK(a.onFrame(9, 100));
        CHECK(a.onFrame(16, 50));
        CHECK(a.onFrame(8, 80));
        CHECK(!a.onFrame(3, 80));      // not an audio sink
        CHECK(a.hasPending());
        CHECK(!a.consumed(9, 99));     // not a whole frame yet
        CHECK(a.consumed(9, 1));       // now one is ready: wake the session thread
        CHECK(a.consumed(16, 50));
        CHECK(a.consumed(8, 80));
        CHECK(!a.consumed(3, 10));
        CHECK(!a.consumed(9, 0));
        Sent s;
        a.drain(&sendAck, &s);
        CHECK(s.ch.size() == 3);
        if (s.ch.size() == 3) CHECK(s.ch[0] == 8 && s.ch[1] == 9 && s.ch[2] == 16);
        for (size_t i = 0; i < s.rx.size(); ++i) CHECK(s.rx[i] != 0);   // NBT_AA_ACK_TIMES default on
        CHECK(!a.hasPending());
        Sent s2;
        a.drain(&sendAck, &s2);
        CHECK(s2.ch.empty());
    }

    // 4. A failed send drops the rest of that stream's batch, the next stream still goes out.
    {
        AudioAcks a;
        a.setDeferred(true);
        a.startSession();
        for (int i = 0; i < 3; ++i) { a.onFrame(8, 10); a.onFrame(9, 10); }
        a.consumed(8, 30);
        a.consumed(9, 30);
        Sent s;
        s.ok = false;
        a.drain(&sendAck, &s);
        CHECK(s.ch.size() == 2);       // one attempt per stream, then that stream's batch is given up
        if (s.ch.size() == 2) CHECK(s.ch[0] == 8 && s.ch[1] == 9);
    }

    // 5. A new stream (START_INDICATION) acks the old one's leftovers on that channel only.
    {
        AudioAcks a;
        a.setDeferred(true);
        a.startSession();
        a.onFrame(9, 100);
        a.onFrame(9, 100);
        a.onFrame(8, 100);
        Sent s;
        a.onStreamStart(9, &sendAck, &s);
        CHECK(s.ch.size() == 2);
        for (size_t i = 0; i < s.ch.size(); ++i) CHECK(s.ch[i] == 9);
        CHECK(a.hasPending());         // ch 8 still waits
        Sent s2;
        a.onStreamStart(3, &sendAck, &s2);
        CHECK(s2.ch.empty());
    }

    // 6. NBT_AA_ACK_TIMES=0: acks without arrival times. NBT_AA_ACK_FORCE_MS: unconsumed frames acked anyway.
    {
        setenv("NBT_AA_ACK_TIMES", "0", 1);
        setenv("NBT_AA_ACK_FORCE_MS", "1", 1);
        AudioAcks a;
        a.setDeferred(true);
        a.startSession();
        CHECK(!a.ackTimes());
        a.onFrame(16, 10);
        usleep(5000);
        Sent s;
        a.drain(&sendAck, &s);         // never consumed: forced after 1 ms
        CHECK(s.ch.size() == 1);
        if (s.rx.size() == 1) CHECK(s.rx[0] == 0);
        unsetenv("NBT_AA_ACK_TIMES");
        unsetenv("NBT_AA_ACK_FORCE_MS");
    }

    // 7. A new session starts from nothing, with the window it reads then.
    {
        AudioAcks a;
        a.setDeferred(true);
        a.startSession();
        a.onFrame(9, 10);
        CHECK(a.hasPending());
        setenv("NBT_AA_MAX_UNACKED", "6", 1);
        a.setDeferred(false);          // applies from the next session
        CHECK(a.deferred());
        a.startSession();
        CHECK(!a.deferred());
        CHECK(a.maxUnacked() == 6);
        CHECK(!a.hasPending());
        unsetenv("NBT_AA_MAX_UNACKED");
    }

    // 8. Underflow reports: off by default; on, one report per stream since the last drain, guidance, media, system.
    {
        AudioAcks a;
        a.startSession();
        CHECK(!a.postUnderflow(9));
        Sent s;
        a.drainUnderflows(&sendUnderflow, &s);
        CHECK(s.ch.empty());

        a.setUnderflowReports(true);
        a.startSession();
        CHECK(a.postUnderflow(9));     // wake the session thread
        CHECK(a.postUnderflow(16));
        CHECK(a.postUnderflow(8));
        CHECK(a.postUnderflow(8));
        CHECK(!a.postUnderflow(3));
        a.drainUnderflows(&sendUnderflow, &s);
        CHECK(s.ch.size() == 3);
        if (s.ch.size() == 3) CHECK(s.ch[0] == 8 && s.ch[1] == 9 && s.ch[2] == 16);
        Sent s2;
        a.drainUnderflows(&sendUnderflow, &s2);
        CHECK(s2.ch.empty());
        a.logSessionEnd(true);         // [Feature] underflow_reports=on guidance=1 media=1 system=1 (sent, not posted)
    }

    if (fails) { printf("test_audio_acks: %d failure(s)\n", fails); return 1; }
    printf("test_audio_acks OK\n");
    return 0;
}
