// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_SPEECH_FOCUS_HPP
#define AUDIO_SPEECH_FOCUS_HPP

// The nav prompt focus of SpeechOut: when to build, hold and release it, and when ch 8 audio may play. Clock-free and
// not thread-safe (the caller holds its lock); timestamps are monotonic ms. Side effects go through SpeechFocusPort.

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
};

class SpeechFocus {
public:
    struct Tick {
        NavPromptPolicy::Action action;
        uint64_t waited_ms;    // since the last build was queued
        uint64_t idle_ms;      // since the last audio
        const char* rebuild;   // log text of the build for a prompt that waited for the release; NULL = none
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

    // The focus is in use and not backing off.
    bool takesPrompt(uint64_t now) const;
    // ch 8 START; returns the log text, NULL = no focus in use.
    const char* promptStarted(uint64_t now);
    void promptStopped();
    // ch 8 audio may enter the ring: no focus in use, or one building / held / waited for.
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

    SpeechFocusPort& port_;
    NavPromptPolicy policy_;
    bool uses_focus_;
    bool prompt_started_;
    bool waiting_;         // a prompt started while RELEASING: its audio waits for the next build
    bool held_;            // the car may hold our focus: granted, and no remove has run since
    bool build_pending_;   // a build was queued and not granted: its one attempt may still run
    uint32_t removes_;     // focus removes queued and not done yet
    uint64_t last_busy_ms_;
    uint64_t build_ms_;
    uint64_t grant_after_ms_;
    bool stop_sent_;
};

} // namespace nbt

#endif // AUDIO_SPEECH_FOCUS_HPP
