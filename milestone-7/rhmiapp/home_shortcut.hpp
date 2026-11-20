// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#ifndef NBT_AAD_HOME_SHORTCUT_HPP
#define NBT_AAD_HOME_SHORTCUT_HPP
namespace nbt { namespace home {
typedef void (*LogFn)(const char*);
// Optional, firmware-profiled factory dashboard shortcut. Native scene mutations
// run on CHmiAppProcUnit, not on the PluginApp dispatcher thread.
void start(LogFn log);
// Call from the PluginApp status poll. True means the native entry was selected;
// the caller should use its normal start/show path on its own dispatcher thread.
bool poll(bool connected);
void stop();
} }
#endif
