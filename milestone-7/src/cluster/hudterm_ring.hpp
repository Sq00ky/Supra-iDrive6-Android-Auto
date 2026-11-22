// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUDTERM_RING_HPP
#define CLUSTER_HUDTERM_RING_HPP

#include "cluster/hud6wa_profile.hpp"
#include "cluster/hudterm_junction.hpp"

namespace nbt {
namespace hudterm {

namespace ring {
// Q8 offsets from the circle centre on the 66-unit reference frame. Each circular arrow uses concentric arcs
// (radii 186/148, bearings -27..60), the same symmetric head, and rotations of 35 + n*120 degrees.
// Sample the arcs at 12 equal intervals; retain fractional coordinates until the final fit onto the cluster.
static const Point kOutline0[] = {
    {937,-6666}, {1771,-6495}, {2576,-6220}, {3341,-5845}, {4051,-5376},
    {4698,-4822}, {5269,-4191}, {5755,-3492}, {6150,-2738}, {6446,-1940},
    {6640,-1111}, {6727,-264}, {6706,587}, {7962,901}, {5621,2699},
    {4103,168}, {5336,467}, {5353,-210}, {5283,-884}, {5129,-1544},
    {4894,-2179}, {4579,-2779}, {4192,-3335}, {3738,-3837}, {3224,-4278},
    {2658,-4651}, {2050,-4949}, {1409,-5168}, {746,-5305}
};
static const Point kOutline1[] = {
    {5305,4145}, {4739,4781}, {4098,5341}, {3391,5815}, {2630,6197},
    {1827,6479}, {995,6658}, {147,6730}, {-704,6695}, {-1543,6553},
    {-2358,6306}, {-3135,5958}, {-3861,5515}, {-4761,6444}, {-5148,3519},
    {-2197,3469}, {-3072,4388}, {-2494,4741}, {-1876,5017}, {-1228,5214},
    {-560,5327}, {117,5355}, {792,5298}, {1454,5156}, {2093,4931},
    {2699,4627}, {3261,4250}, {3771,3804}, {4221,3298}
};
static const Point kOutline2[] = {
    {-6242,2522}, {-6510,1714}, {-6674,879}, {-6732,29}, {-6682,-820},
    {-6525,-1657}, {-6264,-2467}, {-5902,-3238}, {-5446,-3957}, {-4903,-4613},
    {-4282,-5195}, {-3592,-5693}, {-2845,-6101}, {-3201,-7345}, {-473,-6218},
    {-1906,-3638}, {-2264,-4855}, {-2858,-4530}, {-3407,-4133}, {-3902,-3670},
    {-4334,-3149}, {-4696,-2576}, {-4984,-1963}, {-5192,-1319}, {-5317,-653},
    {-5357,23}, {-5311,699}, {-5180,1364}, {-4967,2007}
};
// The plain entrance starts at the ring's inner edge and keeps the same width as its circular shafts.
static const Point kOutline3[] = {
    {-688,5357}, {688,5357}, {688,10752}, {-688,10752}
};
struct Outline { const Point* points; size_t count; };
static const Outline kOutlines[] = {
    {kOutline0, sizeof(kOutline0) / sizeof(kOutline0[0])},
    {kOutline1, sizeof(kOutline1) / sizeof(kOutline1[0])},
    {kOutline2, sizeof(kOutline2) / sizeof(kOutline2[0])},
    {kOutline3, sizeof(kOutline3) / sizeof(kOutline3[0])}
};
enum { kUnit = 256, kSide = 66, kExtentX = 7962, kMinY = -7345, kMaxY = 10752 };

// Round half away from zero so reflection stays exact after the one conversion to canvas coordinates.
inline int rounded(long value, int denominator) {
    return (int)(value >= 0 ? (value + denominator / 2) / denominator
                            : -((-value + denominator / 2) / denominator));
}
inline int offset(int q8, int side) {
    return rounded((long)q8 * side, kSide * kUnit);
}
inline void append(const Poly& poly, uint8_t colour, Picture& pic) {
    int64_t area = 0;
    for (size_t i = 0; i < poly.pts.size(); ++i) {
        const Point& a = poly.pts[i];
        const Point& b = poly.pts[i + 1 < poly.pts.size() ? i + 1 : 0];
        area += (int64_t)a.x * b.y - (int64_t)b.x * a.y;
    }
    if (!area) return;   // a constrained canvas can collapse a facet to a line
    Item it; it.colour = colour; it.poly = poly; pic.push_back(it);
}
} // namespace ring

struct RingLayout { int cx, cy, side; };

// Fit uniformly, then translate into the canvas. The symmetric envelope keeps both traffic directions centred alike.
inline RingLayout ringLayout(const HudProfile& p) {
    Rect box = ringGlyphBox(p);
    RingLayout l = { (box.x0 + box.x1) / 2, box.y0, 0 };
    int width = p.canvas.x1 - p.canvas.x0, height = p.canvas.y1 - p.canvas.y0;
    int side = box.x1 - box.x0;
    if (side > box.y1 - box.y0) side = box.y1 - box.y0;
    if (width <= 0 || height <= 0 || side <= 0) return l;
    int limit = (int)((long)width * ring::kSide * ring::kUnit / (2 * ring::kExtentX));
    if (side > limit) side = limit;
    limit = (int)((long)height * ring::kSide * ring::kUnit / (ring::kMaxY - ring::kMinY));
    if (side > limit) side = limit;
    while (side > 0 && (2 * ring::offset(ring::kExtentX, side) > width ||
                       ring::offset(ring::kMaxY, side) - ring::offset(ring::kMinY, side) > height))
        --side;   // the two rounded edges can need one extra unit at a half-pixel boundary
    l.side = side;
    l.cy += ring::rounded((long)25 * side, ring::kSide);
    int dx = ring::offset(ring::kExtentX, side);
    if (l.cx - dx < p.canvas.x0) l.cx = p.canvas.x0 + dx;
    if (l.cx + dx > p.canvas.x1) l.cx = p.canvas.x1 - dx;
    int low = ring::offset(ring::kMinY, side), high = ring::offset(ring::kMaxY, side);
    if (l.cy + low < p.canvas.y0) l.cy = p.canvas.y0 - low;
    if (l.cy + high > p.canvas.y1) l.cy = p.canvas.y1 - high;
    return l;
}

namespace ring {
enum { kOuter = 6732, kInner = 4620, kHalfShaft = 1056, kExitBase = 8780, kExitTip = 12300,
       kHalfHead = kHalfShaft + ((2560-kHalfShaft)*7+5)/10, kReturnBase = 8512, kReturnTip = 12032 };

inline Point radial(int angle, int along, int across) {
    int s = isin(angle), c = icos(angle);
    return shapes::pt(rounded((long)along * s + (long)across * c, 4096),
                      rounded((long)along * c - (long)across * s, 4096));
}

// Ordinary Mapbox proportions: 70% wing protrusion, 80-degree return edge, and an 8px external shaft (was 6px).
inline void exitHead(int angle, Point head[5]) {
    const int fold = wingFold(kHalfHead, kHalfShaft);
    head[0] = radial(angle, kExitBase-fold, -kHalfHead);
    head[1] = radial(angle, kExitTip, 0);
    head[2] = radial(angle, kExitBase-fold, kHalfHead);
    head[3] = radial(angle, kExitBase, kHalfShaft);
    head[4] = radial(angle, kExitBase, -kHalfShaft);
}

// A half-degree sample is interpolated before scaling, in Q8 rather than canvas pixels.
inline int halfSin(int angle2) {
    int angle = angle2 / 2;
    if (!(angle2 % 2)) return isin(angle);
    return (isin(angle) + isin(angle + (angle2 > 0 ? 1 : -1))) / 2;
}
inline Point circular(int angle2, int radius) {
    return shapes::pt(rounded((long)radius * halfSin(angle2), 4096),
                      -rounded((long)radius * halfSin(angle2 + 180), 4096));
}
inline Point canvasPoint(const Point& q, const RingLayout& l, bool clockwise) {
    int x = offset(q.x, l.side);
    return shapes::pt(l.cx + (clockwise ? -x : x), l.cy + offset(q.y, l.side));
}
inline void addPoint(Poly& poly, Point p) {
    if (poly.pts.empty() || p.x != poly.pts.back().x || p.y != poly.pts.back().y) poly.pts.push_back(p);
}
inline int64_t turn(const Point& a, const Point& b, const Point& c) {
    return (int64_t)(b.x-a.x)*(c.y-a.y)-(int64_t)(b.y-a.y)*(c.x-a.x);
}
inline bool crosses(const Point& a, const Point& b, const Point& c, const Point& d) {
    int64_t x = turn(a,b,c), y = turn(a,b,d), u = turn(c,d,a), v = turn(c,d,b);
    return ((x < 0 && y > 0) || (x > 0 && y < 0)) && ((u < 0 && v > 0) || (u > 0 && v < 0));
}
inline void addKnown(const ShapeSpec& spec, const RingLayout& l, const Point* q, size_t n,
                     uint8_t colour, Picture& pic, bool foldedExit = false) {
    Poly poly; poly.type = kPolygon;
    Point fitted[7];
    bool repair = foldedExit && n == 7, left = false, right = false;
    if (repair) {
        for (size_t i = 0; i < 7; ++i) fitted[i] = canvasPoint(q[i], l, spec.clockwise);
        // A subpixel neck can round past its wing on a tiny canvas; retain the wing and tip, dropping only that neck.
        left = crosses(fitted[0],fitted[1],fitted[2],fitted[3]);
        right = crosses(fitted[3],fitted[4],fitted[5],fitted[6]);
    }
    for (size_t i = 0; i < n; ++i) {
        if ((i == 1 && left) || (i == 5 && right)) continue;
        addPoint(poly, repair ? fitted[i] : canvasPoint(q[i], l, spec.clockwise));
    }
    if (poly.pts.size() > 1 && poly.pts.front().x == poly.pts.back().x && poly.pts.front().y == poly.pts.back().y)
        poly.pts.pop_back();
    append(poly, colour, pic);
}
inline void addBand(const ShapeSpec& spec, const RingLayout& l, int first2, int last2,
                    uint8_t colour, Picture& pic) {
    std::vector<int> angles;
    int direction = last2 > first2 ? 1 : -1, angle = first2;
    angles.push_back(angle);
    while (angle != last2) {
        int remaining = (last2 - angle) * direction;
        angle += direction * (remaining < 15 ? remaining : 15);
        angles.push_back(angle);
    }
    std::vector<Point> q;
    for (size_t i = 0; i < angles.size(); ++i) q.push_back(circular(angles[i], kOuter));
    for (size_t i = angles.size(); i > 0; --i) q.push_back(circular(angles[i - 1], kInner));
    addKnown(spec, l, &q[0], q.size(), colour, pic);
}
inline Rect knownBounds(const ShapeSpec& spec) {
    Rect b = { -kOuter, -kOuter, kOuter, kOuter };
    if (spec.exit == 360) {
        b.y1 = kReturnTip;
        return b;
    }
    Point q[5]; exitHead(spec.exit, q);
    for (size_t i = 0; i < sizeof(q) / sizeof(q[0]); ++i) {
        if (spec.clockwise) q[i].x = (int16_t)-q[i].x;
        if (q[i].x < b.x0) b.x0 = q[i].x;
        if (q[i].x > b.x1) b.x1 = q[i].x;
        if (q[i].y < b.y0) b.y0 = q[i].y;
        if (q[i].y > b.y1) b.y1 = q[i].y;
    }
    return b;
}
inline int knownBottom(const ShapeSpec& spec, const Rect& bounds, int side) {
    int bottom = offset(bounds.y1, side);
    // Keep a visible approach below the circle even when an upward exit moves the circle towards the footer.
    int entrance = offset(kOuter, side) + (side > 0 ? 2 : 0);
    return spec.exit != 360 && entrance > bottom ? entrance : bottom;
}
inline int floorRatio(int64_t numerator, int64_t denominator) {
    return (int)(numerator >= 0 ? numerator / denominator : -((-numerator + denominator - 1) / denominator));
}
// Keep the incoming bar behind the final, rounded head base. No special angle cutoff or nonradial exit is needed.
inline int entryEnd(const Point& base0, const Point& base1, const Point& tip,
                    int left, int right, int footer) {
    int64_t nx = (int)base1.y - base0.y, ny = (int)base0.x - base1.x;
    if (nx * ((int)tip.x - base0.x) + ny * ((int)tip.y - base0.y) < 0) { nx = -nx; ny = -ny; }
    if (ny <= 0) return footer;
    int64_t edge0 = nx * left, edge1 = nx * right;
    int cap = floorRatio(nx * base0.x + ny * base0.y - (edge0 > edge1 ? edge0 : edge1), ny);
    return cap < footer ? cap : footer;
}
} // namespace ring

// The ring keeps its scale for every F30 exit angle; only the circle, branch and number translate to fit.
inline RingLayout knownRingLayout(const ShapeSpec& spec, const HudProfile& p) {
    RingLayout l = ringLayout(p);
    if (l.side <= 0 || spec.exit < 1 || spec.exit > 360) return l;
    if (spec.exit == 360) l.cy -= ring::rounded((long)3 * l.side, ring::kSide);
    Rect b = ring::knownBounds(spec);
    int width = p.canvas.x1 - p.canvas.x0, height = p.canvas.y1 - p.canvas.y0;
    int limit = (int)((long)width * ring::kSide * ring::kUnit / (b.x1 - b.x0));
    if (l.side > limit) l.side = limit;
    limit = (int)((long)height * ring::kSide * ring::kUnit / (b.y1 - b.y0));
    if (l.side > limit) l.side = limit;
    while (l.side > 0 &&
           (ring::offset(b.x1, l.side) - ring::offset(b.x0, l.side) > width ||
            ring::knownBottom(spec, b, l.side) - ring::offset(b.y0, l.side) > height))
        --l.side;
    int low = ring::offset(b.x0, l.side), high = ring::offset(b.x1, l.side);
    if (l.cx + low < p.canvas.x0) l.cx = p.canvas.x0 - low;
    if (l.cx + high > p.canvas.x1) l.cx = p.canvas.x1 - high;
    low = ring::offset(b.y0, l.side); high = ring::knownBottom(spec, b, l.side);
    if (l.cy + low < p.canvas.y0) l.cy = p.canvas.y0 - low;
    if (l.cy + high > p.canvas.y1) l.cy = p.canvas.y1 - high;
    return l;
}

namespace ring {
inline void knownPicture(const ShapeSpec& spec, const HudProfile& p, const RingLayout& l, Picture& pic) {
    uint8_t background = spec.exit == 360 ? p.fill : p.road;
    addBand(spec, l, 0, 360, background, pic);
    addBand(spec, l, 360, 720, background, pic);
    if (spec.exit == 360) {
        // Full circuit C: separate parallel entry and exit; only this exit is allowed off the radial normal.
        Point input[] = { {1792-kHalfShaft,4620}, {1792+kHalfShaft,4620},
                          {1792+kHalfShaft,11520}, {1792-kHalfShaft,11520} };
        const int base = kReturnBase, fold = wingFold(kHalfHead, kHalfShaft);
        Point outgoing[] = { {-2048-kHalfShaft,4620}, {-2048-kHalfShaft,base},
                             shapes::pt(-2048-kHalfHead,base-fold), {-2048,kReturnTip},
                             shapes::pt(-2048+kHalfHead,base-fold), {-2048+kHalfShaft,base},
                             {-2048+kHalfShaft,4620} };
        addKnown(spec, l, input, 4, p.fill, pic);
        addKnown(spec, l, outgoing, 7, p.fill, pic, true);
        return;
    }
    addBand(spec, l, 360, spec.exit > 180 ? 0 : 360 - 2 * spec.exit, p.fill, pic);
    if (spec.exit > 180) addBand(spec, l, 0, 360 - 2 * spec.exit, p.fill, pic);
    Point head[5]; exitHead(spec.exit, head);
    Point tip = canvasPoint(head[1], l, spec.clockwise);
    Point base0 = canvasPoint(head[0], l, spec.clockwise), base1 = canvasPoint(head[2], l, spec.clockwise);
    int half = offset(kHalfShaft, l.side), left = l.cx - half, right = l.cx + half;
    RingLayout base = ringLayout(p);
    int footer = base.cy + offset(kMaxY, base.side);
    int entrance = l.cy + offset(kOuter, l.side) + 2;
    if (footer < entrance) footer = entrance;
    if (footer > p.canvas.y1) footer = p.canvas.y1;
    int top = l.cy + offset(kInner, l.side), bottom = entryEnd(base0, base1, tip, left, right, footer);
    if (bottom > top) {
        Poly input; input.type = kPolygon;
        input.pts.push_back(shapes::pt(left, top)); input.pts.push_back(shapes::pt(right, top));
        input.pts.push_back(shapes::pt(right, bottom)); input.pts.push_back(shapes::pt(left, bottom));
        append(input, p.fill, pic);
    }
    // The same shaft and head form one filled polygon, leaving room for eight hide handles within a 32-call batch.
    Point outgoing[] = { radial(spec.exit, kInner, -kHalfShaft), head[4], head[0], head[1], head[2], head[3],
                         radial(spec.exit, kInner, kHalfShaft) };
    addKnown(spec, l, outgoing, 7, p.fill, pic, true);
}
} // namespace ring

// Unknown angles use circulation; known angles always get an exit branch, with or without a number.
// Return false only when a valid number cannot be drawn.
inline bool roundaboutPicture(const ShapeSpec& spec, const HudProfile& p, Picture& pic, int exitNumber = 0) {
    bool known = spec.exit >= 1 && spec.exit <= 360;
    RingLayout l = known ? knownRingLayout(spec, p) : ringLayout(p);
    if (known) {
        ring::knownPicture(spec, p, l, pic);
    } else {
        for (size_t i = 0; i < sizeof(ring::kOutlines) / sizeof(ring::kOutlines[0]); ++i) {
            const ring::Outline& outline = ring::kOutlines[i];
            Poly poly; poly.type = kPolygon;
            for (size_t j = 0; j < outline.count; ++j) {
                int x = ring::offset(outline.points[j].x, l.side);
                if (i < 3 && !spec.clockwise) x = -x;
                Point point = shapes::pt(l.cx + x, l.cy + ring::offset(outline.points[j].y, l.side));
                if (poly.pts.empty() || point.x != poly.pts.back().x || point.y != poly.pts.back().y)
                    poly.pts.push_back(point);
            }
            if (poly.pts.size() > 1 && poly.pts.front().x == poly.pts.back().x && poly.pts.front().y == poly.pts.back().y)
                poly.pts.pop_back();
            ring::append(poly, p.fill, pic);
        }
    }
    if (exitNumber < 1 || exitNumber > 99) return true;
    int height = l.side * (exitNumber > 9 ? 18 : 24) / ring::kSide;
    if (height < 6) return false;
    Shape number;
    digits::drawNumber(exitNumber, l.cx, l.cy - height / 2, height, number);
    size_t before = pic.size();
    for (size_t i = 0; i < number.size(); ++i) ring::append(number[i], p.fill, pic);
    return pic.size() > before;
}

} // namespace hudterm
} // namespace nbt
#endif
