// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef LM_GEOMETRY_HPP
#define LM_GEOMETRY_HPP

// LayerManager surface 1280 geometry, shared with the factory CarPlay.
//
// Surface "1280" is the window name from carplay.ini (WindowName=1280): native CarPlay renders into a window with the
// same name, so the LayerManagerService keeps ONE set of source/destination regions for both. While Android Auto is
// shown we set them to the panel size (NBT_SCREEN, e.g. 1280x480); the factory CarPlay of a car coded 1180_480 uses
// 1180x480. Left at ours, CarPlay comes back zoomed in until a reboot (MINI F56, 2026-10-27). So the daemon puts the
// factory regions back whenever it hides the overlay.
//
// Factory values, first hit wins:
//   1. /dev/shmem/nbt-aad-lm1280: what `LayerManagerControl get surface 1280` reported the first time a daemon ran
//      after boot (before we changed anything; shmem is wiped on reboot, so a restarted daemon never re-reads
//      geometry an earlier run left behind)
//   2. carplay.ini [Screen] Width x Height at 0,0 (the CarPlay VideoURL dstx/dsty/dstw/dsth)
// Plain C++98, host-testable (tests/test_lm_geometry.cpp).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace nbt {

struct LmRect {
    int x, y, w, h;
    bool valid;
};

struct LmGeometry {
    LmRect src, dst;
};

// First four integers after `key` on the same line ("- source region: x=0, y=0, w=1180, h=480").
inline LmRect lmParseRegion(const char* text, const char* key) {
    LmRect r;
    r.x = r.y = r.w = r.h = 0;
    r.valid = false;
    const char* p = text ? strstr(text, key) : NULL;
    if (!p) return r;
    p += strlen(key);
    int v[4], n = 0;
    while (*p && *p != '\n' && n < 4) {
        if ((*p >= '0' && *p <= '9') || (*p == '-' && p[1] >= '0' && p[1] <= '9')) {
            v[n++] = (int)strtol(p, (char**)&p, 10);
        } else {
            ++p;
        }
    }
    if (n == 4 && v[2] > 0 && v[3] > 0 && v[2] <= 4096 && v[3] <= 2160) {
        r.x = v[0]; r.y = v[1]; r.w = v[2]; r.h = v[3];
        r.valid = true;
    }
    return r;
}

// Parses `LayerManagerControl get surface <id>` output.
inline LmGeometry lmParseSurface(const char* text) {
    LmGeometry g;
    g.src = lmParseRegion(text, "source region");
    g.dst = lmParseRegion(text, "destination region");
    return g;
}

// Saved form: "src x y w h dst x y w h".
inline bool lmLoad(const char* path, LmGeometry& g) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    int n = fscanf(f, "src %d %d %d %d dst %d %d %d %d", &g.src.x, &g.src.y, &g.src.w, &g.src.h,
                   &g.dst.x, &g.dst.y, &g.dst.w, &g.dst.h);
    fclose(f);
    g.src.valid = g.dst.valid = (n == 8 && g.src.w > 0 && g.src.h > 0 && g.dst.w > 0 && g.dst.h > 0);
    return g.src.valid;
}

inline bool lmSave(const char* path, const LmGeometry& g) {
    if (!g.src.valid || !g.dst.valid) return false;
    FILE* f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "src %d %d %d %d dst %d %d %d %d\n", g.src.x, g.src.y, g.src.w, g.src.h, g.dst.x, g.dst.y, g.dst.w, g.dst.h);
    fclose(f);
    return true;
}

} // namespace nbt

#endif // LM_GEOMETRY_HPP
