// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_ENV_HPP
#define NBT_ENV_HPP

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

namespace nbt {

// HU-hosted wireless (Bluetooth RFCOMM -> Wi-Fi credentials -> TCP accept) is the default since
// v1.4.2 (car-verified). NBT_WIRELESS=0 / off / no restores the legacy outbound phone scanner.
inline bool wirelessEnabled() {
    const char* e = getenv("NBT_WIRELESS");
    if (!e || !*e) return true;
    return !(strcmp(e, "0") == 0 || strcmp(e, "off") == 0 || strcmp(e, "no") == 0 || strcmp(e, "false") == 0);
}

// iDrive knob / tilt / hard keys -> phone is the default since v1.4.3 (car-verified nbt-25).
// NBT_KNOB=0 / off / no disables it.
inline bool knobEnabled() {
    const char* e = getenv("NBT_KNOB");
    if (!e || !*e) return true;
    return !(strcmp(e, "0") == 0 || strcmp(e, "off") == 0 || strcmp(e, "no") == 0 || strcmp(e, "false") == 0);
}

// Bluetooth audio mode (default ON since v1.7.5; car-verified 2026-10-23): leaves Android Auto media to the
// phone's Bluetooth A2DP link. The ServiceDiscovery omits the AA media sink (channel 9) so the phone plays
// media over A2DP, and the daemon asks the AudioManager for the car's Bluetooth audio
// source (`audioconn -b 0x3a 0x1`, car-verified to work from outside the HMI). The A2I pipeline is not
// opened in this mode. Overrides: NBT_BT_SRC (default 0x3a eSRC_A2DP), NBT_BT_SINK (default 0x1
// eSNK_SPEAKERS), NBT_BT_RELEASE=1 (release the source when the phone disconnects), NBT_SDR_DROP.
inline bool btAudioEnabled() {
    // Unset = on. NBT_AUDIO_BT=0 / off / no / false selects the A2I (AA media channel) pipeline instead.
    const char* e = getenv("NBT_AUDIO_BT");
    if (!e || !*e) return true;
    return !(strcmp(e, "0") == 0 || strcmp(e, "off") == 0 || strcmp(e, "no") == 0 || strcmp(e, "false") == 0);
}

inline unsigned long envHex(const char* name, unsigned long def) {
    const char* e = getenv(name);
    if (!e || !*e) return def;
    char* end = NULL;
    unsigned long v = strtoul(e, &end, 0);
    return (end && end != e) ? v : def;
}

} // namespace nbt

#endif // NBT_ENV_HPP
