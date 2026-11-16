// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Restart nbt-aad after a Settings change (rhmiapp-68): pure helpers, host-tested (tests/test_settings_watch.cpp).
//
// The app takes a snapshot of every Settings checkbox when the Settings page (hmiState 14) is entered, and compares
// it when iDrive comes back to the Android Auto main page (hmiState 10). Any difference = restart nbt-aad if it runs.
// Comparing against the state at entry (not each press) means a box ticked and unticked again changes nothing.
#ifndef NBT_SETTINGS_WATCH_HPP
#define NBT_SETTINGS_WATCH_HPP

#include <stddef.h>
#include <string>

namespace nbt {

// One character per checkbox, in `names` order: '1' = flag set, '0' = not.
inline std::string settingsSnapshot(const char* const* names, size_t n, bool (*isSet)(const char*)) {
    std::string s;
    for (size_t i = 0; i < n; ++i) s.push_back(isSet(names[i]) ? '1' : '0');
    return s;
}

// "" = no change. Otherwise "wifi-direct on, native-audio off" for the log. Snapshots of a different length (should not
// happen) count as changed.
inline std::string settingsDiff(const char* const* names, size_t n, const std::string& before, const std::string& after) {
    if (before.size() != n || after.size() != n) return before == after ? std::string() : std::string("checkbox list");
    std::string d;
    for (size_t i = 0; i < n; ++i) {
        if (before[i] == after[i]) continue;
        if (!d.empty()) d += ", ";
        d += names[i];
        d += after[i] == '1' ? " on" : " off";
    }
    return d;
}

// Restart decision (pure): a change was made on this Settings visit, the feature is on, nbt-aad runs, it was not
// (re)started after the visit began (then it already read the new state), and no stop/restart is under way.
enum SettingsRestart { SR_NONE, SR_RESTART, SR_OFF, SR_NOT_RUNNING, SR_STARTED_SINCE, SR_BUSY };
inline SettingsRestart settingsRestartDecide(bool changed, bool enabled, bool daemonRunning, bool startedSinceVisit,
                                            bool stopBusy) {
    if (!changed) return SR_NONE;
    if (!enabled) return SR_OFF;
    if (!daemonRunning) return SR_NOT_RUNNING;
    if (startedSinceVisit) return SR_STARTED_SINCE;
    if (stopBusy) return SR_BUSY;
    return SR_RESTART;
}

} // namespace nbt

#endif // NBT_SETTINGS_WATCH_HPP
