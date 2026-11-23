// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// aa_frames: splitting an outgoing message into frames and building the frame header. Pure, no I/O.
#ifndef AA_FRAMES_HPP
#define AA_FRAMES_HPP

#include <stdint.h>
#include <stddef.h>
#include <vector>

namespace nbt {

// Plaintext per frame. The phone buffers at most 16384 encrypted bytes per frame; like its own frames, header +
// plaintext stays at 16128.
enum { kAaMaxFramePayload = 16120 };

// Frame header flags: the low two bits are the frame type.
enum { kAaFrameMiddle = 0, kAaFrameFirst = 1, kAaFrameLast = 2, kAaFrameBulk = 3, kAaFrameEncrypted = 0x08 };

struct AaFragment {
    size_t offset;
    size_t length;
    uint8_t type;   // kAaFrameMiddle .. kAaFrameBulk
};

inline std::vector<AaFragment> aaSplit(size_t total, size_t maxChunk) {
    std::vector<AaFragment> out;
    if (total == 0 || maxChunk == 0) return out;
    for (size_t off = 0; off < total; off += maxChunk) {
        AaFragment f;
        f.offset = off;
        f.length = total - off < maxChunk ? total - off : maxChunk;
        const bool first = off == 0;
        const bool last = off + f.length >= total;
        f.type = first ? (last ? kAaFrameBulk : kAaFrameFirst) : (last ? kAaFrameLast : kAaFrameMiddle);
        out.push_back(f);
    }
    return out;
}

// 4 bytes; a FIRST frame adds the big-endian total size.
inline std::vector<uint8_t> aaFrameHeader(uint8_t channel, uint8_t flags, uint16_t length, uint32_t total) {
    std::vector<uint8_t> h;
    h.push_back(channel);
    h.push_back(flags);
    h.push_back((uint8_t)(length >> 8));
    h.push_back((uint8_t)length);
    if ((flags & 3) == kAaFrameFirst) {
        h.push_back((uint8_t)(total >> 24));
        h.push_back((uint8_t)(total >> 16));
        h.push_back((uint8_t)(total >> 8));
        h.push_back((uint8_t)total);
    }
    return h;
}

}  // namespace nbt

#endif
