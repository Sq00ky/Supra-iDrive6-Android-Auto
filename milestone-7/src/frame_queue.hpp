// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_FRAME_QUEUE_HPP
#define NBT_FRAME_QUEUE_HPP

// The video queue between the session thread (push) and the decoder worker (pop). The caller holds the lock.
//
// It used to be a std::vector<VideoQueueItem>. With C++03 copy semantics every push copied the frame into the
// vector, every pop copied it back out, and erase(begin()) copy-assigned every frame still waiting, so a full queue cost
// around 15 deep copies of H.264 access units per frame, all under queue_mutex_, which the network thread needs. It is
// a std::list now and the frame bytes are swapped in and out: no frame is copied on its way through, and dropping one
// in the middle touches no other item.
//
// The drop policy is unchanged: at most `cap` frames; a frame arriving at a full queue evicts the oldest frame that is
// neither codec config nor a keyframe; if there is no such frame the new one is rejected. Codec config packets are
// never subject to the cap (postCodecConfig never was).

#include <stdint.h>
#include <stddef.h>
#include <list>
#include <vector>

namespace nbt {

struct VideoQueueItem {
    std::vector<uint8_t> data;
    uint64_t pts;
    bool is_config;
    bool is_idr;
    bool has_inline_config;
    size_t raw_size;
    size_t inline_config_size;
    VideoQueueItem() : pts(0), is_config(false), is_idr(false), has_inline_config(false), raw_size(0),
                       inline_config_size(0) {}
};

class FrameQueue {
public:
    enum PushResult {
        kQueued,                 // appended
        kQueuedDroppedOldest,    // appended; the oldest non-config, non-keyframe item made room (see lastDroppedSize())
        kRejectedFullOfConfig    // not appended: the queue is full and holds only config packets and keyframes
    };

    explicit FrameQueue(size_t cap = 15) : cap_(cap), size_(0), last_dropped_size_(0), last_drop_keyframe_behind_(false) {}

    // Appends `item`, taking its bytes: on return item.data is empty (the scalars are left as they were).
    PushResult push(VideoQueueItem& item) {
        PushResult r = kQueued;
        if (!item.is_config && size_ >= cap_) {
            std::list<VideoQueueItem>::iterator victim = items_.end();
            for (std::list<VideoQueueItem>::iterator it = items_.begin(); it != items_.end(); ++it) {
                if (!it->is_config && !it->is_idr) { victim = it; break; }   // never sacrifice a keyframe
            }
            if (victim == items_.end()) return kRejectedFullOfConfig;
            last_dropped_size_ = victim->data.size();
            // Every frame up to the next keyframe decodes against the one that is gone. A keyframe already queued
            // behind it repairs the picture by itself; without one the caller has to ask the phone for it.
            std::list<VideoQueueItem>::iterator behind = items_.erase(victim);
            last_drop_keyframe_behind_ = false;
            for (; behind != items_.end(); ++behind) {
                if (behind->is_idr) { last_drop_keyframe_behind_ = true; break; }
            }
            --size_;
            r = kQueuedDroppedOldest;
        }
        items_.push_back(VideoQueueItem());
        VideoQueueItem& slot = items_.back();
        slot.data.swap(item.data);
        slot.pts = item.pts;
        slot.is_config = item.is_config;
        slot.is_idr = item.is_idr;
        slot.has_inline_config = item.has_inline_config;
        slot.raw_size = item.raw_size;
        slot.inline_config_size = item.inline_config_size;
        ++size_;
        return r;
    }

    // Moves the oldest item into `out` (its previous bytes are discarded). false when the queue is empty.
    bool pop(VideoQueueItem& out) {
        if (items_.empty()) return false;
        VideoQueueItem& front = items_.front();
        out.data.swap(front.data);
        out.pts = front.pts;
        out.is_config = front.is_config;
        out.is_idr = front.is_idr;
        out.has_inline_config = front.has_inline_config;
        out.raw_size = front.raw_size;
        out.inline_config_size = front.inline_config_size;
        items_.pop_front();
        --size_;
        return true;
    }

    void clear() { items_.clear(); size_ = 0; }
    bool empty() const { return size_ == 0; }
    size_t size() const { return size_; }
    size_t capacity() const { return cap_; }
    size_t lastDroppedSize() const { return last_dropped_size_; }
    // After a push that returned kQueuedDroppedOldest: true if a keyframe was queued behind the dropped frame.
    bool lastDropKeyframeBehind() const { return last_drop_keyframe_behind_; }
    // True if any queued frame is a keyframe (the decoder lost a frame it was handed: is a repair already waiting?).
    bool hasKeyframe() const {
        for (std::list<VideoQueueItem>::const_iterator it = items_.begin(); it != items_.end(); ++it) {
            if (it->is_idr) return true;
        }
        return false;
    }

private:
    std::list<VideoQueueItem> items_;
    size_t cap_;
    size_t size_;                // std::list::size() walks the list on GCC 4.4
    size_t last_dropped_size_;
    bool last_drop_keyframe_behind_;
};

} // namespace nbt

#endif // NBT_FRAME_QUEUE_HPP
