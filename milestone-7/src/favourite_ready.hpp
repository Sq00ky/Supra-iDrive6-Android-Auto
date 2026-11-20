// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_FAVOURITE_READY_HPP
#define NBT_FAVOURITE_READY_HPP
#include <stdint.h>
#include <stddef.h>
namespace nbt {
// This BMW ID6 uses an orange selected-favourite banner. Its ordinary AA app
// header has no such banner. Gate copies on the banner rather than a fixed
// delay: HMI rendering can take longer than the first few captures.
inline bool favouriteLabelsReady(const uint32_t* pixels, int w, int bar, int stride) {
    if (!pixels || w <= 0 || bar <= 0 || stride < w) return false;
    size_t orange = 0, required = (size_t)w * bar / 50;
    if (required < 8) required = 8;
    for (int y = 0; y < bar; ++y)
        for (int x = 0; x < w; ++x) {
            uint32_t p = pixels[(size_t)y * stride + x];
            unsigned r = p & 255u, g = (p >> 8) & 255u, b = (p >> 16) & 255u;
            if ((p >> 24) >= 240 && r >= 64 && g * 100 < r * 60 && b * 100 < r * 40)
                if (++orange >= required) return true;
        }
    return false;
}
// In this panel layout each collapsed favourite preceding the selected banner
// occupies 108/1440 of the width. Find the banner's left edge, not its text:
// reassigned labels and icons do not change which button the image belongs to.
inline int favouriteSelectedButton(const uint32_t* pixels, int w, int bar, int stride) {
    if (!favouriteLabelsReady(pixels,w,bar,stride)) return 0;
    int tile = (w * 108 + 720) / 1440;
    if (tile < 1) return 0;
    for (int x=0;x<w;++x) {
        int orange=0;
        for (int y=0;y<bar*7/8;++y) {
            uint32_t p=pixels[(size_t)y*stride+x];
            unsigned r=p&255u,g=(p>>8)&255u,b=(p>>16)&255u;
            if ((p>>24)>=240 && r>=64 && g*100<r*60 && b*100<r*40) ++orange;
        }
        if (orange >= bar/5) {
            int button=(x+tile/4)/tile+1;
            return button>=1&&button<=8?button:0;
        }
    }
    return 0;
}
inline int favouriteButtonFromMask(unsigned mask) {
    if (!mask || (mask & (mask-1)) || mask>128) return 0;
    int n=1;while(mask>1){mask>>=1;++n;}return n;
}
}
#endif
