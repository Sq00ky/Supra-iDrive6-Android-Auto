// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef MOST_REGISTRY_HPP
#define MOST_REGISTRY_HPP

// The MOST registry, from attribute kAttrRegistry: which device hosts which function block. Pure: host-tested in
// tests/test_most_registry.cpp.

#include "most/msg.hpp"
#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace nbt {
namespace most {

struct RegistryEntry {
    uint16_t address;   // device address
    uint16_t key;       // fblock << 8 | inst. On the car, address 2021 key 5200 is the sender of FB 52/00.
};

inline uint16_t registryKey(uint8_t fblock, uint8_t inst) { return (uint16_t)((fblock << 8) | inst); }

// As serialised in a DSI stream: u32 count, then count x {u16 address, u16 key}, little-endian. False if the length does
// not fit, or for an empty list or one too long to be real.
inline bool decodeRegistry(const uint8_t* p, size_t n, std::vector<RegistryEntry>& out) {
    out.clear();
    if (!p || n < 4) return false;
    uint32_t count = rdLe32(p);
    if (count == 0 || count > 1024 || 4 + (size_t)count * 4 > n) return false;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = p + 4 + i * 4;
        RegistryEntry r;
        r.address = rdLe16(e);
        r.key = rdLe16(e + 2);
        out.push_back(r);
    }
    return true;
}

// The first entry for a key, NULL if none.
inline const RegistryEntry* findRegistryEntry(const std::vector<RegistryEntry>& reg, uint16_t key) {
    for (size_t i = 0; i < reg.size(); ++i)
        if (reg[i].key == key) return &reg[i];
    return NULL;
}

} // namespace most
} // namespace nbt

#endif
