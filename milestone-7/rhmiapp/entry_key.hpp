// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// The "Open Android Auto" entry (raAction 2, the entry a favourite key stores): what one press does. Pure decision
// logic (host-tested in tests/test_entry_key.cpp).
#ifndef NBT_AAD_ENTRY_KEY_HPP
#define NBT_AAD_ENTRY_KEY_HPP

namespace nbt {
namespace entrykey {

enum Action {
    kNothing,       // Android Auto is already on screen
    kShow,          // connected: bring Android Auto to the front
    kMenu,          // starting up, waiting for the phone, or stopping: open our menu so its status shows
    kStartAndMenu   // not running: start nbt-aad and open our menu ("Connecting..." / Force Stop / Restart at hand)
};

// running: nbt-aad is up (any state). connected: its state is "connected". onScreen: its overlay is up.
// stopping: a Force Stop / Restart is in progress.
inline Action decide(bool running, bool connected, bool onScreen, bool stopping) {
    if (stopping) return kMenu;
    if (!running) return kStartAndMenu;
    if (!connected) return kMenu;
    return onScreen ? kNothing : kShow;
}

// The entry started nbt-aad and the phone has just connected: bring Android Auto to the front only while our menu is
// still on top (the driver is waiting for it there) and the overlay isn't up yet. Elsewhere: leave the screen alone.
inline bool showOnConnect(bool menuFocusKnown, bool menuFocused, bool onScreen) {
    return menuFocusKnown && menuFocused && !onScreen;
}

}  // namespace entrykey
}  // namespace nbt

#endif
