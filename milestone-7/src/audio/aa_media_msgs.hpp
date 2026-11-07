// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AA_MEDIA_MSGS_HPP
#define AA_MEDIA_MSGS_HPP

// Plain (pre-encryption) AA media-channel messages the head unit sends, with the layout Google's receiver library uses
// (desktop-head-unit r2.1, gal:: protobufs, decoded from its serialisers). Header-only, host-tested in
// tests/test_media_msgs.cpp. Each message = 2-byte big-endian id + protobuf body.

#include "aa_proto.hpp"

#include <stdint.h>
#include <stddef.h>
#include <vector>

namespace nbt {

enum { AA_MSG_MEDIA_ACK = 0x8004, AA_MSG_AUDIO_UNDERFLOW = 0x800B };

// AV media ack (gal::Ack), id 0x8004, on the stream's channel: { session = 1, ack = 2 (frames acked),
// receive_time = 3 (repeated uint64, not packed: one 0x18 tag per value) }. The DHU's audio sinks send one ack per
// frame carrying the time that frame arrived (getCurrentNanos(): gettimeofday() in ns); its video sink sends no times.
// rx_ns = NULL / n = 0 leaves field 3 out.
inline std::vector<uint8_t> aaMediaAck(uint32_t session, uint32_t count, const uint64_t* rx_ns, size_t n) {
    enum { F_SESSION = 1, F_ACK = 2, F_RECEIVE_TIME = 3 };
    proto::ProtoWriter w(AA_MSG_MEDIA_ACK);
    w.unsignedInteger(F_SESSION, session);
    w.unsignedInteger(F_ACK, count);
    for (size_t i = 0; rx_ns && i < n; ++i) w.unsignedInteger(F_RECEIVE_TIME, rx_ns[i]);
    return w.bytes();
}

// AudioUnderflowNotification, id 0x800b, on the audio sink's channel: { session = 1 }. The DHU sends it from its
// playback callback when the buffer runs dry while the stream is started, once per underflow (AudioSink::
// sendUnderflowNotification, called from AudioCallback::copyAudio).
inline std::vector<uint8_t> aaUnderflowNotification(uint32_t session) {
    enum { F_SESSION = 1 };
    proto::ProtoWriter w(AA_MSG_AUDIO_UNDERFLOW);
    w.unsignedInteger(F_SESSION, session);
    return w.bytes();
}

} // namespace nbt

#endif // AA_MEDIA_MSGS_HPP
