// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CAR_SOURCE_KEY_HPP
#define CAR_SOURCE_KEY_HPP

// "The driver picked another audio source", for browser mode's hold (NBT_BROWSER_HOLD, audio/route_policy.hpp): while
// Android Auto media holds the browser source, a source change gives it back to the car. A consumer of the
// observe-only MOST client, like InputActivity; adds no keys and no requests.
//   - steering-wheel MODE: MFL FBlock 0x15 fkt 0x0513, 01 = pressed, 00 = released (car-verified on a BMW F36 with
//     NBT Evo ID5, 2026-11-05). The car sends it to two receivers (0x2011 and 0x2022), so one press can reach the
//     handler twice: the consumer must take a repeat as the same change.
//   - a source picked in the iDrive menu: not decoded yet.
// The callback runs on the DSI callback thread: see the handler rules in most/client.hpp.

#include "most/client.hpp"
#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace car {

typedef void (*SourceKeyCb)(const char* what, void* user);

// Pure: is this steering-wheel status a source-change press? Host-tested in tests/test_source_key.cpp.
inline const char* sourceKeyPress(uint16_t fkt, const uint8_t* d, size_t n) {
    if (!d) return NULL;
    if (fkt == 0x513 && n >= 1 && d[0] == 1) return "MODE";
    return NULL;
}

class SourceKey {
public:
    SourceKey();

    // Before the client's start(). False: the client has no room for the route.
    bool attach(most::Client& client, SourceKeyCb cb, void* user);

private:
    static void mflTramp(const most::View& m, void* self);

    SourceKeyCb cb_;
    void* user_;
};

} // namespace car
} // namespace nbt

#endif
