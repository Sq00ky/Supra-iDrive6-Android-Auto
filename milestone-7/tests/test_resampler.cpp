// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: PcmResampler's SPEEX engine (speexdsp, fixed point) against the LINEAR one, on the conversions browser
// mode does: 48 kHz -> 44.1 kHz media and guidance, 16 kHz -> 44.1 kHz system sounds, mono -> stereo, drift trim.
#include "pcm_utils.hpp"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static const double kPi = 3.14159265358979323846;

// `seconds` of a sine at `hz`, amplitude `amp`, interleaved over `ch` channels.
static std::vector<int16_t> sine(double hz, uint32_t rate, uint32_t ch, double seconds, double amp = 16000.0) {
    const size_t n = (size_t)(rate * seconds);
    std::vector<int16_t> v(n * ch);
    for (size_t i = 0; i < n; ++i) {
        const int16_t s = (int16_t)lrint(amp * sin(2.0 * kPi * hz * (double)i / rate));
        for (uint32_t c = 0; c < ch; ++c) v[i * ch + c] = s;
    }
    return v;
}

// Level of the sine at `hz` in channel 0 of `x` (rate, ch), in dB relative to `amp`, over the middle half (skips
// the filter's start). A single-bin DFT: the amplitude of exactly that frequency.
static double levelDb(const std::vector<int16_t>& x, uint32_t rate, uint32_t ch, double hz, double amp = 16000.0) {
    const size_t n = x.size() / ch;
    const size_t a = n / 4, b = 3 * n / 4;
    double re = 0, im = 0;
    for (size_t i = a; i < b; ++i) {
        const double w = 2.0 * kPi * hz * (double)i / rate;
        re += x[i * ch] * cos(w);
        im += x[i * ch] * sin(w);
    }
    const double mag = 2.0 * sqrt(re * re + im * im) / (double)(b - a);
    return 20.0 * log10(mag / amp + 1e-12);
}

static std::vector<int16_t> convert(PcmResampler::Engine e, const std::vector<int16_t>& in, uint32_t sr, uint32_t sc,
                                    uint32_t dr, uint32_t dc, int32_t trim = 0) {
    PcmResampler r;
    r.setEngine(e, 5);
    r.configure(sr, sc, dr, dc);
    r.setTrimPpm(trim);
    std::vector<int16_t> out;
    r.process(&in[0], in.size() / sc, out);
    return out;
}

