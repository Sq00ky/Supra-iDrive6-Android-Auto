// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_SPEECH_FORMAT_HPP
#define AUDIO_SPEECH_FORMAT_HPP

#include <stdint.h>

namespace nbt {

struct SpeechInput {
    uint32_t rate;
    uint32_t channels;
};

// The format the phone declared for guidance audio; 0 = not declared, so 16 kHz mono.
inline SpeechInput speechInputFormat(uint32_t declared_rate, uint32_t declared_channels) {
    SpeechInput f;
    f.rate = declared_rate ? declared_rate : 16000;
    f.channels = declared_channels ? declared_channels : 1;
    return f;
}

} // namespace nbt

#endif // AUDIO_SPEECH_FORMAT_HPP
