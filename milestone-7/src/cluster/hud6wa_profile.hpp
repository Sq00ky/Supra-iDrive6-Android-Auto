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
#include <stdlib.h>
#include <stdio.h>

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
    uint16_t hide[8];          // handles the cluster draws itself over the panel during guidance (e.g. 0x15/0x16, the
    int nHide;                 // narrow strips by the distance): SetVisibility-off'd on every picture and distance
                               // update. 0 = none; set by NBT_HUD_HIDE "21 22". Same idea as carplay-advanced hud_hide.
    bool singlePicture;        // draw on bufB until the other buffer is visually verified
    bool panelBeforeText;      // the panel must be visible while writing or clearing text
    uint16_t distanceBar;      // factory potentiometer, 0 when this layout has no known handle
};

// The 6WA, as our F20 showed it:
// F30 owner check (2026-11-26): index7 appeared as a WHITE outline at night. The F20 colour comments below
// are not a portable F30 palette; night road/background indices still need an on-car swatch check.
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
    { 0, 0, 199, 129 },    // canvas: the F20's (above); a cluster that answers ImageGetSize gets its own (fitMeasured).
    { 70, 20, 170, 120 },  // glyph: the F20's; moved only where a measured buffer would cut it off (fitMeasured).
                           // Both stay the fallback for a cluster that does not answer. Pin either at the car:
                           // NBT_HUD_CANVAS / NBT_HUD_GLYPH "x0 y0 x1 y1".
    { 0, 0, 183, 16 },     // lanes: the laneA/laneB handles (9/10) measured 184x17; override NBT_HUD_LANES_BOX.
                           // Lanes draw only with NBT_HUD_LANES=1 (hud_lanes); this is a starting box to tune on the car.
    32,
    8,
    "-./",
    {0}, 0, false, false, 0,
};

// Separately opt-in F15 HUD layout, verified on the user's vehicle. Keep 6WA's F20 profile untouched.
// Panel 0x01, factory arrow buffers 0x04/0x05 (start on verified 0x05), and three confirmed ASCII text handles.
// Only the verified glyph box is treated as drawable; overlay and lane drawing stay disabled pending tests.
const HudProfile k6waF15 = {
    "6wa-f15",
    0x01,
    0x0C, 0x10, 0x0D,
    0x04, 0x05,
    0, 0,
    0, 0,
    4,
    7, 3, 4,
    { 35, 20, 136, 110 },
    { 35, 20, 136, 110 },
    { 0, 0, 0, 0 },
    32,
    8,
    "-./",
    {0}, 0, true, true, 0x13,
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
    if (p.distanceBar) h.allow(p.distanceBar);
    if (p.routeA && p.routeB) {
        h.allow(p.routeA);
        h.allow(p.routeB);
    }
    if (p.laneA && p.laneB) {
        h.allow(p.laneA);
        h.allow(p.laneB);
    }
    for (int i = 0; i < p.nHide; i++) h.allow(p.hide[i]);   // the surfaces we SetVisibility-off (NBT_HUD_HIDE)
    return h;
}

// On-car tuning without a rebuild: the cluster's exact buffer and the arrow's box are hardware, found by eye on the
// car. NBT_HUD_CANVAS / NBT_HUD_GLYPH = "x0 y0 x1 y1" (inclusive); NBT_HUD_FILL = colour index. Unset -> the measured
// defaults above. Same idea as the carplay-advanced hud_box switch that fixed CarPlay on this cluster.
inline bool parseRect(const char* s, Rect& r) {
    if (!s || !*s) return false;
    int a, b, c, d;
    if (sscanf(s, "%d %d %d %d", &a, &b, &c, &d) != 4) return false;
    r.x0 = (int16_t)a; r.y0 = (int16_t)b; r.x1 = (int16_t)c; r.y1 = (int16_t)d;
    return true;
}

