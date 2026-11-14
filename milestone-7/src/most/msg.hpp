// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef MOST_MSG_HPP
#define MOST_MSG_HPP

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>

// DSYSMOSTServer (MOSTSubSystem.DSYSMOSTServer 4.5) message types, as serialized by the DSI stream
// (RE: libimp_sys_most_base_nbt.cpp-ne.so.17, DSYSMOSTTypes::DSIWriteMessageType/ReadMessageType).
// Little-endian, every scalar naturally aligned relative to the payload start.
//   MessageType = u32 transmissionType
//                 u32 variantTag (1 = eTransmissionTXControl, 2 = eTransmissionInfo), u32 variantValue
//                 header { u16 dst; u16 src; u8 fblock; u8 inst; u16 fkt; u8 opType; pad[3]; u32 tag }   (16 B)
//                 u32 payloadLen, u8 payload[payloadLen]
// The DSI request and information ids are in most/ids.hpp.

namespace nbt {
namespace most {

struct Msg {
    uint32_t txType;
    uint32_t varTag;
    uint32_t varVal;
    uint16_t dst, src;
    uint8_t  fblock, inst;
    uint16_t fkt;
    uint8_t  opType;
    uint32_t hdrTag;
    std::vector<uint8_t> data;
};

inline uint32_t rdLe32(const uint8_t* p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }
inline uint16_t rdLe16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// One received message, without copying the payload. Valid only during the callback: data points into the DSI buffer.
struct View {
    uint32_t txType, varTag, varVal;
    uint16_t dst, src;
    uint8_t  fblock, inst;
    uint16_t fkt;
    uint8_t  op;
    uint32_t hdrTag;
    const uint8_t* data;
    size_t   len;
    uint32_t deliveryMs; // entry into MOST client DSI callback, not bus arrival
    uint32_t rxMs;      // monotonic ms when our callback ran (0 until the client sets it); later than arrival after a stall
};

// Returns false if the payload is too short / inconsistent.
inline bool parseView(const uint8_t* p, size_t len, View& m) {
    if (!p || len < 32) return false;
    m.txType = rdLe32(p + 0);
    m.varTag = rdLe32(p + 4);
    m.varVal = rdLe32(p + 8);
    m.dst    = rdLe16(p + 12);
    m.src    = rdLe16(p + 14);
    m.fblock = p[16];
    m.inst   = p[17];
    m.fkt    = rdLe16(p + 18);
    m.op     = p[20];
    m.hdrTag = rdLe32(p + 24);
    uint32_t n = rdLe32(p + 28);
    if (n > len - 32) return false;
    m.data = p + 32;
    m.len  = n;
    m.deliveryMs = 0;
    m.rxMs = 0;
    return true;
}

// The owning form, for the tools and for building messages.
inline bool parseMessage(const uint8_t* p, size_t len, Msg& m) {
    View v;
    if (!parseView(p, len, v)) return false;
    m.txType = v.txType;
    m.varTag = v.varTag;
    m.varVal = v.varVal;
    m.dst    = v.dst;
    m.src    = v.src;
    m.fblock = v.fblock;
    m.inst   = v.inst;
    m.fkt    = v.fkt;
    m.opType = v.op;
    m.hdrTag = v.hdrTag;
    m.data.assign(v.data, v.data + v.len);
    return true;
}

// What a received message is, by the vendor client's test (informationProcessMessage, 0x10c1b8):
//   varVal 19, 20, 21 with txType 1: first, middle and last segment of a longer message
//   varVal 4..9: reverses the dispatch direction, inferred to be the transmit status of a sent message
//   varTag other than 2: the vendor client asserts on it, yet the car does deliver varTag 1 (varVal 0) on messages from
//   a head-unit device to 5000, so kNoInfo is a real class and not an error
enum MsgClass { kNormal, kSegFirst, kSegMiddle, kSegLast, kTxStatus, kNoInfo };

inline MsgClass classify(const View& v) {
    if (v.varTag != 2) return kNoInfo;
    if (v.txType == 1 && v.varVal >= 19 && v.varVal <= 21)
        return (MsgClass)(kSegFirst + (v.varVal - 19));
    if (v.varVal >= 4 && v.varVal <= 9) return kTxStatus;
    return kNormal;
}

inline const char* className(MsgClass c) {
    switch (c) {
        case kNormal: return "normal";
        case kSegFirst: return "seg-first";
        case kSegMiddle: return "seg-middle";
        case kSegLast: return "seg-last";
        case kTxStatus: return "txstatus";
        case kNoInfo: return "noinfo";
    }
    return "?";
}

inline void putLe32(std::vector<uint8_t>& v, uint32_t x) {
    while (v.size() % 4) v.push_back(0);
    for (int i = 0; i < 4; ++i) v.push_back((uint8_t)(x >> (8 * i)));
}
inline void putLe16(std::vector<uint8_t>& v, uint16_t x) {
    while (v.size() % 2) v.push_back(0);
    v.push_back((uint8_t)(x & 0xff));
    v.push_back((uint8_t)(x >> 8));
}
inline void putU8(std::vector<uint8_t>& v, uint8_t x) { v.push_back(x); }

inline std::vector<uint8_t> buildMessage(const Msg& m) {
    std::vector<uint8_t> v;
    putLe32(v, m.txType);
    putLe32(v, m.varTag);
    putLe32(v, m.varVal);
    putLe16(v, m.dst);
    putLe16(v, m.src);
    putU8(v, m.fblock);
    putU8(v, m.inst);
    putLe16(v, m.fkt);
    putU8(v, m.opType);
    putLe32(v, m.hdrTag);
    putLe32(v, (uint32_t)m.data.size());
    v.insert(v.end(), m.data.begin(), m.data.end());
    return v;
}

// registerDevCtrls payload: u32 clientId, u32 count, u16 key[count] (key = fblock << 8 | inst)
inline std::vector<uint8_t> buildRegisterDevCtrls(uint32_t clientId, const std::vector<uint16_t>& keys) {
    std::vector<uint8_t> v;
    putLe32(v, clientId);
    putLe32(v, (uint32_t)keys.size());
    for (size_t i = 0; i < keys.size(); ++i) putLe16(v, keys[i]);
    while (v.size() % 4) v.push_back(0);
    return v;
}

} // namespace most
} // namespace nbt

#endif
