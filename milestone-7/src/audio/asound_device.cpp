// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio/asound_device.hpp"

namespace nbt {

uint32_t pickDeviceRate(uint32_t want, uint32_t force, int min_rate, int max_rate) {
    if (force) return force;
    if (max_rate > 0 && ((int64_t)want > max_rate || (min_rate > 0 && (int64_t)want < min_rate)))
        return (uint32_t)max_rate;
    return want;
}

} // namespace nbt
