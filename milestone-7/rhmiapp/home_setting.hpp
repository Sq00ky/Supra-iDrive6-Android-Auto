// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#ifndef NBT_HOME_SETTING_HPP
#define NBT_HOME_SETTING_HPP

#include "../src/nbt_paths.hpp"

namespace nbt { namespace home {
// A persistent value in writable data; absent or invalid means opt-out.
inline bool readShortcutSetting(const char* choice) {
    FILE* f = fopen(choice, "r");
    if (!f) return false;
    int c = fgetc(f);
    fclose(f);
    return c == '1';
}
inline bool shortcutEnabled() {
    return readShortcutSetting(nbt::dataFile("home-shortcut.setting").c_str());
}
// Publish atomically: a native HMI callback may read this while the menu writes.
inline bool writeShortcutSetting(const char* choice, const char* temporary, bool enabled) {
    FILE* f = fopen(temporary, "w");
    if (!f) return false;
    bool ok = fputs(enabled ? "1\n" : "0\n", f) >= 0;
    if (fclose(f) != 0) ok = false;
    if (ok && rename(temporary, choice) == 0) return true;
    unlink(temporary);
    return false;
}
inline bool setShortcutEnabled(bool enabled) {
    nbt::makeDataDir();
    return writeShortcutSetting(nbt::dataFile("home-shortcut.setting").c_str(),
                                nbt::dataFile("home-shortcut.setting.tmp").c_str(), enabled);
}
} }
#endif
