// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef TOUCH_PROBE_HPP
#define TOUCH_PROBE_HPP
#include <stdint.h>
#include "touch_types.hpp"

namespace nbt {
// Observe-only UDP listener for the factory touch controller stream.
// HMI (CTouchDataClient) binds 0.0.0.0:30567 (30568 for the second display role);
// frames start with a 32-bit ident (0xB0048002 = touch data). Logs the first frames
// as hex; never sends anything. Env: NBT_TOUCH_PROBE=1, NBT_TOUCH_PORT (default 30567),
// NBT_TOUCH_PROBE_SECS (default 60).
bool startTouchProbe(uint16_t port, int seconds);

// Decoded touch forwarder (default on, NBT_TOUCH_FWD=0 disables). Same UDP socket as the probe.
// Frame (112 B): ident B0048002, len 0x68, 16 B header, then 8-byte entries; finger k uses (stride 16 is a hypothesis for k>0; entries are 8 B = [00 c1 40 idx][4 data bytes])
//   x word at byte 52+16k = [ctr, id, x_hi, x_lo]       y word at byte 60+16k = [y_hi, y_lo, size, state<<4]
// state 1 = down, 2 = move, 3 = up. Calibration env: NBT_TOUCH_SX/SY (scale), NBT_TOUCH_OX/OY (offset),
// NBT_TOUCH_W/H (clamp range, default 1280x480).
typedef void (*TouchEventCb)(const TouchEventOut& ev, void* user);
bool startTouchForward(uint16_t port, TouchEventCb cb, void* user);

// Who gets the touch datagrams. The HMI and we both bind UDP 30567 (SO_REUSEPORT); io-pkt (BSD) hands a unicast
// datagram to ONE socket, the one bound last, i.e. ours: while our socket exists iDrive gets no touch at all (car
// 2026-10-28: touch dead after the phone's exit icon / a hard key, knob fine). So the forwarder only holds the port
// while Android Auto is on screen: setTouchOwned(false) closes it (iDrive gets touch back), true re-binds it.
void setTouchOwned(bool owned);
}
#endif
