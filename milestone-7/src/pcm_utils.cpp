// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "pcm_utils.hpp"
#include "nbt_speex.h"
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

uint32_t pickDeviceRate(uint32_t want, uint32_t force, int min_rate, int max_rate) {
    if (force) return force;
    if (max_rate > 0 && ((int64_t)want > max_rate || (min_rate > 0 && (int64_t)want < min_rate)))
        return (uint32_t)max_rate;
    return want;
}

size_t pcmChunkBytes(uint32_t rate, uint32_t channels, uint32_t ms, size_t cap) {
    const size_t frame = (size_t)(channels ? channels : 1) * 2u;
    if (cap < frame) return 0;
    size_t frames = (size_t)((uint64_t)rate * ms / 1000u);
    if (frames < 1) frames = 1;
    const size_t max_frames = cap / frame;
    if (frames > max_frames) frames = max_frames;
    return frames * frame;
}

// ---------------------------------------------------------------------------
// PcmResampler
// ---------------------------------------------------------------------------

PcmResampler::PcmResampler()
    : engine_(LINEAR), quality_(5), spx_(NULL), spx_trim_(0), spx_since_(0), in_count_(0), out_count_(0),
      src_rate_(48000), src_ch_(2), dst_rate_(48000), dst_ch_(2),
      step_q32_(1ull << 32), phase_q32_(0), trim_ppm_(0), have_prev_(false), passthrough_(true) {
    memset(prev_, 0, sizeof(prev_));
}

PcmResampler::~PcmResampler() { destroySpeex(); }

void PcmResampler::destroySpeex() {
    if (spx_) nbt_speex_resampler_destroy(static_cast<SpeexResamplerState*>(spx_));
    spx_ = NULL;
}

void PcmResampler::setEngine(Engine e, int quality) {
    if (quality < 0) quality = 0;
    if (quality > 10) quality = 10;
    if (e == engine_ && quality == quality_) return;
    engine_ = e;
    quality_ = quality;
    destroySpeex();   // re-created on the next process() with the new settings
    updateStep();
    reset();
}

// in/out ratio = src/dst * (1 + trim): reduced to 32-bit numerator / denominator (speex wants spx_uint32_t).
static uint64_t gcd64(uint64_t a, uint64_t b) { while (b) { const uint64_t t = a % b; a = b; b = t; } return a; }

bool PcmResampler::applySpeexRatio() {
    uint64_t num = (uint64_t)src_rate_ * (uint64_t)(1000000 + trim_ppm_);
    uint64_t den = (uint64_t)dst_rate_ * 1000000u;
    const uint64_t g = gcd64(num, den);
    if (g > 1) { num /= g; den /= g; }
    while (num > 0xFFFFFFFFull || den > 0xFFFFFFFFull) { num >>= 1; den >>= 1; }   // < 1e-9 of ratio error
    if (!num || !den) return false;
    const int err = nbt_speex_resampler_set_rate_frac(static_cast<SpeexResamplerState*>(spx_), (spx_uint32_t)num,
                                                      (spx_uint32_t)den, src_rate_, dst_rate_);
    spx_trim_ = trim_ppm_;
    spx_since_ = 0;
    return err == RESAMPLER_ERR_SUCCESS;
}

void PcmResampler::updateStep() {
    const uint64_t base = ((uint64_t)src_rate_ << 32) / dst_rate_;
    step_q32_ = base * (uint64_t)(1000000 + trim_ppm_) / 1000000u;   // base < 2^36, x ~1e6 fits in 64 bits
    // SPEEX stays filtering once it has started (a later return to 1:1 and no trim): switching back to copying
    // would jump by the filter's latency.
    const bool pass = (src_rate_ == dst_rate_) && (src_ch_ == dst_ch_) && trim_ppm_ == 0 && !spx_;
    if (pass != passthrough_) {   // switching between copying and interpolating: start interpolation afresh
        have_prev_ = false;
        phase_q32_ = 0;
    }
    passthrough_ = pass;
}

void PcmResampler::setTrimPpm(int32_t ppm) {
    if (ppm > 20000) ppm = 20000;
    if (ppm < -20000) ppm = -20000;
    trim_ppm_ = ppm;
    updateStep();
}

