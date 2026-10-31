// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: LayerManager surface geometry parsing / saving (src/lm_geometry.hpp).
#include "lm_geometry.hpp"
#include <assert.h>
#include <stdio.h>
using namespace nbt;

int main() {
    // GENIVI LayerManagerControl "get surface" layout
    const char* out =
        "surface 1280 (0x500)\n"
        "---------------------------------------\n"
        "- created by pid:       16838\n"
        "- original size:      x=1180, y=480\n"
        "- destination region: x=0, y=0, w=1180, h=480\n"
        "- source region:      x=0, y=0, w=1180, h=480\n"
        "- opacity:            1\n"
        "- visibility:         0\n";
    LmGeometry g = lmParseSurface(out);
    assert(g.src.valid && g.dst.valid);
    assert(g.src.w == 1180 && g.src.h == 480 && g.dst.x == 0 && g.dst.w == 1180);
    // plain numbers, other spacing
    LmGeometry h = lmParseSurface("source region 0 0 1280 480\ndestination region 10 20 1440 540\n");
    assert(h.src.w == 1280 && h.dst.x == 10 && h.dst.y == 20 && h.dst.h == 540);
    // surface missing / garbage
    LmGeometry e = lmParseSurface("Error: surface 1280 not found\n");
    assert(!e.src.valid && !e.dst.valid);
    assert(!lmParseSurface("source region: x=0, y=0\n").src.valid);
    // save / load
    assert(lmSave("/tmp/nbt_lm_test", g));
    LmGeometry l;
    assert(lmLoad("/tmp/nbt_lm_test", l) && l.src.w == 1180 && l.dst.h == 480);
    assert(!lmLoad("/tmp/does-not-exist-lm", l));
    printf("test_lm_geometry OK\n");
    return 0;
}
