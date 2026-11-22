// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "volume_pill.hpp"
#include "volume.hpp"
#include "volume_glyphs.hpp"

#include <stdlib.h>
#include <string.h>

namespace nbt {

namespace {

// Reference design on a 1280x480 panel; the bitmaps' reference sizes are kVolIconRef / kVolLabelRef.
const int kRefPanelW = 1280;
const int kRefPanelH = 480;
const int kRefW = 400;
const int kRefH = 96;
const int kRefMargin = 26;
const int kRefRadius = 4;
const int kRefIconX = 24;
const int kRefIconTop = 20;
const int kRefBarX0 = 70;
const int kRefBarTop = 31;
const int kRefBarH = 9;
const int kRefLabelTop = 54;

const uint32_t kPanel = pillRgba(18, 22, 27, 0xE6);
const uint32_t kTrack = pillRgba(0x0B, 0x0D, 0x0F, 0xFF);
const uint32_t kSpeaker = pillRgba(0xE6, 0xED, 0xF3, 0xFF);
const uint32_t kLabel = pillRgba(0xD3, 0xE4, 0xF2, 0xFF);
const int kFillTop[3] = { 0xF4, 0xF6, 0xF8 };
const int kFillBottom[3] = { 0xC7, 0xCD, 0xD2 };

int scaled(int v, double s) { return (int)(v * s + 0.5); }

// top over bottom, straight alpha.
uint32_t over(uint32_t top, uint32_t bottom);

const uint32_t kRim = over(pillRgba(255, 255, 255, 0x29), kPanel);
const uint32_t kTrackLine = over(pillRgba(214, 224, 233, 0x99), kTrack);

uint32_t over(uint32_t top, uint32_t bottom) {
    const int ta = top >> 24, ba = bottom >> 24;
    const int oa = ta + ba * (255 - ta) / 255;
    if (oa == 0) return 0;
    int c[3];
    for (int i = 0; i < 3; ++i) {
        const int t = (top >> (8 * i)) & 0xFF, b = (bottom >> (8 * i)) & 0xFF;
        c[i] = (t * ta + b * ba * (255 - ta) / 255) / oa;
    }
    return pillRgba(c[0], c[1], c[2], oa);
}

void rect(PillCanvas& cv, int x0, int y0, int x1, int y1, uint32_t c) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > cv.w) x1 = cv.w;
    if (y1 > cv.h) y1 = cv.h;
    for (int y = y0; y < y1; ++y) {
        uint32_t* row = cv.px + (size_t)y * cv.stride;
        for (int x = x0; x < x1; ++x) row[x] = c;
    }
}

// Body plus a corner cut of radius r.
void roundRect(PillCanvas& cv, int x0, int y0, int x1, int y1, int r, uint32_t c) {
    if (r < 1) r = 1;
    rect(cv, x0 + r, y0, x1 - r, y1, c);
    rect(cv, x0, y0 + r, x0 + r, y1 - r, c);
    rect(cv, x1 - r, y0 + r, x1, y1 - r, c);
    for (int dy = 0; dy < r; ++dy) {
        for (int dx = 0; dx < r; ++dx) {
            if ((r - dx) * (r - dx) + (r - dy) * (r - dy) > r * r) continue;
            rect(cv, x0 + dx, y0 + dy, x0 + dx + 1, y0 + dy + 1, c);
            rect(cv, x1 - 1 - dx, y0 + dy, x1 - dx, y0 + dy + 1, c);
            rect(cv, x0 + dx, y1 - 1 - dy, x0 + dx + 1, y1 - dy, c);
            rect(cv, x1 - 1 - dx, y1 - 1 - dy, x1 - dx, y1 - dy, c);
        }
    }
}

// One 8-bit coverage bitmap in colour c.
void drawAlpha(PillCanvas& cv, int x, int y, int w, int h, const unsigned char* a, uint32_t c) {
    for (int gy = 0; gy < h; ++gy) {
        const int py = y + gy;
        if (py < 0 || py >= cv.h) continue;
        for (int gx = 0; gx < w; ++gx) {
            const int px = x + gx;
            const unsigned char v = a[(size_t)gy * w + gx];
            if (!v || px < 0 || px >= cv.w) continue;
            uint32_t* d = cv.px + (size_t)py * cv.stride + px;
            *d = over((c & 0x00FFFFFFu) | ((uint32_t)((c >> 24) * v / 255) << 24), *d);
        }
    }
}