int main() {
    const PcmResampler::Engine L = PcmResampler::LINEAR, S = PcmResampler::SPEEX;

    // 1. Passband, 48 -> 44.1 kHz stereo (AA media): speex stays flat where linear interpolation dulls the treble.
    {
        const double hz[4] = { 1000, 10000, 15000, 18000 };
        for (int k = 0; k < 4; ++k) {
            std::vector<int16_t> in = sine(hz[k], 48000, 2, 1.0);
            const double ls = levelDb(convert(S, in, 48000, 2, 44100, 2), 44100, 2, hz[k]);
            const double ll = levelDb(convert(L, in, 48000, 2, 44100, 2), 44100, 2, hz[k]);
            printf("  %5.0f Hz: speex %+.2f dB, linear %+.2f dB\n", hz[k], ls, ll);
            CHECK(fabs(ls) < 0.5);                       // speex: flat within 0.5 dB up to 18 kHz
            if (hz[k] >= 15000) CHECK(ll < -2.0);        // linear: the documented ~-2.9 dB at 15 kHz and worse above
        }
    }

    // 2. Aliasing: a 23 kHz tone (above the 22.05 kHz output Nyquist) folds back to 21.1 kHz. Speex removes it.
    {
        std::vector<int16_t> in = sine(23000, 48000, 1, 1.0);
        const double ls = levelDb(convert(S, in, 48000, 1, 44100, 1), 44100, 1, 44100 - 23000);
        const double ll = levelDb(convert(L, in, 48000, 1, 44100, 1), 44100, 1, 44100 - 23000);
        printf("  23 kHz alias at 21.1 kHz: speex %.1f dB, linear %.1f dB\n", ls, ll);
        CHECK(ls < -60.0);
        CHECK(ll > -20.0);
    }

    // 3. Length: 1 s in gives 1 s out minus what is still inside the filter (half its length: ~40 output frames from
    //    48 kHz, ~110 from 16 kHz; it comes out with the next input), every rate browser mode uses.
    {
        const uint32_t rates[3] = { 48000, 16000, 44100 };
        for (int k = 0; k < 3; ++k) {
            std::vector<int16_t> in = sine(440, rates[k], 1, 1.0);
            std::vector<int16_t> out = convert(S, in, rates[k], 1, 44100, 2);   // mono -> stereo
            const long frames = (long)(out.size() / 2);
            printf("  1 s at %u Hz -> %ld frames at 44100 Hz\n", rates[k], frames);
            CHECK(frames > 44100 - 200 && frames <= 44100);
            CHECK(out.size() % 2 == 0 && out[2000] == out[2001]);              // both channels carry the mono signal
        }
    }

    // 4. Drift trim: +/-500 ppm consumes that much more / less input per output frame (44078 / 44122 frames for 1 s).
    {
        std::vector<int16_t> in = sine(440, 48000, 2, 1.0);
        const long fast = (long)(convert(S, in, 48000, 2, 44100, 2, 500).size() / 2);
        const long slow = (long)(convert(S, in, 48000, 2, 44100, 2, -500).size() / 2);
        const long none = (long)(convert(S, in, 48000, 2, 44100, 2, 0).size() / 2);
        printf("  trim: none %ld, +500 ppm %ld, -500 ppm %ld frames\n", none, fast, slow);
        CHECK(none - fast >= 18 && none - fast <= 26);
        CHECK(slow - none >= 18 && slow - none <= 26);
        // the pitch moves by the trim, the level does not
        CHECK(fabs(levelDb(convert(S, in, 48000, 2, 44100, 2, 500), 44100, 2, 440.0 * 1.0005)) < 0.5);
    }

    // 5. Chunked processing joins seamlessly: 37-frame chunks give the same samples as one call.
    {
        std::vector<int16_t> in = sine(5000, 48000, 2, 0.5);
        std::vector<int16_t> whole = convert(S, in, 48000, 2, 44100, 2);
        PcmResampler r;
        r.setEngine(S, 5);
        r.configure(48000, 2, 44100, 2);
        std::vector<int16_t> parts;
        for (size_t f = 0; f < in.size() / 2; f += 37) {
            const size_t n = (in.size() / 2 - f) < 37 ? (in.size() / 2 - f) : 37;
            r.process(&in[f * 2], n, parts);
        }
        CHECK(parts == whole);
        CHECK(r.inFrames() == in.size() / 2 && r.outFrames() == parts.size() / 2);
    }

    // 6. A trim change mid-stream (rate-limited to once per second of output) never clicks: a 100 Hz tone keeps its
    //    small sample-to-sample steps across every change, and the counters keep the ratio.
    {
        PcmResampler r;
        r.setEngine(S, 5);
        r.configure(48000, 2, 44100, 2);
        std::vector<int16_t> in = sine(100, 48000, 2, 4.0);
        std::vector<int16_t> out;
        const size_t step = 480;   // 10 ms blocks
        int32_t trim = 0;
        for (size_t f = 0; f < in.size() / 2; f += step) {
            trim = (trim == 300) ? -300 : trim + 20;   // keeps changing; applied at most once a second
            r.setTrimPpm(trim);
            r.process(&in[f * 2], step, out);
        }
        int maxStep = 0;
        for (size_t i = 2 * 1000; i + 2 < out.size(); i += 2) {
            const int d = abs((int)out[i + 2] - (int)out[i]);
            if (d > maxStep) maxStep = d;
        }
        // 100 Hz at amplitude 16000 sampled at 44.1 kHz changes by at most 2*pi*100*16000/44100 = 228 per sample
        CHECK(maxStep < 260);
        const double ratio = (double)r.outFrames() / (double)r.inFrames();
        CHECK(fabs(ratio - 44100.0 / 48000.0) < 0.002);
    }

    // 7. Same rate, no trim: passthrough (copy). A trim takes it to speex, which then stays even at trim 0.
    {
        PcmResampler r;
        r.setEngine(S, 5);
        r.configure(48000, 2, 48000, 2);
        CHECK(r.passthrough());
        std::vector<int16_t> in = sine(1000, 48000, 2, 0.1), out;
        r.process(&in[0], in.size() / 2, out);
        CHECK(out == in);
        r.setTrimPpm(200);
        CHECK(!r.passthrough());
        out.clear();
        r.process(&in[0], in.size() / 2, out);
        CHECK(!r.passthrough());
        r.setTrimPpm(0);
        CHECK(!r.passthrough());   // stays filtering: switching back to a copy would jump by the filter latency
        // and the linear engine is unchanged
        PcmResampler l;
        l.configure(48000, 2, 48000, 2);
        CHECK(l.engine() == PcmResampler::LINEAR && l.passthrough());
    }

    // 8. Engine switch and reset restart cleanly (no crash, output resumes).
    {
        PcmResampler r;
        r.setEngine(S, 5);
        r.configure(16000, 1, 44100, 2);
        std::vector<int16_t> in = sine(440, 16000, 1, 0.2), out;
        r.process(&in[0], in.size(), out);
        r.reset();
        r.process(&in[0], in.size(), out);
        r.setEngine(L);
        r.process(&in[0], in.size(), out);
        r.setEngine(S, 3);
        r.process(&in[0], in.size(), out);
        CHECK(out.size() / 2 > 4 * 8820 - 400);
        CHECK(r.engine() == S && r.quality() == 3);
    }

    // 9. Every trim stays clean, not just round ones. Fixed-point speex breaks once the ratio's denominator reaches
    //    2^15, and an exact trimmed ratio rarely reduces that far (+171 ppm at 48 -> 44.1 kHz is 1000171:918750): on the
    //    car the first trim, +171 ppm, turned music into noise. A 1 kHz tone must come out as a clean tone moved by the trim, and
    //    the frame count must keep the trimmed ratio, for trims across +/-2 % and each rate pair browser mode uses.
    {
        const uint32_t pairs[4][2] = { { 48000, 44100 }, { 16000, 44100 }, { 48000, 48000 }, { 44100, 44100 } };
        int32_t trims[64];
        size_t nt = 0;
        const int32_t car[6] = { 171, 372, 396, -495, 2274, -15 };
        for (size_t i = 0; i < 6; ++i) trims[nt++] = car[i];
        for (int32_t t = -20000; t <= 20000 && nt < 64; t += 701) trims[nt++] = t;   // 701: no round numbers
        double worstRes = -200, worstRatio = 0;
        for (size_t p = 0; p < 4; ++p) {
            const uint32_t sr = pairs[p][0], dr = pairs[p][1];
            const std::vector<int16_t> in = sine(1000, sr, 2, 2.0);
            const size_t half = in.size() / 4;   // frames in 1 s
            for (size_t i = 0; i < nt; ++i) {
                PcmResampler r;
                r.setEngine(S, 5);
                r.configure(sr, 2, dr, 2);
                r.setTrimPpm(trims[i]);
                std::vector<int16_t> out;
                r.process(&in[0], half, out);   // the first second carries the filter's start-up
                r.resetCounters();
                out.clear();
                r.process(&in[half * 2], half, out);
                // what is left after removing the best-fit sine, middle half, dB re signal. The tone sits at the
                // trimmed frequency to within the ratio's rounding (a few ppm, checked through the frame count
                // below): its exact frequency comes from the phase drift between the two quarters.
                double w = 2.0 * kPi * 1000.0 * (1.0 + trims[i] / 1e6) / dr;
                const size_t n = out.size() / 2, a = n / 4, b = 3 * n / 4, m = (a + b) / 2;
                {
                    double r1 = 0, i1 = 0, r2 = 0, i2 = 0;
                    for (size_t k = a; k < m; ++k) { r1 += out[2 * k] * cos(w * k); i1 -= out[2 * k] * sin(w * k); }
                    for (size_t k = m; k < b; ++k) { r2 += out[2 * k] * cos(w * k); i2 -= out[2 * k] * sin(w * k); }
                    w += atan2(i2 * r1 - r2 * i1, r2 * r1 + i2 * i1) / (double)(m - a);
                }
                double s = 0, c = 0;
                for (size_t k = a; k < b; ++k) { s += out[2 * k] * sin(w * k); c += out[2 * k] * cos(w * k); }
                const double as = 2 * s / (double)(b - a), ac = 2 * c / (double)(b - a);
                double err = 0, sig = 0;
                for (size_t k = a; k < b; ++k) {
                    const double fit = as * sin(w * k) + ac * cos(w * k);
                    err += (out[2 * k] - fit) * (out[2 * k] - fit);
                    sig += fit * fit;
                }
                const double res = 10.0 * log10(err / (sig + 1e-9) + 1e-12);
                const double want = (double)dr / sr / (1.0 + trims[i] / 1e6);
                const double ratioErr = fabs((double)r.outFrames() / (double)r.inFrames() / want - 1.0);
                if (res > worstRes) worstRes = res;
                if (ratioErr > worstRatio) worstRatio = ratioErr;
                if (res > -60.0) printf("  %u -> %u Hz, trim %+d ppm: residual %.1f dB\n", sr, dr, trims[i], res);
            }
        }
        printf("  trims: worst residual %.1f dB, worst frame ratio error %.0f ppm over %u trims x 4 rate pairs\n",
               worstRes, worstRatio * 1e6, (unsigned)nt);
        CHECK(worstRes < -60.0);
        CHECK(worstRatio < 50e-6);   // one output frame in 1 s is 23 ppm
    }

    printf("test_resampler: %s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
