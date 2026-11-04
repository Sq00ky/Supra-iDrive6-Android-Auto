// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: deferred media acks (MediaAckLedger, browser audio mode).
#include "audio/media_ack.hpp"
#include <stdio.h>
#include <vector>

using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static const uint64_t MS = 1000000ull;   // ns

int main() {
    // 1. A frame is acked only once all of its bytes are consumed, oldest first.
    {
        MediaAckLedger l(10);
        l.onFrame(100, 1 * MS);
        l.onFrame(100, 2 * MS);
        l.onFrame(100, 3 * MS);
        CHECK(l.pending() == 3);
        CHECK(!l.ready());
        CHECK(l.take(4 * MS, 0, NULL) == 0);
        l.onConsumed(150);   // frame 1 whole, half of frame 2
        CHECK(l.ready());
        std::vector<uint64_t> t;
        CHECK(l.take(5 * MS, 0, &t) == 1);
        CHECK(t.size() == 1 && t[0] == 1 * MS);
        CHECK(!l.ready());
        l.onConsumed(150);   // rest of 2, all of 3
        t.clear();
        CHECK(l.take(6 * MS, 0, &t) == 2);
        CHECK(t.size() == 2 && t[0] == 2 * MS && t[1] == 3 * MS);
        CHECK(l.pending() == 0);
        CHECK(l.acked() == 3 && l.forced() == 0);
    }

    // 2. A frame unacked for force_ns is released anyway (forced); the bytes it still had are skipped later, so they
    //    don't release the next frame early.
    {
        MediaAckLedger l(10);
        l.onFrame(100, 0);
        l.onFrame(100, 500 * MS);
        l.onConsumed(40);                                  // 60 bytes of frame 1 still in the ring
        CHECK(l.take(999 * MS, 1000 * MS, NULL) == 0);     // not old enough yet
        CHECK(l.take(1000 * MS, 1000 * MS, NULL) == 1);    // frame 1 forced
        CHECK(l.forced() == 1);
        l.onConsumed(60);                                  // frame 1's leftovers: skipped
        CHECK(!l.ready());
        CHECK(l.take(1001 * MS, 1000 * MS, NULL) == 0);
        l.onConsumed(100);                                 // frame 2
        CHECK(l.take(1002 * MS, 1000 * MS, NULL) == 1);
        CHECK(l.forced() == 1 && l.acked() == 2);
        // force_ns 0 never forces
        l.onFrame(10, 0);
        CHECK(l.take(100000 * MS, 0, NULL) == 0);
    }

    // 3. Window-full events: counted once each time the phone fills its window.
    {
        MediaAckLedger l(3);
        l.onFrame(10, 0); l.onFrame(10, 0);
        CHECK(l.windowFull() == 0);
        l.onFrame(10, 0);
        CHECK(l.windowFull() == 1);
        l.onFrame(10, 0);                    // still full: no new event
        CHECK(l.windowFull() == 1);
        CHECK(l.maxPending() == 4);
        l.onConsumed(20);
        CHECK(l.take(0, 0, NULL) == 2);      // 2 left: below the window
        l.onFrame(10, 0);
        CHECK(l.windowFull() == 2);
        CHECK(l.windowFullIv() == 2);
        l.resetInterval();
        CHECK(l.windowFullIv() == 0 && l.windowFull() == 2);
    }

    // 4. releaseAll acks everything (a stream restart) and forgets skipped bytes.
    {
        MediaAckLedger l(10);
        l.onFrame(100, 0);
        l.onFrame(100, 0);
        CHECK(l.take(2000 * MS, 1000 * MS, NULL) == 2);   // both forced: 200 bytes to skip
        l.onFrame(100, 2000 * MS);
        std::vector<uint64_t> t;
        CHECK(l.releaseAll(&t) == 1 && t.size() == 1);
        CHECK(l.pending() == 0);
        l.onFrame(50, 3000 * MS);
        l.onConsumed(50);                                 // not eaten by the old skip
        CHECK(l.ready());
        CHECK(l.forced() == 2);
    }

    // 5. A frame with no bytes is ready at once; consumption with no frames is not banked.
    {
        MediaAckLedger l(10);
        l.onConsumed(1000);
        l.onFrame(0, 0);
        CHECK(l.ready());
        CHECK(l.take(0, 0, NULL) == 1);
        l.onFrame(100, 0);
        CHECK(!l.ready());
        l.onFrame(0, 0);                       // behind an unconsumed frame: waits its turn
        l.onConsumed(100);
        CHECK(l.take(0, 0, NULL) == 2);
    }

    // 6. Closed loop: a phone that sends as fast as its window allows (a prompt, faster than real time) and a mixer
    //    that consumes in real time. The phone is held to the window, every frame is acked, none is forced.
    {
        MediaAckLedger l(10);
        const size_t frame = 4096;             // 42.7 ms of 48 kHz mono
        const double bytes_per_ms = 96.0;      // 48 kHz x 2 bytes
        int sent = 0, total = 200;
        double consumed_carry = 0;
        size_t ring = 0;
        uint32_t max_unacked = 0;
        for (int ms = 0; ms < 20000 && (int)l.acked() < total; ++ms) {
            while (sent < total && l.pending() < 10) {   // the phone sends whenever its window has room
                l.onFrame(frame, (uint64_t)ms * MS);
                ring += frame;
                ++sent;
            }
            if (l.pending() > max_unacked) max_unacked = (uint32_t)l.pending();
            consumed_carry += bytes_per_ms;               // the mixer plays in real time
            size_t c = (size_t)consumed_carry;
            if (c > ring) c = ring;
            consumed_carry -= c;
            ring -= c;
            l.onConsumed(c);
            l.take((uint64_t)ms * MS, 1000 * MS, NULL);
        }
        CHECK((int)l.acked() == total);
        CHECK(l.forced() == 0);
        CHECK(max_unacked <= 10);
        CHECK(l.windowFull() >= 1);
    }

    // 7. Same phone, but the mixer never consumes (no output device): forced acks keep the phone going, slowly.
    {
        MediaAckLedger l(10);
        int sent = 0;
        for (int ms = 0; ms < 10000; ++ms) {
            while (l.pending() < 10) { l.onFrame(4096, (uint64_t)ms * MS); ++sent; }
            l.take((uint64_t)ms * MS, 1000 * MS, NULL);
        }
        CHECK(l.forced() == l.acked());
        CHECK(sent >= 90 && sent <= 110);      // ~10 frames per second instead of a stall
    }

    printf("test_media_ack: %s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
