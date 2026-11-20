// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// Volume overlay: a QNX Screen window on LayerManager surface 1281 of its own, drawn full-panel with a fully
// transparent background, so only the pill is opaque and Android Auto shows through everywhere else.
//
// The pill itself (layout, colours, bitmaps) is drawn by volume_pill.cpp, relative to the panel size read from the
// projection window at runtime.
#include "volume_osd.hpp"
#include "screen_video.h"
#include "volume.hpp"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

namespace nbt {

namespace {

const int kHideMs = 1400;

const int kNoPill = -1;

// Paints the next of the two buffers with level (pillPack, or kNoPill) and posts it; returns screen_post_window's result.
int postPill(screen_window_t win, void* const* bufs, void* const* ptrs, int& frame, const PillLayout& L, int level) {
    PillCanvas cv;
    cv.px = (uint32_t*)ptrs[frame % 2];
    cv.w = L.pw;
    cv.h = L.ph;
    cv.stride = L.pw;
    if (level == kNoPill) pillClear(cv);
    else pillPaint(cv, L, pillPackedPct(level), pillPackedLabel(level));
    int rect[4] = { 0, 0, L.pw, L.ph };
    const int rc = screen_post_window(win, (screen_buffer_t)bufs[frame % 2], 1, rect, 0);
    ++frame;
    return rc;
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

VolumeOsd::VolumeOsd() : ctx_(0), win_(0), running_(false), ready_(false),
                         level_(pillPack(100, kPillLabelEntertainment)), dirty_(false), aaShown_(true),
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

void VolumeOsd::setLevel(int pct, PillLabel label) {
    level_ = pillPack(volumeClampPct(pct), label);
    dirty_ = true;
}

void VolumeOsd::setShown(bool shown) { aaShown_ = shown; }

void VolumeOsd::loop() {
    // Panel size first: the pill is laid out relative to whatever the projection covers (0 = cannot ask).
    int pw = 0, ph = 0;
    if (parent_) {
        int sz[2] = { 0, 0 };
        if (screen_get_window_property_iv((screen_window_t)parent_, SCREEN_PROPERTY_SOURCE_SIZE, sz) == 0 &&
            sz[0] > 0 && sz[1] > 0) {
            pw = sz[0];
            ph = sz[1];
        }
    }
    const PillLayout L = pillLayoutFor(pw, ph);
    printf("[VolOsd] panel %dx%d -> pill %dx%d at %d,%d (scale %.2f)\n", L.pw, L.ph, L.w, L.h, L.x, L.y, L.s);

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
        const int rc = postPill(win, bufs, ptrs, frame, L, kNoPill);
        printf("[VolOsd] window \"%s\" posted an empty frame (rc=%d errno=%d): LayerManager makes surface %d from it\n",
               idstr, rc, rc ? errno : 0, kSurfaceId);
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
                if (shown) postPill(win, bufs, ptrs, frame, L, kNoPill);
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
        // dirty_ is cleared before the level is read: a change that lands in between sets it again for the next pass.
        const bool want = dirty_;
        if (want) dirty_ = false;
        const int lv = level_;
        const int pct = pillPackedPct(lv);
        if (want && lv != cur) {
            const int rc = postPill(win, bufs, ptrs, frame, L, lv);
            if (firstPill) {
                firstPill = false;
                printf("[VolOsd] first pill: %d%% posted rc=%d errno=%d (surface %d %s)\n", pct, rc, rc ? errno : 0,
                       kSurfaceId, surfaceReady_ ? "ready" : "NOT ready: nothing on screen");
            }
            shown = true;
            cur = lv;
            shown_at_ms = (uint64_t)ms;
        } else if (want && shown) {
            // The same level reported again while the pill is up: nothing to redraw, the pill stays up from now. The
            // flag used to stay set here, and drew the pill again on the pass after it had been hidden.
            shown_at_ms = (uint64_t)ms;
        }
        if (shown && ((uint64_t)ms - shown_at_ms) >= kHideMs) {
            // Clear the pill with a fully transparent frame; the surface stays in the layer's order (no LayerManager
            // call per volume change).
            postPill(win, bufs, ptrs, frame, L, kNoPill);
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
