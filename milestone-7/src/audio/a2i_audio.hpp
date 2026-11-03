// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_A2I_AUDIO_HPP
#define AUDIO_A2I_AUDIO_HPP

#include "a2i_client.h"
#include "audio/a2i_stream.hpp"
#include "audio/audio_route.hpp"
#include "audio/backend.hpp"
#include "audio/resampler.hpp"
#include <pthread.h>
#include <stddef.h>
#include <vector>

namespace nbt {

// A2I audio mode (NBT_AUDIO_MODE=a2i, or NBT_AUDIO_BT=0; not audible on the car). Was AudioManager; its log lines keep
// the [AudioManager] prefix. It captures the mic through its own pipeline, so the standalone MicUplink is not used.
//
// Audio pipeline between Android Auto and the NBT Evo A2I endpoints.
//
// Bring-up is staged so each risk can be validated on the car on its own.
// Select with the environment variable NBT_AUDIO_STAGE (default 0 in Fork D until stage 1 is car-verified):
//   0  open endpoints only (the old "M2 safe" behaviour)
//   1  + factory-style prime/Start/notify loop writing SILENCE
//   2  + Android Auto PCM playback and microphone capture
//   3  + stock `audioconn` route build/remove around AA audio streams
// Other environment knobs: NBT_MEDIA_RATE (default 44100, the rate Fork C opens on the car), NBT_ROUTE_MODE
// (0 = AM focus `-b <src> <snk>`, default; 1 = CM `-b <connId>`),
// NBT_ROUTE_MEDIA (default 0xb9 src / 0x31d connId), NBT_ROUTE_NAV (default
// 0xba src / 0x31e connId), NBT_ROUTE_SINK (default 0x1), NBT_AUDIOCONN,
// NBT_AUDIOCONN_SUFFIX.
class A2iAudio : public AudioBackend {
public:
    enum Stage {
        STAGE_OPEN_ONLY = 0,
        STAGE_SILENCE   = 1,
        STAGE_PLAYBACK  = 2,
        STAGE_ROUTE     = 3
    };

    A2iAudio();
    ~A2iAudio();

    bool init();      // read env, open playback endpoints (does not start them)
    virtual void start();     // start playback workers according to the stage
    virtual void stop();

    int stage() const { return stage_; }

    // Android Auto -> car. aa_channel 9 = media (48 kHz stereo),
    // 8 = speech/guidance (16 kHz mono). Safe to call from the session thread. pts is not used.
    virtual void onFrame(uint8_t aa_channel, const uint8_t* pcm, size_t len, uint64_t pts);
    // Phone started/stopped an AV audio stream (AV START/STOP indication). rate / channels are not used.
    virtual void onStreamState(uint8_t aa_channel, bool started, uint32_t rate, uint32_t channels);
    // Phone session ended: flush queues, drop routes, stop the mic.
    virtual void onSessionEnded();

    // Car microphone (echo-cancelled uplink, 16 kHz mono) -> Android Auto, through this pipeline's own capture.
    virtual void setMicCallback(MicDataCallback callback, void* user_data);
    virtual bool startMic(MicUplink&) { return startOwnMic(); }
    virtual void stopMic(MicUplink&) { stopOwnMic(); }

private:
    A2iAudio(const A2iAudio&);
    A2iAudio& operator=(const A2iAudio&);

    static void micThunk(const uint8_t* pcm, size_t size, void* self);
    bool startOwnMic();
    void stopOwnMic();
    bool openPlayback(A2IPlaybackStream& stream, const char* endpoint, const char* fallback);

    int stage_;
    uint32_t media_rate_;
    uint32_t route_media_;
    uint32_t route_nav_;
    uint32_t route_sink_;
    int route_mode_;

    A2IPlaybackStream media_;
    A2IPlaybackStream nav_;
    A2ICaptureStream  mic_;
    PcmResampler media_rs_;
    PcmResampler speech_rs_;
    AudioRoute   route_;
    std::vector<uint8_t> scratch_;

    bool media_routed_;
    bool nav_routed_;
    bool started_;

    MicDataCallback mic_callback_;
    void* mic_user_data_;
};

} // namespace nbt

#endif // AUDIO_A2I_AUDIO_HPP
