// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_FRAME_QUEUE_HPP
#define NBT_FRAME_QUEUE_HPP

// Fixed-slot queue between the session thread and decoder worker. The caller holds the lock.
// Frame byte vectors are swapped into and out of preallocated slots: no per-frame queue-node allocation or byte copy.
// The queue grows its slot array only if uncapped codec-config entries fill all slots.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>

namespace nbt {

struct VideoQueueItem {
    enum { kInlineConfigCapacity = 512 };
    std::vector<uint8_t> data;
    uint8_t inline_config_data[kInlineConfigCapacity];
    std::vector<uint8_t> large_inline_config;
    uint64_t pts;
    size_t data_offset;
    size_t data_length;
    bool is_config;
    bool is_idr;
    bool has_inline_config;
    size_t raw_size;
    size_t inline_config_size;
    VideoQueueItem() : pts(0), data_offset(0), data_length(0), is_config(false), is_idr(false),
                       has_inline_config(false), raw_size(0), inline_config_size(0) {
        memset(inline_config_data, 0, sizeof(inline_config_data));
    }

    uint8_t* inlineConfigData() {
        return inline_config_size <= kInlineConfigCapacity ? inline_config_data : &large_inline_config[0];
    }
    const uint8_t* inlineConfigData() const {
        return inline_config_size <= kInlineConfigCapacity ? inline_config_data : &large_inline_config[0];
    }
};

class FrameQueue {
public:
    enum PushResult {
        kQueued,
        kQueuedDroppedOldest,
        kRejectedFullOfConfig
    };

    explicit FrameQueue(size_t cap = 15)
        : slots_(cap ? cap : 1), cap_(cap ? cap : 1), head_(0), size_(0), last_dropped_size_(0),
          last_drop_keyframe_behind_(false) {}

    // Appends `item`, taking its data buffer. On rejection, item remains unchanged.
    PushResult push(VideoQueueItem& item) {
        PushResult result = kQueued;
        if (!item.is_config && size_ >= cap_) {
            size_t victim = size_;
            for (size_t i = 0; i < size_; ++i) {
                const VideoQueueItem& queued = at(i);
                if (!queued.is_config && !queued.is_idr) { victim = i; break; }
            }
            if (victim == size_) return kRejectedFullOfConfig;

            const VideoQueueItem& dropped = at(victim);
            last_dropped_size_ = effectiveLength(dropped);
            last_drop_keyframe_behind_ = false;
            for (size_t i = victim + 1; i < size_; ++i) {
                if (at(i).is_idr) { last_drop_keyframe_behind_ = true; break; }
            }
            eraseAt(victim);
            result = kQueuedDroppedOldest;
        }

        ensureFreeSlot();
        VideoQueueItem& slot = at(size_);
        copyScalars(item, slot);
        const size_t small_config = item.inline_config_size <= VideoQueueItem::kInlineConfigCapacity
                                        ? item.inline_config_size : VideoQueueItem::kInlineConfigCapacity;
        if (small_config) memcpy(slot.inline_config_data, item.inline_config_data, small_config);
        if (item.inline_config_size > VideoQueueItem::kInlineConfigCapacity) {
            slot.large_inline_config.swap(item.large_inline_config);
        } else {
            std::vector<uint8_t> empty;
            slot.large_inline_config.swap(empty);
        }
        slot.data.swap(item.data);
        item.data.clear();  // the caller retains an empty moved-from item even when the slot held a recycled buffer
        ++size_;
        return result;
    }

    // Moves the oldest item into `out`. Its previous data buffers are swapped back into the freed slot.
    bool pop(VideoQueueItem& out) {
        if (size_ == 0) return false;
        VideoQueueItem& front = slots_[head_];
        swapItems(front, out);
        head_ = (head_ + 1) % slots_.size();
        --size_;
        return true;
    }

    void clear() {
        for (size_t i = 0; i < slots_.size(); ++i) {
            VideoQueueItem empty;
            swapItems(slots_[i], empty);  // release frame/config storage on session end
        }
        head_ = 0;
        size_ = 0;
    }

