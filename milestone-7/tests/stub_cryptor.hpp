// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host-test stand-in for aa_cryptor.hpp: no OpenSSL or handshake, and "encrypted" frames are plain text both ways, so a
// test can read what the session sends and script the phone. It takes the real header's guard, so it has to come first:
// included ahead of aa_session.cpp (tests/test_session_sensors.cpp) or with -include (test_session_bye in the Makefile).
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
    // A payload that starts 0xEE does not decrypt (no real message does): lets a test script a broken stream.
    bool decrypt(const uint8_t* d, size_t n, std::vector<uint8_t>& o) {
        if (n && d[0] == 0xEE) { o.clear(); return false; }
        o.assign(d, d + n);
        return true;
    }
    const char* lastError() const { return ""; }
    size_t pendingOut() const { return 0; }
};
}
#endif
