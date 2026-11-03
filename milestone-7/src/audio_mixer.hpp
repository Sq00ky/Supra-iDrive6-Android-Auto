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
// The first timestamps of a stream are uneven (car 2026-11-02, media ch 9: frame gaps 41.9 / 43.7 / 44.4 / 59.2 /
// 41.7 ms, i.e. 44.4 kHz over the first 231 ms of a 48 kHz stream, snapped to 44100: music 8 % slow and the ring
// overflowing = crackling). So a stream should be given the rates it was actually offered (setCandidates): the
// result is then only ever one of those, the nearest within 25 %.
class PtsRateMeter {
public:
    PtsRateMeter();
    void reset();   // a new stream (AV start): measure again (keeps the candidates)
    // Restricts the result to these rates (the AudioConfigs offered for this sink, at most 4). n = 0: any standard rate
    // within 8 % (snap()).
    void setCandidates(const uint32_t* rates, size_t n);
    // Feeds one frame. Returns the snapped rate once known (and on every later frame), 0 until then or when the
    // timestamps match none of the rates.
    uint32_t add(uint64_t pts_us, size_t bytes, uint32_t channels);

    static uint32_t snap(uint32_t measured);   // nearest standard rate within 8 %, else 0
    // Nearest of rates[0..n) within pct %, else 0.
    static uint32_t snapTo(uint32_t measured, const uint32_t* rates, size_t n, uint32_t pct);

private:
    uint64_t first_pts_;
    uint64_t bytes_;       // bytes of the frames before the latest one
    uint32_t frames_;
    uint32_t rate_;        // result (0 = not known)
    bool done_;
    uint32_t cand_[4];
    size_t ncand_;
};

// Health counters for one AA stream in the mixer (stats interval + session totals). The 10 s stats line only sampled
// the ring once, so a dip in between left no trace; this counts every mixer block of a started stream:
//   short = the block got some audio but not a whole block (the rest was padded)
//   empty = the block got no audio at all
// and keeps the lowest ring fill (bytes, read before the block) seen in the interval.
class StreamWatch {
public:
    StreamWatch();
    void resetInterval();      // after each stats line
    void resetAll();           // session ended
    // One mixer block. started = the stream is between START and STOP; queued = ring bytes before the pull;
    // got / need = converted samples obtained / wanted. Blocks of a stopped stream (its tail) are not counted.
    void onPull(bool started, size_t queued, size_t got, size_t need);

    uint32_t pulls() const { return pulls_; }
    uint32_t shortPulls() const { return short_; }
    uint32_t emptyPulls() const { return empty_; }
    bool hasMin() const { return pulls_ > 0; }
    size_t minQueued() const { return min_queued_; }
    uint64_t totalGaps() const { return total_gaps_; }   // short + empty since the session started

private:
    uint32_t pulls_, short_, empty_;
    size_t min_queued_;
    uint64_t total_gaps_;
};

// Waiting for late input instead of padding with silence (browser mode). When the mixer's previous write blocked,
// the device was full and has just freed one of its 5 fragments: it still holds ~4 (~92 ms), however much was waited
// before. A wait of at most `budget` ms for a frame that is a few ms late is then inaudible, while padding puts a hole
// in the music at once. Waiting is allowed only in that state (device_full), so waits can never add up and drain
// the device: if the last write did not block, the block is padded, which refills it. After a wait runs out the
// stream counts as starved and is padded at once until a whole block is available again (data really stopped,
// e.g. a pause without STOP).

// Output frames that queued input will give after conversion: ring bytes at src_rate / src_ch, plus already
// converted pending samples (out_ch interleaved).
size_t estimateOutFrames(size_t ring_bytes, size_t pending_samples, uint32_t src_rate, uint32_t src_ch,
                         uint32_t out_rate, uint32_t out_ch);

class UnderrunGuard {
public:
    explicit UnderrunGuard(uint32_t budget_ms = 30);
    void setBudget(uint32_t ms) { budget_ms_ = ms; }   // 0 = never wait (pad at once, the old behaviour)
    void reset();                                       // session ended
    // Before a block, after waited_ms of waiting for it: true = wait a little longer for this stream. device_full =
    // the mixer's previous write blocked (the device holds ~4 fragments); without it the block is padded.
    bool shouldWait(bool started, size_t avail_frames, size_t need_frames, uint32_t waited_ms, bool device_full);
    // After the block's pull: a whole block clears the starved state.
    void onPulled(bool started, size_t got_frames, size_t need_frames);
    bool starved() const { return starved_; }
    uint32_t timeouts() const { return timeouts_; }    // waits that ran out (the block was padded)

private:
    uint32_t budget_ms_;
    bool starved_;
    uint32_t timeouts_;
};

// Short linear fades so a real gap (padding that could not be avoided, or the end of a prompt) is not a click:
// fadeOutTail ramps the last fade_frames of buf[0, frames) down to silence, fadeInHead the first fade_frames up.
void fadeOutTail(int16_t* buf, size_t frames, size_t channels, size_t fade_frames);
void fadeInHead(int16_t* buf, size_t frames, size_t channels, size_t fade_frames);

