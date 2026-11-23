// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#pragma once
#include "nbt_env.hpp"

namespace nbt {

// NBT_RADIO_TILE=1 shows the iDrive radio in the Android Auto media tile. Off by default.
inline bool radioTileEnabled() { return envFlag("NBT_RADIO_TILE") == 1; }

} // namespace nbt
