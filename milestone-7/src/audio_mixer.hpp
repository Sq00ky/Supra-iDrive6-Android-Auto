// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_MIXER_HPP
#define AUDIO_MIXER_HPP

// Three-input PCM mixer with media ducking (C++03 / GCC 4.4 compatible, no QNX or libm dependencies; host-tested
// in tests/test_audio_mixer.cpp). Used by the browser audio mode (browser_audio.cpp) to play the Android Auto
// MEDIA, GUIDANCE and SYSTEM_AUDIO streams as one stream:
//
//   out = clip( media * media_gain * duck(t)  +  guidance * guidance_gain  +  system * system_gain )
//
// duck(t) ramps linearly from 1 down to duck_gain over attack_ms while a ducking input (guidance or system sound)
// is active, stays down for hold_ms after it ends (so gaps between the words of a prompt don't pump the music),
// then ramps back up to 1 over release_ms. The ramp is applied per frame, so gain changes never click.

#include <stdint.h>
#include <stddef.h>

namespace nbt {

struct DuckConfig {
    float    duck_gain;       // media gain while ducked (linear, 0..1)
    uint32_t attack_ms;       // 1 -> duck_gain
    uint32_t release_ms;      // duck_gain -> 1
    uint32_t hold_ms;         // stay ducked this long after the last ducking input
    float    media_gain;      // fixed per-stream gains (linear)
    float    guidance_gain;
    float    system_gain;

    DuckConfig();
};

class AudioMixer {
public:
    AudioMixer();

    void configure(uint32_t rate, uint32_t channels, const DuckConfig& cfg);

    // Mixes `frames` interleaved frames into out. Any input may be NULL (= silence). `duck` says whether a ducking
    // input (guidance / system sound) is active during this block; the hold timer is handled here.
    void mix(const int16_t* media, const int16_t* guidance, const int16_t* system, size_t frames, bool duck,
             int16_t* out);

    float duckLevel() const { return duck_; }      // current duck factor (1 = not ducked)
    bool ducking() const { return hold_left_ > 0 || duck_ < 1.0f; }
    const DuckConfig& config() const { return cfg_; }

    // 10^(db/20) without libm (whole dB, clamped to -60..+12).
    static float dbToGain(int db);

private:
    uint32_t rate_;
    uint32_t ch_;
    DuckConfig cfg_;
    float duck_;            // current duck factor
    float attack_step_;     // per frame
    float release_step_;    // per frame
    uint32_t hold_frames_;
    uint32_t hold_left_;    // frames left before the release ramp may start
};

// Works out the sample rate of an AA PCM stream from its frame timestamps (microseconds). The head unit offers the
// phone a list of AudioConfigs per sink and the phone doesn't say which one it took: the DHU manifest offers ch 8
// 16 kHz and 48 kHz mono, and the car showed the phone sending 48 kHz (2048 samples per 42.7 ms frame, 2026-11-01)
// where 16 kHz was assumed, so prompts played 3x slow. The rate is bytes before the latest frame / its timestamp
// distance from the first, snapped to a standard rate; it is known once the frames span 200 ms (at least 3 frames).
class PtsRateMeter {
public:
    PtsRateMeter();
    void reset();   // a new stream (AV start): measure again
    // Feeds one frame. Returns the snapped rate once known (and on every later frame), 0 until then or when the
    // timestamps don't match a standard rate within 8 %.
    uint32_t add(uint64_t pts_us, size_t bytes, uint32_t channels);

    static uint32_t snap(uint32_t measured);   // nearest standard rate within 8 %, else 0

private:
    uint64_t first_pts_;
    uint64_t bytes_;       // bytes of the frames before the latest one
    uint32_t frames_;
    uint32_t rate_;        // result (0 = not known)
    bool done_;
};

} // namespace nbt

#endif // AUDIO_MIXER_HPP
