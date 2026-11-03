// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_DRIFT_HPP
#define AUDIO_DRIFT_HPP

#include <stdint.h>
#include <stddef.h>

namespace nbt {

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

} // namespace nbt

#endif // AUDIO_DRIFT_HPP
