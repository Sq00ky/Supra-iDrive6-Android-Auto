// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_BACKEND_HPP
#define AUDIO_BACKEND_HPP

// One interface for the audio modes (NBT_AUDIO_MODE=bt|browser|a2i): BtAudio, BrowserAudio, A2iAudio. main.cpp
// picks one at start-up and passes it the session's audio events; nothing else in main depends on the mode.
// A virtual class with fn + void* callbacks (no std::function: QNX GCC 4.4).

#include "audio/mic_uplink.hpp"
#include <stddef.h>
#include <stdint.h>

namespace nbt {

class AudioBackend {
public:
    virtual ~AudioBackend() {}

    // Daemon start-up, after the mode's own init and before the first session.
    virtual void start() {}

    // Session thread, both transports (startSession()): AASession::start() succeeded.
    virtual void onSessionStarted() {}
    // Session thread: one AA audio frame. channel = AA channel (9 media, 8 guidance, 16 system audio); pts = the
    // frame's timestamp in us (0 = none).
    virtual void onFrame(uint8_t channel, const uint8_t* pcm, size_t len, uint64_t pts) = 0;
    // Session thread: AV START / STOP indication. rate / channels: the format the phone declared (0 = none given).
    virtual void onStreamState(uint8_t channel, bool started, uint32_t rate, uint32_t channels) = 0;
    // Session thread: the session ended.
    virtual void onSessionEnded() = 0;

    // Main loop, while a session is active: the phone opened / closed the mic (Assistant).
    virtual void onMicOpened() {}
    virtual void onMicClosed() {}

    // Car microphone. setMicCallback once at start-up; startMic / stopMic from the main loop. `standalone` is the
    // uplink main keeps for the modes without their own capture (all but A2I).
    virtual void setMicCallback(MicDataCallback, void*) {}
    virtual bool startMic(MicUplink& standalone) { return standalone.start(); }
    virtual void stopMic(MicUplink& standalone) { standalone.stop(); }

    // Shutdown starts: drop queued audioconn requests, so none starts while the daemon tears down.
    virtual void cancelPending() {}
    // Shutdown, after the session has stopped.
    virtual void stop() = 0;
};

} // namespace nbt

#endif // AUDIO_BACKEND_HPP
