// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: the two-finger gesture capture's pure parts (src/gesture_sniff.hpp): the kernel filter run through a
// small BPF interpreter, UDP payload extraction, and the repeat rule.
#include "gesture_sniff.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
using namespace nbt;
using namespace nbt::gesture;
typedef std::vector<uint8_t> B;

static unsigned runFilter(const riclist::FilterInsn* f, int n, const B& p) {
    uint32_t a = 0;
    for (int pc = 0; pc < n; ++pc) {
        const riclist::FilterInsn& i = f[pc];
        if (i.code == 0x28) { if (i.k + 2 > p.size()) return 0; a = ((uint32_t)p[i.k] << 8) | p[i.k + 1]; }
        else if (i.code == 0x30) { if (i.k + 1 > p.size()) return 0; a = p[i.k]; }
        else if (i.code == 0x15) pc += (a == i.k) ? i.jt : i.jf;
        else if (i.code == 0x06) return i.k;
        else assert(!"unknown instruction");
    }
    assert(!"ran off the end");
    return 0;
}

// Ethernet (+ optional VLAN tag) / IPv4 (ihl words) / UDP to dport, carrying pay.
static B frame(bool vlan, int ihl, uint8_t proto, uint16_t dport, const B& pay) {
    B f(12, 0x11);
    if (vlan) { f.push_back(0x81); f.push_back(0x00); f.push_back(0x00); f.push_back(0x49); }
    f.push_back(0x08); f.push_back(0x00);
    B ip(ihl * 4, 0);
    ip[0] = (uint8_t)(0x40 | ihl);
    ip[9] = proto;
    f.insert(f.end(), ip.begin(), ip.end());
    const uint16_t ul = (uint16_t)(8 + pay.size());
    const uint8_t udp[8] = { 0x77, 0x67, (uint8_t)(dport >> 8), (uint8_t)dport, (uint8_t)(ul >> 8), (uint8_t)ul, 0, 0 };
    f.insert(f.end(), udp, udp + 8);
    f.insert(f.end(), pay.begin(), pay.end());
    return f;
}

// One camera gesture PDU: ident, length (8-byte header + 3-byte body), header, gesture id + 2 bytes.
static B gesturePdu(uint32_t ident, uint8_t id) {
    const uint8_t b[19] = { (uint8_t)(ident >> 24), (uint8_t)(ident >> 16), (uint8_t)(ident >> 8), (uint8_t)ident,
                            0, 0, 0, 11, 0, 0, 0, 0, 0, 0, 0, 0, id, 1, 0 };
    return B(b, b + 19);
}

int main() {
    riclist::FilterInsn f[kFilterLen];
    udpPortFilter(kCameraPort, f);
    const B g = gesturePdu(0xB5308005u, 5);
    assert(runFilter(f, kFilterLen, frame(false, 5, 17, 30567, g)) == 0xFFFF);
    assert(runFilter(f, kFilterLen, frame(true, 5, 17, 30567, g)) == 0xFFFF);
    assert(runFilter(f, kFilterLen, frame(false, 5, 17, 30568, g)) == 0);
    assert(runFilter(f, kFilterLen, frame(true, 5, 17, 30501, g)) == 0);
    assert(runFilter(f, kFilterLen, frame(false, 5, 6, 30567, g)) == 0);     // TCP
    assert(runFilter(f, kFilterLen, frame(true, 5, 6, 30567, g)) == 0);

    // Payload extraction, incl. IP options and a hand-coordinate PDU in front of the gesture.
    B two;
    const uint8_t hand[25] = { 0xB5, 0x30, 0x80, 0x06, 0, 0, 0, 17 };
    two.insert(two.end(), hand, hand + 25);
    two.insert(two.end(), g.begin(), g.end());
    const bool vl[2] = { false, true };
    for (int v = 0; v < 2; ++v) for (int ihl = 5; ihl <= 6; ++ihl) {
        const B fr = frame(vl[v], ihl, 17, 30567, two);
        const uint8_t* p = NULL; size_t n = 0; uint16_t dp = 0;
        assert(udpPayload(&fr[0], fr.size(), &p, &n, &dp));
        assert(dp == 30567 && n == two.size() && memcmp(p, &two[0], n) == 0);
        CamPdu cp[4];
        assert(splitCameraPdus(p, n, cp, 4) == 2);
        assert(!isGesturePdu(cp[0]) && isGesturePdu(cp[1]) && cp[1].body[0] == 5);
    }
    { const B fr = frame(false, 5, 6, 30567, g); const uint8_t* p; size_t n; uint16_t dp;
      assert(!udpPayload(&fr[0], fr.size(), &p, &n, &dp)); }
    { B fr = frame(false, 5, 17, 30567, g); fr.resize(30); const uint8_t* p; size_t n; uint16_t dp;
      assert(!udpPayload(&fr[0], fr.size(), &p, &n, &dp)); }
    { B fr = frame(false, 5, 17, 30567, g); fr.resize(fr.size() - 5); const uint8_t* p; size_t n; uint16_t dp;
      assert(udpPayload(&fr[0], fr.size(), &p, &n, &dp) && n == g.size() - 5); }   // short capture: clamped

    // Repeats: the camera re-sends while the hand is held.
    Runs r;
    assert(r.fresh(5, 1000));
    assert(!r.fresh(5, 1100));
    assert(!r.fresh(5, 1850));     // 750 ms after the previous PDU: still the same gesture
    assert(r.fresh(5, 2700));      // 850 ms gap: a new one
    assert(r.fresh(3, 2750));      // another gesture
    assert(r.fresh(5, 2800));      // back to two fingers
    Runs w;
    assert(w.fresh(5, 0xFFFFFF00u));
    assert(!w.fresh(5, 0x00000010u)); // across the 32-bit wrap
    printf("test_gesture_sniff: OK\n");
    return 0;
}
