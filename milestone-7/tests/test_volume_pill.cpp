// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: the volume pill (volume_pill.hpp). Also writes tests/build/volume_pill_*.ppm to look at.
#include "volume_pill.hpp"
#include "volume_glyphs.hpp"

#include <stdio.h>
#include <string.h>
#include <vector>

using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static void testLabel() {
    CHECK(pillLabelFor(false, false) == kPillLabelEntertainment);
    CHECK(pillLabelFor(true, false) == kPillLabelNavigation);   // our nav prompt focus is routed
    CHECK(pillLabelFor(false, true) == kPillLabelPhoneCall);
    CHECK(pillLabelFor(true, true) == kPillLabelPhoneCall);     // a call wins
    CHECK(!strcmp(pillLabelName(kPillLabelEntertainment), "Entertainment"));
    CHECK(!strcmp(pillLabelName(kPillLabelNavigation), "Navigation"));
    CHECK(!strcmp(pillLabelName(kPillLabelPhoneCall), "Phone call"));
}

struct Frame {
    std::vector<uint32_t> px;
    PillCanvas cv;
    explicit Frame(const PillLayout& L) : px((size_t)L.pw * L.ph, 0x12345678u) {
        cv.px = &px[0]; cv.w = L.pw; cv.h = L.ph; cv.stride = L.pw;
    }
    uint32_t at(int x, int y) const { return px[(size_t)y * cv.stride + x]; }
};

static const uint32_t kPanelColour = pillRgba(18, 22, 27, 0xE6);   // the design's panel

static int R(uint32_t c) { return c & 0xFF; }
static int G(uint32_t c) { return (c >> 8) & 0xFF; }
static int B(uint32_t c) { return (c >> 16) & 0xFF; }
static int A(uint32_t c) { return (c >> 24) & 0xFF; }
static bool bright(uint32_t c) { return R(c) > 190 && G(c) > 190 && B(c) > 190 && A(c) == 0xFF; }
static bool dark(uint32_t c) { return R(c) < 40 && G(c) < 40 && B(c) < 40; }

// Pixels of the label area that are clearly lighter than the panel.
static int labelInk(const Frame& f, const PillLayout& L) {
    int n = 0;
    for (int y = L.labelY; y < L.labelY + 26; ++y)
        for (int x = L.x + 100; x < L.x + 300; ++x)
            if (R(f.at(x, y)) > R(kPanelColour) + 40) ++n;
    return n;
}

static void writePpm(const Frame& f, const PillLayout& L, const char* name) {
    char path[128];
    snprintf(path, sizeof(path), "tests/build/volume_pill_%s.ppm", name);
    FILE* o = fopen(path, "wb");
    if (!o) return;
    const int x0 = L.x - 20, y0 = L.y - 20, w = L.w + 40, h = L.h + 40;
    fprintf(o, "P6\n%d %d\n255\n", w, h);
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x) {
            const uint32_t c = f.at(x, y);
            const int a = A(c), bg = 0x9A;   // over a mid grey, so the alpha shows
            const unsigned char rgb[3] = { (unsigned char)((R(c) * a + bg * (255 - a)) / 255),
                                           (unsigned char)((G(c) * a + bg * (255 - a)) / 255),
                                           (unsigned char)((B(c) * a + bg * (255 - a)) / 255) };
            fwrite(rgb, 1, 3, o);
        }
    fclose(o);
}

