// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef MEDIA_ACK_HPP
#define MEDIA_ACK_HPP

// Deferred media acks for one AA audio stream (C++03 / GCC 4.4, header-only, host-tested in tests/test_media_ack.cpp).
//
// The phone may have at most max_unacked frames of a stream unacknowledged (our AV setup response says 10). Acking a
// frame as soon as it arrives throws that window away: the phone runs on its own clock. Google's head unit (the DHU's
// AudioCallback::dataAvailableCallback) acks a frame only once its playback buffer has room for it, so a phone that
// runs ahead is held back by the car's clock. This ledger does the same for the browser audio mode: a frame is acked
// once the mixer has taken all of its bytes from the stream's ring (played or discarded).
//
// Bytes are consumed oldest first, the way the ring hands them out. A frame unacked for force_ns is acked anyway
// (counted as forced), so a mixer that stops consuming (no output device, the car has not granted the source yet)
// slows the phone down but never stalls it; the bytes such a frame still had in the ring are skipped when they are
// consumed later, so they do not release the next frames early.
//
// Not thread-safe: the owner (AASession) guards it with a mutex. Times are CLOCK_MONOTONIC nanoseconds.

#include <stdint.h>
#include <stddef.h>
#include <deque>
#include <vector>

namespace nbt {

class MediaAckLedger {
public:
    explicit MediaAckLedger(uint32_t window = 10) : window_(window ? window : 1) { reset(); resetTotals(); }

    void setWindow(uint32_t w) { window_ = w ? w : 1; }
    uint32_t window() const { return window_; }

    // New stream (START) or session: forget pending frames and skipped bytes. Totals are kept (resetTotals()).
    void reset() {
        q_.clear();
        first_unconsumed_ = 0;
        skip_ = 0;
        full_ = false;
    }

    // A frame with `bytes` of PCM arrived. Must be called before its bytes can be consumed.
    void onFrame(size_t bytes, uint64_t now_ns) {
        Frame f;
        f.left = bytes;
        f.t = now_ns;
        q_.push_back(f);
        if (bytes == 0 && first_unconsumed_ == q_.size() - 1) ++first_unconsumed_;
        if (q_.size() > max_pending_) max_pending_ = (uint32_t)q_.size();
        if (q_.size() > max_pending_iv_) max_pending_iv_ = (uint32_t)q_.size();
        if (!full_ && q_.size() >= window_) {   // the phone has used its whole window: it waits for our next ack
            full_ = true;
            ++window_full_;
            ++window_full_iv_;
        }
    }

    // The consumer took `bytes` of this stream (oldest first): played, or dropped from the ring.
    void onConsumed(size_t bytes) {
        if (skip_) {
            const size_t d = bytes < skip_ ? bytes : skip_;
            skip_ -= d;
            bytes -= d;
        }
        while (bytes && first_unconsumed_ < q_.size()) {
            Frame& f = q_[first_unconsumed_];
            const size_t d = bytes < f.left ? bytes : f.left;
            f.left -= d;
            bytes -= d;
            if (f.left == 0) ++first_unconsumed_;
        }
        // Bytes beyond the known frames (a tail from before a reset) are ignored, never banked.
    }

    // True when take() would release a frame without forcing (wake the sender).
    bool ready() const { return !q_.empty() && q_.front().left == 0; }

    // Releases the frames to ack now: every fully consumed frame at the front, plus any unacked for force_ns or more
    // (force_ns 0 = never force). Appends their arrival times to *times when given. Returns how many.
    uint32_t take(uint64_t now_ns, uint64_t force_ns, std::vector<uint64_t>* times) {
        uint32_t n = 0;
        while (!q_.empty()) {
            const Frame& f = q_.front();
            const bool done = (f.left == 0);
            const bool old = force_ns && now_ns >= f.t && now_ns - f.t >= force_ns;
            if (!done && !old) break;
            if (!done) {
                skip_ += f.left;   // its remaining bytes are still in the ring: they must not count for later frames
                ++forced_;
                ++forced_iv_;
            }
            if (times) times->push_back(f.t);
            q_.pop_front();
            if (first_unconsumed_ > 0) --first_unconsumed_;
            ++n;
        }
        acked_ += n;
        acked_iv_ += n;
        if (q_.size() < window_) full_ = false;
        return n;
    }

    // Releases everything pending (stream restart: ack the old stream's leftovers). Not counted as forced.
    uint32_t releaseAll(std::vector<uint64_t>* times) {
        uint32_t n = 0;
        while (!q_.empty()) {
            if (times) times->push_back(q_.front().t);
            q_.pop_front();
            ++n;
        }
        acked_ += n;
        acked_iv_ += n;
        reset();
        return n;
    }

    size_t pending() const { return q_.size(); }
    uint64_t oldestNs() const { return q_.empty() ? 0 : q_.front().t; }

    // Statistics. *_iv = since resetInterval() (the periodic stats line), the others since resetTotals() (session).
    uint32_t acked() const { return acked_; }
    uint32_t forced() const { return forced_; }
    uint32_t windowFull() const { return window_full_; }   // times the phone filled its window
    uint32_t maxPending() const { return max_pending_; }
    uint32_t ackedIv() const { return acked_iv_; }
    uint32_t forcedIv() const { return forced_iv_; }
    uint32_t windowFullIv() const { return window_full_iv_; }
    uint32_t maxPendingIv() const { return max_pending_iv_; }
    void resetInterval() { acked_iv_ = forced_iv_ = window_full_iv_ = max_pending_iv_ = 0; }
    void resetTotals() { acked_ = forced_ = window_full_ = max_pending_ = 0; resetInterval(); }

private:
    struct Frame {
        size_t left;   // bytes not consumed yet
        uint64_t t;    // arrival
    };
    std::deque<Frame> q_;        // unacked frames, oldest first
    size_t first_unconsumed_;    // index of the first frame with bytes left (q_.size() if none)
    size_t skip_;                // bytes of force-acked frames still in the ring
    uint32_t window_;
    bool full_;
    uint32_t acked_, forced_, window_full_, max_pending_;
    uint32_t acked_iv_, forced_iv_, window_full_iv_, max_pending_iv_;
};

} // namespace nbt

#endif // MEDIA_ACK_HPP
