// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_NAV_PROMPT_POLICY_HPP
#define AUDIO_NAV_PROMPT_POLICY_HPP

// Nav-prompt audio focus: build, hold, release, backoff. Clock-free and not thread-safe; timestamps are
// monotonic ms and never ahead of `now`.
//   IDLE -tryBuild-> BUILDING -granted-> ROUTED -startRelease-> RELEASING -released-> IDLE

#include <stdint.h>

namespace nbt {

class NavPromptPolicy {
public:
    enum State { IDLE = 0, BUILDING = 1, ROUTED = 2, RELEASING = 3 };

    enum Action {
        NONE = 0,
        GRANT_TIMEOUT,     // not granted in time: startRelease, backoff
        RELEASE,           // prompt over and held long enough: startRelease
        RELEASE_TIMEOUT    // the release never reported back: released()
    };

    struct Config {
        uint32_t grant_timeout_ms;
        uint32_t hold_ms;
        uint32_t backoff_ms;
        uint32_t release_timeout_ms;
        Config() : grant_timeout_ms(2000), hold_ms(500), backoff_ms(30000), release_timeout_ms(5000) {}
    };

    NavPromptPolicy() : state_(IDLE), since_ms_(0), backoff_until_ms_(0) {}

    void configure(const Config& cfg) { cfg_ = cfg; }
    State state() const { return state_; }

    // False during the backoff: the prompt goes into the 0xb8 mix instead.
    bool available(uint64_t now) const { return now >= backoff_until_ms_; }

    // True = queue the focus build now (IDLE and past any backoff).
    bool tryBuild(uint64_t now) {
        if (state_ != IDLE || !available(now)) return false;
        state_ = BUILDING;
        since_ms_ = now;
        return true;
    }
    void buildFailed() { state_ = IDLE; }

    // Returns the state it arrived in; BUILDING becomes ROUTED.
    State granted(uint64_t now) {
        const State st = state_;
        if (st == BUILDING) { state_ = ROUTED; since_ms_ = now; }
        return st;
    }

    bool mayPlay() const { return state_ == ROUTED; }

    // True = a release has to be queued (was BUILDING or ROUTED).
    bool startRelease(uint64_t now) {
        if (state_ != BUILDING && state_ != ROUTED) return false;
        state_ = RELEASING;
        since_ms_ = now;
        return true;
    }
    void released() { if (state_ == RELEASING) state_ = IDLE; }

    void backoff(uint64_t now) { backoff_until_ms_ = now + cfg_.backoff_ms; }
    void clearBackoff() { backoff_until_ms_ = 0; }

    // busy = a prompt started or the ring is not empty; last_busy_ms = the last time that was true.
    Action decide(uint64_t now, bool busy, uint64_t last_busy_ms) const {
        if (state_ == BUILDING && now - since_ms_ > cfg_.grant_timeout_ms) return GRANT_TIMEOUT;
        if (state_ == RELEASING && now - since_ms_ > cfg_.release_timeout_ms) return RELEASE_TIMEOUT;
        if (state_ == ROUTED && !busy && now - last_busy_ms > cfg_.hold_ms) return RELEASE;
        return NONE;
    }

private:
    Config cfg_;
    State state_;
    uint64_t since_ms_;
    uint64_t backoff_until_ms_;
};

} // namespace nbt

#endif // AUDIO_NAV_PROMPT_POLICY_HPP
