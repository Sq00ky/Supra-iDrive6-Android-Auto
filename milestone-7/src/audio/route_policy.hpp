// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_ROUTE_POLICY_HPP
#define AUDIO_ROUTE_POLICY_HPP

// When browser mode builds and releases the car's browser audio source (connection 0xb8). Header-only and clock-free:
// the caller passes the time in, so it is host-tested (tests/test_route_policy.cpp). Not thread-safe: BrowserAudio
// holds its route mutex around every call, and runs audioconn and prints the log lines itself.
//
//   IDLE --tryBuild--> BUILDING --granted--> ROUTED --startRelease--> RELEASING --released--> IDLE
//                         `--------------------------startRelease------^
// RELEASING: `audioconn -r` queued; no new build until it has run, so a build can never be overtaken by it.
// A build that is never granted times out (grant_timeout_ms); the caller then drops the queued audio, releases and
// backs off (backoff_ms) before the next try. A remove that never reports back times out too (release_timeout_ms).
// Once routed, the source is released when it has been quiet: idle_s after AA media last played (paused, between
// tracks: resuming then doesn't rebuild), or prompt_idle_ms after a prompt / system sound when no AA media played
// (the user is on the radio, which is then cut only for the prompt).
// Hold (NBT_BROWSER_HOLD=1): once AA media has played, a pause keeps the source however long; it goes back to the car
// when the driver picks another source (yieldToCar(), e.g. the steering-wheel MODE button). After that, AA media alone
// no longer builds it (a late media frame must not take it back from the radio) until the phone asks for audio focus
// again or starts a new media stream (endYield()); prompts and sounds still build it and release it as above.

#include <stdint.h>

namespace nbt {

class RoutePolicy {
public:
    enum State { IDLE = 0, BUILDING = 1, ROUTED = 2, RELEASING = 3 };

    // What the mixer should do this block (decide()).
    enum Action {
        NONE = 0,
        GRANT_TIMEOUT,     // not granted in time: drop queued audio, startRelease, backoff
        RELEASE_TIMEOUT,   // the remove never reported back: released()
        RELEASE_IDLE,      // no Android Auto sound for a while: startRelease
        RELEASE_PROMPT     // prompt / sound over, no AA media playing: startRelease
    };

    struct Config {
        uint32_t grant_timeout_ms;
        uint32_t release_timeout_ms;
        uint32_t backoff_ms;
        int idle_s;           // NBT_BROWSER_IDLE_S (0 = keep the source until the session ends)
        int prompt_idle_ms;   // NBT_BROWSER_PROMPT_IDLE_MS (0 = use idle_s)
        bool hold;            // NBT_BROWSER_HOLD=1: after AA media, keep the source until the driver changes it
        Config() : grant_timeout_ms(75000), release_timeout_ms(20000), backoff_ms(60000), idle_s(10),
                   prompt_idle_ms(1000), hold(false) {}
    };

    RoutePolicy() : state_(IDLE), since_ms_(0), backoff_until_ms_(0), yielded_(false) {}

    void configure(const Config& cfg) { cfg_ = cfg; }
    const Config& config() const { return cfg_; }

    State state() const { return state_; }
    uint64_t since() const { return since_ms_; }   // when the current state was entered
    bool routed() const { return state_ == ROUTED; }

    // As the stats line names the state.
    static const char* stateName(State s) {
        return s == ROUTED ? "granted" : (s == BUILDING ? "requested" : (s == RELEASING ? "releasing" : "idle"));
    }

    // A stream needs the source: true = start a build now (IDLE and past any backoff). The caller then queues
    // audioconn and calls buildFailed() if it could not.
    bool tryBuild(uint64_t now) {
        if (state_ != IDLE || now < backoff_until_ms_) return false;
        state_ = BUILDING;
        since_ms_ = now;
        return true;
    }
    void buildFailed() { state_ = IDLE; }

    // audioconn reported the grant. Returns the state it arrived in: BUILDING (now ROUTED), ROUTED (a refocus build
    // granted again) or IDLE / RELEASING (released meanwhile: ignored, the queued remove follows).
    State granted(uint64_t now) {
        const State st = state_;
        if (st == BUILDING) { state_ = ROUTED; since_ms_ = now; }
        return st;
    }

    // Release: true = a remove has to be queued (it was BUILDING or ROUTED; now RELEASING). False = idle, or a remove
    // is already on its way.
    bool startRelease(uint64_t now) {
        if (state_ != BUILDING && state_ != ROUTED) return false;
        state_ = RELEASING;
        since_ms_ = now;
        return true;
    }
    void released() { if (state_ == RELEASING) state_ = IDLE; }

    // After a grant timeout: no new build before now + backoff_ms. clearBackoff() at session end.
    void backoff(uint64_t now) { backoff_until_ms_ = now + cfg_.backoff_ms; }
    void clearBackoff() { backoff_until_ms_ = 0; }
    uint64_t backoffUntil() const { return backoff_until_ms_; }

    // Hold: does AA media hold the source, i.e. is there something a source change takes away? last_media_ms as for
    // decide(). A source that is only building for media counts: the driver changed source before the grant.
    bool holdsMedia(uint64_t last_media_ms) const {
        return last_media_ms != 0 && (state_ == BUILDING || state_ == ROUTED);
    }
    // The driver picked another source: from now on AA media alone does not build the source (needsSource()). The
    // caller releases it (startRelease) and tells the phone it lost audio focus.
    void yieldToCar() { yielded_ = true; }
    bool yielded() const { return yielded_; }
    // The phone asked for audio focus again or started a new media stream: AA media may take the source back. Also at
    // session end. Returns whether it had yielded.
    bool endYield() { const bool y = yielded_; yielded_ = false; return y; }
    // Does this block's activity need the source? media = the AA media stream is started or has audio queued; other =
    // the same for a guidance prompt or a system sound.
    bool needsSource(bool media, bool other) const { return other || (media && !yielded_); }

    // Once per mixer block. active = a stream is started or audio is queued. last_sound_ms = the last block with
    // sound; last_media_ms = the last block with AA media sound or an open media stream (0 = none this session).
    Action decide(uint64_t now, bool active, uint64_t last_sound_ms, uint64_t last_media_ms) const {
        if (state_ == BUILDING && now - since_ms_ > cfg_.grant_timeout_ms) return GRANT_TIMEOUT;
        if (state_ == RELEASING && now - since_ms_ > cfg_.release_timeout_ms) return RELEASE_TIMEOUT;
        if (state_ != ROUTED || active) return NONE;
        const uint64_t idle = cfg_.idle_s > 0 ? (uint64_t)cfg_.idle_s * 1000u : 0;
        const uint64_t media_hold = cfg_.hold ? 0 : idle;   // 0 = media holds it until the session ends or a yield
        const bool media_recent = last_media_ms && (media_hold == 0 || now - last_media_ms < media_hold);
        const uint64_t quiet = now - last_sound_ms;
        if (media_recent) return (media_hold > 0 && quiet > media_hold) ? RELEASE_IDLE : NONE;
        if (cfg_.prompt_idle_ms > 0) return quiet > (uint64_t)cfg_.prompt_idle_ms ? RELEASE_PROMPT : NONE;
        return (idle > 0 && quiet > idle) ? RELEASE_IDLE : NONE;
    }

private:
    Config cfg_;
    State state_;
    uint64_t since_ms_;
    uint64_t backoff_until_ms_;
    bool yielded_;
};

} // namespace nbt

#endif // AUDIO_ROUTE_POLICY_HPP
