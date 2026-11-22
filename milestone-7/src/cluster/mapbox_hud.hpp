// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// Mapbox maneuver glyphs on the 6WA HUD. The polished convex-piece drawings in mapbox_glyphs.hpp (CC0, Mapbox; the same
// set the CarPlay HUD uses) mapped into the picture buffer, as a nicer alternative to the parametric shapes
// (hudterm_shapes.hpp drawShape). Same picture model: convex polygons with a colour index, in the profile's glyph box.
//
// The glyph is chosen by the kombiDirection() family, so the Mapbox path reads a maneuver exactly like the parametric
// one (left variants are the right glyph mirrored). Roundabouts are drawn from their exit angle;
// ferry/unknown fold to the straight glyph via kombiDirection.
//
// Opt-in: NBT_HUD_GLYPHS=1 (on / yes / true) in nbt-aad.env draws these; unset or off keeps the parametric shapes, the
// default look on every 6WA.
#ifndef CLUSTER_MAPBOX_HUD_HPP
#define CLUSTER_MAPBOX_HUD_HPP

#include "nbt_env.hpp"                    // envFlag
#include "cluster/mapbox_glyphs.hpp"      // mbx::Piece + the glyph tables
#include "cluster/hudterm_junction.hpp"   // Item, Picture
#include "cluster/hud6wa_profile.hpp"     // HudProfile (fill, road, glyph)
#include "cluster/hudterm_shapes.hpp"     // Rect, Point, Poly, pt(), poly(); kombiDirection via nav_msg
#include <stdlib.h>
#include <vector>

