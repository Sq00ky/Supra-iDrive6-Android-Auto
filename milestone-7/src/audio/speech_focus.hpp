// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_SPEECH_FOCUS_HPP
#define AUDIO_SPEECH_FOCUS_HPP

// The nav prompt focus of SpeechOut: when to build, hold and release it, when ch 8 audio may play, and when the A2I
// endpoint is open. Clock-free and not thread-safe (the caller holds its lock); timestamps are monotonic ms. Side
// effects go through SpeechFocusPort.
//
// The endpoint is open only around a prompt: opened at its start (together with the focus build), closed once the
// focus is released again (without a focus: hold_ms after the last audio). The car's own nav prompter (natp) uses the
// same endpoint and cannot speak while we hold it; when it opens it anyway, ours stops taking buffers. A prompt whose
// endpoint has taken no buffer for stall_ms while it may play (stall_ms + open_ms while a fresh open has not taken its
// first one) is dropped: ring flushed, focus released, endpoint closed, and the rest of the prompt is not queued.

#include "audio/nav_prompt_policy.hpp"
#include <stddef.h>
#include <stdint.h>

namespace nbt {

class SpeechFocusPort {
public:
    virtual ~SpeechFocusPort() {}
    // false = not queued. Completion: SpeechFocus::granted() (not called when the build fails).
    virtual bool queueBuild() = 0;
    // false = not queued. Completion: SpeechFocus::removeDone(), whatever the outcome.
    virtual bool queueRemove() = 0;
    // Every pending job of the route stops before its next audioconn run.
    virtual void cancelPending() = 0;
    // The endpoint plays the ring (on) or silence (off).
    virtual void setFeed(bool on) = 0;
    virtual void flush() = 0;
    virtual size_t queuedBytes() const = 0;
    // A focus build or remove is in flight (the volume pill's quiet window starts when that ends).
    virtual void setFocusBusy(bool busy, uint64_t now) = 0;
    // Asynchronous and idempotent: the endpoint plays the ring once open, or silence while the feed is off. Queued
    // audio stays in the ring while it opens. openEndpoint also retries an open that failed.
    virtual void openEndpoint() = 0;
    virtual void closeEndpoint() = 0;
    // Buffers the endpoint has taken; it only has to change while the endpoint plays.
    virtual uint32_t endpointWrites() const = 0;
};

class SpeechFocus {
public:
    struct Tick {
        NavPromptPolicy::Action action;
        uint64_t waited_ms;    // since the last build was queued
        uint64_t idle_ms;      // since the last audio
        const char* rebuild;   // log text of the build for a prompt that waited for the release; NULL = none
        bool stalled;          // the endpoint stopped taking buffers: prompt dropped
        uint64_t stall_ms;     // stalled: since the endpoint last took a buffer
        size_t dropped;        // stalled: bytes flushed from the ring
        bool closed;           // the endpoint was closed after the prompt
        bool gave_up;          // GRANT_TIMEOUT: the max_refusals-th in a row, takesPrompt() is false until the session end
    };
    struct Stop {
        bool sent;   // a release was started or a remove queued during the shutdown
        bool held;   // the focus may still be held: granted with no remove run since, or a remove still out
    };

    explicit SpeechFocus(SpeechFocusPort& port);
    // uses_focus = a source is set and audioconn was found; false = ch 8 plays at once.
    void configure(const NavPromptPolicy::Config& cfg, bool uses_focus);

    NavPromptPolicy::State state() const { return policy_.state(); }
    uint64_t grantAfterMs() const { return grant_after_ms_; }
    bool endpointWanted() const { return endpoint_; }

    // The focus is in use, not backing off and not given up (max_refusals grant timeouts in a row this session).
    bool takesPrompt(uint64_t now) const;
    // ch 8 START; returns the log text, NULL = no focus in use.
    const char* promptStarted(uint64_t now);
    void promptStopped();
    // ch 8 audio may enter the ring: no focus in use, or one building / held / waited for; not after a stall.
    bool playsFrames() const;
    // A prompt started or has audio queued while it may play.
    bool active() const;
    // The 100 ms timer.
    Tick tick(uint64_t now);
    // The build's completion; returns the policy state it arrived in.
    NavPromptPolicy::State granted(uint64_t now);
    // The remove's completion; ran = audioconn ran.
    void removeDone(uint64_t now, bool ran);
    // true = a release was started or a remove queued.
    bool sessionEnded(uint64_t now);

    // Shutdown, once tick() no longer runs: beginStop, stopStep until false (or the caller's limit), endStop.
    // true = a build may still run, so every pending job of the route was cancelled.
    bool beginStop(uint64_t now);
    // Queues the last remove if the focus may be held or a build may run; true = a remove is out, keep waiting.
    bool stopStep();
    Stop endStop(uint64_t now);

private:
    SpeechFocus(const SpeechFocus&);
    SpeechFocus& operator=(const SpeechFocus&);

    // After a successful tryBuild: queues the focus request; returns the log text.
    const char* queueBuild(uint64_t now);
    bool queueRemove();
    // Starts the focus release if one is built or building; true = started.
    bool release(uint64_t now);
    void syncBusy(uint64_t now);
    void openEndpoint(uint64_t now);
    bool closeEndpoint();
    // The endpoint's progress clock restarts (it has stall_ms from now to take a buffer, plus open_ms if not live).
    void resetProgress(uint64_t now);

    SpeechFocusPort& port_;
    NavPromptPolicy policy_;
    bool uses_focus_;
    bool prompt_started_;
    bool waiting_;         // a prompt started while RELEASING: its audio waits for the next build
    bool held_;            // the car may hold our focus: granted, and no remove has run since
    bool build_pending_;   // a build was queued and not granted: its one attempt may still run
    uint32_t removes_;     // focus removes queued and not done yet
    uint32_t refusals_;    // grant timeouts since the last grant or session end
    uint64_t last_busy_ms_;
    uint64_t build_ms_;
    uint64_t grant_after_ms_;
    bool stop_sent_;
    bool endpoint_;          // openEndpoint was asked for and closeEndpoint not since
    bool live_;              // the endpoint made progress since it was asked open (its open is done)
    bool stalled_;           // the endpoint stalled during this prompt: its audio is dropped until the next start
    uint32_t writes_seen_;   // endpointWrites() at the last progress
    uint64_t progress_ms_;   // when the endpoint last took a buffer (or the clock was restarted)
};

} // namespace nbt

#endif // AUDIO_SPEECH_FOCUS_HPP
