// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUD_UNIFIED_HPP
#define CLUSTER_HUD_UNIFIED_HPP
#include "cluster/hud_unified_data.hpp"
#include "cluster/hudterm_junction.hpp"
#include "cluster/hud6wa_profile.hpp"

namespace nbt { namespace hudterm { namespace unified {

inline const Glyph* glyph(int type, bool& mirror) {
    mirror = false;
    switch (kombinav::kombiDirection(type)) {
        case 2: return &kGlyphs[1];
        case 3: mirror = true; return &kGlyphs[1];
        case 6: return &kGlyphs[2];
        case 7: mirror = true; return &kGlyphs[2];
        case 4: return &kGlyphs[3];
        case 5: mirror = true; return &kGlyphs[3];
        case 8: return &kGlyphs[4];
        case 9: mirror = true; return &kGlyphs[4];
        case 25: return &kGlyphs[5];
        case 26: mirror = true; return &kGlyphs[5];
        case 28: return &kGlyphs[6];
        case 29: mirror = true; return &kGlyphs[6];
        case 37: return &kGlyphs[7];
        case 38: mirror = true; return &kGlyphs[7];
        case 32: return &kGlyphs[8];
        case 33: mirror = true; return &kGlyphs[8];
        case 10: return &kGlyphs[9];
        case 14: case 15: return NULL;
        default: return &kGlyphs[0];
    }
}

inline void append(const Outline& outline, uint8_t colour, bool mirror, int axis, int dx, int dy,
                   int tailDelta, Picture& out) {
    if (!outline.count) return;
    Item item; item.colour = colour; item.poly.type = kPolygon;
    for (size_t i = 0; i < outline.count; ++i) {
        size_t source = mirror ? outline.count-1-i : i;
        Point p = outline.points[source];
        if (source >= outline.tailStart) p.y += tailDelta;
        if (mirror) p.x = axis-p.x;
        p.x += dx; p.y += dy;
        item.poly.pts.push_back(p);
    }
    out.push_back(item);
}

inline int tailDelta(const Glyph& g, int stage) {
    if (stage < 1) return 0;
    if (stage > 4) stage = 4;
    return g.curveY + ((g.nearTail-g.curveY)*stage+3)/4 - g.farTail;
}

inline void roads(int type, const HudProfile& profile, bool near, Picture& out) {
    bool mirror; const Glyph* g = glyph(type,mirror); if (!g) return;
    const Road* list = near ? g->nearRoads : g->farRoads;
    size_t count = near ? g->nearCount : g->farCount;
    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < count; ++i)
            append(pass ? list[i].fill : list[i].edge, pass ? profile.road : profile.edge,
                   mirror,124,0,0,0,out);
}

inline void route(int type, const HudProfile& profile, int stage, Picture& out) {
    bool mirror; const Glyph* g = glyph(type,mirror); if (!g) return;
    int delta = tailDelta(*g,stage);
    // Paint every border before either fill, so the head cannot cut a dark seam through its stem.
    append(g->bodyEdge,profile.edge,mirror,124,0,0,delta,out);
    append(g->headEdge,profile.edge,mirror,124,0,0,0,out);
    append(g->body,stage ? profile.route : profile.fill,mirror,124,0,0,delta,out);
    append(g->head,stage ? profile.route : profile.fill,mirror,124,0,0,0,out);
    // MOST has no documented compound-contour encoding: one solid pin body, then its window mask.
    append(g->hole,profile.edge,mirror,124,0,0,0,out);
}

inline bool far(int type, const HudProfile& profile, Picture& out) {
    bool mirror; if (!glyph(type,mirror)) return false;
    roads(type,profile,false,out); route(type,profile,0,out); return true;
}

inline bool lanes(const std::vector<kombinav::AaLane>& lanes, const Rect& box,
                  uint8_t other, uint8_t take, Picture& out) {
    size_t arrows = 0;
    for (size_t i = 0; i < lanes.size(); ++i) arrows += lanes[i].size();
    if (lanes.empty() || !arrows || arrows > kLaneMaxArrows) return false;
    int n = (int)lanes.size(), room = box.x1-box.x0-2*kLaneMargin;
    int pitch = n > 1 && room/(n-1) < kLanePitch ? room/(n-1) : kLanePitch;
    int start = (box.x0+box.x1)/2-pitch*(n-1)/2;
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < n; ++i)
            for (size_t k = 0; k < lanes[i].size(); ++k) {
                const kombinav::LaneDirection& d = lanes[i][k];
                if (d.highlighted != (pass == 1)) continue;
                int index = d.shape >= 2 && d.shape <= 9 ? d.shape/2 : 0;
                bool mirror = index && (d.shape & 1);
                const Lane& lane = kLanes[index];
                append(lane.body,pass ? take : other,mirror,48,start+i*pitch-24,box.y0,0,out);
                append(lane.head,pass ? take : other,mirror,48,start+i*pitch-24,box.y0,0,out);
            }
    return true;
}

}}} // namespace nbt::hudterm::unified
#endif
