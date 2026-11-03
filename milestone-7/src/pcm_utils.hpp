// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef PCM_UTILS_HPP
#define PCM_UTILS_HPP

// PCM helpers for the A2I audio path (C++03 / GCC 4.4 compatible).

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <pthread.h>

namespace nbt {

// Thread-safe byte ring buffer. The producer never blocks: when the ring is
// full the OLDEST bytes are discarded so playback latency stays bounded.
class PcmRing {
public:
    explicit PcmRing(size_t capacity);
    ~PcmRing();

    // Returns the number of bytes that had to be dropped to make room.
    size_t write(const uint8_t* data, size_t len);
    // Copies up to len bytes into out; returns the number copied.
    size_t read(uint8_t* out, size_t len);
    size_t size() const;
    size_t capacity() const { return capacity_; }
    size_t clear();   // returns the bytes discarded
    uint64_t droppedBytes() const;
    uint64_t writtenBytes() const;   // every byte ever written (dropped ones too)

private:
    PcmRing(const PcmRing&);
    PcmRing& operator=(const PcmRing&);

    std::vector<uint8_t> buf_;
    size_t capacity_;
    size_t head_;   // read index
    size_t used_;
    uint64_t dropped_;
    uint64_t written_;
    mutable pthread_mutex_t mutex_;
};

// The output device rate to ask for. want = the configured rate, kept if the device's reported range allows it, else
// its maximum (this car's browser device reports 22050..44100, so 48000 becomes 44100). force (non-zero) is asked for
// as is, whatever the device reports (NBT_BROWSER_FORCE_RATE experiment). min/max <= 0 = range unknown.
uint32_t pickDeviceRate(uint32_t want, uint32_t force, int min_rate, int max_rate);

// Bytes of 16-bit PCM to read per step: `ms` milliseconds at rate / channels, in whole frames, at least one frame and
// at most `cap` bytes (rounded down to whole frames). Keeps a fixed read buffer safe whatever rate a stream declares.
// Returns 0 only if cap is smaller than one frame.
size_t pcmChunkBytes(uint32_t rate, uint32_t channels, uint32_t ms, size_t cap);

// Streaming resampler + channel mapper for interleaved signed 16-bit little-endian PCM. State is kept between
// process() calls so consecutive chunks join without clicks.
//
// Two engines:
//   LINEAR  linear interpolation (the default; the Bluetooth / A2I paths use it). Cheap, but its response is a
//           triangle filter, sinc^2(f/fs): about -2.9 dB at 15 kHz from 48 kHz, nothing stops 22-24 kHz folding back
//           when going to 44.1 kHz, and under a drift trim the treble pulses as the interpolation point drifts.
//   SPEEX   the speexdsp resampler (nbt_speex.h, BSD-3), fixed point, windowed-sinc polyphase filter: flat to ~89 %
//           of the lower Nyquist, 80-100 dB stopband (quality 4-5), about 1 ms latency. Resamples at the source
//           channel count; channel mapping runs after it.
class PcmResampler {
public:
    enum Engine { LINEAR = 0, SPEEX = 1 };

    PcmResampler();
    ~PcmResampler();

    void configure(uint32_t src_rate, uint32_t src_channels,
                   uint32_t dst_rate, uint32_t dst_channels);
    void reset();

    // Kept across configure() / reset(); a change restarts the conversion (a short discontinuity). quality 0..10
    // (speex only; 5 = 80 taps, ~100 dB stopband). If speexdsp can't be set up on first use, the resampler carries on
    // with LINEAR and engine() says so: callers compare it with what they asked for.
    void setEngine(Engine e, int quality = 5);
    Engine engine() const { return engine_; }
    int quality() const { return quality_; }

    // Frames in / out since resetCounters() (a ratio sanity check for the caller).
    uint64_t inFrames() const { return in_count_; }
    uint64_t outFrames() const { return out_count_; }
    void resetCounters() { in_count_ = out_count_ = 0; }

    // Appends converted interleaved frames to `out`.
    void process(const int16_t* in, size_t in_frames, std::vector<int16_t>& out);

    // Byte-oriented convenience wrapper (len is truncated to whole frames).
    void processBytes(const uint8_t* in, size_t len, std::vector<uint8_t>& out);

    // Clock-drift trim: consume `ppm` parts per million more input per output frame (negative = less), i.e. play
    // the stream that much faster. Clamped to +/-20000; kept across configure() / reset(). Non-zero takes a
    // same-rate, same-channel stream out of passthrough. SPEEX applies a new trim at most once per second of output
    // (each change recomputes its filter); the first one at once.
    void setTrimPpm(int32_t ppm);
    int32_t trimPpm() const { return trim_ppm_; }

    bool passthrough() const { return passthrough_; }
    uint32_t srcRate() const { return src_rate_; }
    uint32_t dstRate() const { return dst_rate_; }

private:
    PcmResampler(const PcmResampler&);
    PcmResampler& operator=(const PcmResampler&);

    void emit(const int32_t* frame, std::vector<int16_t>& out) const;
    void updateStep();
    void processLinear(const int16_t* in, size_t in_frames, std::vector<int16_t>& out);
    void processSpeex(const int16_t* in, size_t in_frames, std::vector<int16_t>& out);
    void destroySpeex();
    bool applySpeexRatio();   // sets the speex ratio from the rates and trim; false on failure

    Engine   engine_;
    int      quality_;
    void*    spx_;            // SpeexResamplerState* (SPEEX, created on first use)
    int32_t  spx_trim_;       // trim the speex ratio was last set for
    uint64_t spx_since_;      // output frames since that ratio was set
    std::vector<int16_t> spx_buf_;
    uint64_t in_count_, out_count_;

    uint32_t src_rate_;
    uint32_t src_ch_;
    uint32_t dst_rate_;
    uint32_t dst_ch_;
    uint64_t step_q32_;      // source frames advanced per output frame (Q32.32: sub-ppm resolution for the trim)
    uint64_t phase_q32_;     // position between prev_ and the next input frame (Q32.32, < 1.0 between frames)
    int32_t  trim_ppm_;
    bool     have_prev_;
    bool     passthrough_;
    int32_t  prev_[8];
};

} // namespace nbt

#endif // PCM_UTILS_HPP
