// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#ifndef NBT_AAD_HOME_VISIBILITY_HPP
#define NBT_AAD_HOME_VISIBILITY_HPP
#include "home_shortcut.hpp"
namespace nbt { namespace home {
// Call only on the native HMI thread, after the firmware profile has passed.
// True means the verified ID6L route applies; generic menu insertion is skipped.
// Retains the factory icon/caption; home_selection observes shortcut selection.
bool updateNativeVisibility(void* factory, void* variants, void* carplay,
    bool aaWanted, bool probeOnly, bool stopping, LogFn logger);
} }
#endif
