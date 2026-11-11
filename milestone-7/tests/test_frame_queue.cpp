// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for src/frame_queue.hpp: the video queue's cap, its drop policy, FIFO order, and that frame bytes are
// handed through without being copied.
#include "frame_queue.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
using namespace nbt;

static VideoQueueItem frame(size_t n, uint8_t fill, uint64_t pts, bool idr = false, bool config = false) {
    VideoQueueItem f;
    f.data.assign(n, fill);
    f.pts = pts;
    f.is_idr = idr;
    f.is_config = config;
    f.raw_size = n;
    return f;
}

int main() {
    // One frame in, the same bytes out: the buffer the session thread filled is the one the worker gets.
    {
        FrameQueue q(15);
        assert(q.empty() && q.size() == 0 && q.capacity() == 15);
        VideoQueueItem in = frame(100, 0xAB, 7);
        const uint8_t* buf = &in.data[0];
        assert(q.push(in) == FrameQueue::kQueued);
        assert(in.data.empty());                       // taken, not copied
        assert(q.size() == 1 && !q.empty());
        VideoQueueItem out;
        out.data.assign(5, 0);                         // leftovers from a previous pop are discarded
        assert(q.pop(out));
        assert(&out.data[0] == buf);                   // zero-copy: same heap block
        assert(out.data.size() == 100 && out.data[99] == 0xAB && out.pts == 7 && out.raw_size == 100);
        assert(q.empty());
        assert(!q.pop(out));                           // empty: nothing to pop
    }
    printf("zero copy: ok\n");

    // FIFO order through a full queue, every item by identity.
    {
        FrameQueue q(15);
        const uint8_t* bufs[15];
        for (int i = 0; i < 15; ++i) {
            VideoQueueItem f = frame(10 + i, (uint8_t)i, i);
            bufs[i] = &f.data[0];
            assert(q.push(f) == FrameQueue::kQueued);
        }
        assert(q.size() == 15);
        for (int i = 0; i < 15; ++i) {
            VideoQueueItem out;
            assert(q.pop(out));
            assert(&out.data[0] == bufs[i] && out.pts == (uint64_t)i && out.data.size() == (size_t)(10 + i));
        }
        assert(q.empty());
    }
    printf("fifo: ok\n");

    // Full of plain frames: the 16th evicts the oldest, order is kept, the eviction is reported with its size.
    {
        FrameQueue q(15);
        for (int i = 0; i < 15; ++i) { VideoQueueItem f = frame(10 + i, 0, i); assert(q.push(f) == FrameQueue::kQueued); }
        VideoQueueItem f = frame(99, 1, 15);
        assert(q.push(f) == FrameQueue::kQueuedDroppedOldest);
        assert(f.data.empty());
        assert(q.lastDroppedSize() == 10);             // frame 0 was 10 bytes
        assert(q.size() == 15);
        VideoQueueItem out;
        assert(q.pop(out) && out.pts == 1);            // frame 0 gone, frame 1 is now first
        for (int i = 2; i <= 15; ++i) { assert(q.pop(out) && out.pts == (uint64_t)i); }
        assert(q.empty());
    }
    printf("drop oldest: ok\n");

    // A keyframe at the front is never the victim: the first plain frame behind it goes instead.
    {
        FrameQueue q(15);
        for (int i = 0; i < 15; ++i) { VideoQueueItem f = frame(20 + i, 0, i, i == 0); assert(q.push(f) == FrameQueue::kQueued); }
        VideoQueueItem f = frame(99, 1, 15);
        assert(q.push(f) == FrameQueue::kQueuedDroppedOldest);
        assert(q.lastDroppedSize() == 21);             // frame 1
        VideoQueueItem out;
        assert(q.pop(out) && out.pts == 0 && out.is_idr);
        assert(q.pop(out) && out.pts == 2);
    }
    printf("keyframes kept: ok\n");

    // Nothing droppable (all keyframes / config): the new frame is rejected and its bytes are left with the caller.
    {
        FrameQueue q(15);
        for (int i = 0; i < 15; ++i) {
            VideoQueueItem f = frame(8, 0, i, (i % 2) == 0, (i % 2) == 1);
            assert(q.push(f) == FrameQueue::kQueued);
        }
        VideoQueueItem f = frame(99, 1, 15);
        assert(q.push(f) == FrameQueue::kRejectedFullOfConfig);
        assert(f.data.size() == 99);                   // not taken
        assert(q.size() == 15);
    }
    printf("reject when nothing droppable: ok\n");

    // Codec config is never subject to the cap (postCodecConfig never checked it).
    {
        FrameQueue q(15);
        for (int i = 0; i < 15; ++i) { VideoQueueItem f = frame(8, 0, i); assert(q.push(f) == FrameQueue::kQueued); }
        VideoQueueItem cfg = frame(30, 0x67, 0, false, true);
        assert(q.push(cfg) == FrameQueue::kQueued);
        assert(q.size() == 16);
        VideoQueueItem f = frame(99, 1, 16);           // a plain frame now evicts the oldest plain one, not the config
        assert(q.push(f) == FrameQueue::kQueuedDroppedOldest);
        assert(q.size() == 16);
        VideoQueueItem out;
        for (int i = 1; i < 15; ++i) assert(q.pop(out) && out.pts == (uint64_t)i && !out.is_config);
        assert(q.pop(out) && out.is_config && out.data.size() == 30);
        assert(q.pop(out) && out.pts == 16);
        assert(q.empty());
    }
    printf("config bypasses cap: ok\n");

    // After a drop the caller must know whether a keyframe is already queued BEHIND the dropped frame (it repairs the
    // picture by itself) or whether the phone has to be asked for one. A keyframe AHEAD of the victim does not count:
    // the frames after the victim decode against it, not against a later keyframe.
    {
        // no keyframe anywhere: ask
        FrameQueue q(15);
        assert(!q.hasKeyframe());
        for (int i = 0; i < 15; ++i) { VideoQueueItem f = frame(8, 0, i); q.push(f); }
        assert(!q.hasKeyframe());
        VideoQueueItem f = frame(8, 0, 15);
        assert(q.push(f) == FrameQueue::kQueuedDroppedOldest && !q.lastDropKeyframeBehind());
    }
    {
        // a keyframe queued behind the victim: no need to ask
        FrameQueue q(15);
        for (int i = 0; i < 15; ++i) { VideoQueueItem f = frame(8, 0, i, i == 9); q.push(f); }
        assert(q.hasKeyframe());
        VideoQueueItem f = frame(8, 0, 15);
        assert(q.push(f) == FrameQueue::kQueuedDroppedOldest && q.lastDropKeyframeBehind());
    }
    {
        // the only keyframe is AHEAD of the victim (it is kept, the victim is the first plain frame after it): ask
        FrameQueue q(15);
        for (int i = 0; i < 15; ++i) { VideoQueueItem f = frame(8, 0, i, i == 0); q.push(f); }
        VideoQueueItem f = frame(8, 0, 15);
        assert(q.push(f) == FrameQueue::kQueuedDroppedOldest && !q.lastDropKeyframeBehind());
        assert(q.hasKeyframe());
    }
    {
        // the flag follows the latest drop, and a keyframe popped out of the queue is no longer counted
        FrameQueue q(15);
        for (int i = 0; i < 15; ++i) { VideoQueueItem f = frame(8, 0, i, i == 9); q.push(f); }
        VideoQueueItem f = frame(8, 0, 15);
        assert(q.push(f) == FrameQueue::kQueuedDroppedOldest && q.lastDropKeyframeBehind());
        VideoQueueItem out;
        while (q.pop(out) && !out.is_idr) { }          // drain up to and including the keyframe
        assert(!q.hasKeyframe());
        for (int i = 0; i < 20; ++i) { VideoQueueItem g = frame(8, 0, 100 + i); q.push(g); }
        assert(!q.hasKeyframe() && !q.lastDropKeyframeBehind());
    }
    printf("keyframe behind a drop: ok\n");

    // clear() empties it and the next push starts over.
    {
        FrameQueue q(3);
        for (int i = 0; i < 3; ++i) { VideoQueueItem f = frame(8, 0, i); q.push(f); }
        q.clear();
        assert(q.empty() && q.size() == 0);
        VideoQueueItem f = frame(8, 0, 9);
        assert(q.push(f) == FrameQueue::kQueued && q.size() == 1);
        VideoQueueItem out;
        assert(q.pop(out) && out.pts == 9);
    }
    printf("clear: ok\n");

    printf("test_frame_queue OK\n");
    return 0;
}
