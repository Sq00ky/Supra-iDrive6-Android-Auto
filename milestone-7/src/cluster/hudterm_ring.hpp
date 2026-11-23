// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUDTERM_RING_HPP
#define CLUSTER_HUDTERM_RING_HPP

#include "cluster/hud6wa_profile.hpp"
#include "cluster/hudterm_junction.hpp"

namespace nbt {
namespace hudterm {

namespace ring {
// Q8 sample of the accepted analytic SVG: 92-degree circular arrows, repeated every120 degrees.
// RO/RI=6732/4620, 18x10 heads with0.4-unit quadratic corner trims; no raster tracing.
// Circular samples are less than one reference pixel apart; round onto the HUD only when fitted.
static const Point kOutline0[] = {
    {-6728,235}, {-6732,-11}, {-6727,-256}, {-6713,-502}, {-6691,-746},
    {-6659,-990}, {-6618,-1232}, {-6569,-1473}, {-6511,-1711}, {-6444,-1948},
    {-6369,-2182}, {-6285,-2413}, {-6193,-2640}, {-6092,-2864}, {-5984,-3085},
    {-5867,-3301}, {-5743,-3513}, {-5611,-3720}, {-5471,-3922}, {-5325,-4119},
    {-5171,-4311}, {-5010,-4497}, {-4843,-4676}, {-4669,-4850}, {-4489,-5017},
    {-4303,-5178}, {-4111,-5331}, {-3914,-5478}, {-3711,-5617}, {-3504,-5748},
    {-3292,-5872}, {-3075,-5989}, {-2855,-6097}, {-2630,-6197}, {-2403,-6289},
    {-2172,-6372}, {-1938,-6447}, {-1701,-6514}, {-1462,-6571}, {-1222,-6620},
    {-979,-6660}, {-736,-6692}, {-491,-6714}, {-246,-6728}, {0,-6732},
    {0,-7878}, {5,-7918}, {19,-7937}, {43,-7935}, {76,-7911},
    {2484,-5745}, {2512,-5710}, {2522,-5676}, {2512,-5642}, {2484,-5607},
    {76,-3441}, {43,-3417}, {19,-3415}, {5,-3434}, {0,-3474},
    {0,-4620}, {-247,-4613}, {-494,-4594}, {-739,-4561}, {-982,-4515},
    {-1222,-4456}, {-1458,-4384}, {-1691,-4300}, {-1918,-4203}, {-2140,-4094},
    {-2356,-3974}, {-2566,-3842}, {-2767,-3699}, {-2961,-3546}, {-3147,-3383},
    {-3323,-3209}, {-3490,-3027}, {-3647,-2836}, {-3794,-2637}, {-3929,-2430},
    {-4054,-2216}, {-4166,-1996}, {-4267,-1770}, {-4356,-1540}, {-4432,-1304},
    {-4495,-1065}, {-4546,-823}, {-4584,-579}, {-4608,-333}, {-4619,-86},
    {-4617,161}
};
static const Point kOutline1[] = {
    {3160,-5944}, {3375,-5825}, {3586,-5698}, {3791,-5563}, {3991,-5421},
    {4187,-5272}, {4376,-5116}, {4560,-4953}, {4738,-4783}, {4909,-4607},
    {5074,-4425}, {5232,-4237}, {5383,-4043}, {5527,-3844}, {5663,-3640},
    {5792,-3431}, {5914,-3217}, {6027,-2999}, {6132,-2777}, {6230,-2552},
    {6319,-2323}, {6399,-2090}, {6471,-1856}, {6535,-1618}, {6589,-1379},
    {6635,-1137}, {6672,-895}, {6700,-651}, {6720,-406}, {6730,-160},
    {6731,85}, {6724,331}, {6707,576}, {6682,820}, {6647,1064},
    {6604,1305}, {6552,1546}, {6491,1784}, {6422,2019}, {6344,2252},
    {6258,2482}, {6163,2709}, {6060,2932}, {5949,3151}, {5830,3366},
    {6822,3939}, {6855,3963}, {6864,3985}, {6851,4005}, {6814,4022},
    {3733,5023}, {3689,5031}, {3655,5022}, {3630,4997}, {3614,4955},
    {2942,1786}, {2938,1746}, {2948,1724}, {2971,1721}, {3009,1737},
    {4001,2310}, {4119,2093}, {4225,1869}, {4319,1641}, {4400,1407},
    {4469,1170}, {4526,929}, {4569,686}, {4599,440}, {4616,193},
    {4620,-54}, {4610,-301}, {4588,-547}, {4552,-792}, {4503,-1034},
    {4441,-1273}, {4367,-1509}, {4280,-1741}, {4180,-1967}, {4069,-2188},
    {3946,-2402}, {3812,-2610}, {3667,-2810}, {3511,-3002}, {3346,-3186},
    {3170,-3360}, {2986,-3525}, {2793,-3680}, {2592,-3824}, {2384,-3957},
    {2169,-4079}
};
static const Point kOutline2[] = {
    {3567,5709}, {3357,5835}, {3142,5954}, {2922,6065}, {2699,6167},
    {2472,6262}, {2242,6348}, {2009,6425}, {1773,6494}, {1535,6555},
    {1295,6606}, {1053,6649}, {810,6683}, {565,6708}, {320,6724},
    {75,6732}, {-171,6730}, {-416,6719}, {-661,6699}, {-905,6671},
    {-1148,6633}, {-1389,6587}, {-1629,6532}, {-1866,6468}, {-2101,6396},
    {-2333,6315}, {-2561,6226}, {-2787,6128}, {-3009,6022}, {-3226,5909},
    {-3440,5787}, {-3649,5658}, {-3853,5521}, {-4051,5376}, {-4245,5225},
    {-4433,5067}, {-4615,4902}, {-4790,4730}, {-4960,4552}, {-5123,4368},
    {-5278,4178}, {-5427,3983}, {-5569,3782}, {-5703,3576}, {-5830,3366},
    {-6822,3939}, {-6860,3955}, {-6883,3952}, {-6893,3930}, {-6890,3890},
    {-6217,721}, {-6201,679}, {-6177,654}, {-6142,645}, {-6098,653},
    {-3018,1654}, {-2981,1671}, {-2967,1691}, {-2976,1713}, {-3009,1737},
    {-4001,2310}, {-3872,2521}, {-3731,2724}, {-3580,2920}, {-3419,3107},
    {-3248,3286}, {-3067,3455}, {-2878,3614}, {-2681,3763}, {-2476,3901},
    {-2263,4028}, {-2045,4143}, {-1820,4246}, {-1590,4338}, {-1356,4417},
    {-1118,4483}, {-876,4536}, {-632,4577}, {-387,4604}, {-140,4618},
    {108,4619}, {354,4606}, {600,4581}, {845,4542}, {1086,4490},
    {1325,4426}, {1560,4349}, {1790,4259}, {2016,4157}, {2235,4043},
    {2448,3918}
};
static const Point kOutline3[] = {
    {-1056,4620}, {1056,4620}, {1056,10752}, {-1056,10752}
};
struct Outline { const Point* points; size_t count; };
static const Outline kOutlines[] = {
    {kOutline0, sizeof(kOutline0) / sizeof(kOutline0[0])},
    {kOutline1, sizeof(kOutline1) / sizeof(kOutline1[0])},
    {kOutline2, sizeof(kOutline2) / sizeof(kOutline2[0])},
    {kOutline3, sizeof(kOutline3) / sizeof(kOutline3[0])}
};
enum { kUnit = 256, kSide = 66, kExtentX = 7962, kMinY = -7345, kMaxY = 10752 };
enum { kCirculationExtentX = 6893, kCirculationMinY = -7937, kCirculationMaxY = 10752 };

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
inline RingLayout ringLayout(const HudProfile& p, int extentX = ring::kExtentX,
                             int minY = ring::kMinY, int maxY = ring::kMaxY) {
    Rect box = ringGlyphBox(p);
    RingLayout l = { (box.x0 + box.x1) / 2, box.y0, 0 };
    int width = p.canvas.x1 - p.canvas.x0, height = p.canvas.y1 - p.canvas.y0;
    int side = box.x1 - box.x0;
    if (side > box.y1 - box.y0) side = box.y1 - box.y0;
    if (width <= 0 || height <= 0 || side <= 0) return l;
    int limit = (int)((long)width * ring::kSide * ring::kUnit / (2 * extentX));
    if (side > limit) side = limit;
    limit = (int)((long)height * ring::kSide * ring::kUnit / (maxY - minY));
    if (side > limit) side = limit;
    while (side > 0 && (2 * ring::offset(extentX, side) > width ||
                       ring::offset(maxY, side) - ring::offset(minY, side) > height))
        --side;   // the two rounded edges can need one extra unit at a half-pixel boundary
    l.side = side;
    l.cy += ring::rounded((long)25 * side, ring::kSide);
    int dx = ring::offset(extentX, side);
    if (l.cx - dx < p.canvas.x0) l.cx = p.canvas.x0 + dx;
    if (l.cx + dx > p.canvas.x1) l.cx = p.canvas.x1 - dx;
    int low = ring::offset(minY, side), high = ring::offset(maxY, side);
    if (l.cy + low < p.canvas.y0) l.cy = p.canvas.y0 - low;
    if (l.cy + high > p.canvas.y1) l.cy = p.canvas.y1 - high;
    return l;
}

// Unknown circulation fits its actual envelope; default parameters preserve known-exit fitting.
inline RingLayout circulationLayout(const HudProfile& p) {
    return ringLayout(p, ring::kCirculationExtentX, ring::kCirculationMinY, ring::kCirculationMaxY);
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
// Remove tiny closed detours and zero-area spurs introduced by circulation rounding.
inline void removeBacktracks(Poly& poly) {
    bool changed = true;
    while (changed && poly.pts.size() > 2) {
        changed = false;
        for (size_t i = 0; i < poly.pts.size(); ++i) {
            for (size_t j = i + 2; j < poly.pts.size(); ++j) {
                if (poly.pts[i].x != poly.pts[j].x || poly.pts[i].y != poly.pts[j].y) continue;
                // A repeated native vertex closes a rounding loop; retain the longer contour around the arrow.
                if (j - i <= poly.pts.size() - (j - i))
                    poly.pts.erase(poly.pts.begin() + i + 1, poly.pts.begin() + j + 1);
                else {
                    poly.pts.erase(poly.pts.begin() + j, poly.pts.end());
                    poly.pts.erase(poly.pts.begin(), poly.pts.begin() + i);
                }
                changed = true;
                break;
            }
            if (changed) break;
            Point a = poly.pts[i ? i - 1 : poly.pts.size() - 1];
            Point b = poly.pts[i], c = poly.pts[i + 1 < poly.pts.size() ? i + 1 : 0];
            if (!turn(a,b,c) && (int64_t)(b.x-a.x)*(c.x-b.x) + (int64_t)(b.y-a.y)*(c.y-b.y) <= 0) {
                poly.pts.erase(poly.pts.begin() + i);
                changed = true;
                break;
            }
        }
    }
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
    std::vector<int> inner;
    angle = first2;
    inner.push_back(angle);
    while (angle != last2) {
        int remaining = (last2 - angle) * direction;
        angle += direction * (remaining < 5 ? remaining : 5);
        inner.push_back(angle);
    }
    std::vector<Point> q;
    for (size_t i = 0; i < angles.size(); ++i) q.push_back(circular(angles[i], kOuter));
    for (size_t i = inner.size(); i > 0; --i) q.push_back(circular(inner[i - 1], kInner));
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
    RingLayout l = known ? knownRingLayout(spec, p) : circulationLayout(p);
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
            ring::removeBacktracks(poly);
            ring::append(poly, p.fill, pic);
        }
    }
    if (exitNumber < 1 || exitNumber > 99) return true;
    int height = l.side * (exitNumber > 9 ? (known ? 18 : 15) : 24) / ring::kSide;
    if (!known && l.side < ring::kSide) --height;   // preserve clearance after coarse circulation rounding
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
