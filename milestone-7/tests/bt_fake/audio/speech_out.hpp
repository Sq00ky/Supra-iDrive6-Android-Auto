#pragma once
#include "audio/audio_route.hpp"
#include "audio/config.hpp"
#include <stddef.h>
#include <stdint.h>
namespace nbt {
extern int btSpeechFrames, btSpeechEnds;
class SpeechOut {
public:
    explicit SpeechOut(AudioRoute&) {}
    void init(const SpeechConfig&) {}
    void frame(const uint8_t*, size_t) { ++btSpeechFrames; }
    void state(bool, uint32_t, uint32_t) {}
    void onSessionEnded() { ++btSpeechEnds; }
    bool stop() { return true; }
};
}