// Clock-drift correction for the AA media stream (browser mode). The phone produces audio on its own clock, the car
// plays it on another. With acks on arrival nothing ties them: a phone even 50 ppm slow drains its ~130 ms lead in
// ~45 min and then gaps every few seconds; a fast one makes the latency creep up. With deferred acks (media_ack.hpp)
// a phone may be held to the ack window instead: its queue then sits near window x frame length (~427 ms for 10
// frames), which no trim can lower (faster playing only makes it send sooner). The target cap must then cover that
// level (raiseMaxTarget), or the loop sees a permanent error above a 300 ms cap and pins the trim at +max_ppm.
// DriftControl watches how much media is queued (ring + converted, in ms of audio) once per mixer block:
//   - for the first learn_ms of a stream it only learns the level the phone naturally keeps (smoothed), bounded to
//     [min_target_ms, max_target_ms]: that becomes the target, so no latency is added;
//   - then it returns a resampler trim in ppm proportional to the smoothed error (kp ppm per ms; 10 ppm/ms gives a
//     ~100 s loop time constant and a ~10 ms steady offset at 100 ppm drift), at most +/-max_ppm, changing by at
//     most slew ppm per second. Positive = the queue is above target = play faster.
// +/-500 ppm is under 1 cent of pitch: inaudible. A queue that grows means the phone is fast, so we consume faster.
struct DriftConfig {
    uint32_t learn_ms, min_target_ms, max_target_ms;
    int32_t max_ppm;
    double kp_ppm_per_ms, smooth_ms, slew_ppm_per_s;
    DriftConfig();
};

class DriftControl {
public:
    DriftControl();
    void configure(const DriftConfig& cfg);
    void reset();                                       // stream start / stop / session end
    // Raises max_target_ms to at least ms (never lowers it); kept across reset(). Takes effect when learning ends.
    void raiseMaxTarget(uint32_t ms) { if (ms > cfg_.max_target_ms) cfg_.max_target_ms = ms; }
    uint32_t maxTarget() const { return cfg_.max_target_ms; }
    // One mixer block: fill_ms = queued media in ms of audio. Returns the trim to apply (0 while learning).
    int32_t update(double fill_ms, uint64_t now_ms);
    bool learning() const { return learning_; }
    double target() const { return target_; }
    double smoothed() const { return ema_; }
    int32_t ppm() const;

private:
    DriftConfig cfg_;
    bool started_, learning_;
    uint64_t t_start_, t_last_;
    double ema_, target_, ppm_;
};

// A proportional-integral version of the same loop (the default; NBT_BROWSER_DRIFT_LOOP=p keeps DriftControl's
// proportional trim). DriftControl's P-only trim has to stay off target to produce any trim at all: 10 ppm per ms, so
// a phone 100 ppm off holds the queue ~10 ms away from where it should be, 300 ppm ~30 ms. The integral term removes
// that offset and, once settled, equals the phone's clock error in ppm, which the stats log.
//
// The queue moves by (phone - trim) x 1e-3 ms per second, so with trim = kp*e + ki*integral(e) the error obeys
// e'' + 1e-3*kp*e' + 1e-3*ki*e = 0: natural frequency wn = 1 / natural_s, damping zeta, kp = 2*zeta*wn*1000,
// ki = wn^2*1000. Defaults: natural_s 100 (DriftControl's 100 s time constant), zeta 0.707: kp 14.1 ppm/ms,
// ki 0.1 ppm/(ms*s), settles in ~10 minutes with ~4 % overshoot. The input is DriftControl's smoothed queue minus its
// learned target, so learning, smoothing and the target bounds are shared. Output capped at +/-max_ppm and slewed
// like DriftControl; the integral stops growing while the output is held at a cap or by the slew (no wind-up).
struct DriftPiConfig {
    double natural_s, damping, slew_ppm_per_s;
    int32_t max_ppm;
    DriftPiConfig();
};

class DriftPi {
public:
    DriftPi();
    void configure(const DriftPiConfig& cfg);
    void reset();
    // err_ms = smoothed queue - target (positive: too much queued, play faster). Returns the trim in ppm.
    int32_t update(double err_ms, uint64_t now_ms);
    int32_t ppm() const;
    double integral() const { return integ_; }   // ppm: the phone's clock error once settled
    double kp() const { return kp_; }
    double ki() const { return ki_; }

private:
    DriftPiConfig cfg_;
    double kp_, ki_;
    bool started_;
    uint64_t t_last_;
    double integ_, ppm_;
};

// The input rate of one AA audio stream. Each stream start may carry the phone's declared format (START_INDICATION
// configuration_index -> the offered AudioConfig): that is used from the first frame, and the timestamps are then
// only a cross-check (a disagreement is reported once, never acted on). Without a declaration the rate is measured
// from the timestamps, limited to the offered rates (PtsRateMeter::setCandidates).
class StreamRate {
public:
    StreamRate();
    void configure(uint32_t default_rate, const uint32_t* offered, size_t n, uint32_t channels);
    void reset();                      // session ended: back to the default, nothing declared
    void start(uint32_t declared);     // stream start; declared = the phone's rate, 0 = none
    // One frame. Returns true when rate() changed. *mismatch (optional) is set to the measured rate, once per start,
    // if the timestamps disagree with a declaration (0 otherwise).
    bool frame(uint64_t pts_us, size_t bytes, uint32_t* mismatch);
    uint32_t rate() const { return rate_; }
    bool declared() const { return declared_; }

private:
    PtsRateMeter meter_;
    uint32_t default_, rate_, ch_;
    bool declared_, warned_;
};

} // namespace nbt

#endif // AUDIO_MIXER_HPP
