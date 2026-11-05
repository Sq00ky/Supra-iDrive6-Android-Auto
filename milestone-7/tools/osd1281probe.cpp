// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// osd1281probe -- can the volume pill be its OWN LayerManager surface (1281) on the Android Auto layer?
//
// Why this is worth a car run (LayerManagerService, decompiled 2026-11-05, lm1281_me):
//   * ScreenRenderer::eventLoop (@0x001299e4) runs FUN_00128d50 for a window on a POST event (type 9) and on a
//     PROPERTY event whose property is SCREEN_PROPERTY_ID_STRING (0x14). A CREATE event (type 1) only refs the window.
//   * FUN_00128d50 (@0x00128d50) parses the window's id string (FUN_001251c8 -> number), looks the surface up
//     (scene vtbl+0x20) and, when there is none, CREATES it (scene vtbl+0x0c), then binds the window as that
//     surface's native content (surface+0xb0 = 1, surface+0xa4 = window).
//   * That is why the newest "1280" window that posts takes surface 1280, and why the 2026-11-04 osdprobe surface
//     96 never drew: no window was ever NAMED "96", so nothing was bound to it.
// Never tried on a car: a window named "1281" that posts. This probe does exactly that, puts surface 1281 on the
// projection layer 0x80000000 next to 1280, and takes it off again.
//
// It deliberately does NOT use SCREEN_PROPERTY_VISIBLE: LayerManagerService itself uses 0x33 for VISIBLE, while the
// tree's screen_video.h says 43, so visibility goes through ilm_surfaceSetVisibility only. It never calls
// ilm_displaySetRenderOrder (that is what emptied screen 0 once) and only touches layer 0x80000000's list.
//
// Run with Android Auto ON SCREEN (phone connected, AA picture showing):
//   cp /mnt/umass01200t7/osd1281probe /tmp/osd1281probe
//   /tmp/osd1281probe > /mnt/umass01200t7/osd1281probe.log
// About 25 s. Watch for a MAGENTA pill with a white bar, bottom centre. Phase A 10 s, phase B 10 s (order flipped).
// Optional argument: another id instead of 1281 (e.g. /tmp/osd1281probe 1282).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dlfcn.h>
#include <time.h>

#include "screen_video.h"

typedef unsigned int u32;
typedef int          i32;

static const u32 kProjLayer = 0x80000000u;
static const u32 kAaSurface = 1280u;

// ---- timing / shell helpers ---------------------------------------------------------------------------
static double nowS() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
static double g_t0 = 0;
#define LOG(...) do { printf("[%6.2f] ", nowS() - g_t0); printf(__VA_ARGS__); fflush(stdout); } while (0)

// Run a command, print its output indented, keep a copy in out (may be NULL).
static void runCmd(const char* cmd, char* out, size_t outSz) {
    if (out && outSz) out[0] = 0;
    LOG("$ %s\n", cmd);
    FILE* p = popen(cmd, "r");
    if (!p) { LOG("  (popen failed errno=%d)\n", errno); return; }
    char line[512];
    size_t used = 0;
    while (fgets(line, sizeof(line), p)) {
        printf("    %s", line);
        size_t n = strlen(line);
        if (out && used + n + 1 < outSz) { memcpy(out + used, line, n); used += n; out[used] = 0; }
    }
    pclose(p);
    fflush(stdout);
}

// Surface ids listed after "surface render order:" in a "get layer" dump (decimal before each "(0x...)").
static int parseRenderOrder(const char* dump, u32* ids, int max) {
    const char* k = strstr(dump, "surface render order:");
    if (!k) return -1;
    k += strlen("surface render order:");
    int n = 0;
    while (*k && *k != '\n' && n < max) {
        while (*k == ' ' || *k == ',' || *k == '\t') ++k;
        if (*k < '0' || *k > '9') break;
        ids[n++] = (u32)strtoul(k, (char**)&k, 10);
        while (*k && *k != ',' && *k != '\n') ++k;   // skip "(0x500)"
    }
    return n;
}

static bool surfaceExists(u32 id, char* buf, size_t bufSz) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "/opt/sys/bin/LayerManagerControl get surface %u 2>&1", id);
    runCmd(cmd, buf, bufSz);
    return strstr(buf, "created by pid") != NULL;
}

// ---- ilm (dlopen, exact signatures from libilmClient.gpp.so.1) ---------------------------------------
typedef struct {
    void* lib;
    int (*init)(void);
    int (*destroy)(void);
    int (*commit)(void);
    int (*layerAddSurface)(u32 layer, u32 surface);          // (layer, surface): verified 2026-11-04 (STEP 1 ok)
    int (*layerRemoveSurface)(u32 layer, u32 surface);
    int (*layerSetRenderOrder)(u32 layer, u32* surfaces, u32 count);   // 3 args: a 2-arg call clears the list
    int (*surfaceSetVisibility)(u32 surface, i32 visible);
    int (*surfaceSetDestinationRectangle)(u32 surface, i32 x, i32 y, i32 w, i32 h);
    int (*surfaceSetSourceRectangle)(u32 surface, i32 x, i32 y, i32 w, i32 h);
} Ilm;

