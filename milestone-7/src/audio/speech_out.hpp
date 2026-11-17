// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_SPEECH_OUT_HPP
#define AUDIO_SPEECH_OUT_HPP

// NBT_SPEECH (on by default in browser mode, NBT_SPEECH=0 turns it off; off in BT mode, NBT_SPEECH=1 turns it on): play
// Android Auto guidance (channel 8, the format the phone declared) on an A2I playback endpoint. Without it, BT mode acks
// and drops every prompt (ch 8 must stay in the ServiceDiscovery) and browser mode keeps guidance in its mix.
//   NBT_SPEECH_EP       endpoint (browser mode default /input_mono_0, the car's nav prompter; BT mode
//                       /input_stereo_AirPlayAlt; /input_mono_* ones are opened mono)
//   NBT_SPEECH_SRC      AM source of the nav prompt focus `audioconn -b <src> <sink>` (browser mode default 0x1a,
//                       eSRC_NAV_AUTO; BT mode 0). 0 = no focus: BT mode plays at once, browser mode keeps guidance in
//                       its mix. Audio waits in the ring for the grant.
//   NBT_SPEECH_SINK     AM sink (default 0x1, speakers)
//   NBT_SPEECH_HOLD_MS  the focus is released this long after the ring has played out (default 500)
// Earlier A2I *write* experiments crashed the head unit; this uses only the factory prime/Start/GetBuffer/Out_Write
// loop (car-verified for 20k+ writes). The endpoint is opened at start-up only to learn its format, then like the
// car's own nav prompter for each prompt: opened at the prompt start, closed once the focus is given back (without a
// focus: NBT_SPEECH_HOLD_MS after the last audio). Held open, it kept the car's own nav prompts silent, and a car
// prompt that opened it anyway left ours dead. Open and close run on the endpoint thread; prompt audio waits in the
// ring meanwhile. An endpoint that stops taking buffers mid-prompt drops that prompt (SpeechFocus, stall_ms).

#include "audio/a2i_stream.hpp"
#include "audio/audio_route.hpp"
#include "audio/config.hpp"
#include "audio/resampler.hpp"
#include "audio/speech_focus.hpp"
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace nbt {

class SpeechOut : private SpeechFocusPort {
public:
    // route: the audioconn wrapper for the nav prompt focus (may be shared with the mode's own requests).
    explicit SpeechOut(AudioRoute& route);
    ~SpeechOut();

    // Start-up: with NBT_SPEECH=1 (cfg.enabled), learns the endpoint's format on a background thread.
    void init(const SpeechConfig& cfg);
    // Session thread: the endpoint format is known, the focus is in use (NBT_SPEECH_SRC, audioconn found), not backing
    // off, and the endpoint did not fail to open 3 times in a row this session (now: monotonic ms).
    bool takesPrompt(uint64_t now) const;
    // Session thread: one ch 8 PCM frame in the format given to state().
    void frame(const uint8_t* pcm, size_t len);
    // Session thread: ch 8 AV START / STOP indication with the declared format (0 = none given).
    void state(bool started, uint32_t rate, uint32_t channels);
    // Any thread: a prompt started or has audio queued while it may play: no focus in use, or one building, held or
    // waited for.
    bool active() const;
    // Session ended, after the route's cancelPending() (which would cancel the remove queued here): give the focus
    // back, drop queued audio.
    void onSessionEnded();
    // Shutdown: gives the focus back and waits up to 2 s for audioconn to have done it. true = a focus build may still
    // have been queued, so it cancelled every pending job of the route (the caller queues its own again).
    bool stop();

private:
    SpeechOut(const SpeechOut&);
    SpeechOut& operator=(const SpeechOut&);

    static void* endpointThreadThunk(void* self);
    static void* tickThreadThunk(void* self);
    static void grantedThunk(void* self);
    static void removeDoneThunk(void* self, bool ran);
    bool openFirst();        // learns the format; true = ready
    void endpointThread();
    void tickThread();
    void granted();
    void removeDone(bool ran);

    // SpeechFocusPort, called by focus_ under lock_.
    bool queueBuild();
    bool queueRemove();
    void cancelPending();
    void setFeed(bool on);
    void flush();
    size_t queuedBytes() const;
    void setFocusBusy(bool busy, uint64_t now);
    void openEndpoint();
    void closeEndpoint();
    uint32_t endpointWrites() const;

    AudioRoute& route_;
    const char* endpoint_;        // NBT_SPEECH_EP (NULL / empty = the factory alternate nav endpoint)
    A2IPlaybackStream* speech_;   // created by init(); opened and closed only by the endpoint thread
    volatile bool ready_;         // the endpoint format is known (ep_cfg_, ep_rate_, ep_ch_, rs_ set)
    uint32_t src_;
    uint32_t sink_;
    A2IStreamConfig ep_cfg_;      // what opened at start-up (endpoint, rate, cookie), used for every open
    uint32_t ep_rate_;
    uint32_t ep_ch_;

    // Session thread only.
    PcmResampler rs_;
    std::vector<uint8_t> buf_;
    uint32_t frames_;
    uint32_t bytes_;
    int peak_;

    // Guarded by lock_ (session thread, tick thread, audioconn worker, endpoint thread).
    mutable pthread_mutex_t lock_;
    SpeechFocus focus_;
    uint32_t prompts_;
    uint32_t prompt_rate_;
    uint32_t prompt_ch_;
    pthread_cond_t ep_cond_;   // the endpoint thread waits on it for ep_want_ / ep_stop_
    bool ep_want_;             // the focus wants the endpoint open
    bool ep_open_;             // open with its worker running
    bool ep_failed_;           // the open for the current want failed: no retry before the next openEndpoint
    uint32_t ep_fails_;        // failed opens in a row (reset by a good one and at the session end)
    bool ep_stop_;

    pthread_t ep_thread_;
    bool ep_valid_;
    pthread_t tick_;
    bool tick_valid_;
    volatile bool tick_stop_;
};

} // namespace nbt

#endif // AUDIO_SPEECH_OUT_HPP
