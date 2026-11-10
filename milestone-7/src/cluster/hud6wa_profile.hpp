// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUD6WA_PROFILE_HPP
#define CLUSTER_HUD6WA_PROFILE_HPP

// Everything specific to one cluster's HUDTerminal layout: which instance does what, which colours, where to draw.
// Fixed, not learned: the 6WA's values were read from the factory navigation's own traffic on an F20 (S19), and the
// same instances drew our arrow, distance, unit and street there. A cluster with the same protocol and another layout is
// a new profile, not a new driver. HudTerminal's profile check (cluster/hud_terminal.hpp) stops the driver if the
// factory navigation is seen using the instances differently.
//
// What the factory navigation does with the 6WA's instances (car): 1 shows or hides the whole guidance panel; 2 street,
// 3 distance, 4 unit (texts); 5 and 6 the double-buffered main picture, swapped with SwitchVisibility; 7..10 other
// drawing layers and 22 a visibility of unknown use: we never touch those. At the end of a route it switches 1 off,
// clears 5..10, switches 2 and 22 off and empties the texts, leaving 3, 4 and buffer 6 visible.

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
    uint8_t fill;              // fill colour index for the picture (a role: the cluster's colour scheme picks the RGB)
    Rect canvas;               // the drawable area, as far as it is known
    Rect glyph;                // where the picture goes, inside the canvas
    size_t streetChars;        // longest street text
    size_t distanceChars;      // longest distance text
    const char* streetPunct;   // punctuation kept in a street name (toClusterText); anything else becomes a space
};

// The 6WA, as our F20 showed it:
//   - fill 4 is the factory navigation's colour for its main picture (white at night on our car; other colour schemes
//     of a related cluster firmware make it orange: the scheme is the cluster's choice)
//   - canvas: x from 0 at the panel's left edge, clipped somewhere below 200; y from 0 near the top, clipped about
//     120..140 (photos, rough). The glyph box is where our test arrow (60 x 100, centred on 120,70) showed whole.
//   - texts up to 32 ASCII characters showed whole. A comma made a text show nothing; '-', '.' and '/' are
//     untested guesses (one bad character blanks the text, nothing worse), the distance keeps only its decimal point.
const HudProfile k6wa = {
    "6wa",
    1,
    2, 3, 4,
    5, 6,
    4,
    { 0, 0, 199, 129 },
    { 70, 20, 170, 120 },
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
    return h;
}

inline HandleSet textsOf(const HudProfile& p) {
    HandleSet h;
    h.allow(p.street);
    h.allow(p.distance);
    h.allow(p.unit);
    return h;
}

inline HandleSet buffersOf(const HudProfile& p) {
    HandleSet h;
    h.allow(p.bufA);
    h.allow(p.bufB);
    return h;
}

} // namespace hudterm
} // namespace nbt

#endif