static void dm(void** slot, void* lib, const char* name) {
    *slot = dlsym(lib, name);
    if (!*slot) LOG("  dlsym(%s) MISSING\n", name);
}

static bool ilmOpen(Ilm& g) {
    memset(&g, 0, sizeof(g));
    const char* paths[] = { "/opt/sys/lib/libilmClient.gpp.so.1", "/opt/sys/lib/libilmClient.cpp-ne.so.1" };
    for (unsigned i = 0; i < 2 && !g.lib; ++i) {
        g.lib = dlopen(paths[i], RTLD_NOW);
        LOG("dlopen(%s) -> %s\n", paths[i], g.lib ? "OK" : dlerror());
    }
    if (!g.lib) return false;
    dm((void**)&g.init, g.lib, "ilm_init");
    dm((void**)&g.destroy, g.lib, "ilm_destroy");
    dm((void**)&g.commit, g.lib, "ilm_commitChanges");
    dm((void**)&g.layerAddSurface, g.lib, "ilm_layerAddSurface");
    dm((void**)&g.layerRemoveSurface, g.lib, "ilm_layerRemoveSurface");
    dm((void**)&g.layerSetRenderOrder, g.lib, "ilm_layerSetRenderOrder");
    dm((void**)&g.surfaceSetVisibility, g.lib, "ilm_surfaceSetVisibility");
    dm((void**)&g.surfaceSetDestinationRectangle, g.lib, "ilm_surfaceSetDestinationRectangle");
    dm((void**)&g.surfaceSetSourceRectangle, g.lib, "ilm_surfaceSetSourceRectangle");
    return g.init && g.commit && g.layerAddSurface && g.layerRemoveSurface && g.layerSetRenderOrder &&
           g.surfaceSetVisibility && g.surfaceSetDestinationRectangle && g.surfaceSetSourceRectangle;
}

// ---- drawing ---------------------------------------------------------------------------------------
// Pixels are A,R,G,B in a 32-bit word on this panel (0xFFFFFF18 drew yellow: the low byte is blue).
static const uint32_t kClear   = 0x00000000u;
static const uint32_t kMagenta = 0xFFFF00FFu;
static const uint32_t kWhite   = 0xFFFFFFFFu;
static const uint32_t kDark    = 0xFF202020u;

static void paint(uint32_t* px, int pw, int ph, int step) {
    for (int i = 0; i < pw * ph; ++i) px[i] = kClear;
    const int w = pw * 30 / 100, h = ph * 12 / 100;
    const int x0 = (pw - w) / 2, y0 = ph - h - ph * 6 / 100;
    const int r = h / 2;
    for (int y = 0; y < h; ++y) {
        int inset = 0;
        if (y < r) inset = r - y;                      // crude rounded ends
        if (y >= h - r) inset = y - (h - r) + 1;
        if (inset > r) inset = r;
        uint32_t* row = px + (size_t)(y0 + y) * pw;
        for (int x = x0 + inset; x < x0 + w - inset; ++x) row[x] = kMagenta;
    }
    // white progress bar inside, moving with each step so a frozen frame is obvious
    const int bx = x0 + r, bw = w - 2 * r, by = y0 + h / 2 - 3;
    const int fill = bw * ((step * 7) % 100 + 1) / 100;
    for (int y = by; y < by + 6; ++y) {
        uint32_t* row = px + (size_t)y * pw;
        for (int x = bx; x < bx + bw; ++x) row[x] = (x < bx + fill) ? kWhite : kDark;
    }
}

