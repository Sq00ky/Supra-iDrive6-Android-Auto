// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// BMW Gesture Control camera PDUs on UDP 30567 (HmiMain3D CTouchDataClient::processSocketStream FUN_028dbe30):
//   u32 ident (big-endian), u32 length (bytes after this field), u64 header, body.
// (header read by FUN_028d9f6c; processSocketStream drops a datagram shorter than 17 bytes.) One datagram can carry
// several PDUs back to back (car log G11 2026-10-29: 44 bytes = a 19-byte gesture PDU + a 25-byte 0xB5308006
// hand-coordinate PDU). Gesture PDUs 0xB5308005 / 0xB5098004 (FUN_028dba84) carry u8 gesture id, u8, u8 (car: the 2nd
// byte counts up while the driver circles). CFreespaceGestureHandler::handleGesture (log strings):
//   1, 2  ignored                3 ROTATE_CW  -> volume(2) = up      4 ROTATE_CCW -> volume(1) = down
//   (volume = a volume key press + release on the iDrive controller device m_pDevCcsTc, FUN_02393874(dev, dir, 3))
//   5 TWO  -> handleTwoGesture: SKIP_FORWARD (IEntSkipService) or MUTE (IHmiAudioService), set in iDrive
//   6 SWIPE_LEFT -> log only     7 SWIPE_RIGHT -> SwipeRightConsumer, else rejects / closes the popup on screen
//   8 POINT -> PointConsumer, else confirms the popup on screen
// The camera only reports what iDrive enabled (configureGestures FUN_00da50a0 -> SOME/IP 0xB5300005 to the camera's
// UDP 30501): mask 1 << (id - 1): 0x0C rotate, +0x10 TWO when coded, +0x40 swipe right / +0x80 point only while a
// popup or consumer can take them. Plain C++98, host-tested (tests/test_gesture_pdu.cpp).
#ifndef NBT_GESTURE_PDU_HPP
#define NBT_GESTURE_PDU_HPP

#include <stddef.h>
#include <stdint.h>

namespace nbt {

struct CamPdu {
    uint32_t ident;
    const unsigned char* body;   // after the 16-byte header
    uint32_t bodyLen;
};

inline uint32_t camBe32(const unsigned char* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

// Splits a datagram into PDUs; returns how many (stops at the first malformed one).
inline int splitCameraPdus(const unsigned char* b, size_t n, CamPdu* out, int max) {
    int k = 0;
    size_t off = 0;
    while (b && off + 16 <= n && k < max) {
        uint32_t len = camBe32(b + off + 4);
        if (len < 8 || (size_t)len > n - off - 8) break;
        out[k].ident = camBe32(b + off);
        out[k].body = b + off + 16;
        out[k].bodyLen = len - 8;
        ++k;
        off += 8 + (size_t)len;
    }
    return k;
}

inline bool isGesturePdu(const CamPdu& p) {
    return (p.ident == 0xB5308005u || p.ident == 0xB5098004u) && p.bodyLen >= 1;
}

inline const char* gestureName(unsigned id) {
    switch (id) {
    case 3: return "rotate clockwise (volume up)";
    case 4: return "rotate anticlockwise (volume down)";
    case 5: return "two fingers (next track or mute, as set in iDrive)";
    case 6: return "swipe left";
    case 7: return "swipe right (closes / rejects an iDrive popup)";
    case 8: return "point (confirms an iDrive popup)";
    default: return "unknown gesture";
    }
}

} // namespace nbt

#endif
