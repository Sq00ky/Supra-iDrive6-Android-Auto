// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// Mapbox maneuver glyphs on the 6WA HUD. The polished convex-piece drawings in mapbox_glyphs.hpp (CC0, Mapbox; the same
// set the CarPlay HUD uses) mapped into the picture buffer, as a nicer alternative to the parametric shapes
// (hudterm_shapes.hpp drawShape). Same picture model: convex polygons with a colour index, in the profile's glyph box.
//
// The glyph is chosen by the kombiDirection() family, so the Mapbox path reads a maneuver exactly like the parametric
// one (left variants are the right glyph mirrored). Families with no static glyph (roundabouts) return false and the
// caller falls back to the parametric shape; ferry/unknown fold to the straight glyph via kombiDirection.
//
// On by default; NBT_HUD_GLYPHS=0 in nbt-aad.env falls back to the parametric shapes (to compare on the car).
#ifndef CLUSTER_MAPBOX_HUD_HPP
#define CLUSTER_MAPBOX_HUD_HPP

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

// Append a glyph's convex pieces to the picture. tone 0 (route) -> the fill colour; tone 1 (context) -> the road tone.
inline void mbxGlyph(const mbx::Piece* g, unsigned n, bool mirror, const HudProfile& prof, Picture& pic) {
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

inline bool mbxEnabled() {
    const char* e = getenv("NBT_HUD_GLYPHS");
    return !(e && e[0] == '0');   // on by default
}

// Draw the maneuver as a Mapbox glyph. Returns false (nothing appended) when there is no glyph for it, so the caller
// draws the parametric shape.
inline bool mbxManeuver(int aa_type, const HudProfile& prof, Picture& pic) {
    if (!mbxEnabled()) return false;
    const mbx::Piece* g; unsigned n; bool mir;
    if (!mbxGlyphForFamily(kombinav::kombiDirection(aa_type), g, n, mir)) return false;
    mbxGlyph(g, n, mir, prof, pic);
    return true;
}

} // namespace hudterm
} // namespace nbt

#endif
