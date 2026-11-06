// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// Volume overlay: a QNX Screen window on LayerManager surface 1281 of its own, drawn full-panel with a fully
// transparent background, so only the pill is opaque and Android Auto shows through everywhere else.
//
// Positioning is RELATIVE to the panel: the panel size is read from the projection window at runtime and every
// metric (pill, padding, gaps, bar, icon, digits, bottom margin) is the approved mockup's 1280x480 design scaled
// by it. Digits and the speaker are pre-rasterised bitmaps, so there is a set per size and the nearest is picked.
#include "volume_osd.hpp"
#include "screen_video.h"
#include "volume.hpp"
#include "volume_glyphs.hpp"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>

namespace nbt {

namespace {

// Reference design, in 1280x480 pixels (volume_osd_mock.html, the approved mockup).
const int kRefW = 372;
const int kRefH = 58;
const int kRefPad = 24;
const int kRefGap = 16;
const int kRefBarH = 6;
const int kRefNumBox = 46;
const int kRefMargin = 26;
const int kRefIcon = 22;
const int kRefDigit = 17;

const int kHideMs = 1400;
const int kFallbackW = NBT_SCREEN_WIDTH_SUPRA_WIDESCREEN;
const int kFallbackH = NBT_SCREEN_HEIGHT_SUPRA_WIDESCREEN;

// Colour words are B,G,R,A in memory on this unit: a fill set to AA blue (0x4A90D9) came out ORANGE on the car,
// and a green->amber->red ramp came out green->blue->dark blue, both consistent with red and blue being swapped.
// Baked in - the temporary NBT_VOL_BGR / NBT_VOL_RGB switches are gone, because there is one verified order.
inline uint32_t rgba(int r, int g, int b, int a) {
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

// Volume ramp: four stops for a smooth sweep - deep green -> lime -> amber -> red.
inline void ramp(int pct, int* r, int* g, int* b) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    static const int stops[4][4] = {
        {  0, 0x2F, 0xA8, 0x4F },   // 0%   green
        { 35, 0x7B, 0xC0, 0x43 },   // 35%  lime
        { 70, 0xF2, 0xA2, 0x1E },   // 70%  amber
        {100, 0xE0, 0x3B, 0x30 },   // 100% red
    };
    int i = 0;
    while (i < 2 && pct > stops[i + 1][0]) ++i;
    const int* a = stops[i];
    const int* c = stops[i + 1];
    const int span = c[0] - a[0];
    const int t = span > 0 ? (pct - a[0]) * 255 / span : 255;
    *r = a[1] + (c[1] - a[1]) * t / 255;
    *g = a[2] + (c[2] - a[2]) * t / 255;
    *b = a[3] + (c[3] - a[3]) * t / 255;
}

const uint32_t kPill   = rgba(0x08, 0x0A, 0x0D, 0xD1);   // dark navy, 0.82 alpha (mockup body)
const uint32_t kBorder = rgba(0xFF, 0xFF, 0xFF, 0x17);   // mockup: 1 px rgba(255,255,255,.09)
const uint32_t kTrack  = rgba(0xFF, 0xFF, 0xFF, 0x24);   // white, 14%
const uint32_t kGlyph  = rgba(0xCF, 0xDA, 0xE6, 0xFF);
const uint32_t kText   = rgba(0xE8, 0xEE, 0xF4, 0xFF);

struct Canvas {
    uint32_t* px;
    int w, h, stride;

