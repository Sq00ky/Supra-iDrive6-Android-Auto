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
// DSI request ids: 0xF registerMOSTClientExt(string name), 1 registerDevCtrls(u32 client, u32 n, u16 keys[n]),
//                  2 processMessage(u32 client, MessageType).
// DSI response ids: 0x80000000 responseMOSTClient{u32 clientId; u16 deviceId}, 0x80000001 informationProcessMessage{MessageType}.

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

// Returns false if the payload is too short / inconsistent.
inline bool parseMessage(const uint8_t* p, size_t len, Msg& m) {
    if (!p || len < 32) return false;
    m.txType = rdLe32(p + 0);
    m.varTag = rdLe32(p + 4);
    m.varVal = rdLe32(p + 8);
    m.dst    = rdLe16(p + 12);
    m.src    = rdLe16(p + 14);
    m.fblock = p[16];
    m.inst   = p[17];
    m.fkt    = rdLe16(p + 18);
    m.opType = p[20];
    m.hdrTag = rdLe32(p + 24);
    uint32_t n = rdLe32(p + 28);
    if (n > len - 32) return false;
    m.data.assign(p + 32, p + 32 + n);
    return true;
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
