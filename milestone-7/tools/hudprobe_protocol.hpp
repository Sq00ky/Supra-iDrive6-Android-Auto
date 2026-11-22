// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef HUDPROBE_PROTOCOL_HPP
#define HUDPROBE_PROTOCOL_HPP

#include "cluster/hudterm.hpp"

namespace nbt {
namespace hudprobe {

enum { kLastHandle = 0x40 };
enum SizeForm { kShort, kWithSelector, kForms };

// The two read-only ImageGetSize forms share an op and differ by a selector byte.
inline most::Msg sizeQuery(uint16_t host, uint8_t instance, uint16_t handle, SizeForm form, uint32_t tag) {
    most::Msg request = most::Msg();
    request.varTag = 1;
    request.dst = host;
    request.fblock = hudterm::kFBlock;
    request.inst = instance;
    request.fkt = hudterm::kGetSize;
    request.opType = most::kOpStartResultAck;
    request.hdrTag = tag;
    hudterm::putBe16(request.data, handle);
    if (form == kWithSelector) request.data.push_back(1);
    return request;
}

struct Size {
    unsigned width, height;
    bool answered;
    Size() : width(0), height(0), answered(false) {}
    bool present() const { return width != 0 && height != 0; }
};

inline bool sizeReply(const most::View& reply, unsigned& handle, SizeForm& form, Size& size) {
    if (reply.fkt != hudterm::kGetSize || reply.op != most::kOpResultAck || !reply.data) return false;
    if (reply.len == 6) form = kShort;
    else if (reply.len == 7 && reply.data[2] == 1) form = kWithSelector;
    else return false;
    handle = hudterm::rdBe16(reply.data);
    if (!handle || handle > kLastHandle) return false;
    const uint8_t* dimensions = reply.data + 2 + (form == kWithSelector ? 1 : 0);
    size.width = hudterm::rdBe16(dimensions);
    size.height = hudterm::rdBe16(dimensions + 2);
    size.answered = true;
    return true;
}

inline const char* itemRole(unsigned kind) {
    switch (kind) {
        case 2: return "unit text";
        case 3: return "street text";
        case 4: return "picture";
        case 5: return "distance text";
        case 6: return "distance bar";
        default: return "unknown";
    }
}

inline const char* layoutHint(bool lowPictures, bool highPictures) {
    if (lowPictures && highPictures) return "mixed signatures; compare the factory frame/item handles";
    if (highPictures) return "6WB-like (picture handle 0x1F or 0x20)";
    if (lowPictures) return "6WA/MINI-like (picture handle 0x05 or 0x06)";
    return "undetermined";
}

} // namespace hudprobe
} // namespace nbt

#endif