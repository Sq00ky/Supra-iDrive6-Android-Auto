// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_SPEECH_OUT_HPP
#define AUDIO_SPEECH_OUT_HPP

// NBT_SPEECH=1 (Bluetooth audio mode only, OFF by default, test parked first): play Android Auto guidance / speech
// (channel 8, 16 kHz mono) on an A2I playback endpoint. In BT mode ch 8 must stay in the ServiceDiscovery (the phone
// needs >= 1 audio sink), so without this switch every spoken nav prompt is received, acked and dropped.
//   NBT_SPEECH_EP     endpoint (default /input_stereo_AirPlayAlt, opened at start-up as in the stable Milestone 2)
//   NBT_SPEECH_SRC    AM source requested with `audioconn -b <src> <sink>` while a prompt plays (default 0 = none
//                     since v1.6.7: AM refuses 0xba anyway and each request touches AM while BT holds focus),
//                     released with `-r` when the prompt ends
//   NBT_SPEECH_SINK   AM sink (default 0x1, speakers)
// Earlier A2I *write* experiments crashed the head unit; this uses only the factory prime/Start/GetBuffer/Out_Write
// loop (car-verified for 20k+ writes), opens the endpoint once on its own thread and never closes it mid-run.

#include "audio/a2i_stream.hpp"
#include "audio/audio_route.hpp"
#include "audio/pcm_utils.hpp"
#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace nbt {

class SpeechOut {
public:
    // route: the Bluetooth mode's audioconn wrapper, shared for the optional NBT_SPEECH_SRC focus request.
    explicit SpeechOut(AudioRoute& route);

    // Start-up: with NBT_SPEECH=1, opens the endpoint on a background thread.
    void init();
    // Session thread: one ch 8 PCM frame (16 kHz mono s16le).
    void frame(const uint8_t* pcm, size_t len);
    // Session thread: ch 8 AV START / STOP indication.
    void state(bool started);
    // Session ended: give a held speech source back, drop queued audio.
    void onSessionEnded();
    // Shutdown.
    void stop();

private:
    SpeechOut(const SpeechOut&);
    SpeechOut& operator=(const SpeechOut&);

    static bool enabled();
    static void* openThreadThunk(void* self);
    void openThread();

    AudioRoute& route_;
    A2IPlaybackStream* speech_;   // set by the open thread, then never changes
    volatile bool ready_;
    PcmResampler rs_;             // used on the session thread only
    std::vector<uint8_t> buf_;
    uint32_t src_;
    uint32_t sink_;
    bool routed_;
    uint32_t frames_;
    uint32_t bytes_;
    int peak_;
    uint32_t prompts_;
};

} // namespace nbt

#endif // AUDIO_SPEECH_OUT_HPP