int main(int argc, char** argv) {
    g_t0 = nowS();
    const u32 sid = (argc > 1) ? (u32)strtoul(argv[1], NULL, 10) : 1281u;
    char idstr[16];
    snprintf(idstr, sizeof(idstr), "%u", sid);
    printf("osd1281probe (" __DATE__ " " __TIME__ ") -- window id \"%s\" as its own LayerManager surface\n", idstr);
    if (sid == kAaSurface || sid == 0) { printf("refusing id %u\n", sid); return 2; }

    static char buf[16384];

    // ---- preconditions: Android Auto on screen, panel size -------------------------------------------
    runCmd("/opt/sys/bin/LayerManagerControl get layer 0x80000000 2>&1", buf, sizeof(buf));
    u32 order0[16];
    int n0 = parseRenderOrder(buf, order0, 16);
    bool aaOnLayer = false;
    for (int i = 0; i < n0; ++i) if (order0[i] == kAaSurface) aaOnLayer = true;
    if (!aaOnLayer) {
        LOG("STOP: surface 1280 is not on layer 0x80000000 (render order has %d entries). Run this with the Android "
            "Auto picture on screen.\n", n0);
        return 1;
    }
    if (n0 != 1) LOG("note: layer 0x80000000 already holds %d surfaces; phase B (order flip) will be skipped\n", n0);

    int pw = 1280, ph = 480;
    runCmd("/opt/sys/bin/LayerManagerControl get screen 0x0 2>&1", buf, sizeof(buf));
    {
        const char* r = strstr(buf, "resolution:");
        int x = 0, y = 0;
        if (r && sscanf(r, "resolution: x=%d, y=%d", &x, &y) == 2 && x > 0 && y > 0 && x <= 4096 && y <= 4096) {
            pw = x;
            ph = y;
        }
    }
    LOG("panel %dx%d\n", pw, ph);

    char sbuf[4096];
    surfaceExists(sid, sbuf, sizeof(sbuf));   // before: expected "not found"
    runCmd("/opt/sys/bin/LayerManagerControl get surfaces 2>&1", buf, sizeof(buf));

    // ---- the window: same recipe as the daemon's VolOsd (proven to draw), only the id differs -----------
    screen_context_t ctx = 0;
    screen_window_t win = 0;
    if (screen_create_context(&ctx, SCREEN_APPLICATION_CONTEXT) != 0) { LOG("screen_create_context errno=%d\n", errno); return 1; }
    if (screen_create_window_type(&win, ctx, SCREEN_APPLICATION_WINDOW) != 0) { LOG("create_window errno=%d\n", errno); return 1; }
    screen_set_window_property_cv(win, SCREEN_PROPERTY_ID_STRING, (int)strlen(idstr), idstr);
    int usage = SCREEN_USAGE_NATIVE | SCREEN_USAGE_WRITE;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_USAGE, &usage);
    int format = SCREEN_FORMAT_RGBA8888;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_FORMAT, &format);
    int size[2] = { pw, ph };
    screen_set_window_property_iv(win, SCREEN_PROPERTY_SOURCE_SIZE, size);
    int pos[2] = { 0, 0 };
    screen_set_window_property_iv(win, SCREEN_PROPERTY_POSITION, pos);
    if (screen_create_window_buffers(win, 2) != 0) { LOG("create_window_buffers errno=%d\n", errno); return 1; }
    void* bufs[2] = { 0, 0 };
    screen_get_window_property_pv(win, SCREEN_PROPERTY_RENDER_BUFFERS, bufs);
    void* ptrs[2] = { 0, 0 };
    for (int i = 0; i < 2; ++i) if (bufs[i]) screen_get_buffer_property_pv((screen_buffer_t)bufs[i], SCREEN_PROPERTY_POINTER, &ptrs[i]);
    if (!ptrs[0] || !ptrs[1]) { LOG("no buffer pointers\n"); return 1; }
    LOG("window %p id \"%s\" %dx%d buffers %p %p\n", (void*)win, idstr, pw, ph, ptrs[0], ptrs[1]);

    int step = 0;
    paint((uint32_t*)ptrs[0], pw, ph, step);
    int rect[4] = { 0, 0, pw, ph };
    int prc = screen_post_window(win, (screen_buffer_t)bufs[0], 1, rect, 0);
    LOG("first post rc=%d errno=%d (LayerManager binds/creates the surface on this post)\n", prc, errno);
    usleep(800 * 1000);

    bool have = surfaceExists(sid, sbuf, sizeof(sbuf));
    if (!have) {
        // Fallback: make the surface exist under that id, then re-announce the id string (PROPERTY event 0x14).
        LOG("surface %u was NOT created by the post; trying 'create surface' + re-announcing the id string\n", sid);
        char cmd[160];
        snprintf(cmd, sizeof(cmd), "/opt/sys/bin/LayerManagerControl create surface %u 0 %d %d 7 2>&1", sid, pw, ph);
        runCmd(cmd, buf, sizeof(buf));
        // Screen sends the POST event once per window (daemon log: two type-9 events per session), so a re-post
        // does not re-run FUN_00128d50; a PROPERTY event on the id string does (eventLoop case 2, name 0x14).
        // Toggle through a non-numeric id (parses to -1, ignored) so the property really changes twice.
        screen_set_window_property_cv(win, SCREEN_PROPERTY_ID_STRING, 1, "x");
        usleep(200 * 1000);
        screen_set_window_property_cv(win, SCREEN_PROPERTY_ID_STRING, (int)strlen(idstr), idstr);
        paint((uint32_t*)ptrs[1], pw, ph, ++step);
        prc = screen_post_window(win, (screen_buffer_t)bufs[1], 1, rect, 0);
        LOG("re-post rc=%d\n", prc);
        usleep(800 * 1000);
        have = surfaceExists(sid, sbuf, sizeof(sbuf));
    }
    if (!have) {
        LOG("RESULT: no surface %u exists for our window. LayerManager did not create it. Nothing else to try here.\n", sid);
        screen_destroy_window(win);
        screen_destroy_context(ctx);
        return 0;
    }
    LOG("surface %u exists. native content line above should be non-zero.\n", sid);

    // ---- put it on the projection layer next to 1280 ---------------------------------------------------
    Ilm ilm;
    if (!ilmOpen(ilm)) { LOG("ilm symbols missing\n"); screen_destroy_window(win); return 1; }
    int rc = ilm.init();
    LOG("ilm_init rc=%d\n", rc);
    if (rc != 0) { screen_destroy_window(win); return 1; }

    rc = ilm.layerAddSurface(kProjLayer, sid);                       LOG("layerAddSurface(0x80000000, %u) rc=%d\n", sid, rc);
    rc = ilm.surfaceSetSourceRectangle(sid, 0, 0, pw, ph);           LOG("surfaceSetSourceRectangle rc=%d\n", rc);
    rc = ilm.surfaceSetDestinationRectangle(sid, 0, 0, pw, ph);      LOG("surfaceSetDestinationRectangle rc=%d\n", rc);
    rc = ilm.surfaceSetVisibility(sid, 1);                           LOG("surfaceSetVisibility(1) rc=%d\n", rc);
    rc = ilm.commit();                                               LOG("commit rc=%d\n", rc);
    surfaceExists(sid, sbuf, sizeof(sbuf));
    runCmd("/opt/sys/bin/LayerManagerControl get layer 0x80000000 2>&1", buf, sizeof(buf));
    u32 order1[16];
    int n1 = parseRenderOrder(buf, order1, 16);

    LOG(">>> PHASE A (10 s): layer order as added. Is a MAGENTA pill with a moving white bar visible at the bottom, "
        "over the Android Auto picture?\n");
    for (int i = 0; i < 20; ++i) {
        usleep(500 * 1000);
        int b = (++step) % 2;
        paint((uint32_t*)ptrs[b], pw, ph, step);
        screen_post_window(win, (screen_buffer_t)bufs[b], 1, rect, 0);
    }

    if (n0 == 1 && n1 == 2 && order1[0] == kAaSurface && order1[1] == sid) {
        u32 flip[2] = { sid, kAaSurface };
        rc = ilm.layerSetRenderOrder(kProjLayer, flip, 2);           LOG("PHASE B: layerSetRenderOrder({%u, 1280}, 2) rc=%d\n", sid, rc);
        rc = ilm.commit();                                           LOG("commit rc=%d\n", rc);
        runCmd("/opt/sys/bin/LayerManagerControl get layer 0x80000000 2>&1", buf, sizeof(buf));
        LOG(">>> PHASE B (10 s): order flipped. Did the pill appear or disappear compared with phase A?\n");
        for (int i = 0; i < 20; ++i) {
            usleep(500 * 1000);
            int b = (++step) % 2;
            paint((uint32_t*)ptrs[b], pw, ph, step);
            screen_post_window(win, (screen_buffer_t)bufs[b], 1, rect, 0);
        }
    } else {
        LOG("PHASE B skipped (layer order after the add: %d entries)\n", n1);
    }

    // ---- put everything back -----------------------------------------------------------------------------
    rc = ilm.surfaceSetVisibility(sid, 0);                           LOG("restore: surfaceSetVisibility(0) rc=%d\n", rc);
    rc = ilm.layerRemoveSurface(kProjLayer, sid);                    LOG("restore: layerRemoveSurface rc=%d\n", rc);
    rc = ilm.commit();                                               LOG("restore: commit rc=%d\n", rc);
    runCmd("/opt/sys/bin/LayerManagerControl get layer 0x80000000 2>&1", buf, sizeof(buf));
    u32 order2[16];
    int n2 = parseRenderOrder(buf, order2, 16);
    // Only repair an EMPTY list: if the daemon stepped Android Auto aside meanwhile, the layer legitimately holds
    // something else (its hide is 'render order 0x100') and must be left alone.
    if (n2 == 0) {
        u32 only[1] = { kAaSurface };
        rc = ilm.layerSetRenderOrder(kProjLayer, only, 1);
        ilm.commit();
        LOG("restore: layer 0x80000000 was left empty, 1280 put back alone rc=%d\n", rc);
        runCmd("/opt/sys/bin/LayerManagerControl get layer 0x80000000 2>&1", buf, sizeof(buf));
    }
    if (ilm.destroy) ilm.destroy();
    screen_destroy_window(win);
    screen_destroy_context(ctx);
    usleep(300 * 1000);
    surfaceExists(sid, sbuf, sizeof(sbuf));
    LOG("done\n");
    return 0;
}
