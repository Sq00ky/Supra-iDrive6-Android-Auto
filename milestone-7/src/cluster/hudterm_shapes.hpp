// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUDTERM_SHAPES_HPP
#define CLUSTER_HUDTERM_SHAPES_HPP

// Maneuver pictures for HUDTerminal clusters (cluster/hudterm.hpp): our own geometry, drawn as filled polygons. Pure and
// integer-only (the daemon doesn't link libm); host-tested in tests/test_hud6wa.cpp, which draws every maneuver type
// and checks each item keeps the cluster's limits and stays inside the box it is fitted to.
//
// A maneuver becomes a ShapeSpec (its family, from the groups kombiDirection() already makes of the Android Auto types),
// the spec is drawn in a design square of 0..1000 with y down and the car coming up from the bottom edge, and the
// drawing is fitted into a driver's box on the cluster. Every item is one ImageAddPolygon; items of one picture may
// overlap (they share one fill colour), so a bent arrow is a stem, a round joint and an arrow, with no outline maths.
//
// Families:
//   arrow       stem up from the bottom, then an arrow at the turn angle (0 straight on, + to the right, up to 180),
//               with an optional thin stub for the road not taken (a fork's other branch, the highway on an exit)
//   u-turn      stem up, a half ring over the top, an arrow down on the side of the turn
//   roundabout  stem up into a ring, an arrow out of the ring at the exit; the exit's place on the ring follows the
//               direction of travel and the phone's exit angle (straight across when it sends none)
//   flag        the destination: a pole and a swallow-tailed flag

#include "cluster/hudterm.hpp"
#include "cluster/nav_msg.hpp"
#include <stdint.h>
#include <vector>

