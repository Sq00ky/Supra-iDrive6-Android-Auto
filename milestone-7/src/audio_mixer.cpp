// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio_mixer.hpp"
#include <string.h>

namespace nbt {

DuckConfig::DuckConfig()
    : duck_gain(0.25f),     // -12 dB
      attack_ms(80),
      release_ms(500),
      hold_ms(600),
      media_gain(1.0f),
      guidance_gain(1.0f),
      system_gain(1.0f) {
}

AudioMixer::AudioMixer()
    : rate_(48000), ch_(2), duck_(1.0f), attack_step_(1.0f), release_step_(1.0f), hold_frames_(0), hold_left_(0) {
    configure(48000, 2, DuckConfig());
}

float AudioMixer::dbToGain(int db) {
    if (db < -60) db = -60;
    if (db > 12) db = 12;
    const float kStep = 1.12201845f;   // 10^(1/20): one dB
    float g = 1.0f;
    if (db >= 0) { for (int i = 0; i < db; ++i) g *= kStep; }
    else         { for (int i = 0; i < -db; ++i) g /= kStep; }
    return g;
}

static float clampUnit(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void AudioMixer::configure(uint32_t rate, uint32_t channels, const DuckConfig& cfg) {
    rate_ = rate ? rate : 48000;
    ch_ = channels ? channels : 2;
    cfg_ = cfg;
    cfg_.duck_gain = clampUnit(cfg_.duck_gain);
    const float span = 1.0f - cfg_.duck_gain;
    uint32_t af = (uint32_t)((uint64_t)rate_ * cfg_.attack_ms / 1000u);
    uint32_t rf = (uint32_t)((uint64_t)rate_ * cfg_.release_ms / 1000u);
    attack_step_ = af ? span / (float)af : 1.0f;     // 0 ms = jump
    release_step_ = rf ? span / (float)rf : 1.0f;
    hold_frames_ = (uint32_t)((uint64_t)rate_ * cfg_.hold_ms / 1000u);
    duck_ = 1.0f;
    hold_left_ = 0;
}

static inline int16_t sat16(float v) {
    if (v > 32767.0f) return 32767;
    if (v < -32768.0f) return -32768;
    return (int16_t)(v < 0 ? v - 0.5f : v + 0.5f);
}

void AudioMixer::mix(const int16_t* media, const int16_t* guidance, const int16_t* system, size_t frames,
                     bool duck, int16_t* out) {
    if (!out) return;
    if (duck) hold_left_ = hold_frames_ > 0 ? hold_frames_ : 1;
    for (size_t f = 0; f < frames; ++f) {
        // Duck envelope, one step per frame.
        if (hold_left_ > 0) {
            if (duck_ > cfg_.duck_gain) { duck_ -= attack_step_; if (duck_ < cfg_.duck_gain) duck_ = cfg_.duck_gain; }
            if (!duck) --hold_left_;     // while ducking input is live the hold stays full
        } else if (duck_ < 1.0f) {
            duck_ += release_step_;
            if (duck_ > 1.0f) duck_ = 1.0f;
        }
        const float mg = cfg_.media_gain * duck_;
        for (uint32_t c = 0; c < ch_; ++c) {
            const size_t i = f * ch_ + c;
            float v = 0.0f;
            if (media) v += (float)media[i] * mg;
            if (guidance) v += (float)guidance[i] * cfg_.guidance_gain;
            if (system) v += (float)system[i] * cfg_.system_gain;
            out[i] = sat16(v);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------

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

void fadeOutTail(int16_t* buf, size_t frames, size_t channels, size_t fade_frames) {
    if (!buf || !channels || !frames) return;
    if (fade_frames > frames) fade_frames = frames;
    const size_t first = frames - fade_frames;
    for (size_t f = 0; f < fade_frames; ++f) {
        // gain (fade_frames - 1 - f) / fade_frames: the last frame reaches 0
        const int32_t num = (int32_t)(fade_frames - 1 - f), den = (int32_t)fade_frames;
        for (size_t c = 0; c < channels; ++c) {
            int16_t& v = buf[(first + f) * channels + c];
            v = (int16_t)((int32_t)v * num / den);
        }
    }
}

void fadeInHead(int16_t* buf, size_t frames, size_t channels, size_t fade_frames) {
    if (!buf || !channels || !frames) return;
    if (fade_frames > frames) fade_frames = frames;
    for (size_t f = 0; f < fade_frames; ++f) {
        // gain (f + 1) / (fade_frames + 1): starts just above 0, never fully silent
        const int32_t num = (int32_t)(f + 1), den = (int32_t)(fade_frames + 1);
        for (size_t c = 0; c < channels; ++c) {
            int16_t& v = buf[f * channels + c];
            v = (int16_t)((int32_t)v * num / den);
        }
    }
}

DriftConfig::DriftConfig()
    : learn_ms(10000), min_target_ms(60), max_target_ms(300), max_ppm(500), kp_ppm_per_ms(10.0), smooth_ms(2000.0),
      slew_ppm_per_s(20.0) {}

DriftControl::DriftControl() { reset(); }

void DriftControl::configure(const DriftConfig& cfg) {
    cfg_ = cfg;
    reset();
}

void DriftControl::reset() {
    started_ = false;
    learning_ = true;
    t_start_ = t_last_ = 0;
    ema_ = target_ = ppm_ = 0.0;
}

int32_t DriftControl::ppm() const {
    return (int32_t)(ppm_ >= 0 ? ppm_ + 0.5 : ppm_ - 0.5);
}

int32_t DriftControl::update(double fill_ms, uint64_t now_ms) {
    if (fill_ms < 0) fill_ms = 0;
    if (!started_) {
        started_ = true;
        t_start_ = t_last_ = now_ms;
        ema_ = fill_ms;
        return 0;
    }
    if (now_ms <= t_last_) return ppm();
    const double dt = (double)(now_ms - t_last_);
    t_last_ = now_ms;
    // Smooth out the sawtooth of 42.7 ms AA frames arriving and blocks being taken.
    ema_ += (fill_ms - ema_) * dt / (cfg_.smooth_ms + dt);
    if (learning_) {
        if (now_ms - t_start_ < cfg_.learn_ms) return 0;
        learning_ = false;
        target_ = ema_;
        if (target_ < cfg_.min_target_ms) target_ = cfg_.min_target_ms;   // too little lead: build some up
        if (target_ > cfg_.max_target_ms) target_ = cfg_.max_target_ms;   // a backlog (e.g. waiting for the grant)
    }
    double want = cfg_.kp_ppm_per_ms * (ema_ - target_);
    if (want > cfg_.max_ppm) want = cfg_.max_ppm;
    if (want < -cfg_.max_ppm) want = -cfg_.max_ppm;
    const double step = cfg_.slew_ppm_per_s * dt / 1000.0;
    if (want > ppm_ + step) ppm_ += step;
    else if (want < ppm_ - step) ppm_ -= step;
    else ppm_ = want;
    return ppm();
}

DriftPiConfig::DriftPiConfig() : natural_s(100.0), damping(0.707), slew_ppm_per_s(20.0), max_ppm(500) {}

DriftPi::DriftPi() : kp_(0), ki_(0) { configure(DriftPiConfig()); }

void DriftPi::configure(const DriftPiConfig& cfg) {
    cfg_ = cfg;
    const double wn = cfg_.natural_s > 0 ? 1.0 / cfg_.natural_s : 0.01;
    kp_ = 2.0 * cfg_.damping * wn * 1000.0;
    ki_ = wn * wn * 1000.0;
    reset();
}

void DriftPi::reset() {
    started_ = false;
    t_last_ = 0;
    integ_ = ppm_ = 0.0;
}

int32_t DriftPi::ppm() const {
    return (int32_t)(ppm_ >= 0 ? ppm_ + 0.5 : ppm_ - 0.5);
}

int32_t DriftPi::update(double err_ms, uint64_t now_ms) {
    if (!started_) {
        started_ = true;
        t_last_ = now_ms;
        return ppm();
    }
    if (now_ms <= t_last_) return ppm();
    const double dt = (double)(now_ms - t_last_) / 1000.0;   // s
    t_last_ = now_ms;
    const double cap = (double)cfg_.max_ppm;
    // Candidate integral, then the output it gives; keep the integral step only if the output is not pinned (a cap or
    // the slew) in the direction this error pushes.
    const double integ_next = integ_ + ki_ * err_ms * dt;
    double want = kp_ * err_ms + integ_next;
    if (want > cap) want = cap;
    if (want < -cap) want = -cap;
    const double step = cfg_.slew_ppm_per_s * dt;
    double out = want;
    if (want > ppm_ + step) out = ppm_ + step;
    else if (want < ppm_ - step) out = ppm_ - step;
    const bool pinned_up = (out < kp_ * err_ms + integ_next) && err_ms > 0;
    const bool pinned_down = (out > kp_ * err_ms + integ_next) && err_ms < 0;
    if (!pinned_up && !pinned_down) integ_ = integ_next;
    if (integ_ > cap) integ_ = cap;
    if (integ_ < -cap) integ_ = -cap;
    ppm_ = out;
    return ppm();
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