inline void applyHudEnv(HudProfile& p) {
    Rect r;
    if (parseRect(getenv("NBT_HUD_CANVAS"), r)) p.canvas = r;
    if (parseRect(getenv("NBT_HUD_GLYPH"), r))  p.glyph = r;
    if (parseRect(getenv("NBT_HUD_LANES_BOX"), r)) p.lanes = r;
    const char* f = getenv("NBT_HUD_FILL");
    if (f && *f) p.fill = (uint8_t)atoi(f);
    // NBT_HUD_HIDE "21 22": handles (decimal) the cluster draws over the panel itself -> SetVisibility-off each picture.
    const char* hd = getenv("NBT_HUD_HIDE");
    if (hd && *hd) {
        p.nHide = 0;
        for (const char* s = hd; *s && p.nHide < 8; ) {
            while (*s && (*s < '0' || *s > '9')) ++s;   // any separator (space, comma): space-free env values are safe
            if (!*s) break;
            int v = atoi(s);
            if (v > 0 && v <= 255) p.hide[p.nHide++] = (uint16_t)v;
            while (*s >= '0' && *s <= '9') ++s;
        }
    }
}

// Fit the measured buffer, keeping the F20 fallback unless its box is cut off or the measured field is the F30
// cluster's 176x126. sabifa's factory captures (#264) put the HUD arrow at 37,23..82,68 on 126x77, and the cluster
// arrow near 50,36..121,113: the same proportional box fits both. Round to the nearest pixel when scaling it.
// Env pins win; no answer keeps the original F20 profile.
inline void fitMeasured(int w, int h, HudProfile& p) {
    if (w <= 0 || h <= 0) return;
    if (!getenv("NBT_HUD_CANVAS")) {
        p.canvas.x0 = 0; p.canvas.y0 = 0;
        p.canvas.x1 = (int16_t)(w - 1); p.canvas.y1 = (int16_t)(h - 1);
    }
    bool cut = p.glyph.x0 < 0 || p.glyph.y0 < 0 || p.glyph.x1 >= w || p.glyph.y1 >= h;
    if ((cut || (!p.singlePicture && w == 176 && h == 126)) && !getenv("NBT_HUD_GLYPH")) {
        p.glyph.x0 = (int16_t)((w * 37 + 63) / 126); p.glyph.x1 = (int16_t)((w * 82 + 63) / 126);
        p.glyph.y0 = (int16_t)((h * 23 + 38) / 77);  p.glyph.y1 = (int16_t)((h * 68 + 38) / 77);
    }
}

// The ring fitting box, from the measured base profile. Ordinary glyph fitting remains separate; env pins win.
inline Rect ringGlyphBox(const HudProfile& p) {
    Rect box = p.glyph;
    if (!getenv("NBT_HUD_GLYPH") && p.canvas.x1 > p.canvas.x0 && p.canvas.y1 > p.canvas.y0) {
        int cw = p.canvas.x1 - p.canvas.x0 + 1, ch = p.canvas.y1 - p.canvas.y0 + 1;
        // Keep the car-tested ring size (53 then +25% on a 126x77 HUD) without enlarging ordinary arrows.
        bool cut = k6wa.glyph.x0 < p.canvas.x0 || k6wa.glyph.y0 < p.canvas.y0 ||
                   k6wa.glyph.x1 > p.canvas.x1 || k6wa.glyph.y1 > p.canvas.y1;
        if (!(cw == 176 && ch == 126) && cut) {
            int side = (cw < ch ? cw : ch) * 25 / 36;
            if (side > 100) side = 100;
            int x = p.canvas.x0 + (cw - side) / 2, y = p.canvas.y0 + (ch - side + 1) * 2 / 3;
            box.x0 = x; box.y0 = y; box.x1 = x + side; box.y1 = y + side;
        }
        int w = (box.x1 - box.x0) * 5 / 4, h = (box.y1 - box.y0) * 5 / 4;
        if (w > p.canvas.x1 - p.canvas.x0) w = p.canvas.x1 - p.canvas.x0;
        if (h > p.canvas.y1 - p.canvas.y0) h = p.canvas.y1 - p.canvas.y0;
        int x = (box.x0 + box.x1 - w) / 2, y = (box.y0 + box.y1 - h) / 2;
        if (x < p.canvas.x0) x = p.canvas.x0;
        if (y < p.canvas.y0) y = p.canvas.y0;
        if (x + w > p.canvas.x1) x = p.canvas.x1 - w;
        if (y + h > p.canvas.y1) y = p.canvas.y1 - h;
        box.x0 = x; box.y0 = y; box.x1 = x + w; box.y1 = y + h;
    }
    return box;
}

} // namespace hudterm
} // namespace nbt

#endif
