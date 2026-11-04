// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: most::Reassembler. Joining, the full-header key, replaced and missing first segments, and the limits.
#include "most/reassembler.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>

using namespace nbt::most;

static View seg(uint16_t dst, uint16_t fkt, uint32_t varVal, const char* text, uint32_t rxMs = 100) {
    View v;
    v.txType = 1; v.varTag = 2; v.varVal = varVal; v.dst = dst; v.src = 0x2021;
    v.fblock = 0x52; v.inst = 0; v.fkt = fkt; v.op = 0xC; v.hdrTag = 33;
    v.data = reinterpret_cast<const uint8_t*>(text);
    v.len = strlen(text);
    v.rxMs = rxMs;
    return v;
}

static Reassembler::Result feed(Reassembler& r, const View& v, View& out) { return r.feed(v, classify(v), out); }

static std::string text(const View& v) { return std::string(reinterpret_cast<const char*>(v.data), v.len); }

int main() {
    View out;

    // First, middles, last.
    {
        Reassembler r;
        assert(feed(r, seg(0x2022, 0xC18, 19, "aa"), out) == Reassembler::kPending);
        assert(feed(r, seg(0x2022, 0xC18, 20, "bb"), out) == Reassembler::kPending);
        assert(feed(r, seg(0x2022, 0xC18, 20, "cc"), out) == Reassembler::kPending);
        assert(feed(r, seg(0x2022, 0xC18, 21, "dd", 250), out) == Reassembler::kJoined);
        assert(text(out) == "aabbccdd");
        assert(out.fblock == 0x52 && out.fkt == 0xC18 && out.dst == 0x2022 && out.op == 0xC && out.hdrTag == 33);
        assert(out.rxMs == 250);                  // when the message became whole
        assert(out.varVal == 0 && classify(out) == kNormal);
        // The entry is gone: another last segment has nothing to join.
        assert(feed(r, seg(0x2022, 0xC18, 21, "ee"), out) == Reassembler::kLost);
    }

    // First then last, with no middle, and an empty payload throughout.
    {
        Reassembler r;
        assert(feed(r, seg(0x2022, 0xC18, 19, "x"), out) == Reassembler::kPending);
        assert(feed(r, seg(0x2022, 0xC18, 21, "y"), out) == Reassembler::kJoined && text(out) == "xy");
        assert(feed(r, seg(0x2022, 0xC18, 19, ""), out) == Reassembler::kPending);
        assert(feed(r, seg(0x2022, 0xC18, 21, ""), out) == Reassembler::kJoined && out.len == 0);
    }

    // Missing first segment: the rest is lost, as there is nothing to attach it to.
    {
        Reassembler r;
        assert(feed(r, seg(0x2022, 0xC18, 20, "bb"), out) == Reassembler::kLost);
        assert(feed(r, seg(0x2022, 0xC18, 21, "cc"), out) == Reassembler::kLost);
    }

    // A first segment replaces a pending entry with the same header.
    {
        Reassembler r;
        assert(feed(r, seg(0x2022, 0xC18, 19, "old"), out) == Reassembler::kPending);
        assert(feed(r, seg(0x2022, 0xC18, 19, "new"), out) == Reassembler::kPending);
        assert(feed(r, seg(0x2022, 0xC18, 21, "!"), out) == Reassembler::kJoined && text(out) == "new!");
        assert(r.evicted() == 0);
    }

    // The key: each receiver's copy joins separately, and each function too, even when interleaved.
    {
        Reassembler r;
        assert(feed(r, seg(0x2022, 0xC18, 19, "1"), out) == Reassembler::kPending);
        assert(feed(r, seg(0x204D, 0xC18, 19, "2"), out) == Reassembler::kPending);
        assert(feed(r, seg(0x2022, 0x440, 19, "3"), out) == Reassembler::kPending);
        View other = seg(0x2022, 0xC18, 19, "4");
        other.hdrTag = 34;
        assert(feed(r, other, out) == Reassembler::kPending);
        assert(feed(r, seg(0x204D, 0xC18, 21, "b"), out) == Reassembler::kJoined && text(out) == "2b" && out.dst == 0x204D);
        assert(feed(r, seg(0x2022, 0xC18, 21, "a"), out) == Reassembler::kJoined && text(out) == "1a" && out.hdrTag == 33);
        assert(feed(r, seg(0x2022, 0x440, 21, "c"), out) == Reassembler::kJoined && text(out) == "3c");
    }

    // The joined data survives until the next feed, even when the segments' own buffers are gone.
    {
        Reassembler r;
        char a[4] = "abc";
        assert(feed(r, seg(0x2022, 0xC18, 19, a), out) == Reassembler::kPending);
        memset(a, 'z', 3);
        assert(feed(r, seg(0x2022, 0xC18, 21, "d"), out) == Reassembler::kJoined && text(out) == "abcd");
    }

    // The cap: exactly at it joins, one byte over drops the entry.
    {
        Reassembler r;
        std::string half(Reassembler::kMaxPayload / 2, 'x');
        assert(feed(r, seg(0x2022, 0xC18, 19, half.c_str()), out) == Reassembler::kPending);
        assert(feed(r, seg(0x2022, 0xC18, 21, half.c_str()), out) == Reassembler::kJoined);
        assert(out.len == (size_t)Reassembler::kMaxPayload);

        assert(feed(r, seg(0x2022, 0xC18, 19, half.c_str()), out) == Reassembler::kPending);
        assert(feed(r, seg(0x2022, 0xC18, 20, half.c_str()), out) == Reassembler::kPending);
        assert(feed(r, seg(0x2022, 0xC18, 20, "y"), out) == Reassembler::kLost);
        assert(feed(r, seg(0x2022, 0xC18, 21, "z"), out) == Reassembler::kLost);    // nothing left to join

        std::string big(Reassembler::kMaxPayload + 1, 'x');
        assert(feed(r, seg(0x2022, 0xC18, 19, big.c_str()), out) == Reassembler::kLost);
        assert(feed(r, seg(0x2022, 0xC18, 21, "z"), out) == Reassembler::kLost);
    }

    // All slots busy: a new message takes the oldest slot, and the others still join.
    {
        Reassembler r;
        for (uint16_t i = 0; i < Reassembler::kSlots; ++i)
            assert(feed(r, seg(0x2022, (uint16_t)(0x100 + i), 19, "p"), out) == Reassembler::kPending);
        assert(r.evicted() == 0);
        assert(feed(r, seg(0x2022, 0x200, 19, "n"), out) == Reassembler::kPending);
        assert(r.evicted() == 1);
        assert(feed(r, seg(0x2022, 0x100, 21, "!"), out) == Reassembler::kLost);       // the oldest went
        assert(feed(r, seg(0x2022, 0x101, 21, "!"), out) == Reassembler::kJoined && text(out) == "p!");
        assert(feed(r, seg(0x2022, 0x200, 21, "!"), out) == Reassembler::kJoined && text(out) == "n!");
        // A freed slot is used before anything is evicted.
        assert(feed(r, seg(0x2022, 0x300, 19, "m"), out) == Reassembler::kPending);
        assert(r.evicted() == 1);
    }

    printf("test_most_reassembler OK\n");
    return 0;
}
