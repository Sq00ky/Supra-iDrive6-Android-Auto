// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#ifndef NBT_AAD_HOME_SELECTION_HPP
#define NBT_AAD_HOME_SELECTION_HPP
#include "home_shortcut.hpp"
namespace nbt { namespace home {
typedef void (*SelectFn)();
// Native HMI-thread registration/removal of our own widget-signal observer.
void updateNativeSelection(void* sceneManager, void* carplay, bool wanted,
    bool probeOnly, bool stopping, LogFn logger, SelectFn selected);
} }
#endif