    void clear(uint32_t c) {
        for (int y = 0; y < h; ++y) {
            uint32_t* row = px + (size_t)y * stride;
            for (int x = 0; x < w; ++x) row[x] = c;
        }
    }
    void rect(int x0, int y0, int x1, int y1, uint32_t c) {
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > w) x1 = w;
        if (y1 > h) y1 = h;
        for (int y = y0; y < y1; ++y) {
            uint32_t* row = px + (size_t)y * stride;
            for (int x = x0; x < x1; ++x) row[x] = c;
        }
    }
    // Rounded rect: body plus a corner cut, so the pill reads as a pill at any size.
    void pill(int x0, int y0, int x1, int y1, int r, uint32_t c) {
        if (r < 1) r = 1;
        rect(x0 + r, y0, x1 - r, y1, c);
        rect(x0, y0 + r, x0 + r, y1 - r, c);
        rect(x1 - r, y0 + r, x1, y1 - r, c);
        for (int dy = 0; dy < r; ++dy) {
            for (int dx = 0; dx < r; ++dx) {
                const int d2 = (r - dx) * (r - dx) + (r - dy) * (r - dy);
                if (d2 <= r * r) {
                    rect(x0 + dx, y0 + dy, x0 + dx + 1, y0 + dy + 1, c);
                    rect(x1 - 1 - dx, y0 + dy, x1 - dx, y0 + dy + 1, c);
                    rect(x0 + dx, y1 - 1 - dy, x0 + dx + 1, y1 - dy, c);
                    rect(x1 - 1 - dx, y1 - 1 - dy, x1 - dx, y1 - dy, c);
                }
            }
        }
    }
};

// Every drawn coordinate comes from this, so another panel only changes numbers here.
struct Layout {
    int pw, ph;          // panel
    int w, h;            // pill
    int x, y;            // pill top-left
    int pad, gap, barH, numBox, radius;
    int iconPx;
    const unsigned char* icon;
    const VolGlyphSet* gs;
};

int nearestIcon(int target) {
    int best = 0, bestd = 1 << 30;
    for (int i = 0; i < kVolIconCount; ++i) {
        const int d = abs(kVolIcons[i].px - target);
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}

int nearestGlyphs(int target) {
    int best = 0, bestd = 1 << 30;
    for (int i = 0; i < kVolSetCount; ++i) {
        const int d = abs(kVolSets[i].px - target);
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}

Layout layoutFor(int pw, int ph) {
    if (pw <= 0 || ph <= 0) { pw = kFallbackW; ph = kFallbackH; }
    // Height drives the scale (a pill is a height-sensitive element); width only caps it so the pill never takes
    // more than ~40% of a very narrow panel. Clamped so a daft EDID cannot produce a 4x pill.
    double s = (double)ph / 480.0;
    const double wCap = (double)pw * 0.40 / (double)kRefW;
    if (s > wCap) s = wCap;
    if (s < 0.80) s = 0.80;
    if (s > 1.60) s = 1.60;

    Layout L;
    L.pw = pw;
    L.ph = ph;
    L.w = (int)(kRefW * s + 0.5);
    L.h = (int)(kRefH * s + 0.5);
    L.pad = (int)(kRefPad * s + 0.5);
    L.gap = (int)(kRefGap * s + 0.5);
    L.barH = (int)(kRefBarH * s + 0.5);
    if (L.barH < 3) L.barH = 3;
    L.numBox = (int)(kRefNumBox * s + 0.5);
    L.radius = L.h / 2;
    const int margin = (int)(kRefMargin * s + 0.5);
    L.x = (pw - L.w) / 2;
    L.y = ph - L.h - margin;

    const int iconIdx = nearestIcon((int)(kRefIcon * s + 0.5));
    const int glyphIdx = nearestGlyphs((int)(kRefDigit * s + 0.5));
    L.iconPx = kVolIcons[iconIdx].px;
    L.icon = kVolIcons[iconIdx].alpha;
    L.gs = &kVolSets[glyphIdx];
    return L;
}

// Source-over blend of one pixel, alpha 0..1.
static void blendPx(Canvas& cv, int x, int y, uint32_t c, float a) {
    if (x < 0 || y < 0 || x >= cv.w || y >= cv.h) return;
    if (a <= 0.0f) return;
    if (a > 1.0f) a = 1.0f;
    uint32_t* d = cv.px + (size_t)y * cv.stride + x;
    const float sa = ((c >> 24) & 0xFF) / 255.0f * a;
    const float ia = 1.0f - sa;
    const float dr = (float)((*d) & 0xFF), dg = (float)(((*d) >> 8) & 0xFF);
    const float db = (float)(((*d) >> 16) & 0xFF), da = (float)(((*d) >> 24) & 0xFF);
    const float sr = (float)(c & 0xFF), sg = (float)((c >> 8) & 0xFF), sb = (float)((c >> 16) & 0xFF);
    const int orr = (int)(sr * sa + dr * ia);
    const int ogg = (int)(sg * sa + dg * ia);
    const int obb = (int)(sb * sa + db * ia);
    const int oaa = (int)(sa * 255.0f + da * ia);
    *d = ((uint32_t)(oaa & 0xFF) << 24) | ((uint32_t)(obb & 0xFF) << 16) |
         ((uint32_t)(ogg & 0xFF) << 8) | (uint32_t)(orr & 0xFF);
}

// One 8-bit alpha coverage bitmap (digits, speaker icon).
static void drawAlpha(Canvas& cv, int x, int y, int w, int h, const unsigned char* a, uint32_t c) {
    for (int gy = 0; gy < h; ++gy) {
        const unsigned char* row = a + (size_t)gy * w;
        for (int gx = 0; gx < w; ++gx) {
            const unsigned char v = row[gx];
            if (v) blendPx(cv, x + gx, y + gy, c, (float)v / 255.0f);
        }
    }
}

// "65" in the scale-appropriate tabular bold set, right-aligned to rightX, centred on centreY.
void drawValue(Canvas& cv, int rightX, int centreY, int pct, uint32_t c, const VolGlyphSet* gs) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", pct);
    const int len = (int)strlen(buf);
    int x = rightX - len * gs->adv;
    for (int i = 0; i < len; ++i) {
        const VolGlyph& g = gs->g[buf[i] - '0'];
        drawAlpha(cv, x + g.xoff, centreY - (int)g.h / 2, g.w, g.h, g.alpha, c);
        x += gs->adv;
    }
}

// Bar with rounded caps. The cap inset is measured inward from the nearest end, which is what makes it round.
void drawBar(Canvas& cv, int x0, int x1, int y, int h, int pct) {
    const int r = h / 2;
    cv.pill(x0, y, x1, y + h, r, kTrack);            // track: white 14%
    const int fillW = (x1 - x0) * volumeClampPct(pct) / 100;
    if (fillW < 2) return;

    int rr = 0, gg = 0, bb = 0;
    ramp(pct, &rr, &gg, &bb);
    const int lr = rr + (255 - rr) * 26 / 100;       // lighter tip, like the mockup's gradient
    const int lg = gg + (255 - gg) * 26 / 100;
    const int lb = bb + (255 - bb) * 26 / 100;

    for (int i = 0; i < fillW; ++i) {
        const int t = fillW > 1 ? i * 255 / (fillW - 1) : 255;
        const uint32_t col = rgba(rr + (lr - rr) * t / 255,
                                  gg + (lg - gg) * t / 255,
                                  bb + (lb - bb) * t / 255, 0xFF);
        int inset = 0;
        if (i < r) {
            const int dy = r - i;
            inset = r - (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
        } else if (i >= fillW - r) {
            const int dy = r - (fillW - 1 - i);
            if (dy > 0) inset = r - (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
        }
        if (inset * 2 >= h) continue;
        cv.rect(x0 + i, y + inset, x0 + i + 1, y + h - inset, col);
    }
}

void paint(Canvas& cv, const Layout& L, int pct) {
    cv.clear(0x00000000u);            // transparent: Android Auto shows through
    if (pct < 0) return;              // "hidden": transparent frame, window stays up

    cv.pill(L.x, L.y, L.x + L.w, L.y + L.h, L.radius, kBorder);
    cv.pill(L.x + 1, L.y + 1, L.x + L.w - 1, L.y + L.h - 1, L.radius - 1, kPill);

    const int cy = L.y + L.h / 2;
    drawAlpha(cv, L.x + L.pad, cy - L.iconPx / 2, L.iconPx, L.iconPx, L.icon, kGlyph);

    const int barX0 = L.x + L.pad + L.iconPx + L.gap;
    const int numRight = L.x + L.w - L.pad;
    const int barX1 = numRight - L.numBox - L.gap;
    drawBar(cv, barX0, barX1, cy - L.barH / 2, L.barH, pct);
    drawValue(cv, numRight, cy, pct, kText, L.gs);
}

// Surface 1281 as LayerManager creates it has an EMPTY source region and visibility 0, and is on no layer
// (osd1281probe: "source region: x=0, y=0, w=0, h=0" until set). Give it the panel as source and destination and make it
// visible; whether it is drawn is then decided by the Android Auto layer's render order alone (VideoManager). Returns the
// native surface LayerManager reports for it: <= 0 or 4294967295 = no window bound (yet), -1 = no answer.
long long lmSetupPillSurface(int id, int w, int h) {
    const char* const L = "/opt/sys/bin/LayerManagerControl";
    char cmd[640];
    snprintf(cmd, sizeof(cmd),
             "%s set surface %d source region 0 0 %d %d 2>/dev/null; "
             "%s set surface %d destination region 0 0 %d %d 2>/dev/null; "
             "%s set surface %d visibility 1 2>/dev/null; "
             "%s get surface %d 2>&1",
             L, id, w, h, L, id, w, h, L, id, L, id);
    FILE* p = popen(cmd, "r");
    if (!p) return -1;
    char line[256];
    long long native = -1;
    while (fgets(line, sizeof(line), p)) {
        const char* k = strstr(line, "native surface:");
        if (k) native = strtoll(k + 15, NULL, 10);
    }
    pclose(p);
    return native;
}

} // namespace

const int VolumeOsd::kSurfaceId;

VolumeOsd::VolumeOsd() : ctx_(0), win_(0), running_(false), ready_(false), pct_(100), dirty_(false), aaShown_(true),
                         surfaceReady_(false), parent_(0), ctxParent_(0) {}

VolumeOsd::~VolumeOsd() { stop(); }

void* VolumeOsd::threadThunk(void* self) {
    static_cast<VolumeOsd*>(self)->loop();
    return 0;
}

bool VolumeOsd::start(void* ctx, void* parentWindow) {
    // A level from before this window existed (the car reports one at start-up) must not show a pill at once.
    dirty_ = false;
    surfaceReady_ = false;
    ctxParent_ = ctx;
    parent_ = parentWindow;   // used only to read the panel size; the OSD owns its own window
    pthread_t t;
    running_ = true;
    if (pthread_create(&t, NULL, &VolumeOsd::threadThunk, this) != 0) {
        running_ = false;
        printf("[VolOsd] could not start the drawing thread\n");
        return false;
    }
    pthread_detach(t);
    for (int i = 0; i < 20 && !ready_ && running_; ++i) usleep(10 * 1000);
    printf("[VolOsd] %s\n", ready_ ? "window up (panel-sized, transparent background, pill placed relative to the panel)"
                                   : "no window: the OSD will not appear");
    return ready_;
}

void VolumeOsd::stop() {
    if (!running_) return;
    running_ = false;
    // Wait for the drawing thread to destroy the window (surface 1281 then has no window bound).
    for (int i = 0; i < 50 && ready_; ++i) usleep(10 * 1000);
    usleep(20 * 1000);
}

void VolumeOsd::setPct(int pct) {
    pct_ = volumeClampPct(pct);
    dirty_ = true;
}

void VolumeOsd::setShown(bool shown) { aaShown_ = shown; }

void VolumeOsd::loop() {
    // Panel size first: the pill is laid out relative to whatever the projection covers. 1280x480 is only the
    // fallback for "cannot ask".
    int pw = kFallbackW, ph = kFallbackH;
    if (parent_) {
        int sz[2] = { 0, 0 };
        if (screen_get_window_property_iv((screen_window_t)parent_, SCREEN_PROPERTY_SOURCE_SIZE, sz) == 0 &&
            sz[0] > 0 && sz[1] > 0) {
            pw = sz[0];
            ph = sz[1];
        }
    }
    const Layout L = layoutFor(pw, ph);
    printf("[VolOsd] panel %dx%d -> pill %dx%d at %d,%d, icon %d px, digits %d px\n",
           L.pw, L.ph, L.w, L.h, L.x, L.y, L.iconPx, L.gs->px);

    // A top-level window named "1281", drawn at FULL panel size with a transparent background.
    //   * LayerManagerService (ScreenRenderer::eventLoop -> FUN_00128d50, decompiled 2026-11-05) reads a window's id
    //     string on its first post, finds the surface with that number or CREATES it, and binds the window to it.
    //     Car-verified the same day with tools/osd1281probe on the Supra: surface 1281 appeared with the window bound,
    //     and FIRST in layer 0x80000000's render order it showed over the Android Auto picture with alpha.
    //   * Named "1280" (v1.9.08-v1.9.17) the pill took the projection's own surface: black screen with only the pill,
    //     reverse camera left under Android Auto, and the decoder close/rebuild that worked around it gave a grey
    //     picture. A non-numeric name ("nbtvolo") was never drawn: it names no surface.
    //   * Panel-sized because a 384x58 buffer in a full-panel surface showed a grey wash outside the painted part.
    // Same window recipe as the probe: no z-order and no VISIBLE property; LayerManager alone decides what is drawn.
    screen_context_t ctx = 0;
    screen_window_t win = 0;
    if (screen_create_context(&ctx, SCREEN_APPLICATION_CONTEXT) != 0) {
        printf("[VolOsd] screen_create_context failed errno=%d\n", errno);
        running_ = false;
        return;
    }
    if (screen_create_window_type(&win, ctx, SCREEN_APPLICATION_WINDOW) != 0) {
        printf("[VolOsd] screen_create_window_type failed errno=%d\n", errno);
        running_ = false;
        return;
    }
    // The id string is a LayerManager surface name, not a resolution: it does not change with the panel.
    char idstr[12];
    snprintf(idstr, sizeof(idstr), "%d", kSurfaceId);
    screen_set_window_property_cv(win, SCREEN_PROPERTY_ID_STRING, (int)strlen(idstr), idstr);

    int usage = SCREEN_USAGE_NATIVE | SCREEN_USAGE_WRITE;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_USAGE, &usage);
    int format = SCREEN_FORMAT_RGBA8888;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_FORMAT, &format);
    int size[2] = { L.pw, L.ph };
    screen_set_window_property_iv(win, SCREEN_PROPERTY_SOURCE_SIZE, size);
    int pos[2] = { 0, 0 };
    screen_set_window_property_iv(win, SCREEN_PROPERTY_POSITION, pos);
    // No SCREEN_PROPERTY_TRANSPARENCY here on purpose: the tree's screen_video.h numbers properties for this unit's
    // Screen and carries no transparency constant, so a guessed value would set a different property. Setting
    // SCREEN_PROPERTY_FORMAT to an alpha format already implies SCREEN_TRANSPARENCY_SOURCE_OVER.

    if (screen_create_window_buffers(win, 2) != 0) {
        printf("[VolOsd] screen_create_window_buffers failed errno=%d\n", errno);
        screen_destroy_window(win);
        running_ = false;
        return;
    }

    void* bufs[2] = { 0, 0 };
    screen_get_window_property_pv(win, SCREEN_PROPERTY_RENDER_BUFFERS, bufs);
    if (!bufs[0] || !bufs[1]) {
        printf("[VolOsd] no render buffers\n");
        screen_destroy_window(win);
        running_ = false;
        return;
    }

    // Only the pixel pointer is queried: screen_get_buffer_property_iv is declared in the tree's header but is
    // not exported by the unit's libscreen, so the stride is taken as the window width, as test_display.cpp does.
    void* ptrs[2] = { 0, 0 };
    for (int i = 0; i < 2; ++i) {
        screen_get_buffer_property_pv((screen_buffer_t)bufs[i], SCREEN_PROPERTY_POINTER, &ptrs[i]);
    }
    if (!ptrs[0] || !ptrs[1]) {
        printf("[VolOsd] no buffer pointers\n");
        screen_destroy_window(win);
        running_ = false;
        return;
    }

    ctx_ = ctx;
    win_ = win;
    ready_ = true;

    int cur = -1;
    int frame = 0;
    uint64_t shown_at_ms = 0;
    bool shown = false;
    int ms = 0;
    bool firstPill = true;
    bool aaOn = true;           // what the drawing currently follows

    // An empty (transparent) frame at once: the first post is what makes LayerManager create surface 1281.
    {
        Canvas cv;
        cv.px = (uint32_t*)ptrs[frame % 2];
        cv.w = L.pw;
        cv.h = L.ph;
        cv.stride = L.pw;
        paint(cv, L, -1);
        int rect[4] = { 0, 0, L.pw, L.ph };
        int rc = screen_post_window(win, (screen_buffer_t)bufs[frame % 2], 1, rect, 0);
        printf("[VolOsd] window \"%s\" posted an empty frame (rc=%d errno=%d): LayerManager makes surface %d from it\n",
               idstr, rc, rc ? errno : 0, kSurfaceId);
        ++frame;
    }
    for (int tries = 0; running_ && !surfaceReady_ && tries < 10; ++tries) {
        usleep(300 * 1000);
        const long long native = lmSetupPillSurface(kSurfaceId, L.pw, L.ph);
        if (native > 0 && native != 4294967295LL) {
            surfaceReady_ = true;
            printf("[VolOsd] LayerManager surface %d ready (native surface %lld, %dx%d, visibility 1): the pill goes on "
                   "top of Android Auto while the picture is shown\n", kSurfaceId, native, L.pw, L.ph);
        }
    }
    if (running_ && !surfaceReady_)
        printf("[VolOsd] LayerManager made no surface %d for the pill window within 3 s: no volume pill this time\n",
               kSurfaceId);

    while (running_) {
        const bool aa = aaShown_;
        if (aa != aaOn) {
            aaOn = aa;
            if (!aa) {
                // Android Auto stepped aside: its layer's hide order already took the surface away. Clear a pill that
                // was up, so it is not still there when the picture comes back.
                if (shown) {
                    Canvas cv;
                    cv.px = (uint32_t*)ptrs[frame % 2];
                    cv.w = L.pw;
                    cv.h = L.ph;
                    cv.stride = L.pw;
                    paint(cv, L, -1);
                    int rect[4] = { 0, 0, L.pw, L.ph };
                    screen_post_window(win, (screen_buffer_t)bufs[frame % 2], 1, rect, 0);
                    ++frame;
                }
                dirty_ = false;
                shown = false;
                cur = -1;
            }
            printf("[VolOsd] %s with Android Auto\n", aa ? "back" : "hidden");
        }
        if (!aaOn) {
            dirty_ = false;     // a volume change while iDrive has the screen: iDrive shows its own bar
            usleep(100 * 1000);
            ms += 100;
            continue;
        }
        const bool want = dirty_;
        const int pct = pct_;
        if (want && pct != cur) {
            Canvas cv;
            cv.px = (uint32_t*)ptrs[frame % 2];
            cv.w = L.pw;
            cv.h = L.ph;
            cv.stride = L.pw;
            paint(cv, L, pct);
            int rect[4] = { 0, 0, L.pw, L.ph };
            int rc = screen_post_window(win, (screen_buffer_t)bufs[frame % 2], 1, rect, 0);
            if (firstPill) {
                firstPill = false;
                printf("[VolOsd] first pill: %d%% posted rc=%d errno=%d (surface %d %s)\n", pct, rc, rc ? errno : 0,
                       kSurfaceId, surfaceReady_ ? "ready" : "NOT ready: nothing on screen");
            }
            shown = true;
            cur = pct;
            shown_at_ms = (uint64_t)ms;
            dirty_ = false;
            ++frame;
        }
        if (shown && ((uint64_t)ms - shown_at_ms) >= kHideMs) {
            // Clear the pill with a fully transparent frame; the surface stays in the layer's order (no LayerManager
            // call per volume change).
            Canvas cv;
            cv.px = (uint32_t*)ptrs[frame % 2];
            cv.w = L.pw;
            cv.h = L.ph;
            cv.stride = L.pw;
            paint(cv, L, -1);
            int rect[4] = { 0, 0, L.pw, L.ph };
            screen_post_window(win, (screen_buffer_t)bufs[frame % 2], 1, rect, 0);
            ++frame;
            shown = false;
            cur = -1;
        }
        usleep(100 * 1000);   // 10 Hz is plenty for a bar
        ms += 100;
    }

    surfaceReady_ = false;
    screen_destroy_window(win);
    win_ = 0;
    ctx_ = 0;
    ready_ = false;
}
} // namespace nbt
