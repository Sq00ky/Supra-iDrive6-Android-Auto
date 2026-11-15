// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_FAVOURITE_STRIP_HPP
#define NBT_FAVOURITE_STRIP_HPP
#include <stdio.h>
#include <stddef.h>
namespace nbt {
struct FavouriteStripRegion { int x, y, w, h; };
// Derive the native strip boundary. AA uses preview=false and keeps its full geometry;
// the helper uses the boundary to copy only the native top strip to a separate surface.
// 64/540 is the reference calibration, not a universal native-widget size.
inline FavouriteStripRegion favouriteStripRegion(int width, int height, bool preview, int overrideHeight = 0) {
    FavouriteStripRegion r = { 0, 0, width, height };
    if (!preview || width <= 0 || height < 3) return r;
    int bar = overrideHeight;
    if (bar <= 0 || bar > height / 3) bar = (64 * height + 270) / 540;
    if (bar < 1) bar = 1;
    if (bar >= height) bar = height - 1;
    r.y = bar; r.h = height - bar;
    return r;
}
inline void favouriteRenderOrder(char* out, size_t size, unsigned aa, int pill, bool preview) {
    if (pill > 0)
        snprintf(out, size, preview ? "0x%x,0x503,0x%x,0x100" : "0x%x,0x%x", (unsigned)pill, aa);
    else
        snprintf(out, size, preview ? "0x503,0x%x,0x100" : "0x%x", aa);
}
}
#endif
