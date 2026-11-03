// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: the AA audio channel table (audio/channels.hpp).
#include "audio/channels.hpp"
#include <stdio.h>
#include <string.h>

using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main() {
    // 1. The three sinks, both directions.
    CHECK(audioStreamForChannel(9) == AUDIO_MEDIA);
    CHECK(audioStreamForChannel(8) == AUDIO_GUIDANCE);
    CHECK(audioStreamForChannel(16) == AUDIO_SYSTEM);
    CHECK(audioChannelForStream(AUDIO_MEDIA) == 9);
    CHECK(audioChannelForStream(AUDIO_GUIDANCE) == 8);
    CHECK(audioChannelForStream(AUDIO_SYSTEM) == 16);
    for (int s = 0; s < AUDIO_NSTREAMS; ++s) CHECK(audioStreamForChannel(audioChannelForStream(s)) == s);

    // 2. Every other channel (control, video, input, sensors, mic, Bluetooth, nav status ...) is not an audio sink.
    for (unsigned ch = 0; ch < 256; ++ch) {
        const bool sink = ch == 8 || ch == 9 || ch == 16;
        CHECK(isAaAudioSinkChannel(ch) == sink);
        if (!sink) CHECK(audioStreamForChannel(ch) == -1);
    }
    CHECK(audioChannelForStream(-1) == 0);
    CHECK(audioChannelForStream(AUDIO_NSTREAMS) == 0);

    // 3. Names, as the log lines print them.
    CHECK(strcmp(audioStreamName(AUDIO_MEDIA), "media") == 0);
    CHECK(strcmp(audioStreamName(AUDIO_GUIDANCE), "guidance") == 0);
    CHECK(strcmp(audioStreamName(AUDIO_SYSTEM), "system") == 0);
    CHECK(strcmp(audioStreamName(-1), "?") == 0);
    CHECK(strcmp(audioStreamName(AUDIO_NSTREAMS), "?") == 0);

    // 4. Source channel counts: media stereo, guidance and system mono.
    CHECK(audioSourceChannels(AUDIO_MEDIA) == 2);
    CHECK(audioSourceChannels(AUDIO_GUIDANCE) == 1);
    CHECK(audioSourceChannels(AUDIO_SYSTEM) == 1);
    CHECK(audioSourceChannels(AUDIO_NSTREAMS) == 0);

    // 5. Log order: guidance, media, system, each stream once (the deferred-ack and underflow lines depend on it).
    CHECK(kAudioLogOrder[0] == AUDIO_GUIDANCE);
    CHECK(kAudioLogOrder[1] == AUDIO_MEDIA);
    CHECK(kAudioLogOrder[2] == AUDIO_SYSTEM);
    {
        int seen = 0;
        for (int i = 0; i < AUDIO_NSTREAMS; ++i) seen |= 1 << kAudioLogOrder[i];
        CHECK(seen == 7);
    }

    if (fails) { printf("test_audio_channels: %d failure(s)\n", fails); return 1; }
    printf("test_audio_channels OK\n");
    return 0;
}
