// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_VOLUME_PILL_HPP
#define NBT_VOLUME_PILL_HPP

// The volume pill's content and drawing, free of QNX so it is host-testable (tests/test_volume_pill.cpp). Metrics are
// the approved design's 1280x480 values scaled with the panel.

#include <stdint.h>

struct VolIcon;
struct VolLabelSet;

namespace nbt {

// Which volume the pill names.
enum PillLabel { kPillLabelEntertainment = 0, kPillLabelNavigation = 1 };

// focusRouted: our nav prompt focus is routed, so the knob sets the nav volume. The car's volume message cannot
// tell (car/volume_level.hpp), so BMW's own prompts count as entertainment.
inline PillLabel pillLabelFor(bool focusRouted) {
    return focusRouted ? kPillLabelNavigation : kPillLabelEntertainment;
}

inline const char* pillLabelName(PillLabel l) {
    return l == kPillLabelEntertainment ? "Entertainment" : (l == kPillLabelNavigation ? "Navigation" : "no label");
}

// Level 0..100 and label in one int, so another thread never reads half a pair.
inline int pillPack(int pct, PillLabel l) { return pct | ((int)l << 8); }
inline int pillPackedPct(int v) { return v & 0xFF; }
inline PillLabel pillPackedLabel(int v) { return (PillLabel)(v >> 8); }

// Colour words are B,G,R,A in memory on this unit.
inline uint32_t pillRgba(int r, int g, int b, int a) {
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

struct PillCanvas {
    uint32_t* px;
    int w, h, stride;
};

// Panel coordinates.
struct PillLayout {
    int pw, ph;
    int x, y, w, h;
    double s;
    int radius;
    int iconX, iconY;
    int barX0, barX1, barY, barH;
    int labelY;
    const VolIcon* icon;
    const VolLabelSet* labels;
};

// pw/ph <= 0: 1280x480.
PillLayout pillLayoutFor(int pw, int ph);

// The whole canvas transparent: no pill.
void pillClear(PillCanvas& cv);

// The whole canvas: transparent outside the pill.
void pillPaint(PillCanvas& cv, const PillLayout& L, int pct, PillLabel label);

} // namespace nbt

#endif // NBT_VOLUME_PILL_HPP
