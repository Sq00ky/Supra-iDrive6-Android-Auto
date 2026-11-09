// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUDTERM_JUNCTION_HPP
#define CLUSTER_HUDTERM_JUNCTION_HPP

// Junction views for HUDTerminal clusters: the roads of a maneuver with the route drawn over them, the way the factory
// navigation draws its near view on the 6WA. In the factory capture of issue 52 its roads are dark-edged grey lines in
// one picture buffer and its route a white line with an arrowhead in another, on the same coordinates. Android Auto
// sends no map, so only the roads the maneuver itself names are drawn: in and out, both branches of a fork, the main
// road and the slip road of an exit, a roundabout's ring with its exit number.
//
// Drawn in the cluster's canvas units, not fitted into a box: roads run past the canvas edges and the cluster clips
// them, as the factory's own points do. Every road and route is one filled polygon, a thick path with mitred corners,
// so the look needs no line-width or line-colour calls. Pure and integer-only like cluster/hudterm_shapes.hpp.
//
// Lanes (the phone's lane arrows) are drawn here too, as small thick paths in a box of their own.

#include "cluster/hudterm_digits.hpp"
#include "cluster/hudterm_shapes.hpp"
#include "cluster/nav_msg.hpp"

#include <vector>

namespace nbt {
namespace hudterm {

// A filled polygon and the fill colour it is drawn with.
struct Item {
    uint8_t colour;
    Poly poly;
};
typedef std::vector<Item> Picture;

// The fill colours of a junction view, as colour indexes (roles: the cluster's colour scheme picks the RGB).
struct JunctionColours {
    uint8_t edge, road, route;
};

// Widths in canvas units. The factory draws roads 18 wide with a 12-wide core and its route 8 wide; ours are a little
// heavier: the edge stays 3 either side of a road and 2 either side of the route.
struct JunctionStyle {
    int roadW, roadCore;       // a road's dark edge, and the grey road inside it
    int routeW, routeCore;     // the route's dark edge, and the route inside it
    int headL, headW;          // the route's arrowhead: length, and half its width
    int edgeHead;              // how much bigger the arrowhead's dark edge is
    int numberH;               // a roundabout's exit number, tall
};
const JunctionStyle kJunctionStyle = { 22, 16, 16, 12, 19, 13, 3, 20 };

// ---- thick paths ----------------------------------------------------------------------------------------------------

// A path as points and the heading of each leg (0 up, + clockwise on screen): headings come from how the path is built,
// so no square root or arctangent is needed for its outline.
namespace junction {
// x / 2 rounded half to even: unlike truncation it mirrors exactly (f(-x) = -f(x), and f(720 - x) = 360 - f(x)), so a
// left and a right roundabout or U-turn come out as mirror images to the unit.
inline int halfEven(int x) {
    if (!(x & 1)) return x / 2;
    int lo = (x - 1) / 2;
    return (lo & 1) ? lo + 1 : lo;
}
}

struct Path {
    std::vector<Point> pts;
    std::vector<int> heads;    // heads[i]: the leg from pts[i] to pts[i + 1]

