// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_MANAGER_HPP
#define AUDIO_MANAGER_HPP

#include "a2i_client.h"
#include "a2i_stream.hpp"
#include "audio_route.hpp"
#include "pcm_utils.hpp"
#include <pthread.h>
#include <stddef.h>
#include <vector>

namespace nbt {

// C++03/GCC 4.4 compatible callback
typedef void (*MicDataCallback)(const uint8_t* pcm_data, size_t size, void* user_data);

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
class AudioManager {
public:
    enum Stage {
        STAGE_OPEN_ONLY = 0,
        STAGE_SILENCE   = 1,
        STAGE_PLAYBACK  = 2,
        STAGE_ROUTE     = 3
    };

    AudioManager();
    ~AudioManager();

    bool init();      // read env, open playback endpoints (does not start them)
    void start();     // start playback workers according to the stage
    void stop();

    int stage() const { return stage_; }

    // Android Auto -> car. aa_channel 9 = media (48 kHz stereo),
    // 8 = speech/guidance (16 kHz mono). Safe to call from the session thread.
    void onAaAudio(uint8_t aa_channel, const uint8_t* pcm, size_t len);
    // Phone started/stopped an AV audio stream (AV START/STOP indication).
    void onAaStreamState(uint8_t aa_channel, bool started);
    // Phone session ended: flush queues, drop routes, stop the mic.
    void onSessionEnded();

    // Car microphone (echo-cancelled uplink, 16 kHz mono) -> Android Auto.
    void setMicCallback(MicDataCallback callback, void* user_data = NULL);
    bool startMic();
    void stopMic();

private:
    AudioManager(const AudioManager&);
    AudioManager& operator=(const AudioManager&);

    static void micThunk(const uint8_t* pcm, size_t size, void* self);
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

#endif // AUDIO_MANAGER_HPP
