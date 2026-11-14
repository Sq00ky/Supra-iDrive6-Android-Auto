// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: the speech input format follows the declared guidance format (audio/speech_format.hpp).
#include "audio/speech_format.hpp"
#include <stdio.h>

using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main() {
    SpeechInput f = speechInputFormat(48000, 1);
    CHECK(f.rate == 48000 && f.channels == 1);
    f = speechInputFormat(16000, 1);
    CHECK(f.rate == 16000 && f.channels == 1);
    f = speechInputFormat(0, 0);
    CHECK(f.rate == 16000 && f.channels == 1);
    f = speechInputFormat(48000, 2);
    CHECK(f.rate == 48000 && f.channels == 2);

    if (fails) { printf("test_speech_format: %d failure(s)\n", fails); return 1; }
    printf("test_speech_format OK\n");
    return 0;
}
