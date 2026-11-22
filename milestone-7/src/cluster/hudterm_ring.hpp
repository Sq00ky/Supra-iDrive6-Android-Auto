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
// The entrance clones the circular arrow's head and shaft width; it does not turn with traffic direction.
static const Point kOutline3[] = {
    {-1964,8935}, {0,6732}, {1964,8935}
};
static const Point kOutline4[] = {
    {-688,8679}, {688,8679}, {688,10752}, {-688,10752}
};
struct Outline { const Point* points; size_t count; };
static const Outline kOutlines[] = {
    {kOutline0, sizeof(kOutline0) / sizeof(kOutline0[0])},
    {kOutline1, sizeof(kOutline1) / sizeof(kOutline1[0])},
    {kOutline2, sizeof(kOutline2) / sizeof(kOutline2[0])},
    {kOutline3, sizeof(kOutline3) / sizeof(kOutline3[0])},
    {kOutline4, sizeof(kOutline4) / sizeof(kOutline4[0])}
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

// A circulation symbol does not encode an exit angle. Return false only when a valid number cannot be drawn.
inline bool roundaboutPicture(const ShapeSpec& spec, const HudProfile& p, Picture& pic, int exitNumber = 0) {
    RingLayout l = ringLayout(p);
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
