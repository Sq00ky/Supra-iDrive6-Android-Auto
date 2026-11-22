// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Deliberately return this translation unit's arrays, rather than an inline selector chosen by the linker.
#include "cluster/mapbox_hud.hpp"

const nbt::hudterm::mbx::Piece* mapboxOtherTuGlyph(int family) {
    return family==5 ? nbt::hudterm::mbx::kArrowsharpright : nbt::hudterm::mbx::kArrow180right;
}

const nbt::hudterm::mbx::CurveBody* mapboxOtherTuBody(int family) {
    return &nbt::hudterm::mbx::kCurveBodies[family==5 ? 0 : 1];
}

bool mapboxOtherTuBounds(int family, nbt::hudterm::Rect& bounds) {
    return nbt::hudterm::mbxOriginalBounds(family,bounds);
}
