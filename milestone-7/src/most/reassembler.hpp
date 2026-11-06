// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef MOST_REASSEMBLER_HPP
#define MOST_REASSEMBLER_HPP

// Joins segmented messages, as the vendor client does (handleSegmentedMessages, 0x10c058). Segments are buffered by full
// header equality (dst, src, fblock, inst, fkt, op, hdrTag): a first segment replaces a pending entry, and a last one
// hands out the joined payload with varVal 0. dst is part of the key, so each receiver's copy joins separately.
//
// The vendor's table has no limits. We see the head unit's traffic and not just our own, so this one has them: a fixed
// number of slots, a cap on each message, and the oldest entry goes when a new one needs room. A pending entry has no
// timeout: it waits for its last segment, its next first segment, or eviction. The client buffers only headers a route
// wants. Pure: host-tested in tests/test_most_reassembler.cpp.

#include "most/msg.hpp"
#include <vector>

namespace nbt {
namespace most {

class Reassembler {
public:
    enum { kSlots = 8, kMaxPayload = 4096 };
    enum Result { kPending, kJoined, kLost };

    Reassembler() : clock_(0), evicted_(0) {
        for (int i = 0; i < kSlots; ++i) { used_[i] = false; stamp_[i] = 0; }
    }

    // c: the class of m, one of the three segment classes. kJoined: out is the whole message, with varVal 0 so it
    // classifies as normal, and its data stays valid until the next feed. kLost: this segment could not be used.
    Result feed(const View& m, MsgClass c, View& out) {
        int s = find(m);
        if (c == kSegFirst) {
            if (s < 0) s = takeSlot();
            used_[s] = true;
            stamp_[s] = ++clock_;
            head_[s] = m;
            head_[s].data = NULL;
            head_[s].len = 0;
            buf_[s].clear();
            return append(s, m) ? kPending : kLost;
        }
        if (s < 0) return kLost;
        if (!append(s, m)) return kLost;
        if (c != kSegLast) return kPending;
        used_[s] = false;
        out = head_[s];
        out.varVal = 0;
        out.rxMs = m.rxMs;
        out.data = buf_[s].empty() ? m.data : &buf_[s][0];
        out.len = buf_[s].size();
        return kJoined;
    }

    // Entries dropped to make room for a new one. Their remaining segments then count as lost.
    uint32_t evicted() const { return evicted_; }

private:
    static bool sameHeader(const View& a, const View& b) {
        return a.dst == b.dst && a.src == b.src && a.fblock == b.fblock && a.inst == b.inst && a.fkt == b.fkt &&
               a.op == b.op && a.hdrTag == b.hdrTag;
    }

    int find(const View& m) const {
        for (int i = 0; i < kSlots; ++i)
            if (used_[i] && sameHeader(head_[i], m)) return i;
        return -1;
    }

    int takeSlot() {
        int oldest = 0;
        for (int i = 0; i < kSlots; ++i) {
            if (!used_[i]) return i;
            if (clock_ - stamp_[i] > clock_ - stamp_[oldest]) oldest = i;
        }
        ++evicted_;
        return oldest;
    }

    // False: over the cap, and the entry is gone.
    bool append(int s, const View& m) {
        if (buf_[s].size() + m.len > (size_t)kMaxPayload) {
            used_[s] = false;
            return false;
        }
        buf_[s].insert(buf_[s].end(), m.data, m.data + m.len);
        return true;
    }

    bool used_[kSlots];
    uint32_t stamp_[kSlots];
    View head_[kSlots];
    std::vector<uint8_t> buf_[kSlots];
    uint32_t clock_;
    uint32_t evicted_;
};

} // namespace most
} // namespace nbt

#endif
