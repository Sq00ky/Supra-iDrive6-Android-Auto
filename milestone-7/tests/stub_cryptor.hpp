// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host-test stand-in for aa_cryptor.hpp: no OpenSSL, and "encrypted" frames are plain, so a test can read what the session
// sends. Include it before aa_session.cpp: it takes the real header's guard (tests/test_session_sensors.cpp).
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
    bool encrypt(const uint8_t* d, size_t n, std::vector<uint8_t>& o) { o.assign(d, d + n); return true; }
    bool decrypt(const uint8_t* d, size_t n, std::vector<uint8_t>& o) { o.assign(d, d + n); return true; }
    const char* lastError() const { return ""; }
    size_t pendingOut() const { return 0; }
};
}
#endif
