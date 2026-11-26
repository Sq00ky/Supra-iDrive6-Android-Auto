// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUD_ROUNDABOUT_EXIT_HPP
#define CLUSTER_HUD_ROUNDABOUT_EXIT_HPP

#include "cluster/hud_ring60.hpp"

namespace nbt { namespace hudterm {

// The step starts inside the roundabout: show the departure, without reusing an entry-relative angle or exit number.
inline bool roundaboutExitPicture(const ShapeSpec& spec, const HudProfile& profile, Picture& pic) {
    Picture source;
    const Point arrow[] = {{58,-24}, {58,-51}, {52,-50}, {62,-62}, {72,-50}, {66,-51}, {66,-24}};
    Item item; item.colour = profile.fill; item.poly.type = kPolygon;
    item.poly.pts.assign(arrow, arrow + sizeof(arrow)/sizeof(arrow[0]));
    source.push_back(item);
    // The circular road covers the shaft's root, keeping a rectangular heel out of the join.
    ring60::band(180, 270, profile.road, false, 0, source);
    ring60::band(90, 180, profile.fill, false, 0, source);

    // Reuse the smooth native circle contours and uniform fitting; the approved ring drawings stay untouched.
    RingLayout layout = ringLayout(profile, 30*ring::kUnit, -62*ring::kUnit, 0);
    for (size_t i = 0; i < source.size(); ++i) {
        Poly poly; poly.type = kPolygon;
        for (size_t j = 0; j < source[i].poly.pts.size(); ++j) {
            const Point& p = source[i].poly.pts[j];
            int x = ring::offset((p.x-62)*ring::kUnit, layout.side);
            int y = ring::offset(p.y*ring::kUnit, layout.side);
            ring::addPoint(poly, shapes::pt(layout.cx + (spec.clockwise ? -x : x), layout.cy + y));
        }
        ring60::compact(poly);
        ring::removeBacktracks(poly);
        ring::append(poly, source[i].colour, pic);
    }
    return true;
}

} }
#endif
