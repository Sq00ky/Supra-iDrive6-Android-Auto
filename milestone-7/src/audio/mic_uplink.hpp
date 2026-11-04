// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_MIC_UPLINK_HPP
#define AUDIO_MIC_UPLINK_HPP

// Car microphone -> phone (Assistant / voice search), AA channel 14.
// The phone opens the mic with AVInputOpenRequest; the main loop then starts the factory echo-cancelled uplink
// (A2I /output_mono_AirPlayUL, 16 kHz mono, the CarPlay Siri path) and the session streams it back. In
// Bluetooth and browser audio modes the A2I playback pipeline stays closed, so the capture runs standalone
// (MicUplink); A2I mode captures through its own pipeline (A2iAudio). NBT_MIC=0 or /opt/aa/mic.disable keeps the
// mic closed (the open request is still answered).

#include "audio/a2i_stream.hpp"
#include <stddef.h>
#include <stdint.h>

namespace nbt {

// C++03/GCC 4.4 compatible callback
typedef void (*MicDataCallback)(const uint8_t* pcm_data, size_t size, void* user_data);

// NBT_MIC (default on) and no /opt/aa/mic.disable.
bool micEnabled();

// The standalone echo-cancelled uplink.
class MicUplink {
public:
    MicUplink(MicDataCallback cb, void* user) : mic_("mic"), cb_(cb), user_(user) {}
    // Opens the endpoint on first use, then starts the capture (already running: true).
    bool start();
    void stop() { mic_.stop(); }

private:
    MicUplink(const MicUplink&);
    MicUplink& operator=(const MicUplink&);

    A2ICaptureStream mic_;
    MicDataCallback cb_;
    void* user_;
};

// Level meter (once per ~1 s of audio) and optional raw dump, to tell real speech from a silent / unrouted uplink:
// NBT_MIC_DUMP=1 writes the first ~30 s of each run to /dev/shmem/nbt-aad-mic.raw (S16LE 16 kHz mono).
// One capture thread at a time.
void micLevelMeter(const uint8_t* pcm_data, size_t size);

} // namespace nbt

#endif // AUDIO_MIC_UPLINK_HPP
