// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio/stream_rate.hpp"
#include <string.h>

namespace nbt {

PtsRateMeter::PtsRateMeter() : ncand_(0) {
    memset(cand_, 0, sizeof(cand_));
    reset();
}

void PtsRateMeter::reset() {
    first_pts_ = 0;
    bytes_ = 0;
    frames_ = 0;
    rate_ = 0;
    done_ = false;
}

void PtsRateMeter::setCandidates(const uint32_t* rates, size_t n) {
    ncand_ = 0;
    for (size_t i = 0; rates && i < n && ncand_ < sizeof(cand_) / sizeof(cand_[0]); ++i)
        if (rates[i]) cand_[ncand_++] = rates[i];
}

uint32_t PtsRateMeter::snapTo(uint32_t measured, const uint32_t* rates, size_t n, uint32_t pct) {
    uint32_t best = 0, best_diff = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) {
        uint32_t d = measured > rates[i] ? measured - rates[i] : rates[i] - measured;
        if (d < best_diff) { best_diff = d; best = rates[i]; }
    }
    return (best && (uint64_t)best_diff * 100u <= (uint64_t)best * pct) ? best : 0;
}

uint32_t PtsRateMeter::snap(uint32_t measured) {
    static const uint32_t kRates[] = { 8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000 };
    return snapTo(measured, kRates, sizeof(kRates) / sizeof(kRates[0]), 8u);
}

uint32_t PtsRateMeter::add(uint64_t pts_us, size_t bytes, uint32_t channels) {
    if (done_) return rate_;
    if (channels == 0) channels = 1;
    if (frames_ == 0 || pts_us <= first_pts_) {   // first frame, or the clock went back: start over
        first_pts_ = pts_us;
        bytes_ = bytes;
        frames_ = 1;
        return 0;
    }
    const uint64_t span = pts_us - first_pts_;
    if (frames_ >= 2 && span >= 200000u) {
        const uint64_t samples = bytes_ / (2u * channels);
        const uint32_t measured = (uint32_t)(samples * 1000000u / span);
        rate_ = ncand_ ? snapTo(measured, cand_, ncand_, 25u) : snap(measured);
        done_ = true;
        return rate_;
    }
    bytes_ += bytes;
    ++frames_;
    return 0;
}
StreamRate::StreamRate() : default_(48000), rate_(48000), ch_(1), declared_(false), warned_(false) {}

void StreamRate::configure(uint32_t default_rate, const uint32_t* offered, size_t n, uint32_t channels) {
    default_ = default_rate;
    ch_ = channels ? channels : 1;
    meter_.setCandidates(offered, n);
    reset();
}

void StreamRate::reset() {
    rate_ = default_;
    declared_ = false;
    warned_ = false;
    meter_.reset();
}

void StreamRate::start(uint32_t declared) {
    meter_.reset();   // every start is measured again
    warned_ = false;
    declared_ = declared != 0;
    if (declared_) rate_ = declared;
}

bool StreamRate::frame(uint64_t pts_us, size_t bytes, uint32_t* mismatch) {
    if (mismatch) *mismatch = 0;
    if (!pts_us) return false;
    const uint32_t r = meter_.add(pts_us, bytes, ch_);
    if (!r || r == rate_) return false;
    if (declared_) {
        if (!warned_ && mismatch) *mismatch = r;
        warned_ = true;
        return false;
    }
    rate_ = r;
    return true;
}

} // namespace nbt
