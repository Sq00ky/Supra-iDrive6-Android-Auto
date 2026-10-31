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
    void clear();
    uint64_t droppedBytes() const;

private:
    PcmRing(const PcmRing&);
    PcmRing& operator=(const PcmRing&);

    std::vector<uint8_t> buf_;
    size_t capacity_;
    size_t head_;   // read index
    size_t used_;
    uint64_t dropped_;
    mutable pthread_mutex_t mutex_;
};

// Streaming linear-interpolation resampler + channel mapper for interleaved
// signed 16-bit little-endian PCM. State is kept between process() calls so
// consecutive chunks join without clicks.
class PcmResampler {
public:
    PcmResampler();

    void configure(uint32_t src_rate, uint32_t src_channels,
                   uint32_t dst_rate, uint32_t dst_channels);
    void reset();

    // Appends converted interleaved frames to `out`.
    void process(const int16_t* in, size_t in_frames, std::vector<int16_t>& out);

    // Byte-oriented convenience wrapper (len is truncated to whole frames).
    void processBytes(const uint8_t* in, size_t len, std::vector<uint8_t>& out);

    bool passthrough() const { return passthrough_; }
    uint32_t srcRate() const { return src_rate_; }
    uint32_t dstRate() const { return dst_rate_; }

private:
    void emit(const int32_t* frame, std::vector<int16_t>& out) const;

    uint32_t src_rate_;
    uint32_t src_ch_;
    uint32_t dst_rate_;
    uint32_t dst_ch_;
    uint32_t step_q16_;      // source frames advanced per output frame (Q16.16)
    uint32_t phase_q16_;     // position between prev_ and the next input frame
    bool     have_prev_;
    bool     passthrough_;
    int32_t  prev_[8];
};

} // namespace nbt

#endif // PCM_UTILS_HPP
