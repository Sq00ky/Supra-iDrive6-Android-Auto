// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio/playout.hpp"

namespace nbt {

StreamWatch::StreamWatch() : pulls_(0), short_(0), empty_(0), min_queued_(0), total_gaps_(0) {}

void StreamWatch::resetInterval() {
    pulls_ = short_ = empty_ = 0;
    min_queued_ = 0;
}

void StreamWatch::resetAll() {
    resetInterval();
    total_gaps_ = 0;
}

void StreamWatch::onPull(bool started, size_t queued, size_t got, size_t need) {
    if (!started) return;
    if (pulls_ == 0 || queued < min_queued_) min_queued_ = queued;
    ++pulls_;
    if (got == 0) { ++empty_; ++total_gaps_; }
    else if (got < need) { ++short_; ++total_gaps_; }
}

size_t estimateOutFrames(size_t ring_bytes, size_t pending_samples, uint32_t src_rate, uint32_t src_ch,
                         uint32_t out_rate, uint32_t out_ch) {
    const size_t pending = out_ch ? pending_samples / out_ch : 0;
    if (!src_rate || !src_ch) return pending;
    const uint64_t in_frames = ring_bytes / ((size_t)src_ch * 2u);
    return pending + (size_t)(in_frames * out_rate / src_rate);
}

UnderrunGuard::UnderrunGuard(uint32_t budget_ms) : budget_ms_(budget_ms), starved_(false), timeouts_(0) {}

void UnderrunGuard::reset() {
    starved_ = false;
    timeouts_ = 0;
}

bool UnderrunGuard::shouldWait(bool started, size_t avail_frames, size_t need_frames, uint32_t waited_ms,
                               bool can_wait) {
    if (!budget_ms_ || !started) return false;
    if (avail_frames >= need_frames) { starved_ = false; return false; }
    if (starved_ || !can_wait) return false;
    if (waited_ms >= budget_ms_) { starved_ = true; ++timeouts_; return false; }
    return true;
}

void UnderrunGuard::onPulled(bool started, size_t got_frames, size_t need_frames) {
    if (started && got_frames >= need_frames) starved_ = false;
}

bool waitIsSafe(bool last_write_blocked, int64_t lead_ms, uint32_t budget_ms, uint32_t block_ms) {
    if (last_write_blocked) return true;
    return lead_ms >= (int64_t)budget_ms + (int64_t)block_ms + 20;
}

PrerollGate::PrerollGate()
    : target_(0), hold_ms_(0), open_(true), since_ms_(0), holds_(0), total_holds_(0) {}

void PrerollGate::configure(size_t target_frames, uint32_t hold_ms) {
    target_ = target_frames;
    hold_ms_ = hold_ms;
    restart();
}

void PrerollGate::restart() {
    open_ = (target_ == 0);
    since_ms_ = 0;
}

void PrerollGate::reset() {
    restart();
    holds_ = 0;
    total_holds_ = 0;
}

bool PrerollGate::ready(bool started, size_t avail_frames, uint64_t now_ms) {
    if (!started) {               // stopped: its tail plays out, and the next START holds it again
        restart();
        return true;
    }
    if (open_) return true;
    if (avail_frames == 0) { since_ms_ = 0; return false; }
    if (since_ms_ == 0) since_ms_ = now_ms ? now_ms : 1;
    if (avail_frames >= target_ || now_ms - since_ms_ >= (uint64_t)hold_ms_) {
        open_ = true;
        since_ms_ = 0;
        ++holds_;
        ++total_holds_;
        return true;
    }
    return false;
}

void PrerollGate::onPulled(bool started, size_t got_frames) {
    if (started && open_ && target_ && got_frames == 0) { open_ = false; since_ms_ = 0; }
}

} // namespace nbt
