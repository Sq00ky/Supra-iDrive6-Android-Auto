// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host-test stand-in for aa_cryptor.hpp (OpenSSL not needed to exercise session lifecycle).
#ifndef AA_CRYPTOR_HPP
#define AA_CRYPTOR_HPP
#include <stdint.h>
#include <stddef.h>
#include <vector>
namespace nbt {
class AACryptor {
public:
    AACryptor() {}
    bool init() { return true; }
    void cleanup() {}
    bool startHandshake(std::vector<uint8_t>&) { return false; }
    bool processHandshakeData(const uint8_t*, size_t, std::vector<uint8_t>&) { return false; }
    bool isHandshakeComplete() const { return false; }
    bool encrypt(const uint8_t*, size_t, std::vector<uint8_t>&) { return false; }
    bool decrypt(const uint8_t* d, size_t n, std::vector<uint8_t>& o) { o.assign(d, d + n); return true; }
};
}
#endif