namespace nbt {
namespace hudterm {

enum { kDesign = 1000 };   // the design square's side
const int kNoStub = 999;

struct Rect {
    int16_t x0, y0, x1, y1;   // inclusive
};

struct Poly {
    PolyType type;
    std::vector<Point> pts;
};
typedef std::vector<Poly> Shape;

enum ShapeKind { kShapeArrow, kShapeUTurn, kShapeRoundabout, kShapeFlag };

struct ShapeSpec {
    ShapeKind kind;
    int angle;        // arrow: degrees, 0 straight on, + right, -180..180; u-turn: -1 left, +1 right
    int stub;         // arrow: the road not taken, degrees like angle, or kNoStub
    bool clockwise;   // roundabout: the direction of travel round it
    int exit;         // roundabout: degrees from the entry to the exit in the direction of travel, 1..360
    ShapeSpec() : kind(kShapeArrow), angle(0), stub(kNoStub), clockwise(false), exit(180) {}
    bool operator==(const ShapeSpec& o) const {
        return kind == o.kind && angle == o.angle && stub == o.stub && clockwise == o.clockwise && exit == o.exit;
    }
    bool operator!=(const ShapeSpec& o) const { return !(*this == o); }
};

// ---- integer trigonometry -------------------------------------------------------------------------------------------

// sin(deg) * 4096, any whole degree.
inline int isin(int deg) {
    static const int16_t kSin[91] = {
           0,   71,  143,  214,  286,  357,  428,  499,  570,  641,  711,  782,  852,  921,  991, 1060,
        1129, 1198, 1266, 1334, 1401, 1468, 1534, 1600, 1666, 1731, 1796, 1860, 1923, 1986, 2048, 2110,
        2171, 2231, 2290, 2349, 2408, 2465, 2522, 2578, 2633, 2687, 2741, 2793, 2845, 2896, 2946, 2996,
        3044, 3091, 3138, 3183, 3228, 3271, 3314, 3355, 3396, 3435, 3474, 3511, 3547, 3582, 3617, 3650,
        3681, 3712, 3742, 3770, 3798, 3824, 3849, 3873, 3896, 3917, 3937, 3956, 3974, 3991, 4006, 4021,
        4034, 4046, 4056, 4065, 4074, 4080, 4086, 4090, 4094, 4095, 4096,
    };
    int d = deg % 360;
    if (d < 0) d += 360;
    if (d <= 90) return kSin[d];
    if (d <= 180) return kSin[180 - d];
    if (d <= 270) return -kSin[d - 180];
    return -kSin[360 - d];
}
inline int icos(int deg) { return isin(deg + 90); }

// r * f / 4096, rounded half away from zero.
inline int mulTrig(int r, int f) {
    long p = (long)r * f;
    return (int)(p >= 0 ? (p + 2048) / 4096 : -((-p + 2048) / 4096));
}

// ---- maneuver -> spec -----------------------------------------------------------------------------------------------

// Android Auto maneuver type (and the roundabout exit angle) -> picture. The families are kombiDirection()'s groups, so
// the 6WA and the J29 read a maneuver the same way.
inline ShapeSpec shapeFor(int aa_type, bool has_exit_angle, int64_t exit_angle) {
    ShapeSpec s;
    switch (kombinav::kombiDirection(aa_type)) {
        case 2: s.angle = -45; break;                             // slight left, keep left
        case 3: s.angle = 45; break;
        case 4: s.angle = -135; break;                            // sharp
        case 5: s.angle = 135; break;
        case 6: s.angle = -90; break;
        case 7: s.angle = 90; break;
        case 8: s.kind = kShapeUTurn; s.angle = -1; break;
        case 9: s.kind = kShapeUTurn; s.angle = 1; break;
        case 10: s.kind = kShapeFlag; break;                      // destination
        case 14: case 15:
            s.kind = kShapeRoundabout;
            s.clockwise = kombinav::kombiDirection(aa_type) == 14;
            if (has_exit_angle && exit_angle >= 1 && exit_angle <= 360) s.exit = (int)exit_angle;
            break;
        case 25: s.angle = -45; break;                            // on ramp
        case 26: s.angle = 45; break;
        case 28: s.angle = -45; s.stub = 20; break;               // off ramp: the highway goes on (bent away,
                                                                  // so the head stays clear of it)
        case 29: s.angle = 45; s.stub = -20; break;
        case 32: s.angle = -30; break;                            // merge
        case 33: s.angle = 30; break;
        case 37: s.angle = -40; s.stub = 40; break;               // fork: the other branch as a stub
        case 38: s.angle = 40; s.stub = -40; break;
        default: break;                                           // straight on
    }
    return s;
}

// For the log: "arrow +90", "arrow -45 stub 0", "u-turn left", "roundabout ccw exit 90", "flag".
inline std::string describeSpec(const ShapeSpec& s) {
    char b[48];
    switch (s.kind) {
        case kShapeUTurn: snprintf(b, sizeof(b), "u-turn %s", s.angle < 0 ? "left" : "right"); break;
        case kShapeRoundabout: snprintf(b, sizeof(b), "roundabout %s exit %d", s.clockwise ? "cw" : "ccw", s.exit); break;
        case kShapeFlag: snprintf(b, sizeof(b), "flag"); break;
        default:
            if (s.stub == kNoStub) snprintf(b, sizeof(b), "arrow %+d", s.angle);
            else snprintf(b, sizeof(b), "arrow %+d stub %+d", s.angle, s.stub);
            break;
    }
    return b;
}

// ---- drawing in the design square -----------------------------------------------------------------------------------

namespace shapes {

const int kShaft = 180;          // arrow and stem width (the car-proven test arrow: 20 of its 100 px)
const int kHeadW = 400, kHeadL = 240;
const int kStubW = 90, kStubL = 220;   // short, so a slight turn's head clears it
const int kBottom = 960;         // where every stem starts

inline Point pt(int x, int y) { Point p; p.x = (int16_t)x; p.y = (int16_t)y; return p; }

// A point at distance u along heading `deg` (0 up, + clockwise on screen) and v to its right, from (x, y).
inline Point along(int x, int y, int deg, int u, int v) {
    return pt(x + mulTrig(u, isin(deg)) + mulTrig(v, icos(deg)), y - mulTrig(u, icos(deg)) + mulTrig(v, isin(deg)));
}

inline Poly poly(const std::vector<Point>& p) { Poly q; q.type = kPolygon; q.pts = p; return q; }

// A bar of width w from (x, y), `len` long on heading deg.
inline Poly bar(int x, int y, int deg, int len, int w) {
    std::vector<Point> p;
    p.push_back(along(x, y, deg, 0, -w / 2));
    p.push_back(along(x, y, deg, len, -w / 2));
    p.push_back(along(x, y, deg, len, w / 2));
    p.push_back(along(x, y, deg, 0, w / 2));
    return poly(p);
}

// An arrow from (x, y), `len` to its tip on heading deg: a shaft and a head, 7 points.
inline Poly arrow(int x, int y, int deg, int len, int headW = kHeadW, int headL = kHeadL) {
    int b = len - headL;
    std::vector<Point> p;
    p.push_back(along(x, y, deg, 0, -kShaft / 2));
    p.push_back(along(x, y, deg, b, -kShaft / 2));
    p.push_back(along(x, y, deg, b, -headW / 2));
    p.push_back(along(x, y, deg, len, 0));
    p.push_back(along(x, y, deg, b, headW / 2));
    p.push_back(along(x, y, deg, b, kShaft / 2));
    p.push_back(along(x, y, deg, 0, kShaft / 2));
    return poly(p);
}

// A regular n-gon of radius r round (x, y): the round joint where a stem meets a bent arrow.
inline Poly disc(int x, int y, int r, int n) {
    std::vector<Point> p;
    for (int i = 0; i < n; ++i) p.push_back(along(x, y, i * 360 / n, r, 0));
    return poly(p);
}

// The band between radii r0 < r1 round (x, y) from heading a0 to a1 (degrees, a0 < a1, clockwise on screen), in
// `steps` steps: outer edge one way, inner edge back.
inline Poly band(int x, int y, int r0, int r1, int a0, int a1, int steps) {
    std::vector<Point> p;
    for (int i = 0; i <= steps; ++i) p.push_back(along(x, y, a0 + (a1 - a0) * i / steps, r1, 0));
    for (int i = steps; i >= 0; --i) p.push_back(along(x, y, a0 + (a1 - a0) * i / steps, r0, 0));
    return poly(p);
}

inline void drawArrow(const ShapeSpec& s, Shape& out) {
    int a = s.angle < -180 ? -180 : s.angle > 180 ? 180 : s.angle;
    int m = a < 0 ? -a : a, side = a < 0 ? -1 : 1;
    // The junction moves away from the turn so the picture stays centred; sharp turns start higher and run longer, so
    // the head coming back down clears the stem.
    int shift = m < 23 ? 0 : m < 68 ? 100 : 180;
    int jx = 500 - side * shift, jy = m > 112 ? 420 : 520, len = m > 150 ? 520 : m > 112 ? 640 : 420;
    out.push_back(bar(jx, kBottom, 0, kBottom - jy, kShaft));
    if (s.stub != kNoStub) out.push_back(bar(jx, jy, s.stub, kStubL, kStubW));
    if (m) out.push_back(disc(jx, jy, kShaft / 2, 12));
    out.push_back(arrow(jx, jy, a, len));
}

inline void drawUTurn(const ShapeSpec& s, Shape& out) {
    int side = s.angle < 0 ? -1 : 1, r = 200, cy = 420;
    int stemX = 500 - side * r, downX = 500 + side * r;
    out.push_back(bar(stemX, kBottom, 0, kBottom - cy, kShaft));
    out.push_back(band(500, cy, r - kShaft / 2, r + kShaft / 2, -90, 90, 10));   // the top half, either way round
    out.push_back(arrow(downX, cy, 180, 420));
}

inline void drawRoundabout(const ShapeSpec& s, Shape& out) {
    const int cx = 500, cy = 420, r0 = 120, r1 = 190, rm = (r0 + r1) / 2;
    out.push_back(bar(cx, kBottom, 0, kBottom - (cy + rm), kShaft));
    out.push_back(band(cx, cy, r0, r1, 0, 180, 12));
    out.push_back(band(cx, cy, r0, r1, 180, 360, 12));
    // The entry is at the bottom (heading 180). Anticlockwise travel (right-hand traffic) reaches the right-hand side
    // first, so its exit heading is 180 - exit; clockwise is 180 + exit.
    int h = s.clockwise ? 180 + s.exit : 180 - s.exit;
    h %= 360;
    if (h < 0) h += 360;
    Point from = along(cx, cy, h, rm, 0);
    out.push_back(arrow(from.x, from.y, h, 245, 340, 180));
}

inline void drawFlag(Shape& out) {
    out.push_back(bar(400, 900, 0, 740, 60));
    std::vector<Point> f;
    f.push_back(pt(430, 170));
    f.push_back(pt(800, 170));
    f.push_back(pt(700, 305));
    f.push_back(pt(800, 440));
    f.push_back(pt(430, 440));
    out.push_back(poly(f));
}

} // namespace shapes

// The picture in the design square (0..1000, y down).
inline void drawShape(const ShapeSpec& s, Shape& out) {
    out.clear();
    switch (s.kind) {
        case kShapeUTurn: shapes::drawUTurn(s, out); break;
        case kShapeRoundabout: shapes::drawRoundabout(s, out); break;
        case kShapeFlag: shapes::drawFlag(out); break;
        default: shapes::drawArrow(s, out); break;
    }
}

// Design square -> the box on the cluster (its corners map to the box's corners). Points outside the square are
// clamped to it first, so nothing ever lands outside the box.
inline void fitShape(const Shape& in, const Rect& box, Shape& out) {
    out = in;
    int w = box.x1 - box.x0, h = box.y1 - box.y0;
    for (size_t i = 0; i < out.size(); ++i)
        for (size_t k = 0; k < out[i].pts.size(); ++k) {
            int x = out[i].pts[k].x, y = out[i].pts[k].y;
            x = x < 0 ? 0 : x > kDesign ? kDesign : x;
            y = y < 0 ? 0 : y > kDesign ? kDesign : y;
            out[i].pts[k].x = (int16_t)(box.x0 + (x * w + kDesign / 2) / kDesign);
            out[i].pts[k].y = (int16_t)(box.y0 + (y * h + kDesign / 2) / kDesign);
        }
}

} // namespace hudterm
} // namespace nbt

#endif
