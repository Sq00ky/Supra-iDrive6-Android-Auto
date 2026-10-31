// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef TOUCH_TYPES_HPP
#define TOUCH_TYPES_HPP

namespace nbt {

// One decoded touch event as sent to the phone (Android Auto InputEventIndication / TouchEvent).
// action: 0 = PRESS (first finger down), 1 = RELEASE (last finger up), 2 = DRAG (move),
//         5 = POINTER_DOWN (additional finger), 6 = POINTER_UP (a finger lifted, others stay).
// p[] lists all fingers that are down at that moment (the lifting finger included for UP events);
// index is the position in p[] of the finger the action refers to.
struct TouchPointer { int id; int x; int y; };
struct TouchEventOut {
    int n;
    TouchPointer p[5];   // up to kTouchMaxContacts fingers
    int action;
    int index;
};

} // namespace nbt

#endif
