// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: three-input mixer and media ducking (browser audio mode).
#include "audio/audio_mixer.hpp"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static bool near(float a, float b, float tol) { return a > b - tol && a < b + tol; }

// Mixer / device / phone at 1 ms resolution (output frames at 44.1 kHz): the device holds 5 blocks of 1024 frames and
// plays continuously; the mixer writes a block whenever there is room. Its write "blocked" when it had to wait for
// that room, i.e. the device was full: only then may UnderrunGuard wait. The phone delivers 42.667 ms frames
// (1881.6 output frames) at speed (1 = real time; 0.99 = 1 % slow), each late by 0..jitter ms, starting with lead_ms
// of audio queued, and stops sending at stop_ms (still "started": a pause without STOP).
struct SimResult { int gaps, underruns, max_wait_ms, waited_after_stop; double min_dev_ms; };
static SimResult simulateMixer(uint32_t budget_ms, int jitter_ms, int lead_ms, int seconds, int stop_ms,
                               double speed = 1.0) {
    const double fpm = 44.1, block = 1024.0, cap = 5 * 1024.0, frame_out = 2048.0 * 44100.0 / 48000.0;
    double ring = lead_ms * fpm, dev = cap, nominal = 0;
    uint32_t seed = 12345u;
    double next_arrival = 0;
    UnderrunGuard g(budget_ms);
    SimResult r = { 0, 0, 0, 0, 1e9 };
    uint32_t waited = 0;
    bool blocked = true;   // the device starts full: the first write waits for room
    for (int t = 0; t < seconds * 1000; ++t) {
        while (t >= next_arrival && t < stop_ms) {
            ring += frame_out;
            nominal += 2048.0 / 48.0 / speed;
            seed = seed * 1103515245u + 12345u;
            next_arrival = nominal + (jitter_ms ? (double)((seed >> 16) % (uint32_t)(jitter_ms + 1)) : 0.0);
        }
        bool wrote_this_ms = false;
        while (dev <= cap - block) {
            const size_t avail = (size_t)ring;
            if (g.shouldWait(true, avail, (size_t)block, waited, blocked)) {
                ++waited;
                if ((int)waited > r.max_wait_ms) r.max_wait_ms = (int)waited;
                if (t >= stop_ms) ++r.waited_after_stop;
                break;   // wait this ms
            }
            const double got = ring < block ? (double)(size_t)ring : block;
            ring -= got;
            g.onPulled(true, (size_t)got, (size_t)block);
            if (got < block) ++r.gaps;
            dev += block;
            waited = 0;
            // the next write blocks if it has to wait for room (the device is full again), not if room is left now
            blocked = !(dev <= cap - block);
            wrote_this_ms = true;
        }
        (void)wrote_this_ms;
        dev -= fpm;
        if (dev / fpm < r.min_dev_ms) r.min_dev_ms = dev / fpm;
        if (dev < 0) { ++r.underruns; dev = 0; }
    }
    return r;
}

// Media drift, closed loop at 1 ms resolution in ms of audio: the phone delivers 42.667 ms frames at its own speed
// (phone_ppm > 0 = fast), each late by 0..jitter ms, starting with lead_ms queued; the mixer takes a 23.2 ms block
// per block period, scaled by the trim DriftControl returns (or no trim when correct = false).
// pi = true: the trim comes from DriftPi (fed DriftControl's smoothed queue minus its target) instead of DriftControl.
// late_err = the mean |smoothed queue - target| over the last quarter of the run (the steady offset).
struct DriftSim { double final_ema, target, final_ppm, max_fill, late_err, integral; int gaps_after_learn, max_abs_ppm;
                  bool quiet_learning; };
