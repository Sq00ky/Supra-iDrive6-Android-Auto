// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CAR_CCS_CONFIG_HPP
#define CAR_CCS_CONFIG_HPP

// The iDrive controller and steering-wheel voice button settings, read from the environment in one place (host-tested:
// tests/test_ccs_config.cpp). Read once at start-up and handed to the consumer.
//   NBT_KNOB_INST=hex      controller instance (default 03)
//   NBT_KNOB_CCS_INST=hex  older CCS controller (fkt C10/C20) instance (default 01)
//   NBT_KNOB_INVERT=1      reverse the rotary direction
//   NBT_KEY_<SLOT>=n       Android keycode for a hard key, 0 = ignore
//                          (SLOT = MENU OPTIONS BACK COM MEDIA NAV MAP;
//                           defaults 3 HOME, 0, 4 BACK, 5 CALL, 65537 MEDIA, 65538 NAV, 65538 NAV)
//   NBT_PTT_ASSISTANT=1    a long press of the steering-wheel voice button starts the Google Assistant

#include "car/ccs_decoder.hpp"
#include "nbt_env.hpp"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

namespace nbt {
namespace car {

struct CcsConfig {
    uint8_t  inst;
    uint8_t  ccsInst;
    bool     invert;
    bool     slotSet[CCS_SLOTS];    // false: the decoder's default key stays
    uint32_t slotKey[CCS_SLOTS];
    bool     pttAssistant;

    CcsConfig() : inst(0x03), ccsInst(0x01), invert(false), pttAssistant(false) {
        for (int s = 0; s < CCS_SLOTS; ++s) { slotSet[s] = false; slotKey[s] = 0; }
    }
};

inline CcsConfig readCcsConfig() {
    CcsConfig c;
    const char* e = getenv("NBT_KNOB_INST");
    if (e && *e) c.inst = (uint8_t)strtoul(e, NULL, 16);
    e = getenv("NBT_KNOB_CCS_INST");
    if (e && *e) c.ccsInst = (uint8_t)strtoul(e, NULL, 16);
    c.invert = envFlag("NBT_KNOB_INVERT") == 1;
    for (int s = 0; s < CCS_SLOTS; ++s) {
        char name[32];
        snprintf(name, sizeof(name), "NBT_KEY_%s", CcsDecoder::slotName(s));
        e = getenv(name);
        if (e && *e) { c.slotSet[s] = true; c.slotKey[s] = (uint32_t)strtoul(e, NULL, 0); }
    }
    e = getenv("NBT_PTT_ASSISTANT");
    c.pttAssistant = e && e[0] == '1';
    return c;
}

inline void applyCcsConfig(const CcsConfig& c, CcsDecoder& d) {
    d.setInstance(c.inst);
    d.setCcsInstance(c.ccsInst);
    d.setInvert(c.invert);
    for (int s = 0; s < CCS_SLOTS; ++s)
        if (c.slotSet[s]) d.setSlotKey(s, c.slotKey[s]);
}

} // namespace car
} // namespace nbt

#endif
