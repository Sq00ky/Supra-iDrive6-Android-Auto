// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_VOLUME_HPP
#define NBT_VOLUME_HPP

// Volume model for the AA-side gain stage (the only gain we can actually set: there is no A2I/amp gain call on
// this unit, and the per-stream mixer gains are fixed in the coding). Kept free of QNX/iOS dependencies so the
// curve and the clamp rules are host-testable (tests/test_volume.cpp).
//
// pct 0..100 maps linearly in dB over a 40 dB range: 100 = 0 dB, 50 = -20 dB, 0 = -40 dB (and the mixer treats
// anything at or below -40 as silence in practice). A dB-linear curve is what makes the slider feel even; a
// linear-amplitude curve spends most of the travel doing nothing audible.

#include <stdint.h>
#include <stdio.h>

namespace nbt {

const int kVolumeMinPct = 0;
const int kVolumeMaxPct = 100;
const int kVolumeStepPct = 5;      // one notch / one key press
const int kVolumeRangeDb = 40;     // 100 % -> 0 dB, 0 % -> -40 dB

inline int volumeClampPct(int pct) {
    if (pct < kVolumeMinPct) return kVolumeMinPct;
    if (pct > kVolumeMaxPct) return kVolumeMaxPct;
    return pct;
}

// pct -> attenuation in whole dB (0 at 100 %, -40 at 0 %). Whole dB because AudioMixer::dbToGain takes int dB.
inline int volumePctToDb(int pct) {
    pct = volumeClampPct(pct);
    // (100 - pct) * 40 / 100, rounded to nearest
    const int span = (kVolumeMaxPct - pct) * kVolumeRangeDb;
    return -(int)((span + 50) / kVolumeMaxPct);
}

// 10^(db/20) without libm, same curve as AudioMixer::dbToGain (whole dB, clamped to -60..+12).
inline float volumeDbToGain(int db) {
    if (db < -60) db = -60;
    if (db > 12) db = 12;
    const float kStep = 1.12201845f;   // 10^(1/20)
    float g = 1.0f;
    if (db >= 0) { for (int i = 0; i < db; ++i) g *= kStep; }
    else         { for (int i = 0; i < -db; ++i) g /= kStep; }
    return g;
}

// pct -> linear gain, 0..1.
inline float volumePctToGain(int pct) {
    return volumeDbToGain(volumePctToDb(pct));
}


// "VOL 65" style label for logs and (later) the OSD.
inline void volumeLabel(int pct, char* buf, size_t n) {
    pct = volumeClampPct(pct);
    snprintf(buf, n, "%d%%", pct);
}

} // namespace nbt

#endif // NBT_VOLUME_HPP