static DriftSim simulateDrift(double phone_ppm, int jitter_ms, double lead_ms, int seconds, bool correct,
                              bool pi = false) {
    DriftControl dc;
    DriftPi dp;
    double err_sum = 0;
    long err_n = 0;
    const double frame_ms = 2048.0 / 48.0, block_ms = 1024.0 / 44.1;
    double fill = lead_ms, nominal = 0, next_arrival = 0, next_block = block_ms;
    uint32_t seed = 777u;
    int32_t ppm = 0;
    DriftSim r = { 0, 0, 0, 0, 0, 0, 0, 0, true };
    for (int t = 0; t < seconds * 1000; ++t) {
        while (t >= next_arrival) {
            fill += frame_ms;
            nominal += frame_ms / (1.0 + phone_ppm * 1e-6);
            seed = seed * 1103515245u + 12345u;
            next_arrival = nominal + (jitter_ms ? (double)((seed >> 16) % (uint32_t)(jitter_ms + 1)) : 0.0);
        }
        while (t >= next_block) {
            const double need = block_ms * (1.0 + ppm * 1e-6);
            if (fill < need) {
                if (!dc.learning() || !correct) { if (t > 10000) ++r.gaps_after_learn; }
                fill = 0;
            } else {
                fill -= need;
            }
            if (correct) {
                ppm = dc.update(fill, (uint64_t)t);
                if (pi) ppm = dc.learning() ? 0 : dp.update(dc.smoothed() - dc.target(), (uint64_t)t);
                if (dc.learning() && ppm != 0) r.quiet_learning = false;
            }
            if (t > seconds * 750 && !dc.learning()) { err_sum += fabs(dc.smoothed() - dc.target()); ++err_n; }
            if (abs(ppm) > r.max_abs_ppm) r.max_abs_ppm = abs(ppm);
            if (t > 20000 && fill > r.max_fill) r.max_fill = fill;
            next_block += block_ms;
        }
    }
    r.final_ema = dc.smoothed();
    r.target = dc.target();
    r.final_ppm = ppm;
    r.late_err = err_n ? err_sum / err_n : 0;
    r.integral = dp.integral();
    return r;
}

// Deferred acks, closed loop at 1 ms: the phone is held to the ack window. It sends a frame (42.667 ms of audio,
// arriving 2..2+jitter ms later) whenever fewer than `window` frames are unacked; a frame is acked once the mixer
// has consumed all of it. Its own clock doesn't matter: our consumption paces it. raise_cap = what BrowserAudio does
// under deferred acks (target cap = window x frame + 60 ms); pi = DriftPi sets the trim.
struct PacedSim { int32_t final_ppm, max_abs_ppm; int gaps; double target, ema; };
static PacedSim simulatePaced(uint32_t window, int jitter_ms, int seconds, bool raise_cap, bool pi) {
    DriftControl dc;
    DriftPi dp;
    const double frame_ms = 2048.0 / 48.0, block_ms = 1024.0 / 44.1;
    if (raise_cap) dc.raiseMaxTarget((uint32_t)(window * frame_ms) + 60u);
    double fill = 0, consumed = 0, next_block = block_ms;
    long sent = 0;
    std::vector<double> in_flight;   // arrival times of sent frames not arrived yet
    uint32_t seed = 4242u;
    int32_t ppm = 0;
    PacedSim r = { 0, 0, 0, 0, 0 };
    for (int t = 0; t < seconds * 1000; ++t) {
        const long acked = (long)(consumed / frame_ms);
        while (sent - acked < (long)window) {      // the phone fills its window
            seed = seed * 1103515245u + 12345u;
            in_flight.push_back(t + 2.0 + (jitter_ms ? (double)((seed >> 16) % (uint32_t)(jitter_ms + 1)) : 0.0));
            ++sent;
        }
        for (size_t i = 0; i < in_flight.size();) {
            if (in_flight[i] <= t) { fill += frame_ms; in_flight.erase(in_flight.begin() + i); } else ++i;
        }
        while (t >= next_block) {
            const double need = block_ms * (1.0 + ppm * 1e-6);
            if (fill < need) { if (t > 20000) ++r.gaps; consumed += fill; fill = 0; }
            else { fill -= need; consumed += need; }
            ppm = dc.update(fill, (uint64_t)t);
            if (pi) ppm = dc.learning() ? 0 : dp.update(dc.smoothed() - dc.target(), (uint64_t)t);
            if (abs(ppm) > r.max_abs_ppm) r.max_abs_ppm = abs(ppm);
            next_block += block_ms;
        }
    }
    r.final_ppm = ppm;
    r.target = dc.target();
    r.ema = dc.smoothed();
    return r;
}

