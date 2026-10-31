// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "pcm_utils.hpp"
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
      dropped_(0) {
    pthread_mutex_init(&mutex_, NULL);
}

PcmRing::~PcmRing() {
    pthread_mutex_destroy(&mutex_);
}

size_t PcmRing::write(const uint8_t* data, size_t len) {
    if (!data || len == 0) return 0;
    pthread_mutex_lock(&mutex_);

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

void PcmRing::clear() {
    pthread_mutex_lock(&mutex_);
    head_ = 0;
    used_ = 0;
    pthread_mutex_unlock(&mutex_);
}

uint64_t PcmRing::droppedBytes() const {
    pthread_mutex_lock(&mutex_);
    uint64_t d = dropped_;
    pthread_mutex_unlock(&mutex_);
    return d;
}

// ---------------------------------------------------------------------------
// PcmResampler
// ---------------------------------------------------------------------------

PcmResampler::PcmResampler()
    : src_rate_(48000), src_ch_(2), dst_rate_(48000), dst_ch_(2),
      step_q16_(65536), phase_q16_(0), have_prev_(false), passthrough_(true) {
    memset(prev_, 0, sizeof(prev_));
}

void PcmResampler::configure(uint32_t src_rate, uint32_t src_channels,
                             uint32_t dst_rate, uint32_t dst_channels) {
    src_rate_ = src_rate ? src_rate : 48000;
    dst_rate_ = dst_rate ? dst_rate : 48000;
    src_ch_ = src_channels < 1 ? 1 : (src_channels > 8 ? 8 : src_channels);
    dst_ch_ = dst_channels < 1 ? 1 : (dst_channels > 8 ? 8 : dst_channels);
    step_q16_ = static_cast<uint32_t>((static_cast<uint64_t>(src_rate_) << 16) / dst_rate_);
    passthrough_ = (src_rate_ == dst_rate_) && (src_ch_ == dst_ch_);
    reset();
}

void PcmResampler::reset() {
    phase_q16_ = 0;
    have_prev_ = false;
    memset(prev_, 0, sizeof(prev_));
}

void PcmResampler::emit(const int32_t* frame, std::vector<int16_t>& out) const {
    int32_t mapped[8];
    if (src_ch_ == dst_ch_) {
        for (uint32_t c = 0; c < dst_ch_; ++c) mapped[c] = frame[c];
    } else if (src_ch_ == 1) {
        for (uint32_t c = 0; c < dst_ch_; ++c) mapped[c] = frame[0];
    } else if (dst_ch_ == 1) {
        int32_t sum = 0;
        for (uint32_t c = 0; c < src_ch_; ++c) sum += frame[c];
        mapped[0] = sum / static_cast<int32_t>(src_ch_);
    } else {
        for (uint32_t c = 0; c < dst_ch_; ++c) mapped[c] = (c < src_ch_) ? frame[c] : 0;
    }
    for (uint32_t c = 0; c < dst_ch_; ++c) {
        int32_t v = mapped[c];
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        out.push_back(static_cast<int16_t>(v));
    }
}

void PcmResampler::process(const int16_t* in, size_t in_frames, std::vector<int16_t>& out) {
    if (!in || in_frames == 0) return;

    if (passthrough_) {
        out.insert(out.end(), in, in + in_frames * dst_ch_);
        return;
    }

    for (size_t f = 0; f < in_frames; ++f) {
        int32_t cur[8];
        for (uint32_t c = 0; c < src_ch_; ++c) cur[c] = in[f * src_ch_ + c];

        if (!have_prev_) {
            for (uint32_t c = 0; c < src_ch_; ++c) prev_[c] = cur[c];
            have_prev_ = true;
            phase_q16_ = 0;
        }

        while (phase_q16_ < 65536U) {
            int32_t interp[8];
            for (uint32_t c = 0; c < src_ch_; ++c) {
                int64_t d = static_cast<int64_t>(cur[c]) - prev_[c];
                interp[c] = prev_[c] + static_cast<int32_t>((d * static_cast<int64_t>(phase_q16_)) >> 16);
            }
            emit(interp, out);
            phase_q16_ += step_q16_;
        }
        phase_q16_ -= 65536U;
        for (uint32_t c = 0; c < src_ch_; ++c) prev_[c] = cur[c];
    }
}

void PcmResampler::processBytes(const uint8_t* in, size_t len, std::vector<uint8_t>& out) {
    size_t frame_bytes = static_cast<size_t>(src_ch_) * 2;
    size_t frames = len / frame_bytes;
    if (frames == 0) return;

    std::vector<int16_t> src(frames * src_ch_);
    memcpy(&src[0], in, frames * frame_bytes);   // PCM is little-endian on host and target

    std::vector<int16_t> dst;
    dst.reserve(frames * dst_ch_ * (static_cast<size_t>(dst_rate_) / src_rate_ + 1));
    process(&src[0], frames, dst);

    if (!dst.empty()) {
        size_t old = out.size();
        out.resize(old + dst.size() * 2);
        memcpy(&out[old], &dst[0], dst.size() * 2);
    }
}

} // namespace nbt
