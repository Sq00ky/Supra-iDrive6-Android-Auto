// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// The KOMBI's list service (BMW ... REMOTEINSTRUMENTCLUSTER.RICServiceList, SOME/IP 0xB00E, instance 1): the track list
// the instrument cluster shows when the thumb wheel is used (header = device name, rows "Previous title" / current
// track / "Next title"). Header-only frame builders, no I/O. Same host, port and frame layout as the route-guidance
// service (cluster/j29_graph.hpp): body [0:4] instance handle, [4:8] model id (<type><index24>), [8] model type,
// [9:13] payload length (= body length - 17), [13:17] tag, [17:] payload.
//
// Everything here is from ONE capture on a GR Supra (2026-11-10, iDrive playing Bluetooth audio from a phone), iDrive
// (client 0x0001, interface version 1) -> 160.48.199.96:30502, instance handle 0x64:
//   0x0003  text model 0x03<index>:   payload = <model id> 03 00 03 00 00 00 <len> FE FF <UTF-16BE> 00 00, tag 4
//           index 1 = device name, 2 = "Previous title", 4 = "Next title", 3 / 5 / 6 ... = one per track title seen
//   0x000B  list model 0x0000006B (type 0x30): three rows that name text models (and icons 0x07000005 / 0x07000006)
//   0x0009  small values: model 0x70, 0x71, and 0x65 = 0x6B (list shown) / 0xFFFFFFFF (list closed)
//   0x0004  commit, body = the handle alone; iDrive sends it after every group
// iDrive re-sends the list, the three values and the commit on every thumb-wheel step, but a text model only when its
// text is new. Whether the KOMBI takes these frames from a second client is what tools/ricprobe is for.
#ifndef CLUSTER_RIC_LIST_HPP
#define CLUSTER_RIC_LIST_HPP

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

#include "cluster/j29_graph.hpp"   // kombinav::Bytes, kombinav::utf16be

namespace nbt {
namespace riclist {

typedef kombinav::Bytes Bytes;

static const uint16_t kService = 0xB00E;
static const uint16_t kMethodText = 0x0003, kMethodCommit = 0x0004, kMethodValue = 0x0009, kMethodList = 0x000B;
static const uint32_t kHandle = 0x64;          // iDrive's list instance in the capture (one boot, one car)
static const uint32_t kTextPrevLabel = 2;      // "Previous title"
static const uint32_t kTextNextLabel = 4;      // "Next title"

// Never 0x0001 and friends (create / dispose): iDrive owns the instance.
inline bool methodAllowed(uint16_t m) {
    return m == kMethodText || m == kMethodCommit || m == kMethodValue || m == kMethodList;
}

inline void put32(Bytes& b, uint32_t v) {
    b.push_back((uint8_t)(v >> 24)); b.push_back((uint8_t)(v >> 16)); b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)v);
}

// Text model 0x03<index> with the given UTF-8 text. False when the index does not fit 24 bits or the text does not
// fit the one-byte length (125 UTF-16 units).
inline bool textFrame(uint32_t handle, uint32_t index, const std::string& utf8, Bytes& body) {
    if (index > 0xFFFFFFu) return false;
    const Bytes enc = kombinav::utf16be(utf8);
    const size_t slen = 2 + enc.size() + 2;                 // BOM + text + NUL
    if (slen > 255) return false;
    const uint32_t id = 0x03000000u | index;
    body.clear();
    put32(body, handle);
    put32(body, id);
    body.push_back(0x03);
    put32(body, 0);                                         // payload length, set below
    put32(body, 0x00000004);
    put32(body, id);
    body.push_back(0x03); body.push_back(0x00); body.push_back(0x03);
    body.push_back(0x00); body.push_back(0x00); body.push_back(0x00);
    body.push_back((uint8_t)slen);
    body.push_back(0xFE); body.push_back(0xFF);
    body.insert(body.end(), enc.begin(), enc.end());
    body.push_back(0x00); body.push_back(0x00);
    kombinav::putU32(body, 9, (uint32_t)(body.size() - 17));
    return true;
}

inline Bytes commitFrame(uint32_t handle) {
    Bytes b;
    put32(b, handle);
    return b;
}

// One SOME/IP request (protocol version 1, interface version 1, message type REQUEST).
inline Bytes someipRequest(uint16_t method, const Bytes& body, uint16_t client, uint16_t session) {
    Bytes m;
    m.push_back((uint8_t)(kService >> 8)); m.push_back((uint8_t)kService);
    m.push_back((uint8_t)(method >> 8)); m.push_back((uint8_t)method);
    put32(m, (uint32_t)(8 + body.size()));
    m.push_back((uint8_t)(client >> 8)); m.push_back((uint8_t)client);
    m.push_back((uint8_t)(session >> 8)); m.push_back((uint8_t)session);
    m.push_back(0x01); m.push_back(0x01); m.push_back(0x00); m.push_back(0x00);
    m.insert(m.end(), body.begin(), body.end());
    return m;
}

// 0 = the matching E_OK reply; otherwise why not (for the probe's output). m = one whole SOME/IP message.
inline const char* replyProblem(const Bytes& m, uint16_t method, uint16_t client, uint16_t session) {
    if (m.size() < 16) return "short reply";
    if (((m[0] << 8) | m[1]) != kService) return "reply from another service";
    if (((m[2] << 8) | m[3]) != method) return "reply to another method";
    if (((m[8] << 8) | m[9]) != client || ((m[10] << 8) | m[11]) != session) return "reply to another request";
    if (m[14] == 0x81) return "ERROR reply";
    if (m[14] != 0x80) return "not a RESPONSE";
    if (m[15] != 0x00) return "return code is not E_OK";
    return NULL;
}

}  // namespace riclist
}  // namespace nbt

#endif
