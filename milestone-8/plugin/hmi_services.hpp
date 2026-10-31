// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Thin wrappers around exported HMI services used by the plugin (looked up with dlsym, no fixed addresses).
#ifndef NBT_AAD_HMI_SERVICES_HPP
#define NBT_AAD_HMI_SERVICES_HPP

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace hmi {

// Loads the exported symbols below. Returns false (and logs) if any is missing.
bool loadSymbols();

// Resolve a named plugin object through the framework resolver handed to start(). NULL if not found.
void* resolvePlugin(void* resolver, const char* name);

// Find the HMI processing unit (class CHmiAppProcUnit) in the main dispatcher. NULL until it has registered.
void* findAppProcUnit(void* mainDispatcher);

// Arm a one-shot timer on a processing unit; fn(target, timerId, user, extra) then runs on that unit's thread.
typedef void (*TimerFn)(void* target, unsigned timerId, unsigned user, unsigned extra);
int armTimer(void* procUnit, void* target, TimerFn fn, unsigned delayMs);

} // namespace hmi
} // namespace nbt

#endif
