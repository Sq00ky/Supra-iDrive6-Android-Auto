// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Test helpers: bytes from the car capture of 2026-11-10 (GR Supra, iDrive playing Bluetooth audio) and frame builders.
#ifndef TESTS_RIC_FRAMES_HPP
#define TESTS_RIC_FRAMES_HPP

#include <stdio.h>
#include "cluster/ric_sniff.hpp"

namespace ricf {

using nbt::riclist::Bytes;

inline Bytes hex(const char* s) {
    Bytes b;
    for (; s[0] && s[1]; s += 2) {
        unsigned v = 0;
        sscanf(s, "%2x", &v);
        b.push_back((uint8_t)v);
    }
    return b;
}

// Capture packet 98, whole: iDrive 160.48.199.119:65486 -> KOMBI 160.48.199.96:30502, one SOME/IP request
// (service 0xB00E, method 0x000B, client 0x0001, session 0x000F) carrying the track list.
static const char* const kListPacket =
    "a85b6c4bdbf938455419b1140800450000a42f1e400001067afda030c777a030c760ffce772632ebbab2cabcb4125018cd504d5a0000"
    "b00e000b000000740001000f01010000"
    "000000640000006b300000005b0000000b0000006b300000000000030002000300020700001d0000002a0000000a0700000501030000"
    "02010000000affffffff0103000003000000000a070000060103000004010a00000003000001000000700000007100000004feff0000";
// The request body of that packet (the list as iDrive sends it, middle row = its text model 0x03000003).
static const char* const kListBody =
    "000000640000006b300000005b0000000b0000006b300000000000030002000300020700001d0000002a0000000a0700000501030000"
    "02010000000affffffff0103000003000000000a070000060103000004010a00000003000001000000700000007100000004feff0000";
// The three values iDrive sends after every list, then the commit.
static const char* const kValue70 = "0000006400000070" "2c" "00000008" "00000002" "000000702c00ffff";
static const char* const kValue71 = "0000006400000071" "13" "00000007" "00000001" "00000071130002";
static const char* const kShown = "0000006400000065" "2e" "00000009" "00000003" "000000652e0000006b";
static const char* const kClosed = "0000006400000065" "2e" "00000009" "00000003" "000000652effffffff";
static const char* const kCommit = "00000064";

inline void be16(Bytes& b, unsigned v) { b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)v); }
inline void be32(Bytes& b, uint32_t v) { be16(b, v >> 16); be16(b, v & 0xFFFF); }

inline Bytes someip(uint16_t service, uint16_t method, uint16_t client, uint16_t session, uint8_t type, const Bytes& body) {
    Bytes m;
    be16(m, service); be16(m, method); be32(m, (uint32_t)(8 + body.size())); be16(m, client); be16(m, session);
    m.push_back(1); m.push_back(1); m.push_back(type); m.push_back(0);
    m.insert(m.end(), body.begin(), body.end());
    return m;
}

// Ethernet + IPv4 + TCP around a payload (checksums left 0: nothing here checks them).
inline Bytes ethTcp(uint32_t src, uint16_t sport, uint32_t dst, uint16_t dport, uint32_t seq, const Bytes& payload,
                    bool syn = false, bool vlan = false, uint8_t proto = 6) {
    Bytes f(12, 0x02);
    if (vlan) { be16(f, 0x8100); be16(f, 73); }
    be16(f, 0x0800);
    f.push_back(0x45); f.push_back(0); be16(f, (unsigned)(40 + payload.size())); be32(f, 0x00004000);
    f.push_back(64); f.push_back(proto); be16(f, 0); be32(f, src); be32(f, dst);
    be16(f, sport); be16(f, dport); be32(f, seq); be32(f, 0);
    f.push_back(0x50); f.push_back(syn ? 0x02 : 0x18); be16(f, 0xFFFF); be32(f, 0);
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}

}  // namespace ricf

#endif
