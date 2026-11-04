// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for the volume curve (src/volume.hpp) and the mixer's live gain ramp (audio/mixer.cpp).
#include "volume.hpp"
#include "audio/mixer.hpp"

#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL %s (line %d)\n", msg, __LINE__); ++failures; } } while (0)

static void testCurve() {
    // Endpoints: 100 % is unity, 0 % is silence on the -40 dB floor, and the mapping is monotonic in between.
    CHECK(nbt::volumePctToDb(100) == 0, "100% is 0 dB");
    CHECK(nbt::volumePctToDb(0) == -40, "0% is -40 dB");
    int prev = -1000;
    for (int p = 0; p <= 100; ++p) {
        const int db = nbt::volumePctToDb(p);
        CHECK(db >= prev, "dB must not decrease as pct rises (attenuation gets smaller)");
        prev = db;
        CHECK(db >= -40 && db <= 0, "dB stays in range");
    }
    // Half travel is -20 dB on a dB-linear curve, not -6 dB.
    CHECK(nbt::volumePctToDb(50) == -20, "50% is -20 dB");

    // Clamping: out-of-range input cannot produce a louder-than-unity gain or a NaN.
    CHECK(nbt::volumePctToDb(-10) == -40, "negative clamps to silence");
    CHECK(nbt::volumePctToDb(250) == 0, "over 100 clamps to unity");
    CHECK(fabs(nbt::volumePctToGain(100) - 1.0f) < 0.001f, "100% gain is 1.0");
    CHECK(nbt::volumePctToGain(0) < 0.02f, "0% gain is near silence");
    CHECK(nbt::volumePctToGain(50) < 0.11f && nbt::volumePctToGain(50) > 0.09f, "50% is about -20 dB");

    char buf[16];
    nbt::volumeLabel(65, buf, sizeof(buf));
    CHECK(strcmp(buf, "65%") == 0, "label");
    nbt::volumeLabel(140, buf, sizeof(buf));
    CHECK(strcmp(buf, "100%") == 0, "label clamps");
}

static void testMixerRamp() {
    nbt::AudioMixer mx;
    nbt::DuckConfig cfg;
    cfg.media_gain = 1.0f;
    cfg.duck_gain = 1.0f;      // no ducking in this test
    mx.configure(48000, 2, cfg);

    const size_t frames = 128;
    int16_t media[frames * 2];
    int16_t out[frames * 2];
    for (size_t i = 0; i < frames * 2; ++i) media[i] = 10000;

    // Full gain: the input passes through unchanged.
    mx.mix(media, NULL, NULL, frames, false, out);
    CHECK(out[0] == 10000, "unity gain passes the sample");

    // Ask for silence: the ramp must bring it down over ~50 ms and never overshoot or click.
    mx.setMediaGain(0.0f);
    int16_t last = out[0];
    bool sawIntermediate = false;
    for (int block = 0; block < 30; ++block) {
        mx.mix(media, NULL, NULL, frames, false, out);
        for (size_t i = 0; i < frames * 2; ++i) {
            CHECK(out[i] <= last + 1, "gain only decreases while ramping down");
            if (out[i] > 0 && out[i] < 10000) sawIntermediate = true;
            if (out[i] < 0) { CHECK(false, "no sign flip"); break; }
        }
        last = out[frames * 2 - 1];
    }
    CHECK(sawIntermediate, "gain ramps through intermediate values (no click)");
    CHECK(out[frames * 2 - 1] == 0, "silence reached");

    // Back up to full.
    mx.setMediaGain(1.0f);
    for (int block = 0; block < 30; ++block) mx.mix(media, NULL, NULL, frames, false, out);
    CHECK(out[frames * 2 - 1] == 10000, "unity restored");
}

int main() {
    testCurve();
    testMixerRamp();
    if (failures) { printf("test_volume: %d failure(s)\n", failures); return 1; }
    printf("test_volume: all passed\n");
    return 0;
}
