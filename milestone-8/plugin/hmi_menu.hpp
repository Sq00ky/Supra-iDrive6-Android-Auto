// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Communication menu entry. Must be called on the HMI thread (from the processing-unit timer callback).
#ifndef NBT_AAD_HMI_MENU_HPP
#define NBT_AAD_HMI_MENU_HPP

#include <stdint.h>

namespace nbt {
namespace hmi {

struct MenuEntryCfg {
    const char* menuName;   // provider name, "ST_Com_Selection"
    const char* fallbackState; // state the stock execute path would open ("ST_MyV_DeviceList")
    uint32_t text, icon, focus, order;
};

typedef void (*SelectFn)(void* entry);

// Checks the HmiMain3D functions used below. Call once before addMenuEntry.
bool checkMenuFirmware();

// 1 = added, 0 = menu not built yet (call again later), -1 = failed (do not retry).
int addMenuEntry(const MenuEntryCfg& cfg, SelectFn onSelect);

} // namespace hmi
} // namespace nbt

#endif