void drawBar(PillCanvas& cv, int x0, int x1, int y, int h, int pct) {
    rect(cv, x0, y, x1, y + h, kTrackLine);   // 1 px outline; the fill covers it, as in the design
    rect(cv, x0 + 1, y + 1, x1 - 1, y + h - 1, kTrack);
    const int fw = (x1 - x0) * volumeClampPct(pct) / 100;
    for (int i = 0; i < h; ++i) {
        const int t = h > 1 ? i * 255 / (h - 1) : 0;
        int c[3];
        for (int k = 0; k < 3; ++k) c[k] = kFillTop[k] + (kFillBottom[k] - kFillTop[k]) * t / 255;
        rect(cv, x0, y + i, x0 + fw, y + i + 1, pillRgba(c[0], c[1], c[2], 0xFF));
    }
}

} // namespace

PillLayout pillLayoutFor(int pw, int ph) {
    if (pw <= 0 || ph <= 0) { pw = kRefPanelW; ph = kRefPanelH; }
    // Height drives the scale; width caps it at 40 % of the panel; clamped against a daft EDID.
    double s = (double)ph / kRefPanelH;
    const double wCap = (double)pw * 0.40 / (double)kRefW;
    if (s > wCap) s = wCap;
    if (s < 0.80) s = 0.80;
    if (s > 1.60) s = 1.60;

    PillLayout L;
    L.pw = pw;
    L.ph = ph;
    L.s = s;
    L.w = scaled(kRefW, s);
    L.h = scaled(kRefH, s);
    L.x = (pw - L.w) / 2;
    L.y = ph - L.h - scaled(kRefMargin, s);
    L.radius = scaled(kRefRadius, s);
    L.iconX = L.x + scaled(kRefIconX, s);
    L.iconY = L.y + scaled(kRefIconTop, s);
    L.barX0 = L.x + scaled(kRefBarX0, s);
    L.barX1 = L.x + L.w - (L.barX0 - L.x);   // centred, so 50 % sits over the label's middle
    L.barY = L.y + scaled(kRefBarTop, s);
    L.barH = scaled(kRefBarH, s);
    L.labelY = L.y + scaled(kRefLabelTop, s);

    // The bitmaps come in fixed sizes: take the nearest.
    const int icon = scaled(kVolIconRef, s), label = scaled(kVolLabelRef, s);
    L.icon = &kVolIcons[0];
    for (int i = 1; i < kVolIconCount; ++i)
        if (abs(kVolIcons[i].px - icon) < abs(L.icon->px - icon)) L.icon = &kVolIcons[i];
    L.labels = &kVolLabels[0];
    for (int i = 1; i < kVolLabelCount; ++i)
        if (abs(kVolLabels[i].px - label) < abs(L.labels->px - label)) L.labels = &kVolLabels[i];
    return L;
}

void pillClear(PillCanvas& cv) {
    for (int y = 0; y < cv.h; ++y) memset(cv.px + (size_t)y * cv.stride, 0, (size_t)cv.w * 4);
}

void pillPaint(PillCanvas& cv, const PillLayout& L, int pct, PillLabel label) {
    pillClear(cv);   // transparent: Android Auto shows through

    roundRect(cv, L.x, L.y, L.x + L.w, L.y + L.h, L.radius, kRim);   // 1 px rim
    roundRect(cv, L.x + 1, L.y + 1, L.x + L.w - 1, L.y + L.h - 1, L.radius - 1, kPanel);

    drawAlpha(cv, L.iconX, L.iconY, L.icon->px, L.icon->px, L.icon->alpha, kSpeaker);
    drawBar(cv, L.barX0, L.barX1, L.barY, L.barH, pct);
    const VolBitmap& b = L.labels->word[label];
    drawAlpha(cv, L.x + (L.w - b.w) / 2, L.labelY, b.w, b.h, b.alpha, kLabel);
}

} // namespace nbt
