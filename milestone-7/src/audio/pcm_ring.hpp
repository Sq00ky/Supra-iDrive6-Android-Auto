// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_PCM_RING_HPP
#define AUDIO_PCM_RING_HPP

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

} // namespace nbt

#endif // AUDIO_PCM_RING_HPP
