// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio/pcm_ring.hpp"
#include <string.h>

namespace nbt {

// ---------------------------------------------------------------------------
// PcmRing
// ---------------------------------------------------------------------------

PcmRing::PcmRing(size_t capacity)
    : buf_(capacity ? capacity : 1),
      capacity_(capacity ? capacity : 1),
      head_(0),
      used_(0),
      dropped_(0),
      written_(0) {
    pthread_mutex_init(&mutex_, NULL);
}

PcmRing::~PcmRing() {
    pthread_mutex_destroy(&mutex_);
}

size_t PcmRing::write(const uint8_t* data, size_t len) {
    if (!data || len == 0) return 0;
    pthread_mutex_lock(&mutex_);
    written_ += len;

    size_t dropped = 0;

    // A single write larger than the ring keeps only its newest bytes.
    if (len > capacity_) {
        dropped += len - capacity_ + used_;
        data += len - capacity_;
        len = capacity_;
        head_ = 0;
        used_ = 0;
    } else if (len > capacity_ - used_) {
        size_t need = len - (capacity_ - used_);
        head_ = (head_ + need) % capacity_;
        used_ -= need;
        dropped += need;
    }

    size_t tail = (head_ + used_) % capacity_;
    size_t first = capacity_ - tail;
    if (first > len) first = len;
    memcpy(&buf_[tail], data, first);
    if (len > first) memcpy(&buf_[0], data + first, len - first);
    used_ += len;

    dropped_ += dropped;
    pthread_mutex_unlock(&mutex_);
    return dropped;
}

size_t PcmRing::read(uint8_t* out, size_t len) {
    if (!out || len == 0) return 0;
    pthread_mutex_lock(&mutex_);

    size_t n = len < used_ ? len : used_;
    size_t first = capacity_ - head_;
    if (first > n) first = n;
    if (n > 0) {
        memcpy(out, &buf_[head_], first);
        if (n > first) memcpy(out + first, &buf_[0], n - first);
        head_ = (head_ + n) % capacity_;
        used_ -= n;
    }

    pthread_mutex_unlock(&mutex_);
    return n;
}

size_t PcmRing::size() const {
    pthread_mutex_lock(&mutex_);
    size_t n = used_;
    pthread_mutex_unlock(&mutex_);
    return n;
}

size_t PcmRing::clear() {
    pthread_mutex_lock(&mutex_);
    const size_t n = used_;
    head_ = 0;
    used_ = 0;
    pthread_mutex_unlock(&mutex_);
    return n;
}

uint64_t PcmRing::droppedBytes() const {
    pthread_mutex_lock(&mutex_);
    uint64_t d = dropped_;
    pthread_mutex_unlock(&mutex_);
    return d;
}

uint64_t PcmRing::writtenBytes() const {
    pthread_mutex_lock(&mutex_);
    uint64_t w = written_;
    pthread_mutex_unlock(&mutex_);
    return w;
}

} // namespace nbt
