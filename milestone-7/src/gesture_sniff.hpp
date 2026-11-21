// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// Two-finger gesture -> next track in native audio (issue #163).
// iDrive acts on the camera's two-finger gesture (id 5, gesture_pdu.hpp) through IEntSkipService, which skips the
// ACTIVE audio source. In Bluetooth mode that is iDrive's Bluetooth player, so the phone skips; in native audio our
// sound plays through iDrive's browser source, which has no skip device behind it, so the gesture does nothing.
// The fix here does not touch iDrive: a receive-only BPF tap (same mechanism as cluster/ric_media, never sends)
// sees the camera's UDP datagrams to port 30567 while iDrive still gets every one of them, splits them into PDUs,
// and a NEW two-finger gesture (not the camera repeating it while the hand is held) is handed to a callback; the
// daemon sends AA key 87 (next) to the phone when the "Two-finger gesture: next track" setting is on.
// The camera cannot tell us whether the driver set two fingers to MUTE in iDrive: that is what the setting is for.
// Pure parts (filter, frame parsing, repeat detection) are header-only and host-tested (tests/test_gesture_sniff.cpp).
#ifndef NBT_GESTURE_SNIFF_HPP
#define NBT_GESTURE_SNIFF_HPP

#include "cluster/ric_sniff.hpp"
#include "gesture_pdu.hpp"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace nbt {
namespace gesture {

enum { kCameraPort = 30567, kFilterLen = 15, kRepeatGapMs = 800 };

// Kernel filter: IPv4 UDP to `port`, untagged or one VLAN tag, IPv4 header without options (the camera's are plain).
inline void udpPortFilter(uint16_t port, riclist::FilterInsn* f) {
    using riclist::filterStmt;
    using riclist::filterJeq;
    f[0] = filterStmt(0x28, 12);            // A = ethertype
    f[1] = filterJeq(0x0800, 1, 2, 6);
    f[2] = filterStmt(0x30, 23);            // A = IP protocol
    f[3] = filterJeq(17, 3, 4, 14);
    f[4] = filterStmt(0x28, 36);            // A = UDP destination port
    f[5] = filterJeq(port, 5, 13, 14);
    f[6] = filterJeq(0x8100, 6, 7, 14);     // one VLAN tag: the same checks 4 bytes further on
    f[7] = filterStmt(0x28, 16);
    f[8] = filterJeq(0x0800, 8, 9, 14);
    f[9] = filterStmt(0x30, 27);
    f[10] = filterJeq(17, 10, 11, 14);
    f[11] = filterStmt(0x28, 40);
    f[12] = filterJeq(port, 12, 13, 14);
    f[13] = filterStmt(0x06, 0xFFFF);       // take the whole packet
    f[14] = filterStmt(0x06, 0);            // drop
}

// The UDP payload of an Ethernet frame (untagged or one VLAN tag, any IPv4 header length). False if it is not IPv4
// UDP or is cut short. *dport = the destination port.
inline bool udpPayload(const uint8_t* fr, size_t n, const uint8_t** pay, size_t* len, uint16_t* dport) {
    size_t o = 12;
    if (n < o + 2) return false;
    uint16_t et = (uint16_t)(fr[o] << 8 | fr[o + 1]);
    if (et == 0x8100) {
        o += 4;
        if (n < o + 2) return false;
        et = (uint16_t)(fr[o] << 8 | fr[o + 1]);
    }
    if (et != 0x0800) return false;
    o += 2;                                  // IPv4 header
    if (n < o + 20 || (fr[o] >> 4) != 4 || fr[o + 9] != 17) return false;
    const size_t ihl = (size_t)(fr[o] & 0x0F) * 4;
    if (ihl < 20 || n < o + ihl + 8) return false;
    o += ihl;                                // UDP header
    *dport = (uint16_t)(fr[o + 2] << 8 | fr[o + 3]);
    size_t ulen = (size_t)(fr[o + 4] << 8 | fr[o + 5]);
    if (ulen < 8) return false;
    ulen -= 8;
    o += 8;
    if (ulen > n - o) ulen = n - o;          // a capture can be shorter than the datagram claims
    *pay = fr + o;
    *len = ulen;
    return true;
}

// The camera repeats a gesture PDU while the hand stays in the pose: a PDU of the same gesture within kRepeatGapMs of
// the previous gesture PDU is the same gesture, anything else starts a new one (same rule as touch_probe's log).
class Runs {
public:
    Runs() : last_(-1), at_(0), any_(false) {}
    bool fresh(int id, uint32_t ms) {
        const bool repeat = any_ && id == last_ && (uint32_t)(ms - at_) < (uint32_t)kRepeatGapMs;
        last_ = id;
        at_ = ms;
        any_ = true;
        return !repeat;
    }
private:
    int last_;
    uint32_t at_;
    bool any_;
};

typedef void (*GestureFn)(int id, void* user);

// The capture thread. start() returns at once; the thread opens the BPF device (retrying every 30 s if that fails)
// and calls fn(id, user) once per new gesture, from its own thread.
class Sniffer {
public:
    Sniffer();
    ~Sniffer();
    bool start(GestureFn fn, void* user);
    void stop();
private:
    static void* threadMain(void* self);
    void run();
    GestureFn fn_;
    void* user_;
    volatile bool running_;
    bool started_;
    pthread_t thread_;
    std::string if_;
};

}  // namespace gesture
}  // namespace nbt

#endif