// A guidance prompt as the car plays it (logs 2026-11-03), at 1 ms resolution in output frames at 44.1 kHz. The shim's
// writes never block: the mixer writes a block whenever it is no more than 150 ms ahead of real time, so it runs ~150 ms
// ahead and the device is fed for that long. The phone starts the stream at 1 s and then sends one 42.667 ms frame
// (1881.6 output frames) per frame period on its own clock, each late by 0..jitter ms, with no burst at the start.
// preroll_ms = PrerollGate target and hold (0 = off); lead_rule = waits use waitIsSafe's lead (false = only after a
// write that blocked, which never happens here: the old rule). gaps = short or empty blocks while playing.
struct PromptSim { int gaps, holds, waited_blocks; double min_lead_ms, first_sound_ms; };
static PromptSim simulatePrompt(uint32_t preroll_ms, uint32_t budget_ms, int jitter_ms, int seconds, bool lead_rule) {
    const double fpm = 44.1, block = 1024.0, frame_out = 2048.0 * 44100.0 / 48000.0, frame_ms = 2048.0 / 48.0;
    const int start_ms = 1000;
    const uint32_t block_ms = 23;
    PrerollGate gate;
    gate.configure((size_t)(preroll_ms * 44100u / 1000u), preroll_ms);
    UnderrunGuard guard(budget_ms);
    PromptSim r = { 0, 0, 0, 1e9, -1 };
    double ring = 0, written = 0, next_arrival = start_ms;
    int k = 0;
    uint32_t seed = 4242u, waited = 0;
    for (int t = 0; t < (start_ms + seconds * 1000); ++t) {
        while (t >= next_arrival) {
            ring += frame_out;
            ++k;
            seed = seed * 1103515245u + 12345u;
            const double nominal = start_ms + k * frame_ms;
            const double late = jitter_ms ? (double)((seed >> 16) % (uint32_t)(jitter_ms + 1)) : 0.0;
            next_arrival = nominal + late > next_arrival ? nominal + late : next_arrival;
        }
        const bool started = t >= start_ms;
        while (written / fpm <= t + 150.0) {
            const double lead = written / fpm - t;
            if (started && lead < r.min_lead_ms) r.min_lead_ms = lead;
            const bool mix = gate.ready(started, (size_t)ring, (uint64_t)t + 1);
            const bool live = started && mix;
            const bool can_wait = lead_rule ? waitIsSafe(false, (int64_t)lead, budget_ms, block_ms) : false;
            if (guard.shouldWait(live, (size_t)ring, (size_t)block, waited, can_wait)) {
                if (waited == 0) ++r.waited_blocks;
                ++waited;
                break;   // wait this ms
            }
            if (mix) {
                const double got = ring < block ? (double)(size_t)ring : block;
                if (live && r.first_sound_ms < 0 && got > 0) r.first_sound_ms = t - start_ms;
                ring -= got;
                if (live && got < block) ++r.gaps;
                guard.onPulled(live, (size_t)got, (size_t)block);
                gate.onPulled(live, (size_t)got);
            }
            written += block;
            waited = 0;
        }
    }
    r.holds = (int)gate.totalHolds();
    return r;
}

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

    // 13. Waiting for late input (UnderrunGuard, estimateOutFrames, fades).
    {
        // queued input in output frames: 48 kHz stereo ring bytes + pending stereo samples, to 44.1 kHz stereo
        CHECK(estimateOutFrames(8192, 0, 48000, 2, 44100, 2) == 1881);            // one AA media frame
        CHECK(estimateOutFrames(8192, 200, 48000, 2, 44100, 2) == 1981);          // + 100 pending frames
        CHECK(estimateOutFrames(4096, 0, 48000, 1, 44100, 2) == 1881);            // guidance mono
        CHECK(estimateOutFrames(4096, 10, 0, 1, 44100, 2) == 5);                  // unknown rate: pending only

        UnderrunGuard g(30);
        CHECK(!g.shouldWait(false, 0, 1024, 0, true));       // not started: never wait
        CHECK(!g.shouldWait(true, 1024, 1024, 0, true));     // a whole block is there
        CHECK(!g.shouldWait(true, 800, 1024, 0, false) && !g.starved());   // device not full: pad, not starved
        CHECK(g.shouldWait(true, 800, 1024, 0, true));       // short, device full: wait
        CHECK(g.shouldWait(true, 800, 1024, 28, true));
        CHECK(!g.shouldWait(true, 800, 1024, 30, true) && g.starved() && g.timeouts() == 1);   // ran out: starved
        CHECK(!g.shouldWait(true, 0, 1024, 0, true));        // starved: pad at once, no new wait
        g.onPulled(true, 500, 1024);
        CHECK(g.starved());                                  // a short block does not end it
        CHECK(!g.shouldWait(true, 2000, 1024, 0, true) && !g.starved());   // data back: waiting allowed again
        CHECK(g.shouldWait(true, 10, 1024, 0, true));
        g.onPulled(true, 1024, 1024);
        CHECK(!g.starved());
        g.reset();
        CHECK(g.timeouts() == 0);
        UnderrunGuard off(0);
        CHECK(!off.shouldWait(true, 0, 1024, 0, true));      // NBT_BROWSER_WAIT_MS=0: old behaviour

        // fades: the tail ramps down to exactly 0, the head ramps up from just above 0, stereo both channels
        int16_t b[8 * 2];
        for (int i = 0; i < 16; ++i) b[i] = 8000;
        fadeOutTail(b, 8, 2, 4);
        CHECK(b[0] == 8000 && b[7] == 8000 && b[8] == 6000 && b[9] == 6000 && b[14] == 0 && b[15] == 0);
        for (int f = 4; f < 7; ++f) CHECK(b[2 * f] > b[2 * f + 2]);
        for (int i = 0; i < 16; ++i) b[i] = -9000;
        fadeInHead(b, 8, 2, 4);
        CHECK(b[0] == -1800 && b[1] == -1800 && b[6] == -7200 && b[8] == -9000 && b[15] == -9000);
        fadeOutTail(b, 3, 2, 32);                      // fade longer than the audio: clipped to it
        CHECK(b[4] == 0 && b[5] == 0);
        fadeInHead(NULL, 8, 2, 4);                     // no crash
        fadeOutTail(b, 0, 2, 4);

        // Simulation, phone with no lead (as after drift drained it) and frames up to 20 ms late:
        // padding at once leaves holes; waiting absorbs them; the device never runs dry either way.
        SimResult pad = simulateMixer(0, 20, 0, 60, 1 << 30);
        SimResult wait = simulateMixer(30, 20, 0, 60, 1 << 30);
        printf("simulated 60 s, no lead, jitter 20 ms: gaps %d padding at once, %d waiting (underruns %d / %d, "
               "longest wait %d ms, device never below %.0f ms)\n", pad.gaps, wait.gaps, pad.underruns,
               wait.underruns, wait.max_wait_ms, wait.min_dev_ms);
        CHECK(pad.gaps >= 1 && wait.gaps == 0);
        CHECK(pad.underruns == 0 && wait.underruns == 0);
        CHECK(wait.max_wait_ms <= 30 && wait.min_dev_ms >= 55);   // waits never take the device below ~62 ms
        // With the phone's usual ~130 ms lead nothing needs waiting.
        SimResult lead = simulateMixer(30, 20, 130, 60, 1 << 30);
        CHECK(lead.gaps == 0 && lead.underruns == 0 && lead.max_wait_ms == 0);
        // The phone stops sending at 10 s without a STOP: one bounded wait, then padding at once, device never dry.
        SimResult stop = simulateMixer(30, 5, 130, 20, 10000);
        CHECK(stop.underruns == 0 && stop.waited_after_stop <= 30 && stop.gaps > 100);
        // A phone 1 % slow is short all the time: waiting must never add up and drain the device.
        SimResult slow = simulateMixer(30, 10, 0, 120, 1 << 30, 0.99);
        printf("simulated 120 s, phone 1 %% slow: gaps %d, underruns %d, device never below %.0f ms\n", slow.gaps,
               slow.underruns, slow.min_dev_ms);
        CHECK(slow.underruns == 0 && slow.min_dev_ms >= 55);
    }

    // 14. DriftControl: learn the natural level, then a slow bounded trim that holds it against clock drift.
    {
        DriftControl d;
        CHECK(d.learning() && d.update(130, 0) == 0);
        for (uint64_t t = 23; t < 10000; t += 23) CHECK(d.update(130, t) == 0);   // learning: no trim
        d.update(130, 10010);
        CHECK(!d.learning() && near((float)d.target(), 130.0f, 0.5f));
        // queue 20 ms above target for a while: positive trim (play faster), slewed (<= 20 ppm/s), capped by kp
        int32_t p = 0;
        for (uint64_t t = 10033; t < 11033; t += 23) p = d.update(150, t);
        CHECK(p > 0 && p <= 21);                        // 1 s of slew at most
        for (uint64_t t = 11033; t < 60000; t += 23) p = d.update(150, t);
        CHECK(p >= 190 && p <= 200);                    // ~10 ppm per ms of (smoothed) error
        for (uint64_t t = 60000; t < 200000; t += 23) p = d.update(1000, t);
        CHECK(p == 500);                                // capped at +/-500 ppm
        for (uint64_t t = 200000; t < 400000; t += 23) p = d.update(0, t);
        CHECK(p == -500);
        d.reset();
        CHECK(d.learning() && d.ppm() == 0);
        // target bounds: too little lead is raised to 60 ms, a backlog is lowered to 300 ms
        DriftControl lo, hi;
        for (uint64_t t = 0; t <= 10100; t += 23) { lo.update(5, t); hi.update(900, t); }
        CHECK(near((float)lo.target(), 60.0f, 0.01f) && near((float)hi.target(), 300.0f, 0.01f));

        // Closed loop, one hour, the 1 Nov lead (130 ms), arrivals up to 15 ms late.
        const double cases[] = { -100, 100, -300, 300, 0 };
        for (int i = 0; i < 5; ++i) {
            DriftSim s = simulateDrift(cases[i], 15, 130, 3600, true);
            printf("drift %+5.0f ppm, 1 h corrected: trim %+5.0f ppm, queue %.0f ms (target %.0f), max %.0f ms, "
                   "gaps %d\n", cases[i], s.final_ppm, s.final_ema, s.target, s.max_fill, s.gaps_after_learn);
            CHECK(s.quiet_learning);
            CHECK(s.gaps_after_learn == 0);
            CHECK(fabs(s.final_ppm - cases[i]) <= 20);              // the trim converges to the drift
            CHECK(fabs(s.final_ema - s.target) <= 40);               // the queue stays near its natural level
            CHECK(s.max_abs_ppm <= 500);
        }
        // Without correction a phone 100 ppm slow drains the 130 ms lead within the hour and then gaps.
        DriftSim slow = simulateDrift(-100, 15, 130, 3600, false);
        printf("drift  -100 ppm, 1 h uncorrected: gaps %d\n", slow.gaps_after_learn);
        CHECK(slow.gaps_after_learn > 0);
        // No drift: the trim stays tiny (jitter and the frame sawtooth are smoothed out).
        DriftSim zero = simulateDrift(0, 15, 130, 600, true);
        CHECK(zero.max_abs_ppm <= 25);
        // No lead at all: the 60 ms floor builds one up by playing slightly slower.
        DriftSim none = simulateDrift(0, 5, 0, 900, true);
        CHECK(none.target == 60 && none.final_ema >= 40);
    }

    // 15. DriftPi: the same loop with an integral term. It holds the queue on target where the P loop stays off by
    //     ~0.1 ms per ppm, and its integral settles on the phone's clock error.
    {
        DriftPi p;
        CHECK(near((float)p.kp(), 14.14f, 0.01f) && near((float)p.ki(), 0.1f, 0.0001f));
        CHECK(p.update(10, 0) == 0);                       // first call: starts the clock only
        int32_t v = 0;
        for (uint64_t t = 23; t <= 1000; t += 23) v = p.update(10, t);
        CHECK(v > 0 && v <= 21);                           // slewed like DriftControl (<= 20 ppm/s)
        // A held error at the cap does not wind the integral up beyond the cap.
        for (uint64_t t = 1000; t < 400000; t += 23) v = p.update(1000, t);
        CHECK(v == 500 && p.integral() <= 500.0);
        for (uint64_t t = 400000; t < 900000; t += 23) v = p.update(-1000, t);
        CHECK(v == -500 && p.integral() >= -500.0);
        p.reset();
        CHECK(p.ppm() == 0 && p.integral() == 0);

        const double cases[] = { -100, 100, -300, 300, 0 };
        for (int i = 0; i < 5; ++i) {
            DriftSim pi = simulateDrift(cases[i], 15, 130, 3600, true, true);
            DriftSim pp = simulateDrift(cases[i], 15, 130, 3600, true, false);
            printf("drift %+5.0f ppm, 1 h: PI queue off target %.1f ms (integral %+.0f ppm, trim %+.0f), "
                   "P %.1f ms (trim %+.0f); PI max %.0f ms, gaps %d\n", cases[i], pi.late_err, pi.integral,
                   pi.final_ppm, pp.late_err, pp.final_ppm, pi.max_fill, pi.gaps_after_learn);
            CHECK(pi.quiet_learning);
            CHECK(pi.gaps_after_learn == 0);
            CHECK(pi.max_abs_ppm <= 500);
            CHECK(fabs(pi.final_ppm - cases[i]) <= 20);           // trim = the drift
            CHECK(fabs(pi.integral - cases[i]) <= 20);            // and the integral reads it
            CHECK(pi.late_err <= 4.0);                            // on target, within the jitter
            if (cases[i] != 0) CHECK(pi.late_err < pp.late_err);  // closer than the P loop
        }
    }

    // 16. Deferred acks: a phone held to the 10-frame window keeps ~400 ms queued, whatever the trim. With the old
    //     300 ms target cap both loops pin the trim at +500 ppm; with the cap raised to cover the window they settle
    //     at the queue the window gives and leave the trim near 0.
    {
        CHECK(DriftControl().maxTarget() == 300);
        DriftControl c;
        c.raiseMaxTarget(200);
        CHECK(c.maxTarget() == 300);   // never lowered
        c.raiseMaxTarget(487);
        c.reset();
        CHECK(c.maxTarget() == 487);   // kept across reset
        for (int pi = 0; pi < 2; ++pi) {
            PacedSim stuck = simulatePaced(10, 5, 900, false, pi != 0);
            PacedSim fixed = simulatePaced(10, 5, 900, true, pi != 0);
            printf("paced by acks (%s): 300 ms cap: trim %+d ppm (target %.0f, queue %.0f ms); raised cap: trim %+d ppm, "
                   "max %d (target %.0f, queue %.0f ms), gaps %d\n", pi ? "PI" : "P", stuck.final_ppm, stuck.target,
                   stuck.ema, fixed.final_ppm, fixed.max_abs_ppm, fixed.target, fixed.ema, fixed.gaps);
            CHECK(stuck.final_ppm == 500);                  // the bug the raised cap fixes
            CHECK(abs(fixed.final_ppm) <= 30);
            CHECK(fixed.max_abs_ppm <= 60);
            CHECK(fixed.gaps == 0);
            CHECK(fixed.target > 300);
        }
    }

    // 17. waitIsSafe: a blocked write, or a lead over real time that covers the wait + a block + 20 ms.
    {
        CHECK(waitIsSafe(true, -500, 30, 23));     // the device was full: safe whatever the lead says
        CHECK(waitIsSafe(false, 150, 30, 23));     // the car: writes never block, the mixer runs ~150 ms ahead
        CHECK(waitIsSafe(false, 73, 30, 23));
        CHECK(!waitIsSafe(false, 72, 30, 23));
        CHECK(!waitIsSafe(false, 0, 30, 23));      // just re-anchored after falling behind
        CHECK(!waitIsSafe(false, -40, 30, 23));
    }

    // 18. PrerollGate: hold a started stream until it has a cushion (or its first audio waited hold_ms).
    {
        PrerollGate off;
        CHECK(off.ready(true, 0, 1000));            // never configured: plays at once
        off.configure(0, 60);
        CHECK(off.ready(true, 0, 1000) && !off.holding());

        PrerollGate g;
        g.configure(2646, 60);                       // 60 ms at 44.1 kHz
        CHECK(!g.ready(true, 0, 1000));              // started, nothing queued
        CHECK(!g.ready(true, 1881, 1001));           // one 42.7 ms frame: not enough yet
        CHECK(!g.ready(true, 1881, 1050));
        CHECK(g.ready(true, 3763, 1043) && g.holds() == 1);   // the second frame: plays, with ~85 ms queued
        CHECK(g.ready(true, 500, 1100));             // open: stays open while short
        g.onPulled(true, 500);                       // a short block: the cushion recovers by itself, no new hold
        CHECK(g.ready(true, 0, 1123));
        g.onPulled(true, 0);                         // a whole block with no audio: ran empty, hold again
        CHECK(g.holding() && !g.ready(true, 0, 1146));
        CHECK(!g.ready(true, 900, 1200));
        CHECK(g.ready(true, 900, 1260) && g.holds() == 2);    // only 900 frames, but they waited 60 ms: play them

        g.restart();                                 // a new START
        CHECK(!g.ready(true, 1000, 2000));
        CHECK(g.ready(false, 1000, 2001));           // STOP: the tail plays out at once
        CHECK(!g.ready(true, 1000, 2002));           // and the next start is held again

        g.resetInterval();
        CHECK(g.holds() == 0 && g.totalHolds() == 2);
        g.reset();
        CHECK(g.totalHolds() == 0);
    }

    // 19. A prompt as the car plays it: the phone sends in real time with no burst and the shim never blocks. The old
    //     rules (no pre-roll, waits only after a blocked write) leave the stream without a cushion, so late frames
    //     become holes (the car's "857 of 1024"). Pre-roll alone removes most; with the lead rule the waits cover
    //     the rest. A 60 ms pre-roll delays a prompt by about
    //     one frame period (~43 ms, plus the wait for the next block), never more than 60 ms + one block.
    {
        for (int jitter = 0; jitter <= 60; jitter += (jitter < 20 ? 10 : 20)) {
            PromptSim old_rules = simulatePrompt(0, 30, jitter, 30, false);
            PromptSim preroll = simulatePrompt(60, 30, jitter, 30, false);
            PromptSim both = simulatePrompt(60, 30, jitter, 30, true);
            printf("prompt, jitter 0..%d ms, 30 s: old rules %d gaps; pre-roll %d gaps; pre-roll + lead rule %d gaps "
                   "(%d waits, first sound at %.0f ms, %d holds, lowest lead %.0f ms)\n", jitter, old_rules.gaps,
                   preroll.gaps, both.gaps, both.waited_blocks, both.first_sound_ms, both.holds, both.min_lead_ms);
            if (jitter > 0) CHECK(old_rules.gaps > 0);    // the bug
            CHECK(old_rules.waited_blocks == 0);          // the old rule never waits when writes don't block
            CHECK(both.gaps == 0);
            CHECK(both.holds == 1);
            CHECK(both.first_sound_ms >= 0 && both.first_sound_ms <= 60 + 24);   // the hold, then the next block
            CHECK(both.min_lead_ms >= 100);               // waits never ate into the device's feed
        }
    }

    printf(fails ? "test_audio_mixer: %d FAILED\n" : "test_audio_mixer: all passed\n", fails);
    return fails ? 1 : 0;
}
