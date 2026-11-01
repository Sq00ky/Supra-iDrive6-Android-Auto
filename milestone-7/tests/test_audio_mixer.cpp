// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: three-input mixer and media ducking (browser audio mode).
#include "audio_mixer.hpp"
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static bool near(float a, float b, float tol) { return a > b - tol && a < b + tol; }

int main() {
    // dB helper
    CHECK(near(AudioMixer::dbToGain(0), 1.0f, 1e-6f));
    CHECK(near(AudioMixer::dbToGain(-6), 0.501f, 0.002f));
    CHECK(near(AudioMixer::dbToGain(-12), 0.251f, 0.002f));
    CHECK(near(AudioMixer::dbToGain(6), 1.995f, 0.005f));
    CHECK(near(AudioMixer::dbToGain(-200), AudioMixer::dbToGain(-60), 1e-9f));   // clamped

    const uint32_t rate = 1000;   // 1 frame = 1 ms keeps the arithmetic readable
    DuckConfig cfg;
    cfg.duck_gain = 0.25f; cfg.attack_ms = 100; cfg.release_ms = 200; cfg.hold_ms = 50;
    AudioMixer m;
    m.configure(rate, 2, cfg);

    std::vector<int16_t> media(2 * 1000, 10000), guide(2 * 1000, 0), out(2 * 1000);

    // 1. no ducking input: media passes at full level, nothing else
    m.mix(&media[0], NULL, NULL, 100, false, &out[0]);
    CHECK(out[0] == 10000 && out[199] == 10000 && !m.ducking());

    // 2. guidance starts: the attack ramps media down to 25 % over 100 ms, guidance added on top
    for (size_t i = 0; i < guide.size(); ++i) guide[i] = 1000;
    m.mix(&media[0], &guide[0], NULL, 50, true, &out[0]);
    CHECK(m.ducking());
    CHECK(out[0] < 11000 && out[0] > 10900);           // first frame: barely ducked yet
    CHECK(near(m.duckLevel(), 0.625f, 0.01f));          // half way down after 50 ms
    m.mix(&media[0], &guide[0], NULL, 100, true, &out[0]);
    CHECK(near(m.duckLevel(), 0.25f, 1e-4f));
    CHECK(out[2 * 99] == 2500 + 1000);                 // 10000 * 0.25 + guidance

    // 3. guidance ends: held at 25 % for hold_ms (50), then released over 200 ms
    m.mix(&media[0], NULL, NULL, 50, false, &out[0]);
    CHECK(near(m.duckLevel(), 0.25f, 1e-4f) && out[2 * 49] == 2500);
    m.mix(&media[0], NULL, NULL, 100, false, &out[0]);
    CHECK(near(m.duckLevel(), 0.625f, 0.01f));
    m.mix(&media[0], NULL, NULL, 150, false, &out[0]);
    CHECK(near(m.duckLevel(), 1.0f, 1e-6f) && out[2 * 149] == 10000 && !m.ducking());

    // 4. a short gap inside a prompt (shorter than hold) does not release the duck
    m.mix(&media[0], &guide[0], NULL, 200, true, &out[0]);
    m.mix(&media[0], NULL, NULL, 30, false, &out[0]);
    m.mix(&media[0], &guide[0], NULL, 10, true, &out[0]);
    CHECK(near(m.duckLevel(), 0.25f, 1e-4f));

    // 5. ramps are monotonic (no clicks): during release the media level only rises
    m.mix(&media[0], NULL, NULL, 50, false, &out[0]);   // hold
    m.mix(&media[0], NULL, NULL, 200, false, &out[0]);  // release
    bool mono = true;
    for (size_t f = 1; f < 200; ++f) if (out[2 * f] < out[2 * (f - 1)]) mono = false;
    CHECK(mono && out[0] < out[2 * 199]);

    // 6. saturation instead of wrap-around
    std::vector<int16_t> loud(2 * 10, 30000);
    m.configure(rate, 2, cfg);
    m.mix(&loud[0], &loud[0], &loud[0], 10, false, &out[0]);
    CHECK(out[0] == 32767);
    std::vector<int16_t> neg(2 * 10, -30000);
    m.mix(&neg[0], &neg[0], NULL, 10, false, &out[0]);
    CHECK(out[0] == -32768);

    // 7. system sound ducks too, and fixed gains apply
    DuckConfig g2 = cfg; g2.system_gain = 0.5f; g2.attack_ms = 0;
    m.configure(rate, 2, g2);
    m.mix(&media[0], NULL, &guide[0], 1, true, &out[0]);
    CHECK(out[0] == 2500 + 500);

    // 8. all inputs silent -> silence
    m.configure(rate, 2, cfg);
    m.mix(NULL, NULL, NULL, 10, false, &out[0]);
    CHECK(out[0] == 0 && out[19] == 0);

    // 9. PtsRateMeter: car log 2026-11-01, ch 8 guidance, 4096-byte mono frames 42.7 ms apart = 48 kHz
    {
        PtsRateMeter pm;
        uint64_t pts = 915716682615ull;
        uint32_t r = 0;
        int n = 0;
        for (; n < 20 && r == 0; ++n) { r = pm.add(pts, 4096, 1); pts += 42667; }
        CHECK(r == 48000);
        CHECK(n >= 5 && n <= 7);                       // needs 200 ms of timestamps
        CHECK(pm.add(pts, 4096, 1) == 48000);          // stays known
        pm.reset();
        pts = 1000000;
        r = 0;
        for (n = 0; n < 20 && r == 0; ++n) { r = pm.add(pts, 4096, 1); pts += 128000; }   // 16 kHz mono
        CHECK(r == 16000);
        pm.reset();
        pts = 5000000;
        r = 0;
        for (n = 0; n < 20 && r == 0; ++n) { r = pm.add(pts, 8192, 2); pts += 42667; }    // 48 kHz stereo media
        CHECK(r == 48000);
        pm.reset();
        pm.add(9000000, 4096, 1);
        CHECK(pm.add(8000000, 4096, 1) == 0);          // clock went back: starts over, no result
        CHECK(PtsRateMeter::snap(47100) == 48000 && PtsRateMeter::snap(16300) == 16000);
        CHECK(PtsRateMeter::snap(30000) == 0 || PtsRateMeter::snap(30000) == 32000);
        CHECK(PtsRateMeter::snap(60000) == 0);         // nothing within 8 %
    }

    printf(fails ? "test_audio_mixer: %d FAILED\n" : "test_audio_mixer: all passed\n", fails);
    return fails ? 1 : 0;
}
