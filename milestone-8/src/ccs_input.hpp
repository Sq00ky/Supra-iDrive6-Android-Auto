// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CCS_INPUT_HPP
#define CCS_INPUT_HPP

#include <stddef.h>
#include <stdint.h>
#include "ccs_decoder.hpp"

namespace nbt {

typedef void (*CcsEventCb)(const CcsEvent& ev, void* user);

// iDrive controller input for the daemon (default on; NBT_KNOB=0 disables).
// Observe-only MOST client on MOSTSubSystem.DSYSMOSTServer 4.5 (same registration as test_controls):
// it receives copies of the controller's status messages (FBlock 0xE5) and decodes them with CcsDecoder.
// Nothing is ever sent to the controller. The HMI keeps receiving the same events.
// Env: NBT_KNOB_INST=hex   controller instance (default 03)
//      NBT_KNOB_INVERT=1   reverse the rotary direction
//      NBT_KEY_<SLOT>=n    Android keycode for a hard key, 0 = ignore
//                          (SLOT = MENU OPTIONS BACK COM MEDIA NAV MAP;
//                           defaults 3 HOME, 0, 4 BACK, 5 CALL, 65537 MEDIA, 65538 NAV, 65538 NAV)
//      NBT_KNOB_RAW=1      log every raw DSI event (very noisy)
bool startCcsInput(CcsEventCb cb, void* user);

} // namespace nbt

#endif
