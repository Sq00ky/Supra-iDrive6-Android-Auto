// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio/drift.hpp"

namespace nbt {

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

} // namespace nbt
