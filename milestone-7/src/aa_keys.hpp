// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AA_KEYS_HPP
#define AA_KEYS_HPP

namespace nbt {

// Android key codes / Android Auto extended key codes (all declared on input channel 7 in the SDR).
enum {
    AAK_HOME = 3, AAK_BACK = 4, AAK_CALL = 5,
    AAK_DPAD_UP = 19, AAK_DPAD_DOWN = 20, AAK_DPAD_LEFT = 21, AAK_DPAD_RIGHT = 22, AAK_DPAD_CENTER = 23,
    AAK_SEARCH = 84,
    AAK_ROTARY_CONTROLLER = 65536, AAK_MEDIA = 65537, AAK_NAVIGATION = 65538, AAK_TEL = 65540
};

} // namespace nbt

#endif
