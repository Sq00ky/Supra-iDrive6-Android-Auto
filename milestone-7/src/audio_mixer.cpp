// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio_mixer.hpp"

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

PtsRateMeter::PtsRateMeter() { reset(); }

void PtsRateMeter::reset() {
    first_pts_ = 0;
    bytes_ = 0;
    frames_ = 0;
    rate_ = 0;
    done_ = false;
}

uint32_t PtsRateMeter::snap(uint32_t measured) {
    static const uint32_t kRates[] = { 8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000 };
    uint32_t best = 0, best_diff = 0xffffffffu;
    for (size_t i = 0; i < sizeof(kRates) / sizeof(kRates[0]); ++i) {
        uint32_t d = measured > kRates[i] ? measured - kRates[i] : kRates[i] - measured;
        if (d < best_diff) { best_diff = d; best = kRates[i]; }
    }
    return (best && (uint64_t)best_diff * 100u <= (uint64_t)best * 8u) ? best : 0;
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
        rate_ = snap((uint32_t)(samples * 1000000u / span));
        done_ = true;
        return rate_;
    }
    bytes_ += bytes;
    ++frames_;
    return 0;
}

} // namespace nbt
