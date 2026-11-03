// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AA_MEDIA_MSGS_HPP
#define AA_MEDIA_MSGS_HPP

// Plain (pre-encryption) AA media-channel messages the head unit sends, with the layout Google's receiver library uses
// (desktop-head-unit r2.1, gal:: protobufs, decoded from its serialisers). Header-only, host-tested in
// tests/test_media_msgs.cpp. Each message = 2-byte big-endian id + protobuf body.

#include <stdint.h>
#include <vector>

namespace nbt {

inline void aaPutVarint(std::vector<uint8_t>& o, uint64_t v) {
    while (v >= 0x80) { o.push_back((uint8_t)(v | 0x80)); v >>= 7; }
    o.push_back((uint8_t)v);
}

// AudioUnderflowNotification, id 0x800b, on the audio sink's channel: { session = 1 }. The DHU sends it from its
// playback callback when the buffer runs dry while the stream is started, once per underflow (AudioSink::
// sendUnderflowNotification, called from AudioCallback::copyAudio).
inline std::vector<uint8_t> aaUnderflowNotification(uint32_t session) {
    std::vector<uint8_t> m;
    m.push_back(0x80);
    m.push_back(0x0B);
    m.push_back(0x08);
    aaPutVarint(m, session);
    return m;
}

} // namespace nbt

#endif // AA_MEDIA_MSGS_HPP