static void testLayout() {
    const PillLayout L = pillLayoutFor(1280, 480);
    CHECK(L.w == 400 && L.h == 96 && L.x == 440 && L.y == 358);
    CHECK(L.iconX == 464 && L.iconY == 378 && L.barX0 == 510 && L.barX1 == 770 && L.barY == 389 && L.barH == 9);
    CHECK(L.barX0 + L.barX1 == 2 * L.x + L.w);   // the bar's middle sits over the label's middle
    CHECK(L.labelY == 412 && L.radius == 4);
    CHECK(L.icon->px == 30 && L.labels->px == 21);

    const PillLayout d = pillLayoutFor(0, 0);   // cannot ask: 1280 x 480
    CHECK(d.pw == 1280 && d.ph == 480 && d.w == 400);

    const int sizes[2][2] = { { 1280, 384 }, { 1920, 768 } };   // scale 0.8 and 1.6
    for (int i = 0; i < 2; ++i) {
        const PillLayout s = pillLayoutFor(sizes[i][0], sizes[i][1]);
        CHECK(s.s == (i == 0 ? 0.8 : 1.6));
        for (int w = kPillLabelEntertainment; w <= kPillLabelPhoneCall; ++w) {
            const VolBitmap& b = s.labels->word[w];
            CHECK(b.w < s.w - 2 && s.labelY + b.h <= s.y + s.h - 1);
        }
        CHECK(s.barY + s.barH <= s.labelY);
        CHECK(s.y + s.h <= s.ph && s.x >= 0);
    }
}

static void testPaint() {
    const PillLayout L = pillLayoutFor(1280, 480);
    const int midY = L.barY + L.barH / 2;
    const int end55 = L.barX0 + (L.barX1 - L.barX0) * 55 / 100;
    {   // hidden: a transparent frame
        Frame f(L);
        pillClear(f.cv);
        bool clear = true;
        for (size_t i = 0; i < f.px.size(); ++i) if (A(f.px[i]) != 0) clear = false;
        CHECK(clear);
    }
    {   // Entertainment at 55 %
        Frame f(L);
        pillPaint(f.cv, L, 55, kPillLabelEntertainment);
        CHECK(f.at(L.x + 5, L.y + 48) == kPanelColour);
        CHECK(A(f.at(100, 100)) == 0);                            // Android Auto shows through
        CHECK(A(f.at(L.x - 1, L.y + 40)) == 0);
        CHECK(bright(f.at(end55 - 3, midY)));   // fill up to the level
        CHECK(dark(f.at(end55 + 3, midY)));     // track after it
        CHECK(labelInk(f, L) >= 50);
        writePpm(f, L, "1-entertainment");
    }
    {
        Frame f(L);
        pillPaint(f.cv, L, 55, kPillLabelNavigation);
        CHECK(labelInk(f, L) >= 50);
        writePpm(f, L, "2-navigation");
    }
    {   // a call has its own word
        Frame f(L), g(L);
        pillPaint(f.cv, L, 60, kPillLabelPhoneCall);
        pillPaint(g.cv, L, 60, kPillLabelEntertainment);
        CHECK(labelInk(f, L) >= 50);
        CHECK(f.px != g.px);
        writePpm(f, L, "5-phone-call");
    }
    {   // level 0: an empty track
        Frame f(L);
        pillPaint(f.cv, L, 0, kPillLabelEntertainment);
        CHECK(dark(f.at(L.barX0 + 2, midY)));
        writePpm(f, L, "3-level-0");
    }
    {   // full
        Frame f(L);
        pillPaint(f.cv, L, 100, kPillLabelEntertainment);
        CHECK(bright(f.at(L.barX1 - 2, midY)));
        writePpm(f, L, "4-full");
    }
}

static void testPack() {
    const int v = pillPack(55, kPillLabelNavigation);
    CHECK(pillPackedPct(v) == 55);
    CHECK(pillPackedLabel(v) == kPillLabelNavigation);
    CHECK(pillPackedPct(pillPack(100, kPillLabelEntertainment)) == 100);
    CHECK(pillPackedLabel(pillPack(0, kPillLabelEntertainment)) == kPillLabelEntertainment);
    CHECK(pillPack(55, kPillLabelNavigation) != pillPack(55, kPillLabelEntertainment));   // a label change redraws
}

int main() {
    testLabel();
    testPack();
    testLayout();
    testPaint();
    if (fails) { printf("test_volume_pill: %d failure(s)\n", fails); return 1; }
    printf("test_volume_pill OK\n");
    return 0;
}
