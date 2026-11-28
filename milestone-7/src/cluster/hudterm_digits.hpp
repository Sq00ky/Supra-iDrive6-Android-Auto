// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUDTERM_DIGITS_HPP
#define CLUSTER_HUDTERM_DIGITS_HPP

// Original geometric digit outlines in a 100 x 180 cell, with deliberate diagonal chamfers and a 28-unit stem.
// A filled polygon cannot have a hole: counter glyphs use two simple pieces; other digits use one outline.
// The static table needs no font asset or runtime font library.

#include "cluster/hudterm_shapes.hpp"

namespace nbt {
namespace hudterm {
namespace digits {

enum { kCellW = 100, kCellH = 180, kStroke = 28 };

// At most two simple polygons per digit; a zero count ends the glyph.
struct Glyph {
    int n[2];
    int16_t xy[2][46];
};

inline const Glyph& glyph(int d) {
    static const Glyph g[10] = {
        { { 10, 10 }, { // 0
            { 20, 0, 0, 20, 0, 160, 20, 180, 80, 180, 64, 152, 36, 152, 28, 144, 28, 36, 36, 28 },
            { 20, 0, 80, 0, 100, 20, 100, 160, 80, 180, 64, 152, 72, 144, 72, 36, 64, 28, 36, 28 }
        } },
        { { 11, 0 }, { // 1
            { 16, 28, 44, 0, 72, 0, 72, 154, 96, 154, 96, 180, 4, 180, 4, 154, 44, 154, 44, 40,
              16, 68 },
            { 0 }
        } },
        { { 19, 0 }, { // 2
            { 0, 20, 20, 0, 76, 0, 100, 24, 100, 76, 76, 100, 28, 100, 28, 152, 80, 152, 100, 172,
              100, 180, 0, 180, 0, 96, 24, 72, 64, 72, 72, 64, 72, 36, 64, 28, 0, 28 },
            { 0 }
        } },
        { { 23, 0 }, { // 3
            { 0, 20, 20, 0, 76, 0, 100, 24, 100, 64, 82, 82, 100, 100, 100, 156, 76, 180, 20, 180,
              0, 160, 0, 152, 64, 152, 72, 144, 72, 116, 60, 104, 28, 104, 28, 76, 60, 76, 72, 64,
              72, 36, 64, 28, 0, 28 },
            { 0 }
        } },
        { { 7, 8 }, { // 4
            { 0, 110, 64, 0, 94, 0, 94, 110, 66, 110, 66, 50, 34, 110 },
            { 0, 110, 100, 110, 100, 138, 94, 138, 94, 180, 66, 180, 66, 138, 0, 138 }
        } },
        { { 18, 0 }, { // 5
            { 0, 0, 80, 0, 100, 20, 100, 28, 28, 28, 28, 76, 76, 76, 100, 100, 100, 156, 76, 180,
              0, 180, 0, 172, 20, 152, 64, 152, 72, 144, 72, 112, 64, 104, 0, 104 },
            { 0 }
        } },
        { { 16, 10 }, { // 6
            { 0, 156, 0, 24, 24, 0, 80, 0, 100, 20, 100, 28, 36, 28, 28, 36, 28, 76, 76, 76,
              100, 100, 72, 112, 64, 104, 36, 104, 28, 112, 28, 144 },
            { 0, 156, 24, 180, 76, 180, 100, 156, 100, 100, 72, 112, 72, 144, 64, 152, 36, 152, 28, 144 }
        } },
        { { 7, 0 }, { // 7
            { 0, 0, 100, 0, 100, 28, 44, 180, 12, 180, 68, 28, 0, 28 },
            { 0 }
        } },
        { { 18, 18 }, { // 8
            { 20, 0, 0, 20, 0, 70, 20, 90, 0, 110, 0, 160, 20, 180, 80, 180, 64, 152, 36, 152,
              28, 144, 28, 112, 36, 104, 64, 76, 36, 76, 28, 68, 28, 36, 36, 28 },
            { 20, 0, 80, 0, 100, 20, 100, 70, 80, 90, 100, 110, 100, 160, 80, 180, 64, 152, 72, 144,
              72, 112, 64, 104, 36, 104, 64, 76, 72, 68, 72, 36, 64, 28, 36, 28 }
        } },
        { { 10, 16 }, { // 9
            { 0, 80, 0, 24, 24, 0, 76, 0, 100, 24, 72, 36, 64, 28, 36, 28, 28, 36, 28, 68 },
            { 0, 80, 24, 104, 72, 104, 72, 144, 64, 152, 0, 152, 0, 160, 20, 180, 76, 180, 100, 156,
              100, 24, 72, 36, 72, 68, 64, 76, 36, 76, 28, 68 }
        } },
    };
    return g[d < 0 || d > 9 ? 0 : d];
}

// Glyph width at height h, and the gap between digits.
inline int width(int h) { return (kCellW * h + kCellH / 2) / kCellH; }
inline int gap(int h) { return (h * 18 + 50) / 100; }

// The number n (0..999), h tall with its top at y0, centred on cx: its polygons appended to out.
inline void drawNumber(int n, int cx, int y0, int h, Shape& out) {
    if (h <= 0) return;
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
            for (int j = 0; j < gl.n[p]; ++j) {
                Point point = shapes::pt(x + (gl.xy[p][2 * j] * h + kCellH / 2) / kCellH,
                                        y0 + (gl.xy[p][2 * j + 1] * h + kCellH / 2) / kCellH);
                if (q.pts.empty() || point.x != q.pts.back().x || point.y != q.pts.back().y) q.pts.push_back(point);
            }
            if (q.pts.size() > 1 && q.pts.front().x == q.pts.back().x && q.pts.front().y == q.pts.back().y)
                q.pts.pop_back();
            // Pixel fitting can collapse a corner to a zero-width spur; removing it preserves the filled area.
            for (size_t j = 0; q.pts.size() > 3 && j < q.pts.size();) {
                const Point& a = q.pts[j ? j - 1 : q.pts.size() - 1];
                const Point& b = q.pts[j];
                const Point& c = q.pts[j + 1 < q.pts.size() ? j + 1 : 0];
                long turn = (long)(b.x - a.x) * (c.y - b.y) - (long)(b.y - a.y) * (c.x - b.x);
                long dot = (long)(b.x - a.x) * (c.x - b.x) + (long)(b.y - a.y) * (c.y - b.y);
                if ((b.x == c.x && b.y == c.y) || (!turn && dot < 0)) {
                    q.pts.erase(q.pts.begin() + j);
                    j = 0;
                } else ++j;
            }
            long area = 0;
            for (size_t j = 0; j < q.pts.size(); ++j) {
                const Point& a = q.pts[j];
                const Point& b = q.pts[j + 1 < q.pts.size() ? j + 1 : 0];
                area += (long)a.x * b.y - (long)b.x * a.y;
            }
            if (area) out.push_back(q);
        }
    }
}

} // namespace digits
} // namespace hudterm
} // namespace nbt

#endif