    void start(int x, int y) {
        pts.assign(1, shapes::pt(x, y));
        heads.clear();
    }
    void line(int heading, int len) {
        const Point& p = pts.back();
        pts.push_back(shapes::along(p.x, p.y, heading, len, 0));
        heads.push_back(heading);
    }
    // Round the circle (cx, cy, r) from bearing b0, where the path already is, to b1: clockwise when b1 > b0, in steps
    // of at most `step` degrees. Points are worked out from the centre, so the arc doesn't drift.
    void arc(int cx, int cy, int r, int b0, int b1, int step) {
        int span = b1 > b0 ? b1 - b0 : b0 - b1, n = (span + step - 1) / step, turn = b1 > b0 ? 90 : -90;
        if (n < 1) return;
        int prev = b0;
        for (int i = 1; i <= n; ++i) {
            int b = b0 + (b1 - b0) * i / n;
            pts.push_back(shapes::along(cx, cy, b, r, 0));
            heads.push_back(junction::halfEven(prev + b) + turn);   // the chord's heading
            prev = b;
        }
    }
    // Another path's legs after this one's (it starts where this one ends).
    void append(const Path& o) {
        for (size_t i = 1; i < o.pts.size(); ++i) pts.push_back(o.pts[i]);
        heads.insert(heads.end(), o.heads.begin(), o.heads.end());
    }
};

namespace junction {

inline int norm180(int d) {
    d %= 360;
    if (d > 180) d -= 360;
    if (d < -180) d += 360;
    return d;
}

// a * 4096 / f, rounded half away from zero (f > 0).
inline int divTrig(int a, int f) { return a >= 0 ? (a * 4096 + f / 2) / f : -((-a * 4096 + f / 2) / f); }

// Point i of the path moved `off` to the right of it (left when negative); at a corner along the bisector, by
// off / cos(half the turn), so both edges keep their width. A very sharp corner is limited to 1 / 0.3 of it.
inline Point offsetAt(const Path& p, size_t i, int off) {
    const Point& q = p.pts[i];
    if (i == 0) return shapes::along(q.x, q.y, p.heads[0], 0, off);
    if (i + 1 == p.pts.size()) return shapes::along(q.x, q.y, p.heads[i - 1], 0, off);
    int h0 = p.heads[i - 1], d = norm180(p.heads[i] - h0), c = icos(d / 2);
    if (c < 1229) c = 1229;
    return shapes::along(q.x, q.y, h0 + d / 2, 0, divTrig(off, c));
}

} // namespace junction

// The path as one polygon w wide, with an arrowhead headL long and 2 * headW wide at its end when headL > 0: the left
// edge forward, the head, the right edge back. 2 * points (+ 3 with a head).
inline Poly thickPath(const Path& p, int w, int headL = 0, int headW = 0) {
    Poly q;
    q.type = kPolygon;
    if (p.pts.size() < 2) return q;
    int h = w / 2;
    for (size_t i = 0; i < p.pts.size(); ++i) q.pts.push_back(junction::offsetAt(p, i, -h));
    if (headL > 0) {
        const Point& e = p.pts.back();
        int hd = p.heads.back();
        q.pts.push_back(shapes::along(e.x, e.y, hd, 0, -headW));
        q.pts.push_back(shapes::along(e.x, e.y, hd, headL, 0));
        q.pts.push_back(shapes::along(e.x, e.y, hd, 0, headW));
    }
    for (size_t i = p.pts.size(); i-- > 0;) q.pts.push_back(junction::offsetAt(p, i, h));
    return q;
}

// ---- the junction of a maneuver -------------------------------------------------------------------------------------

struct Layout {
    std::vector<Path> roads;
    bool ring;                 // a roundabout: the ring (cx, cy, r) is a road too
    int cx, cy, r;
    int ex, ey;                // where the maneuver begins: the route's tail runs straight down from here
    Path route;                // the route from (ex, ey) on, to its arrowhead
    int maxTail;               // the tail at its longest: from (ex, ey) to past the canvas's bottom edge
    int exitNumber;            // a roundabout's exit number, drawn in the ring; 0 = none
    Layout() : ring(false), cx(0), cy(0), r(0), ex(0), ey(0), maxTail(0), exitNumber(0) {}
};

// The junction of a maneuver, around the middle of `box` (the picture's usual place) on a canvas whose bottom edge is
// `bottom`. False when the maneuver has no junction view (the destination keeps its flag).
inline bool junctionLayout(int aa_type, bool has_exit_angle, int64_t exit_angle, bool has_exit_number,
                           int64_t exit_number, const Rect& box, int bottom, Layout& L) {
    L = Layout();
    ShapeSpec s = shapeFor(aa_type, has_exit_angle, exit_angle);
    int dir = kombinav::kombiDirection(aa_type);
    int bx = (box.x0 + box.x1) / 2, by = (box.y0 + box.y1) / 2;
    int below = bottom + 30, above = box.y0 - 50;   // where roads run to, past the edges
    Path p;
    if (s.kind == kShapeFlag) return false;
    if (s.kind == kShapeRoundabout) {
        // The entry at the bottom of the ring, the exit where the picture's arrow leaves it (cluster/hudterm_shapes.hpp
        // drawRoundabout): clockwise travel (left-hand traffic) reaches the left side first.
        L.ring = true;
        L.cx = bx;
        L.cy = by - 6;                               // low enough that an exit at the top keeps its arrowhead
        L.r = 26;
        int b1 = s.clockwise ? 180 + s.exit : 180 - s.exit, h = (b1 % 360 + 360) % 360;
        p.start(L.cx, below);
        p.line(0, below - (L.cy + L.r));
        L.roads.push_back(p);
        Point e = shapes::along(L.cx, L.cy, h, L.r, 0);
        p.start(e.x, e.y);
        p.line(h, 140);
        L.roads.push_back(p);
        L.ex = L.cx;
        L.ey = L.cy + L.r;
        L.route.start(L.ex, L.ey);
        L.route.arc(L.cx, L.cy, L.r, 180, b1, 15);
        L.route.line(h, 18);
        if (has_exit_number && exit_number >= 1 && exit_number <= 99) L.exitNumber = (int)exit_number;
    } else if (s.kind == kShapeUTurn) {
        // Up one side, over the top, down the other: a left U-turn goes up the right-hand side.
        int side = s.angle < 0 ? -1 : 1, r = 16, cy = by - 10, stemX = bx - side * r;
        p.start(stemX, below);
        p.line(0, below - cy);
        p.arc(bx, cy, r, -side * 90, side * 90, 15);
        p.line(180, below - cy);
        L.roads.push_back(p);
        L.ex = stemX;
        L.ey = cy;
        L.route.start(L.ex, L.ey);
        L.route.arc(bx, cy, r, -side * 90, side * 90, 15);
        L.route.line(180, 30);
    } else if (dir == 28 || dir == 29) {
        // An exit: the main road straight on, the slip road leaving it gently, then bending away.
        int side = dir == 29 ? 1 : -1, jx = bx + side * 8, jy = by + 14;
        p.start(jx, below);
        p.line(0, below - above);
        L.roads.push_back(p);
        p.start(jx, jy);
        p.line(side * 6, 22);
        p.line(side * 40, 90);
        L.roads.push_back(p);
        L.ex = jx;
        L.ey = jy;
        L.route.start(jx, jy);
        L.route.line(side * 6, 22);
        L.route.line(side * 40, 30);
    } else if (dir == 37 || dir == 38) {
        // A fork: both branches, the route on the one to take.
        int side = dir == 38 ? 1 : -1, jx = bx, jy = by + 8;
        for (int b = -1; b <= 1; b += 2) {
            p.start(jx, below);
            p.line(0, below - jy);
            p.line(b * 32, 120);
            L.roads.push_back(p);
        }
        L.ex = jx;
        L.ey = jy;
        L.route.start(jx, jy);
        L.route.line(side * 32, 46);
    } else {
        // A turn (straight on, slight, normal, sharp, a merge, a slip road on): in and out. The junction moves away
        // from the turn so the picture stays centred; a sharp turn starts higher, so the way out has room below.
        int a = s.angle < -180 ? -180 : s.angle > 180 ? 180 : s.angle;
        int m = a < 0 ? -a : a, side = a < 0 ? -1 : 1;
        int jx = bx - side * (m < 23 ? 0 : 20), jy = m > 112 ? by - 14 : by;
        p.start(jx, below);
        p.line(0, below - jy);
        p.line(a, 150);
        L.roads.push_back(p);
        L.ex = jx;
        L.ey = jy;
        L.route.start(jx, jy);
        L.route.line(a, m > 112 ? 34 : 40);
    }
    L.maxTail = bottom + 10 - L.ey;
    if (L.maxTail < 8) L.maxTail = 8;
    return true;
}

// The roads: every dark edge first, then every grey road over them (two colour changes), and a roundabout's exit
// number in the ring in the route's colour. A ring is two half rings that overlap: a polygon can't have a hole.
inline void drawRoads(const Layout& L, const JunctionColours& c, const JunctionStyle& st, Picture& out) {
    for (int pass = 0; pass < 2; ++pass) {
        int w = pass ? st.roadCore : st.roadW;
        uint8_t col = pass ? c.road : c.edge;
        Item it;
        it.colour = col;
        if (L.ring) {
            it.poly = shapes::band(L.cx, L.cy, L.r - w / 2, L.r + w / 2, -6, 186, 16);   // whole 12-degree steps:
            out.push_back(it);                                                           // the halves mirror
            it.poly = shapes::band(L.cx, L.cy, L.r - w / 2, L.r + w / 2, 174, 366, 16);
            out.push_back(it);
        }
        for (size_t i = 0; i < L.roads.size(); ++i) {
            it.poly = thickPath(L.roads[i], w);
            out.push_back(it);
        }
    }
    if (L.exitNumber) {
        int h = L.exitNumber > 9 ? st.numberH * 7 / 10 : st.numberH;
        Shape n;
        digits::drawNumber(L.exitNumber, L.cx, L.cy - h / 2, h, n);
        for (size_t i = 0; i < n.size(); ++i) {
            Item it;
            it.colour = c.route;
            it.poly = n[i];
            out.push_back(it);
        }
    }
}

// The route with a tail `tail` long (clamped to 8..maxTail) up to where the maneuver begins: its dark edge, then the
// route over it.
inline void drawRoute(const Layout& L, int tail, const JunctionColours& c, const JunctionStyle& st, Picture& out) {
    if (tail > L.maxTail) tail = L.maxTail;
    if (tail < 8) tail = 8;
    Path p;
    p.start(L.ex, L.ey + tail);
    p.line(0, tail);
    p.append(L.route);
    Item it;
    it.colour = c.edge;
    it.poly = thickPath(p, st.routeW, st.headL + st.edgeHead, st.headW + st.edgeHead);
    out.push_back(it);
    it.colour = c.route;
    it.poly = thickPath(p, st.routeCore, st.headL, st.headW);
    out.push_back(it);
}

// The tail for a route stage: 1..stages, the last one the longest.
inline int tailFor(const Layout& L, int stage, int stages) {
    if (stages < 1) stages = 1;
    if (stage < 1) stage = 1;
    if (stage > stages) stage = stages;
    return L.maxTail * stage / stages;
}

// ---- lanes ----------------------------------------------------------------------------------------------------------

enum { kLaneStroke = 6, kLaneHeadL = 8, kLaneHeadW = 6, kLanePitch = 18, kLaneMargin = 10, kLaneMaxArrows = 24 };

// One lane direction (LaneDirection.Shape) as a small arrow standing on (cx, base), h tall.
inline Path laneArrow(int shape, int cx, int base, int h) {
    Path p;
    p.start(cx, base);
    int turn = 0;
    switch (shape) {
        case 2: turn = -45; break;
        case 3: turn = 45; break;
        case 4: turn = -90; break;
        case 5: turn = 90; break;
        case 6: turn = -135; break;
        case 7: turn = 135; break;
        case 8: case 9: {                             // U-turn: up, across, back down
            int side = shape == 8 ? -1 : 1;
            p.line(0, h * 11 / 26);
            p.line(side * 90, h * 5 / 26);
            p.line(180, h * 5 / 26);
            return p;
        }
        default: break;                               // straight on, or unknown
    }
    if (!turn) {
        p.line(0, h - kLaneHeadL - 1);
    } else {
        p.line(0, h / 2);
        p.line(turn, h * 6 / 26);
    }
    return p;
}

// The phone's lanes, left to right, centred in `box` (each arrow's stem at least kLaneMargin inside it, so a turned
// head stays in): the directions not to take first in `other`, then the ones to take (the phone highlights them) in
// `take`, so two colour changes at most. False (out unchanged) when there are none or too many arrows.
inline bool drawLanes(const std::vector<kombinav::AaLane>& lanes, const Rect& box, uint8_t other, uint8_t take,
                      Picture& out) {
    size_t arrows = 0;
    for (size_t i = 0; i < lanes.size(); ++i) arrows += lanes[i].size();
    if (lanes.empty() || arrows == 0 || arrows > kLaneMaxArrows) return false;
    int n = (int)lanes.size(), room = box.x1 - box.x0 - 2 * kLaneMargin;
    int pitch = n > 1 && room / (n - 1) < kLanePitch ? room / (n - 1) : kLanePitch;
    int h = box.y1 - box.y0, x0 = (box.x0 + box.x1) / 2 - pitch * (n - 1) / 2;
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < n; ++i)
            for (size_t k = 0; k < lanes[i].size(); ++k) {
                const kombinav::LaneDirection& d = lanes[i][k];
                if (d.highlighted != (pass == 1)) continue;
                Item it;
                it.colour = pass ? take : other;
                it.poly = thickPath(laneArrow(d.shape, x0 + i * pitch, box.y1, h), kLaneStroke, kLaneHeadL, kLaneHeadW);
                out.push_back(it);
            }
    return true;
}

} // namespace hudterm
} // namespace nbt

#endif
