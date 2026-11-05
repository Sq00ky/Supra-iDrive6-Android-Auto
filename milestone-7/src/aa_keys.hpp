// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AA_KEYS_HPP
#define AA_KEYS_HPP

namespace nbt {

// Android key codes, plus Android Auto's own above 65535. The manifest declares all of them on input channel 7.
enum AaKeyCode {
    AAK_SOFT_LEFT = 1,
    AAK_SOFT_RIGHT = 2,
    AAK_HOME = 3,
    AAK_BACK = 4,
    AAK_CALL = 5,
    AAK_ENDCALL = 6,
    AAK_0 = 7,
    AAK_1 = 8,
    AAK_2 = 9,
    AAK_3 = 10,
    AAK_4 = 11,
    AAK_5 = 12,
    AAK_6 = 13,
    AAK_7 = 14,
    AAK_8 = 15,
    AAK_9 = 16,
    AAK_STAR = 17,
    AAK_POUND = 18,
    AAK_DPAD_UP = 19,
    AAK_DPAD_DOWN = 20,
    AAK_DPAD_LEFT = 21,
    AAK_DPAD_RIGHT = 22,
    AAK_DPAD_CENTER = 23,
    AAK_PLUS = 81,
    AAK_SEARCH = 84,
    AAK_MEDIA_PLAY_PAUSE = 85,
    AAK_MEDIA_NEXT = 87,
    AAK_MEDIA_PREVIOUS = 88,
    AAK_MEDIA_PLAY = 126,
    AAK_MEDIA_PAUSE = 127,
    AAK_DPAD_UP_LEFT = 268,
    AAK_DPAD_DOWN_LEFT = 269,
    AAK_DPAD_UP_RIGHT = 270,
    AAK_DPAD_DOWN_RIGHT = 271,
    AAK_ROTARY_CONTROLLER = 65536,
    AAK_MEDIA = 65537,
    AAK_NAVIGATION = 65538,
    AAK_TEL = 65540,
    AAK_UNKNOWN_65541 = 65541,
    AAK_UNKNOWN_65542 = 65542,
    AAK_UNKNOWN_65543 = 65543
};

} // namespace nbt

#endif
