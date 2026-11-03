// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_ASOUND_DEVICE_HPP
#define AUDIO_ASOUND_DEVICE_HPP

#include <stdint.h>
#include <stddef.h>

namespace nbt {

// The output device rate to ask for. want = the configured rate, kept if the device's reported range allows it, else
// its maximum (this car's browser device reports 22050..44100, so 48000 becomes 44100). force (non-zero) is asked for
// as is, whatever the device reports (NBT_BROWSER_FORCE_RATE experiment). min/max <= 0 = range unknown.
uint32_t pickDeviceRate(uint32_t want, uint32_t force, int min_rate, int max_rate);

} // namespace nbt

#endif // AUDIO_ASOUND_DEVICE_HPP