namespace nbt {
namespace hudterm {

// The 6WA box the Mapbox pieces live in: x 70..170, y 20..120 (the F20 glyph box). Map one point into the profile's
// glyph rect, mirroring x around the box centre (120) for a left variant. Division is by the constant extent (100)
// only, so no __aeabi integer-divide helper is emitted.
inline Point mbxPoint(int x, int y, bool mirror, const Rect& box) {
    if (mirror) x = 240 - x;
    return shapes::pt(box.x0 + (x - 70) * (box.x1 - box.x0) / 100,
                      box.y0 + (y - 20) * (box.y1 - box.y0) / 100);
}

inline int mbxCanonicalFamily(int family) {
    switch (family) {
        case 2: return 3;
        case 4: return 5;
        case 6: return 7;
        case 8: return 9;
        case 25: case 32: case 33: return 26;
        case 28: return 29;
        case 37: return 38;
        default: return family;
    }
}

// Namespace-const arrays have a separate copy in each translation unit. Select metadata by family, never address.
inline const mbx::CurveBody* mbxCurveBody(int family) {
    const int key = mbxCanonicalFamily(family);
    for (size_t i = 0; i < sizeof(mbx::kCurveBodies)/sizeof(mbx::kCurveBodies[0]); ++i)
        if (mbx::kCurveBodies[i].family == key) return &mbx::kCurveBodies[i];
    return 0;
}

// Append a glyph's convex pieces to the picture. tone 0 (route) -> the fill colour; tone 1 (context) -> the road tone.
inline void mbxGlyph(const mbx::Piece* g, unsigned n, bool mirror, const HudProfile& prof, Picture& pic,
                     const mbx::CurveBody* body) {
    for (unsigned i = 0; i < n; i++) {
        const mbx::Piece& pc = g[i];
        std::vector<Point> q;
        for (unsigned j = 0; j < pc.n; j++)
            q.push_back(mbxPoint(pc.p[j][0], pc.p[j][1], mirror, prof.glyph));
        Item it;
        it.colour = pc.tone ? prof.road : prof.fill;
        it.poly = shapes::poly(q);
        pic.push_back(it);
    }
    if (body) {
        std::vector<Point> q;
        for (unsigned j = 0; j < body->n; ++j) {
            int x = mirror ? 2400-body->p[j][0] : body->p[j][0], y = body->p[j][1];
            q.push_back(shapes::pt(prof.glyph.x0+(x-700)*(prof.glyph.x1-prof.glyph.x0)/1000,
                                  prof.glyph.y0+(y-200)*(prof.glyph.y1-prof.glyph.y0)/1000));
        }
        Item it; it.colour = prof.fill; it.poly = shapes::poly(q); pic.push_back(it);
    }
}

// kombiDirection family -> Mapbox glyph (+ mirror for the left variants). false for families with no static glyph
// (roundabout), so the caller draws the parametric shape instead. The left cases fall through to their right glyph.
inline bool mbxGlyphForFamily(int fam, const mbx::Piece*& g, unsigned& n, bool& mir) {
    mir = false;
    switch (fam) {
        case 1:  g = mbx::kArrowstraight;    n = mbx::kArrowstraightN;    return true;   // straight on
        case 2:  mir = true;                                                             // slight left
        case 3:  g = mbx::kArrowslightright; n = mbx::kArrowslightrightN; return true;   // slight right
        case 6:  mir = true;                                                             // normal left
        case 7:  g = mbx::kArrowright;       n = mbx::kArrowrightN;       return true;   // normal right
        case 4:  mir = true;                                                             // sharp left
        case 5:  g = mbx::kArrowsharpright;  n = mbx::kArrowsharprightN;  return true;   // sharp right
        case 8:  mir = true;                                                             // u-turn left
        case 9:  g = mbx::kArrow180right;    n = mbx::kArrow180rightN;    return true;   // u-turn right
        case 10: g = mbx::kDestination;      n = mbx::kDestinationN;      return true;   // destination
        case 25: mir = true;                                                            // on-ramp left
        case 26: g = mbx::kMerge;            n = mbx::kMergeN;            return true;   // on-ramp (merge on)
        case 28: mir = true;                                                            // off-ramp left
        case 29: g = mbx::kOfframp;          n = mbx::kOfframpN;          return true;   // off-ramp
        case 32: mir = true;                                                            // merge left
        case 33: g = mbx::kMerge;            n = mbx::kMergeN;            return true;   // merge right
        case 37: mir = true;                                                            // fork left
        case 38: g = mbx::kFork;             n = mbx::kForkN;             return true;   // fork right
        default: return false;   // roundabout (14/15) and anything else -> parametric
    }
}

// Fitting to post-wing bounds would resize the unchanged shaft and tip along with the narrower wings.
inline bool mbxOriginalBounds(int family, Rect& bounds) {
    const mbx::Piece* glyph; unsigned count; bool mirror;
    if (!mbxGlyphForFamily(family, glyph, count, mirror)) return false;
    const int key = mbxCanonicalFamily(family);
    for (size_t i = 0; i < sizeof(mbx::kOriginalBounds) / sizeof(mbx::kOriginalBounds[0]); ++i) {
        const mbx::OriginalBounds& b = mbx::kOriginalBounds[i];
        if (b.family != key) continue;
        bounds = Rect{ (int16_t)(mirror ? 240 - b.x1 : b.x0), b.y0,
                       (int16_t)(mirror ? 240 - b.x0 : b.x1), b.y1 };
        return true;
    }
    return false;   // destination and non-arrow glyphs retain their current bounds
}

// ---- Mapbox roundabout -----------------------------------------------------------------------------------------------
// A reduced port of the CarPlay plugin's roundabout (hudshapes.h): the ring, the entry stem and the kRbArrow exit arrow
// turned to the exit angle. No junction roads, no exit number. Maths in Mapbox's 512-unit canvas -> the 6WA box
// (boxX/boxY) -> the profile glyph box (mbxPoint). isin/icos here are x4096 (CarPlay's were x1024) and the fixed size is
// k=16, so the divisors fold both in: /4096.
namespace rb {
const int kRadius = 104, kWidth = 64;   // Mapbox: radius 6.5, line 4
struct P16 { int x, y; };
inline int boxX(int v) { return 70 + ((v * 25 + 64) >> 7); }
inline int boxY(int v) { return 20 + ((v * 25 + 64) >> 7); }
inline P16 polar(P16 c, int deg, int r, int w) {
    return { c.x + (r * isin(deg) + w * isin(deg + 90)) / 4096,
             c.y - (r * icos(deg) + w * icos(deg + 90)) / 4096 };
}
inline P16 centre(int A) { return { 256 + 96 * isin(A - 180) / 4096, 256 + 80 * icos(A - 180) / 4096 }; }

// one convex piece (Mapbox-512 points) -> an Item in the glyph box
inline void piece(const P16* p, unsigned n, uint8_t colour, bool mir, const Rect& box, Picture& pic) {
    std::vector<Point> q;
    for (unsigned j = 0; j < n; j++) q.push_back(mbxPoint(boxX(p[j].x), boxY(p[j].y), mir, box));
    long area = 0;
    for (unsigned j = 0; j < n; j++) {
        const Point& prev = q[j ? j - 1 : n - 1];
        area += (long)prev.x * q[j].y - (long)q[j].x * prev.y;
    }
    if (!area) return;   // a short arc can flatten to a line in the measured pixel box
    Item it; it.colour = colour; it.poly = shapes::poly(q); pic.push_back(it);
}
inline void rect(int x0, int y0, int x1, int y1, uint8_t col, bool mir, const Rect& box, Picture& pic) {
    P16 p[4] = { {x0, y0}, {x1, y0}, {x1, y1}, {x0, y1} }; piece(p, 4, col, mir, box, pic);
}
// a band of the circle (radius r, width w) from a0 to a1, convex pieces of at most 40 degrees
inline void arc(P16 c, int r, int w, int a0, int a1, uint8_t col, bool mir, const Rect& box, Picture& pic) {
    int span = a1 > a0 ? a1 - a0 : a0 - a1, dir = a1 > a0 ? 1 : -1;
    if (!span) return;
    int steps = (span + 39) / 40, step = span / steps, rem = span - step * steps, b0 = a0, ro = r + w / 2, ri = r - w / 2;
    for (int i = 1; i <= steps; i++) {
        int b1 = a0 + dir * (i * step + (i < rem ? i : rem)), d = b1 - b0;
        P16 qp[5] = { polar(c, b0, ro, 0), polar(c, b0 + d / 2, ro, 0), polar(c, b1, ro, 0), polar(c, b1, ri, 0), polar(c, b0, ri, 0) };
        P16 o[5]; unsigned k = 0;
        for (unsigned j = 0; j < 5; j++) if (j != 1 || d > 20 || d < -20) o[k++] = qp[j];
        piece(o, k, col, mir, box, pic);
        b0 = b1;
    }
}
}  // namespace rb

// Ring (not driven -> context tone, driven -> route/fill), entry stem, and the kRbArrow exit arrow turned to the exit
// angle. clockwise (left-hand-traffic roundabout) mirrors the whole thing.
inline void mbxRoundabout(int exit, bool clockwise, const HudProfile& prof, Picture& pic) {
    bool mir = clockwise;
    int A = exit, out = 180 - A;   // AA already gives degrees travelled from the entry (180 = straight), unlike CarPlay
    rb::P16 c = rb::centre(A);
    rb::arc(c, rb::kRadius, rb::kWidth, out, -180, prof.road, mir, prof.glyph, pic);   // the part not driven
    rb::arc(c, rb::kRadius, rb::kWidth, 180, out, prof.fill, mir, prof.glyph, pic);    // the way round
    int bottom = c.y + 264 > 512 ? 512 : c.y + 264;
    rb::rect(c.x - 32, c.y + 72, c.x + 32, bottom, prof.fill, mir, prof.glyph, pic);   // the entry stem
    int th = -(A + 90), co = icos(th), si = isin(th);                                  // the exit arrow, out of the ring
    for (unsigned i = 0; i < mbx::kRbArrowN; i++) {
        const mbx::Piece& g = mbx::kRbArrow[i];
        rb::P16 o[8];
        for (unsigned j = 0; j < g.n && j < 8; j++) {
            int x = g.p[j][0], y = g.p[j][1];
            o[j] = { c.x + (x * co - y * si) / 4096, c.y + (x * si + y * co) / 4096 };
        }
        rb::piece(o, g.n, g.tone ? prof.road : prof.fill, mir, prof.glyph, pic);
    }
}

inline bool mbxEnabled() {
    return nbt::envFlag("NBT_HUD_GLYPHS") == 1;   // opt-in: unset, off / no / false keep the parametric shapes
}

// Draw the maneuver as a Mapbox picture (static glyph or the roundabout). Returns false (nothing appended) when there is
// none for it, so the caller draws the parametric shape.
inline bool mbxManeuver(const ShapeSpec& spec, const HudProfile& prof, Picture& pic) {
    if (!mbxEnabled()) return false;
    int fam = kombinav::kombiDirection(spec.aa_type);
    if (fam == 14 || fam == 15) {   // roundabout: 14 = clockwise (left-hand traffic)
        if (!spec.exit) return false;   // unknown angle: the plain ring with its exit number
        mbxRoundabout(spec.exit, spec.clockwise, prof, pic);
        return true;
    }
    const mbx::Piece* g; unsigned n; bool mir;
    if (!mbxGlyphForFamily(fam, g, n, mir)) return false;
    mbxGlyph(g, n, mir, prof, pic, mbxCurveBody(fam));
    return true;
}

} // namespace hudterm
} // namespace nbt

#endif
