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

    // 10. Car log 2026-11-02, media ch 9 (8192-byte stereo frames at 48 kHz): the first gaps were uneven, so the first
    //     231 ms measure 44.4 kHz. Unrestricted that snaps to 44100 (the bug); restricted to the offered 48000 it can't.
    {
        const uint32_t gaps[] = { 41858, 43686, 44379, 59164, 41699 };
        PtsRateMeter free_m, media;
        const uint32_t offered[] = { 48000 };
        media.setCandidates(offered, 1);
        uint64_t pts = 998508798953ull;
        uint32_t rf = 0, rm = 0;
        for (int n = 0; n < 40 && (rf == 0 || rm == 0); ++n) {
            if (!rf) rf = free_m.add(pts, 8192, 2);
            if (!rm) rm = media.add(pts, 8192, 2);
            pts += n < 5 ? gaps[n] : 42667;
        }
        CHECK(rf == 44100);                            // what the car did
        CHECK(rm == 48000);                            // the fix
        media.reset();                                 // a new stream keeps the candidates
        pts = 1000;
        rm = 0;
        for (int n = 0; n < 40 && rm == 0; ++n) { rm = media.add(pts, 8192, 2); pts += 42667; }
        CHECK(rm == 48000);

        // Guidance is offered 16 and 48 kHz: the same uneven start still picks 48000, a 16 kHz stream picks 16000.
        PtsRateMeter g;
        const uint32_t gofs[] = { 16000, 48000 };
        g.setCandidates(gofs, 2);
        pts = 5000;
        uint32_t rg = 0;
        for (int n = 0; n < 40 && rg == 0; ++n) { rg = g.add(pts, 4096, 1); pts += n < 5 ? gaps[n] : 42667; }
        CHECK(rg == 48000);
        g.reset();
        pts = 5000;
        rg = 0;
        for (int n = 0; n < 40 && rg == 0; ++n) { rg = g.add(pts, 4096, 1); pts += 128000; }
        CHECK(rg == 16000);
        CHECK(PtsRateMeter::snapTo(30000, gofs, 2, 25) == 0);   // far from both: unknown, keep the default
    }

    // 11. StreamRate: the phone's declared format (START_INDICATION) wins from the first frame; timestamps only
    //     cross-check it; without a declaration the rate is measured among the offered rates.
    {
        const uint32_t gofs[] = { 16000, 48000 };
        StreamRate g;
        g.configure(48000, gofs, 2, 1);
        CHECK(g.rate() == 48000 && !g.declared());

        // a) declared 16 kHz: used at once, before any frame
        g.start(16000);
        CHECK(g.rate() == 16000 && g.declared());
        uint64_t pts = 1000;
        uint32_t mm = 0, warns = 0;
        bool changed = false;
        for (int n = 0; n < 20; ++n) { changed |= g.frame(pts, 4096, &mm); if (mm) ++warns; pts += 128000; }
        CHECK(!changed && warns == 0 && g.rate() == 16000);   // timestamps agree: silent

        // b) declared 16 kHz but the timestamps say 48 kHz: keep the declaration, warn exactly once
        g.start(16000);
        pts = 1000; warns = 0; changed = false;
        uint32_t seen = 0;
        for (int n = 0; n < 40; ++n) {
            changed |= g.frame(pts, 4096, &mm);
            if (mm) { ++warns; seen = mm; }
            pts += 42667;
        }
        CHECK(!changed && warns == 1 && seen == 48000 && g.rate() == 16000);

        // c) no declaration: measured, limited to the offers (car's uneven start still gives 48000)
        const uint32_t gaps[] = { 41858, 43686, 44379, 59164, 41699 };
        g.start(0);
        CHECK(!g.declared() && g.rate() == 16000);            // keeps the last rate until measured
        pts = 5000; changed = false;
        for (int n = 0; n < 40; ++n) { changed |= g.frame(pts, 4096, &mm); CHECK(mm == 0); pts += n < 5 ? gaps[n] : 42667; }
        CHECK(changed && g.rate() == 48000);

        // d) session end: back to the default, nothing declared; pts 0 frames are ignored
        g.reset();
        CHECK(g.rate() == 48000 && !g.declared());
        CHECK(!g.frame(0, 4096, &mm) && mm == 0);

        // e) media (one offer, stereo): declared 48000 from the start, timestamps agree
        const uint32_t mofs[] = { 48000 };
        StreamRate m;
        m.configure(48000, mofs, 1, 2);
        m.start(48000);
        pts = 998508798953ull; warns = 0;
        for (int n = 0; n < 40; ++n) { m.frame(pts, 8192, &mm); if (mm) ++warns; pts += n < 5 ? gaps[n] : 42667; }
        CHECK(m.rate() == 48000 && warns == 0);
    }

    // 12. StreamWatch: per-block gap counters of a started stream; a stopped stream's tail is not counted.
    {
        StreamWatch w;
        CHECK(!w.hasMin() && w.pulls() == 0 && w.totalGaps() == 0);
        const size_t need = 2048;                       // one block: 1024 frames stereo
        w.onPull(true, 30000, need, need);              // full block
        w.onPull(true, 25000, 900, need);               // short: part of a block
        w.onPull(true, 0, 0, need);                     // empty
        w.onPull(true, 12000, need, need);
        w.onPull(false, 0, 0, need);                    // stream stopped: its tail / silence is not a gap
        CHECK(w.pulls() == 4 && w.shortPulls() == 1 && w.emptyPulls() == 1);
        CHECK(w.hasMin() && w.minQueued() == 0 && w.totalGaps() == 2);
        w.resetInterval();                              // next stats line: interval cleared, total kept
        CHECK(w.pulls() == 0 && !w.hasMin() && w.shortPulls() == 0 && w.totalGaps() == 2);
        w.onPull(true, 40000, need, need);
        w.onPull(true, 35000, need, need);
        CHECK(w.minQueued() == 35000 && w.totalGaps() == 2);   // lowest fill of this interval only
        w.onPull(true, 500, 10, need);
        CHECK(w.totalGaps() == 3 && w.shortPulls() == 1 && w.minQueued() == 500);
        w.resetAll();                                   // session ended
        CHECK(w.totalGaps() == 0 && w.pulls() == 0);
    }

    printf(fails ? "test_audio_mixer: %d FAILED\n" : "test_audio_mixer: all passed\n", fails);
    return fails ? 1 : 0;
}
