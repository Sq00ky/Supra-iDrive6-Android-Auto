// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CAR_CCS_INPUT_HPP
#define CAR_CCS_INPUT_HPP

#include <stddef.h>
#include <stdint.h>
#include "car/ccs_decoder.hpp"

namespace nbt {
namespace car {

typedef void (*CcsEventCb)(const CcsEvent& ev, void* user);

// iDrive controller input for the daemon (default on; NBT_KNOB=0 disables).
// Observe-only MOST client on MOSTSubSystem.DSYSMOSTServer 4.5 (same registration as test_controls):
// it receives copies of the controller's status messages (FBlock 0xE5) and decodes them with CcsDecoder.
// Nothing is ever sent to the controller. The HMI keeps receiving the same events.
// Settings: car/ccs_config.hpp. NBT_KNOB_RAW=1 logs every raw DSI event (very noisy).
bool startCcsInput(CcsEventCb cb, void* user);

} // namespace car
} // namespace nbt

#endif
