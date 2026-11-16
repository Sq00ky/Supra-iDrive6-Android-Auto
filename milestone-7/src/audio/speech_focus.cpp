// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio/speech_focus.hpp"

namespace nbt {

SpeechFocus::SpeechFocus(SpeechFocusPort& port)
    : port_(port), uses_focus_(false), prompt_started_(false), waiting_(false), held_(false), build_pending_(false),
      removes_(0), refusals_(0), last_busy_ms_(0), build_ms_(0), grant_after_ms_(0), stop_sent_(false), endpoint_(false),
      live_(false), stalled_(false), writes_seen_(0), progress_ms_(0) {}

void SpeechFocus::configure(const NavPromptPolicy::Config& cfg, bool uses_focus) {
    policy_.configure(cfg);
    uses_focus_ = uses_focus;
}

bool SpeechFocus::takesPrompt(uint64_t now) const {
    const uint32_t max = policy_.config().max_refusals;
    return uses_focus_ && policy_.available(now) && (max == 0 || refusals_ < max);
}

const char* SpeechFocus::promptStarted(uint64_t now) {
    prompt_started_ = true;
    stalled_ = false;
    last_busy_ms_ = now;
    if (!uses_focus_) {
        port_.setFeed(true);
        openEndpoint(now);
        return NULL;
    }
    const NavPromptPolicy::State st = policy_.state();
    const char* how;
    if (policy_.tryBuild(now)) {
        how = queueBuild(now);
    } else if (st == NavPromptPolicy::ROUTED) {
        how = "focus still held";
    } else if (st == NavPromptPolicy::BUILDING) {
        how = "focus request still pending";
    } else if (st == NavPromptPolicy::RELEASING && policy_.available(now)) {
        waiting_ = true;   // the tick builds again once the release is done
        how = "waiting for the focus release";
    } else {
        how = "focus backing off: dropped";
    }
    if (playsFrames()) openEndpoint(now);   // in parallel with the focus build: both run on their own threads
    syncBusy(now);
    return how;
}

void SpeechFocus::promptStopped() {
    prompt_started_ = false;
}

const char* SpeechFocus::queueBuild(uint64_t now) {
    build_ms_ = now;
    waiting_ = false;
    port_.setFeed(false);
    if (port_.queueBuild()) {
        build_pending_ = true;
        return "requesting the nav prompt focus";
    }
    policy_.buildFailed();
    port_.flush();
    return "could not queue the focus request: dropped";
}

bool SpeechFocus::playsFrames() const {
    if (stalled_) return false;
    const NavPromptPolicy::State st = policy_.state();
    return !uses_focus_ || st == NavPromptPolicy::BUILDING || st == NavPromptPolicy::ROUTED || waiting_;
}

bool SpeechFocus::active() const {
    return playsFrames() && (prompt_started_ || port_.queuedBytes() > 0);
}

NavPromptPolicy::State SpeechFocus::granted(uint64_t now) {
    const NavPromptPolicy::State st = policy_.granted(now);
    held_ = true;
    build_pending_ = false;
    if (st == NavPromptPolicy::BUILDING) {
        refusals_ = 0;
        grant_after_ms_ = now - build_ms_;
        last_busy_ms_ = now;
        resetProgress(now);   // the feed starts now
        port_.setFeed(true);
    } else if (st != NavPromptPolicy::ROUTED) {
        queueRemove();   // granted after we gave up: take it down again
    }
    syncBusy(now);
    return st;
}

bool SpeechFocus::queueRemove() {
    if (!port_.queueRemove()) return false;
    ++removes_;
    return true;
}

bool SpeechFocus::release(uint64_t now) {
    if (!policy_.startRelease(now)) return false;
    port_.setFeed(false);
    // Stays RELEASING until removeDone(); RELEASE_TIMEOUT covers a completion that never comes.
    if (!queueRemove()) policy_.released();
    return true;
}

void SpeechFocus::syncBusy(uint64_t now) {
    const NavPromptPolicy::State st = policy_.state();
    port_.setFocusBusy(st == NavPromptPolicy::BUILDING || st == NavPromptPolicy::RELEASING || removes_ > 0, now);
}

void SpeechFocus::openEndpoint(uint64_t now) {
    if (!endpoint_) live_ = false;   // already asked open: it stays live
    endpoint_ = true;
    port_.openEndpoint();
    resetProgress(now);
}

bool SpeechFocus::closeEndpoint() {
    if (!endpoint_) return false;
    endpoint_ = false;
    port_.closeEndpoint();
    return true;
}

void SpeechFocus::resetProgress(uint64_t now) {
    writes_seen_ = port_.endpointWrites();
    progress_ms_ = now;
}

void SpeechFocus::removeDone(uint64_t now, bool ran) {
    // Not run (cancelled, no slot): held_ stays, so the session end or the shutdown sends another remove.
    if (ran) held_ = false;
    // Counted, so a remove that outlived its RELEASE_TIMEOUT cannot end a newer release.
    if (removes_ && --removes_ == 0) policy_.released();
    syncBusy(now);
}

SpeechFocus::Tick SpeechFocus::tick(uint64_t now) {
    const bool busy = prompt_started_ || port_.queuedBytes() > 0;
    if (busy) last_busy_ms_ = now;
    const uint32_t writes = port_.endpointWrites();
    if (writes != writes_seen_) {   // also the counter reset by a successful open
        writes_seen_ = writes;
        progress_ms_ = now;
        if (endpoint_) live_ = true;
    }
    Tick t;
    t.action = policy_.decide(now, busy, last_busy_ms_);
    t.waited_ms = now - build_ms_;
    t.idle_ms = now - last_busy_ms_;
    t.rebuild = NULL;
    t.stalled = false;
    t.stall_ms = 0;
    t.dropped = 0;
    t.closed = false;
    t.gave_up = false;
    if (t.action == NavPromptPolicy::GRANT_TIMEOUT) {
        port_.flush();
        release(now);
        policy_.backoff(now);
        t.gave_up = ++refusals_ == policy_.config().max_refusals;
    } else if (t.action == NavPromptPolicy::RELEASE) {
        release(now);
    } else if (t.action == NavPromptPolicy::RELEASE_TIMEOUT) {
        policy_.released();
    }
    // The endpoint should be taking buffers: with a focus once it is granted, without one from the prompt start.
    const bool feeding = !uses_focus_ || policy_.state() == NavPromptPolicy::ROUTED;
    const NavPromptPolicy::Config& cfg = policy_.config();
    const uint64_t limit = (uint64_t)cfg.stall_ms + (live_ ? 0u : cfg.open_ms);   // an open may block up to open_ms
    if (endpoint_ && busy && feeding && now - progress_ms_ > limit) {
        t.stalled = true;
        t.stall_ms = now - progress_ms_;
        t.dropped = port_.queuedBytes();
        stalled_ = true;
        port_.flush();
        release(now);
        closeEndpoint();
    }
    if (waiting_ && policy_.state() == NavPromptPolicy::IDLE) {
        waiting_ = false;
        if (busy && policy_.tryBuild(now)) t.rebuild = queueBuild(now);
        else port_.flush();
    }
    // Prompt over and the focus given back (or none in use): free the endpoint for the car's own prompts.
    if (!busy && !waiting_ && policy_.state() == NavPromptPolicy::IDLE && now - last_busy_ms_ > cfg.hold_ms)
        t.closed = closeEndpoint();
    syncBusy(now);
    return t;
}

bool SpeechFocus::sessionEnded(uint64_t now) {
    prompt_started_ = false;
    waiting_ = false;
    stalled_ = false;
    bool releasing = release(now);
    if (!releasing && held_) releasing = queueRemove();   // an earlier remove may have been cancelled
    port_.flush();
    closeEndpoint();
    policy_.clearBackoff();
    refusals_ = 0;
    syncBusy(now);
    return releasing;
}

bool SpeechFocus::beginStop(uint64_t now) {
    // Nothing queues a build any more: cancel one still pending, so no build runs after the shutdown removes.
    const bool cancelled = build_pending_;
    if (cancelled) port_.cancelPending();
    stop_sent_ = release(now);
    return cancelled;
}

bool SpeechFocus::stopStep() {
    if (!removes_) {
        if (stop_sent_ || !(held_ || build_pending_) || !queueRemove()) return false;
        stop_sent_ = true;
    }
    return true;
}

SpeechFocus::Stop SpeechFocus::endStop(uint64_t now) {
    Stop s;
    s.sent = stop_sent_;
    s.held = held_ || removes_;
    syncBusy(now);
    return s;
}

} // namespace nbt
