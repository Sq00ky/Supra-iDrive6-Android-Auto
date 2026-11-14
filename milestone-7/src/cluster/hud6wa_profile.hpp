// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUD6WA_PROFILE_HPP
#define CLUSTER_HUD6WA_PROFILE_HPP

// Everything specific to one cluster's HUDTerminal layout: which instance does what, which colours, where to draw.
// Fixed, not learned: the 6WA's values were read from the factory navigation's own traffic on an F20 (S19), and the
// same instances drew our arrow, distance, unit and street there. A cluster with the same protocol and another layout is
// a new profile, not a new driver.
//
// What the factory navigation does with the 6WA's instances (car): 1 shows or hides the whole guidance panel; 2 street,
// 3 distance, 4 unit (texts); 5 and 6 the double-buffered main picture, swapped with SwitchVisibility; 7..10 other
// drawing layers and 22 a visibility of unknown use (we never touch 22). At the end of a route it switches 1 off,
// clears 5..10, switches 2 and 22 off and empties the texts, leaving 3, 4 and buffer 6 visible.
//
// Its near view (issue 52 capture, F20): the roads in 6, the route over them in 7 ("DecreasingArrow"), on the same
// coordinates; 8 hidden and cleared while the picture is a single arrow, then SwitchVisibility(8 -> 7). So 7/8 is the
// route overlay, double-buffered like 5/6. 9/10 are named LaneGuidance in the firmware; the factory capture doesn't
// use them, so where they show is a guess.

#include "cluster/hudterm.hpp"
#include "cluster/hudterm_shapes.hpp"
#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace hudterm {

struct HudProfile {
    const char* name;
    uint16_t frame;            // the panel: on while we show guidance
    uint16_t street, distance, unit;   // texts
    uint16_t bufA, bufB;       // the double-buffered picture; bufB is the one the navigation leaves visible
    uint16_t routeA, routeB;   // the route overlay over the picture, same coordinates; routeB is visible at rest
                               // (0: no overlay, a junction view's route is drawn into the picture with its roads)
    uint16_t laneA, laneB;     // the lanes; laneB visible at rest (0: no lanes)
    uint8_t fill;              // fill colour index for the picture (a role: the cluster's colour scheme picks the RGB)
    uint8_t edge, road, route; // a junction view's colours: dark road edges, grey roads, the route (and the lanes to
                               // take; the others are drawn in `road`)
    Rect canvas;               // the drawable area, as far as it is known
    Rect glyph;                // where the picture goes, inside the canvas (a junction view is centred on it)
    Rect lanes;                // where the lanes go
    size_t streetChars;        // longest street text
    size_t distanceChars;      // longest distance text
    const char* streetPunct;   // punctuation kept in a street name (toClusterText); anything else becomes a space
};

// The 6WA, as our F20 showed it:
//   - fill 4 is the factory navigation's colour for its main picture (white at night on our car; other colour schemes
//     of a related cluster firmware make it orange: the scheme is the cluster's choice)
//   - junction view colours from the car's colour sweep: 7 black, 3 light grey, 4 white. Whether black covers grey on
//     the panel (the sweep showed 7 as nothing on black) is a car check: if not, the roads lose their edges, nothing
//     worse
//   - lanes in the free strip left of the picture, above where a speed would go: a guess until 9/10 are seen
//   - canvas: x from 0 at the panel's left edge, clipped somewhere below 200; y from 0 near the top, clipped about
//     120..140 (photos, rough). The glyph box is where our test arrow (60 x 100, centred on 120,70) showed whole.
//   - texts up to 32 ASCII characters showed whole. A comma made a text show nothing; '-', '.' and '/' are
//     untested guesses (one bad character blanks the text, nothing worse), the distance keeps only its decimal point.
const HudProfile k6wa = {
    "6wa",
    1,
    2, 3, 4,
    5, 6,
    7, 8,
    9, 10,
    4,
    7, 3, 4,
    { 0, 0, 199, 129 },
    { 70, 20, 170, 120 },
    { 4, 22, 68, 48 },
    32,
    8,
    "-./",
};

// The handles a profile lets the driver use: nothing else can be sent (cluster/hudterm.hpp buildCall).
inline HandleSet handlesOf(const HudProfile& p) {
    HandleSet h;
    h.allow(p.frame);
    h.allow(p.street);
    h.allow(p.distance);
    h.allow(p.unit);
    h.allow(p.bufA);
    h.allow(p.bufB);
    if (p.routeA && p.routeB) {
        h.allow(p.routeA);
        h.allow(p.routeB);
    }
    if (p.laneA && p.laneB) {
        h.allow(p.laneA);
        h.allow(p.laneB);
    }
    return h;
}

} // namespace hudterm
} // namespace nbt

#endif