void PcmResampler::configure(uint32_t src_rate, uint32_t src_channels,
                             uint32_t dst_rate, uint32_t dst_channels) {
    src_rate_ = src_rate ? src_rate : 48000;
    dst_rate_ = dst_rate ? dst_rate : 48000;
    src_ch_ = src_channels < 1 ? 1 : (src_channels > 8 ? 8 : src_channels);
    dst_ch_ = dst_channels < 1 ? 1 : (dst_channels > 8 ? 8 : dst_channels);
    destroySpeex();   // new rates / channels: re-created on the next process()
    updateStep();   // keeps trim_ppm_
    passthrough_ = (src_rate_ == dst_rate_) && (src_ch_ == dst_ch_) && trim_ppm_ == 0;
    reset();
}

void PcmResampler::reset() {
    phase_q32_ = 0;
    have_prev_ = false;
    memset(prev_, 0, sizeof(prev_));
    if (spx_) {
        SpeexResamplerState* st = static_cast<SpeexResamplerState*>(spx_);
        nbt_speex_resampler_reset_mem(st);
        nbt_speex_resampler_skip_zeros(st);   // no leading run of zeros (the filter's latency) after a restart
    }
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
    in_count_ += in_frames;
    const size_t before = out.size();

    if (passthrough_) {
        out.insert(out.end(), in, in + in_frames * dst_ch_);
    } else if (engine_ == SPEEX) {
        processSpeex(in, in_frames, out);
    } else {
        processLinear(in, in_frames, out);
    }
    out_count_ += (out.size() - before) / dst_ch_;
}

void PcmResampler::processSpeex(const int16_t* in, size_t in_frames, std::vector<int16_t>& out) {
    if (!spx_) {
        int err = 0;
        spx_ = nbt_speex_resampler_init(src_ch_, src_rate_, dst_rate_, quality_, &err);
        if (!spx_ || err != RESAMPLER_ERR_SUCCESS || !applySpeexRatio()) {
            destroySpeex();
            engine_ = LINEAR;   // can't set it up: carry on with the linear engine
            processLinear(in, in_frames, out);
            return;
        }
        nbt_speex_resampler_skip_zeros(static_cast<SpeexResamplerState*>(spx_));
        updateStep();   // passthrough_ off from now on
    } else if (spx_trim_ != trim_ppm_ && spx_since_ >= dst_rate_) {
        applySpeexRatio();   // at most once per second of output
    }
    SpeexResamplerState* st = static_cast<SpeexResamplerState*>(spx_);
    const spx_uint32_t chunk = 1024;   // output frames per call
    if (spx_buf_.size() < chunk * src_ch_) spx_buf_.resize(chunk * src_ch_);
    const int16_t* ip = in;
    size_t left = in_frames;
    while (left > 0) {
        spx_uint32_t in_len = (spx_uint32_t)left;
        spx_uint32_t out_len = chunk;
        nbt_speex_resampler_process_interleaved_int(st, ip, &in_len, &spx_buf_[0], &out_len);
        for (spx_uint32_t f = 0; f < out_len; ++f) {
            int32_t frame[8];
            for (uint32_t c = 0; c < src_ch_; ++c) frame[c] = spx_buf_[f * src_ch_ + c];
            emit(frame, out);
        }
        spx_since_ += out_len;
        ip += (size_t)in_len * src_ch_;
        left -= in_len;
        if (in_len == 0 && out_len == 0) break;   // no progress: never spin
    }
}

void PcmResampler::processLinear(const int16_t* in, size_t in_frames, std::vector<int16_t>& out) {
    for (size_t f = 0; f < in_frames; ++f) {
        int32_t cur[8];
        for (uint32_t c = 0; c < src_ch_; ++c) cur[c] = in[f * src_ch_ + c];

        if (!have_prev_) {
            for (uint32_t c = 0; c < src_ch_; ++c) prev_[c] = cur[c];
            have_prev_ = true;
            phase_q32_ = 0;
        }

        const uint64_t one = 1ull << 32;
        while (phase_q32_ < one) {
            int32_t interp[8];
            for (uint32_t c = 0; c < src_ch_; ++c) {
                int64_t d = static_cast<int64_t>(cur[c]) - prev_[c];   // |d| < 2^16, phase < 2^32: fits
                interp[c] = prev_[c] + static_cast<int32_t>((d * static_cast<int64_t>(phase_q32_)) >> 32);
            }
            emit(interp, out);
            phase_q32_ += step_q32_;
        }
        phase_q32_ -= one;
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
