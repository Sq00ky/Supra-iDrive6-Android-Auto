// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CAR_INPUT_ACTIVITY_HPP
#define CAR_INPUT_ACTIVITY_HPP

// "Did the driver just touch a car control?" for the screen hand-over. When iDrive leaves Android Auto's blank state,
// the iDrive app has to tell two cases apart:
//   - the driver pressed something (knob, MENU/BACK/OPTIONS, steering-wheel volume or voice): iDrive reacted to it and
//     the existing hard-key rules apply;
//   - nobody pressed anything we can see: the car opened a screen of its own (camera button, Side View / 360 / Top View,
//     the safety / driver-assistance button, a warning). Android Auto then steps aside and stays aside.
// A consumer of the observe-only MOST client, like CcsInput: it decodes the controller with its own CcsDecoder (same
// settings), so a key the phone never gets (OPTIONS, an unmapped key) still counts, while the controller's routine
// status messages do not. Steering wheel: MFL FBlock 0x15, volume button fkt 0x0401 (and the centre console's 0x0C01,
// same shape, 00 00 KK 01 with KK != 0 pressed) and the voice button fkt 0x0512 (1 = pressed). Adds no keys and no
// requests. The callback runs on the DSI callback thread: see the handler rules in most/client.hpp.

#include "car/ccs_config.hpp"
#include "car/ccs_decoder.hpp"
#include "most/client.hpp"
#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace car {

typedef void (*InputActivityCb)(const char* what, void* user);

// Pure: is this steering-wheel status a press? Host-tested in tests/test_input_activity.cpp.
inline const char* mflPress(uint16_t fkt, const uint8_t* d, size_t n) {
    if (!d) return NULL;
    if ((fkt == 0x401 || fkt == 0xC01) && n >= 3 && d[2] != 0) return d[2] == 0x02 ? "VOLUME_UP" : "VOLUME_DOWN";
    if (fkt == 0x512 && n >= 1 && d[0] == 1) return "VOICE";
    return NULL;
}

class InputActivity {
public:
    InputActivity();

    // Before the client's start(). False: the client has no room for the routes.
    bool attach(most::Client& client, const CcsConfig& cfg, InputActivityCb cb, void* user);

private:
    static void controllerTramp(const most::View& m, void* self);
    static void mflTramp(const most::View& m, void* self);
    static void linkTramp(most::LinkState s, void* self);

    CcsDecoder dec_;
    InputActivityCb cb_;
    void* user_;
};

} // namespace car
} // namespace nbt

#endif