    bool empty() const { return size_ == 0; }
    size_t size() const { return size_; }
    size_t capacity() const { return cap_; }
    size_t lastDroppedSize() const { return last_dropped_size_; }
    bool lastDropKeyframeBehind() const { return last_drop_keyframe_behind_; }
    bool hasKeyframe() const {
        for (size_t i = 0; i < size_; ++i) if (at(i).is_idr) return true;
        return false;
    }

private:
    static size_t effectiveLength(const VideoQueueItem& item) {
        if (item.data_length) return item.data_length;
        return item.data.size() >= item.data_offset ? item.data.size() - item.data_offset : 0;
    }

    static void copyScalars(const VideoQueueItem& from, VideoQueueItem& to) {
        to.pts = from.pts;
        to.data_offset = from.data_offset;
        to.data_length = from.data_length ? from.data_length : effectiveLength(from);
        to.is_config = from.is_config;
        to.is_idr = from.is_idr;
        to.has_inline_config = from.has_inline_config;
        to.raw_size = from.raw_size;
        to.inline_config_size = from.inline_config_size;
    }

    static void swapItems(VideoQueueItem& a, VideoQueueItem& b) {
        a.data.swap(b.data);
        const size_t a_size = a.inline_config_size;
        const size_t b_size = b.inline_config_size;
        const bool a_small = a_size <= VideoQueueItem::kInlineConfigCapacity;
        const bool b_small = b_size <= VideoQueueItem::kInlineConfigCapacity;
        if (a_small && b_small) {
            const size_t inline_bytes = a_size > b_size ? a_size : b_size;
            for (size_t i = 0; i < inline_bytes; ++i) {
                uint8_t byte = a.inline_config_data[i];
                a.inline_config_data[i] = b.inline_config_data[i];
                b.inline_config_data[i] = byte;
            }
        } else if (a_small) {
            if (a_size) memcpy(b.inline_config_data, a.inline_config_data, a_size);
        } else if (b_small && b_size) {
            memcpy(a.inline_config_data, b.inline_config_data, b_size);
        }
        a.large_inline_config.swap(b.large_inline_config);
        uint64_t u64 = a.pts; a.pts = b.pts; b.pts = u64;
        size_t z = a.data_offset; a.data_offset = b.data_offset; b.data_offset = z;
        z = a.data_length; a.data_length = b.data_length; b.data_length = z;
        bool flag = a.is_config; a.is_config = b.is_config; b.is_config = flag;
        flag = a.is_idr; a.is_idr = b.is_idr; b.is_idr = flag;
        flag = a.has_inline_config; a.has_inline_config = b.has_inline_config; b.has_inline_config = flag;
        z = a.raw_size; a.raw_size = b.raw_size; b.raw_size = z;
        z = a.inline_config_size; a.inline_config_size = b.inline_config_size; b.inline_config_size = z;
    }

    VideoQueueItem& at(size_t logical) { return slots_[(head_ + logical) % slots_.size()]; }
    const VideoQueueItem& at(size_t logical) const { return slots_[(head_ + logical) % slots_.size()]; }

    void ensureFreeSlot() {
        if (size_ < slots_.size()) return;
        std::vector<VideoQueueItem> grown(slots_.size() * 2);
        for (size_t i = 0; i < size_; ++i) swapItems(at(i), grown[i]);
        slots_.swap(grown);
        head_ = 0;
    }

    void eraseAt(size_t logical) {
        for (size_t i = logical; i + 1 < size_; ++i) swapItems(at(i), at(i + 1));
        VideoQueueItem empty;
        swapItems(at(size_ - 1), empty);  // release the dropped frame's storage
        --size_;
    }

    std::vector<VideoQueueItem> slots_;
    size_t cap_;
    size_t head_;
    size_t size_;                // includes codec-config entries; plain frames remain capped by cap_
    size_t last_dropped_size_;
    bool last_drop_keyframe_behind_;
};

} // namespace nbt

#endif // NBT_FRAME_QUEUE_HPP
