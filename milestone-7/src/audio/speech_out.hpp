// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_SPEECH_OUT_HPP
#define AUDIO_SPEECH_OUT_HPP

// NBT_SPEECH=1 (OFF by default): play Android Auto guidance (channel 8, the format the phone declared) on an A2I
// playback endpoint. Without it, BT mode acks and drops every prompt (ch 8 must stay in the ServiceDiscovery).
//   NBT_SPEECH_EP       endpoint (default /input_stereo_AirPlayAlt)
//   NBT_SPEECH_SRC      AM source of the nav prompt focus `audioconn -b <src> <sink>` (default 0 = no focus: BT mode
//                       plays at once, browser mode keeps guidance in its mix). Audio waits in the ring for the grant.
//   NBT_SPEECH_SINK     AM sink (default 0x1, speakers)
//   NBT_SPEECH_HOLD_MS  the focus is released this long after the ring has played out (default 500)
// Earlier A2I *write* experiments crashed the head unit; this uses only the factory prime/Start/GetBuffer/Out_Write
// loop (car-verified for 20k+ writes), opens the endpoint once on its own thread and never closes it mid-run.

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

    // Start-up: with NBT_SPEECH=1 (cfg.enabled), opens the endpoint on a background thread.
    void init(const SpeechConfig& cfg);
    // Session thread: the endpoint is open, the focus is in use (NBT_SPEECH_SRC, audioconn found) and not backing off
    // (now: monotonic ms).
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

    static void* openThreadThunk(void* self);
    static void* tickThreadThunk(void* self);
    static void grantedThunk(void* self);
    static void removeDoneThunk(void* self, bool ran);
    void openThread();
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

    AudioRoute& route_;
    const char* endpoint_;        // NBT_SPEECH_EP (NULL / empty = the factory alternate nav endpoint)
    A2IPlaybackStream* speech_;   // set by the open thread, then never changes
    volatile bool ready_;
    uint32_t src_;
    uint32_t sink_;

    // Session thread only.
    PcmResampler rs_;
    std::vector<uint8_t> buf_;
    uint32_t frames_;
    uint32_t bytes_;
    int peak_;

    // Guarded by lock_ (session thread, tick thread, audioconn worker).
    mutable pthread_mutex_t lock_;
    SpeechFocus focus_;
    uint32_t prompts_;
    uint32_t prompt_rate_;
    uint32_t prompt_ch_;

    pthread_t tick_;
    bool tick_valid_;
    volatile bool tick_stop_;
};

} // namespace nbt

#endif // AUDIO_SPEECH_OUT_HPP
