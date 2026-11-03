// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_CHANNELS_HPP
#define AUDIO_CHANNELS_HPP

// The Android Auto audio sinks: AA channel <-> audio stream index, names and source formats. The one table every
// audio consumer uses (browser mixer, deferred acks, underflow reports, frame counters).

#include <stdint.h>

namespace nbt {

// Android Auto audio sink channels. 8 and 9 come from the DHU template; 16 is added by addSystemAudioSink.
enum { AA_CH_GUIDANCE_AUDIO = 8, AA_CH_MEDIA_AUDIO = 9, AA_CH_SYSTEM_AUDIO = 16 };
inline bool isAaAudioSinkChannel(unsigned ch) {
    return ch == AA_CH_GUIDANCE_AUDIO || ch == AA_CH_MEDIA_AUDIO || ch == AA_CH_SYSTEM_AUDIO;
}

// Stream index of an audio sink. Arrays over the three streams use it.
enum AudioStream { AUDIO_MEDIA = 0, AUDIO_GUIDANCE = 1, AUDIO_SYSTEM = 2, AUDIO_NSTREAMS = 3 };

// Lines that list all three streams (deferred acks, underflow reports, SDR offers) name them guidance first;
// iterate in this order so their text stays the same.
static const int kAudioLogOrder[AUDIO_NSTREAMS] = { AUDIO_GUIDANCE, AUDIO_MEDIA, AUDIO_SYSTEM };

// AA channel -> stream index, -1 for any other channel.
inline int audioStreamForChannel(unsigned ch) {
    if (ch == AA_CH_MEDIA_AUDIO) return AUDIO_MEDIA;
    if (ch == AA_CH_GUIDANCE_AUDIO) return AUDIO_GUIDANCE;
    if (ch == AA_CH_SYSTEM_AUDIO) return AUDIO_SYSTEM;
    return -1;
}

// Stream index -> AA channel, 0 for an index out of range.
inline uint8_t audioChannelForStream(int s) {
    static const uint8_t kChannel[AUDIO_NSTREAMS] = { AA_CH_MEDIA_AUDIO, AA_CH_GUIDANCE_AUDIO, AA_CH_SYSTEM_AUDIO };
    return (s >= 0 && s < AUDIO_NSTREAMS) ? kChannel[s] : 0;
}

// "media" / "guidance" / "system", "?" out of range.
inline const char* audioStreamName(int s) {
    return s == AUDIO_MEDIA ? "media" : (s == AUDIO_GUIDANCE ? "guidance" : (s == AUDIO_SYSTEM ? "system" : "?"));
}

// What the phone sends on each sink: media 48 kHz stereo, guidance 48 kHz mono (16 kHz when NBT_GUIDANCE_RATE asks
// for it), system sounds 16 kHz mono.
inline uint32_t audioSourceChannels(int s) {
    return s == AUDIO_MEDIA ? 2u : ((s == AUDIO_GUIDANCE || s == AUDIO_SYSTEM) ? 1u : 0u);
}

} // namespace nbt

#endif // AUDIO_CHANNELS_HPP
