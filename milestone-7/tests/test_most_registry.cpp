// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: the MOST registry decode and lookup (most/registry.hpp).
#include "most/registry.hpp"
#include <assert.h>
#include <stdio.h>

using namespace nbt::most;

int main() {
    assert(registryKey(0x52, 0x00) == 0x5200 && registryKey(0xE5, 0x03) == 0xE503);

    const uint8_t r[] = { 2, 0, 0, 0, 0x21, 0x20, 0x00, 0x52, 0x01, 0x10, 0x02, 0x15 };
    std::vector<RegistryEntry> e;
    assert(decodeRegistry(r, sizeof(r), e) && e.size() == 2);
    assert(e[0].address == 0x2021 && e[0].key == 0x5200);
    assert(e[1].address == 0x1001 && e[1].key == 0x1502);

    const RegistryEntry* f = findRegistryEntry(e, registryKey(0x52, 0x00));
    assert(f && f->address == 0x2021);
    assert(!findRegistryEntry(e, 0xE503));

    assert(!decodeRegistry(r, 8, e) && e.empty());                      // the count does not fit
    assert(!decodeRegistry(NULL, 8, e));
    assert(!decodeRegistry(r, 3, e));
    const uint8_t none[] = { 0, 0, 0, 0 };
    assert(!decodeRegistry(none, sizeof(none), e));
    const uint8_t huge[] = { 0x01, 0x04, 0, 0 };                         // 1025 entries is not a registry
    std::vector<uint8_t> big(huge, huge + 4);
    big.resize(4 + 1025 * 4);
    assert(!decodeRegistry(&big[0], big.size(), e));

    printf("test_most_registry OK\n");
    return 0;
}
