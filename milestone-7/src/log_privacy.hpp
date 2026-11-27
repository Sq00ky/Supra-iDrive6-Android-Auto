// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// What nbt-aad.log may contain by default (#324). Users post their logs publicly, so by default the log leaves out:
//   - the car's GPS position (car GPS fixes, the LOCATION events sent to the phone),
//   - the phone's Bluetooth address (all but the last two bytes),
//   - raw DSI payload bytes (they carry the phone's name and address, contact data, ...).
// NBT_VERBOSE=1 in nbt-aad.env logs all of it again, together with every Android Auto frame (aa_session.cpp).
#ifndef NBT_LOG_PRIVACY_HPP
#define NBT_LOG_PRIVACY_HPP

#include "nbt_env.hpp"

#include <stdio.h>
#include <string>

namespace nbt {

inline bool logVerbose() {
    static int v = -1;
    if (v < 0) v = envFlag("NBT_VERBOSE") > 0 ? 1 : 0;
    return v == 1;
}

// "98:98:FB:0F:1A:66" -> "XX:XX:XX:XX:1A:66" unless NBT_VERBOSE=1. Anything not shaped like a MAC comes back unchanged.
inline std::string logMac(const std::string& a) {
    if (logVerbose() || a.size() != 17) return a;
    std::string m = a;
    for (size_t i = 0; i < 12; ++i)
        if (m[i] != ':') m[i] = 'X';
    return m;
}

// A position for the log: the coordinates with NBT_VERBOSE=1, otherwise only that one was there.
inline std::string logPosition(long lat_e7, long lon_e7) {
    if (!logVerbose()) return "(position hidden, NBT_VERBOSE=1 shows it)";
    char b[64];
    snprintf(b, sizeof(b), "%.6f, %.6f", lat_e7 / 1e7, lon_e7 / 1e7);
    return b;
}

} // namespace nbt

#endif
