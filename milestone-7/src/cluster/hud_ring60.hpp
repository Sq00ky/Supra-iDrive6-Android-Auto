// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUD_RING60_HPP
#define CLUSTER_HUD_RING60_HPP

#include "cluster/hudterm_ring.hpp"
#include "cluster/hud_ring60_data.hpp"

namespace nbt { namespace hudterm { namespace ring60 {

// Drop collinear native vertices before sending a simple filled polygon.
inline void compact(Poly& poly) {
    bool changed = true;
    while (changed && poly.pts.size() > 3) {
        changed = false;
        for (size_t i = 0; i < poly.pts.size(); ++i) {
            size_t before = i ? i - 1 : poly.pts.size() - 1;
            size_t after = i + 1 < poly.pts.size() ? i + 1 : 0;
            if (!ring::turn(poly.pts[before], poly.pts[i], poly.pts[after])) {
                poly.pts.erase(poly.pts.begin() + i);
                changed = true;
                break;
            }
        }
    }
}

inline void append(Poly poly, uint8_t colour, bool clockwise, int cy, Picture& pic) {
    compact(poly);
    for (size_t i = 0; i < poly.pts.size(); ++i) {
        poly.pts[i].x = 62 + (clockwise ? -poly.pts[i].x : poly.pts[i].x);
        poly.pts[i].y += cy;
    }
    ring::append(poly, colour, pic);
}

inline void outline(const NativePoint* points, size_t count, uint8_t colour,
                    bool clockwise, int cy, Picture& pic) {
    Poly poly; poly.type = kPolygon;
    for (size_t i = 0; i < count; ++i) ring::addPoint(poly, shapes::pt(points[i].x, points[i].y));
    append(poly, colour, clockwise, cy, pic);
}

// All sectors share the same convex lattice circles, including their cut endpoints.
inline void arc(Poly& poly, const NativePoint* points, int count, int first, int last, int direction) {
    for (int i = first;;) {
        ring::addPoint(poly, shapes::pt(points[i].x, points[i].y));
        if (i == last) break;
        i += direction;
        if (i < 0) i = count-1;
        else if (i == count) i = 0;
    }
}

inline void band(int first, int last, uint8_t colour, bool clockwise, int cy, Picture& pic) {
    if (first >= last) return;
    Poly poly; poly.type = kPolygon;
    arc(poly, kOuter, sizeof(kOuter)/sizeof(kOuter[0]), kOuterIndex[first], kOuterIndex[last], 1);
    arc(poly, kInner, sizeof(kInner)/sizeof(kInner[0]), kInnerIndex[last], kInnerIndex[first], -1);
    append(poly, colour, clockwise, cy, pic);
}

inline bool picture(const ShapeSpec& spec, const HudProfile& profile, Picture& pic, int number) {
    int angle = spec.exit >= 1 && spec.exit <= 360 ? spec.exit : 0;
    if (angle > 0 && angle < 30) angle = 30;
    if (angle > 330 && angle < 360) angle = 330;
    int cy = !angle ? 38 : angle == 360 ? kReturnCenterY : kPlacement[angle-30].y;
    if (!angle) {
        for (size_t i = 0; i < sizeof(kCirculation)/sizeof(kCirculation[0]); ++i)
            outline(kCirculation[i].points, kCirculation[i].count, profile.fill, spec.clockwise, cy, pic);
    } else {
        uint8_t background = angle == 360 ? profile.fill : profile.road;
        band(0, 180, background, spec.clockwise, cy, pic);
        band(180, 360, background, spec.clockwise, cy, pic);
        if (angle != 360) {
            band(0, std::min(180, angle), profile.fill, spec.clockwise, cy, pic);
            band(180, angle, profile.fill, spec.clockwise, cy, pic);
        }
        int entry = angle == 360 ? kReturnEntryEnd : kPlacement[angle-30].x;
        int offset = angle == 360 ? 7 : 0;
        Poly entrance; entrance.type = kPolygon;
        entrance.pts.push_back(shapes::pt(offset-4,23));
        entrance.pts.push_back(shapes::pt(offset+4,23));
        entrance.pts.push_back(shapes::pt(offset+4,entry));
        entrance.pts.push_back(shapes::pt(offset-4,entry));
        append(entrance, profile.fill, spec.clockwise, cy, pic);
        outline(angle == 360 ? kReturn : kExits[angle-30], 7, profile.fill, spec.clockwise, cy, pic);
    }
    if (number < 1 || number > 99) return true;
    // Preserve the approved single-digit contours, enlarged after native pixel fitting.
    // Two digits use the smaller source caption to retain clearance inside the circle.
    int height = number > 9 ? 18 : 24;
    Shape caption;
    digits::drawNumber(number, 0, -height/2, height, caption);
    for (size_t i = 0; i < caption.size(); ++i) {
        for (size_t j = 0; j < caption[i].pts.size(); ++j) {
            Point& p = caption[i].pts[j];
            // Round on the final positive canvas, including half-pixel caption vertices.
            p.x = (int16_t)(ring::rounded(62L*4+(long)p.x*5,4)-62);
            p.y = (int16_t)(ring::rounded((long)cy*4+(long)p.y*5,4)-cy);
        }
        append(caption[i], profile.fill, false, cy, pic); // captions never mirror
    }
    return true;
}

} } }
#endif
