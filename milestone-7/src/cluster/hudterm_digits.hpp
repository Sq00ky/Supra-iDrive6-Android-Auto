// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUDTERM_DIGITS_HPP
#define CLUSTER_HUDTERM_DIGITS_HPP

// Digits drawn as filled polygons, for numbers a HUDTerminal cluster shows in a picture rather than a text (a
// roundabout's exit number in the junction view; the speed later). Square, like the cluster's own distance digits, in
// a cell 100 wide and 180 tall with a stroke of 28. A polygon can't have a hole, so 0, 6, 8 and 9 are two halves that
// overlap; every other digit is one polygon. Items of one colour may overlap, so nothing needs outline maths.
// Integer-only like cluster/hudterm_shapes.hpp.

#include "cluster/hudterm_shapes.hpp"

namespace nbt {
namespace hudterm {
namespace digits {

enum { kCellW = 100, kCellH = 180, kStroke = 28 };

// One glyph: up to two polygons, each as x, y pairs in the cell; a count of 0 ends the glyph.
struct Glyph {
    int n[2];
    int16_t xy[2][24];
};

namespace detail {
enum { W = kCellW, H = kCellH, S = kStroke, M = (H - S) / 2, M2 = M + S };
}

inline const Glyph& glyph(int d) {
    using namespace detail;
    static const Glyph g[10] = {
        { { 8, 8 }, { { 0,0, 56,0, 56,S, S,S, S,H-S, 56,H-S, 56,H, 0,H },
                      { 44,0, W,0, W,H, 44,H, 44,H-S, W-S,H-S, W-S,S, 44,S } } },
        { { 7, 0 }, { { 50,0, 78,0, 78,H, 50,H, 50,36, 26,50, 26,24 }, { 0 } } },
        { { 12, 0 }, { { 0,0, W,0, W,M2, S,M2, S,H-S, W,H-S, W,H, 0,H, 0,M, W-S,M, W-S,S, 0,S }, { 0 } } },
        { { 12, 0 }, { { 0,0, W,0, W,H, 0,H, 0,H-S, W-S,H-S, W-S,M2, 20,M2, 20,M, W-S,M, W-S,S, 0,S }, { 0 } } },
        { { 10, 0 }, { { 0,0, S,0, S,M, W-S,M, W-S,0, W,0, W,H, W-S,H, W-S,M2, 0,M2 }, { 0 } } },
        { { 12, 0 }, { { 0,0, W,0, W,S, S,S, S,M, W,M, W,H, 0,H, 0,H-S, W-S,H-S, W-S,M2, 0,M2 }, { 0 } } },
        { { 8, 8 }, { { 0,0, W,0, W,S, S,S, S,H-S, 56,H-S, 56,H, 0,H },
                      { 14,M, W,M, W,H, 44,H, 44,H-S, W-S,H-S, W-S,M2, 14,M2 } } },
        { { 6, 0 }, { { 0,0, W,0, W,H, W-S,H, W-S,S, 0,S }, { 0 } } },
        { { 12, 12 }, { { 0,0, 56,0, 56,S, S,S, S,M, 56,M, 56,M2, S,M2, S,H-S, 56,H-S, 56,H, 0,H },
                        { 44,0, W,0, W,H, 44,H, 44,H-S, W-S,H-S, W-S,M2, 44,M2, 44,M, W-S,M, W-S,S, 44,S } } },
        // 9: the 6 turned half a turn
        { { 8, 8 }, { { W,H, 0,H, 0,H-S, W-S,H-S, W-S,S, W-56,S, W-56,0, W,0 },
                      { W-14,H-M, 0,H-M, 0,0, W-44,0, W-44,S, S,S, S,H-M2, W-14,H-M2 } } },
    };
    return g[d < 0 || d > 9 ? 0 : d];
}

// Glyph width at height h, and the gap between digits.
inline int width(int h) { return (kCellW * h + kCellH / 2) / kCellH; }
inline int gap(int h) { return (h * 18 + 50) / 100; }

// The number n (0..999), h tall with its top at y0, centred on cx: its polygons appended to out.
inline void drawNumber(int n, int cx, int y0, int h, Shape& out) {
    if (n < 0) n = 0;
    if (n > 999) n = 999;
    int ds[3], k = 0;
    do { ds[k++] = n % 10; n /= 10; } while (n && k < 3);
    int w = width(h), g = gap(h), total = k * w + (k - 1) * g;
    int x = cx - total / 2;
    for (int i = k - 1; i >= 0; --i, x += w + g) {
        const Glyph& gl = glyph(ds[i]);
        for (int p = 0; p < 2 && gl.n[p]; ++p) {
            Poly q;
            q.type = kPolygon;
            for (int j = 0; j < gl.n[p]; ++j)
                q.pts.push_back(shapes::pt(x + (gl.xy[p][2 * j] * h + kCellH / 2) / kCellH,
                                           y0 + (gl.xy[p][2 * j + 1] * h + kCellH / 2) / kCellH));
            out.push_back(q);
        }
    }
}

} // namespace digits
} // namespace hudterm
} // namespace nbt

#endif
