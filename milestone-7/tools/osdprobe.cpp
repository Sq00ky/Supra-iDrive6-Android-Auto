// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// osdprobe -- does this unit let us own an LM surface for our own Screen window?
//
// Why this exists: on this car only windows that own a LayerManager surface reach the panel.
//   * a window with an unknown id string is never composited (read back z=0 pos=0,0),
//   * a window named "1280" *becomes* the projection surface and covers Android Auto,
//   * a child window of the projection posts rc=0 with the right geometry and is still never drawn,
//     because nothing on this unit runs a window manager (pidin: no winmgr, no layout process).
// So the pill needs its own surface on its own layer, created through the same ilm API Connectivity
// uses for surface 1280 (/opt/sys/lib/libilmClient.gpp.so.1, the only two binaries on the car that
// touch ilm_surfaceCreate are LayerManagerControl and that library).
//
// This probe does not assume the API's pixel-format enum: it enumerates it once and prints the answer,
// so a wrong constant shows up as a logged failure instead of a silently black window.
//
// Run:  cp /mnt/umass01200t7/osdprobe /tmp/osdprobe; /tmp/osdprobe
// It draws for 25 s and then removes everything it created.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dlfcn.h>

#include "screen_video.h"

typedef unsigned int u32;
typedef int          i32;

// ---- pill geometry, same as the daemon's OSD ------------------------------------------------
static const int kScreenW = 1280;
static const int kScreenH = 480;
static const int kOsdW    = 384;
static const int kOsdH    = 58;

static void fill(uint32_t* px, int stride, int x, int y, int w, int h, uint32_t c) {
    for (int j = y; j < y + h; ++j) {
        if (j < 0 || j >= kOsdH) continue;
        uint32_t* row = px + (size_t)j * stride;
        for (int i = x; i < x + w; ++i) {
            if (i < 0 || i >= kOsdW) continue;
            row[i] = c;
        }
    }
}

// Colours are packed B,G,R,A on this panel: a word meant as "white at 6% alpha" (0xFFFFFF18) drew as
// YELLOW in the daemon, so the 0x18 landed in the blue channel. Same packing here.
static const uint32_t kBodyPx = 0xD10D0A08u;  // #0A0D08 navy, alpha 0xD1
static const uint32_t kTrack  = 0x24242424u;
static const uint32_t kFill   = 0xFFB4E67Fu;

static void paint(uint32_t* px, int stride, int pct) {
    for (int i = 0; i < kOsdW * kOsdH; ++i) px[i] = 0x00000000;
    // rounded-ish body: full-width bars with the corners cut, enough to judge colour and position
    for (int y = 0; y < kOsdH; ++y) {
        int inset = 0;
        if (y < 8)  inset = 8 - y;
        if (y >= kOsdH - 8) inset = y - (kOsdH - 8) + 1;
        fill(px, stride, inset, y, kOsdW - 2 * inset, 1, kBodyPx);
    }
    fill(px, stride, 30, 26, 60, 6, kTrack);           // track
    fill(px, stride, 30, 26, 60 * pct / 100, 6, kFill); // fill
    fill(px, stride, 300, 20, 24, 18, 0xFFFFFFFFu);     // digits block
}

// ---- ilm ------------------------------------------------------------------------------------
typedef void* t_ilm_nativehandle;
typedef u32   t_ilm_surface;
typedef u32   t_ilm_layer;
typedef i32   t_ilm_bool;
typedef u32   t_ilm_uint;
typedef i32   t_ilm_int;

typedef struct {
    void* lib;
    int  (*init)(void);
    int  (*destroy)(void);
    int  (*commit)(void);
    int  (*surfaceCreate)(t_ilm_nativehandle, t_ilm_int, t_ilm_int, u32, t_ilm_surface*);
    int  (*surfaceSetDestinationRectangle)(t_ilm_surface, t_ilm_int, t_ilm_int, t_ilm_int, t_ilm_int);
    int  (*surfaceSetSourceRectangle)(t_ilm_surface, t_ilm_int, t_ilm_int, t_ilm_int, t_ilm_int);
    int  (*surfaceSetVisibility)(t_ilm_surface, t_ilm_bool);
    int  (*surfaceSetOpacity)(t_ilm_surface, float);
    int  (*layerCreate)(t_ilm_layer*, t_ilm_uint, t_ilm_uint);
    int  (*layerAddSurface)(t_ilm_layer, t_ilm_surface);
    int  (*layerRemoveSurface)(t_ilm_layer, t_ilm_surface);
    int  (*layerSetDestinationRectangle)(t_ilm_layer, t_ilm_int, t_ilm_int, t_ilm_int, t_ilm_int);
    // Two arguments, zero-terminated array: ilm_layerSetRenderOrder(layer, SURFACES), no count.
    // Verified in LayerManagerControl's "set layer" handler (FUN_0011dba0 @ 0x0011dba0): it calls
    // ilm_layerSetRenderOrder(uVar10, iVar11) and ilm_displaySetRenderOrder(local_30, iVar11) with a
    // NULL in the slot when the list is empty. Passing layer ids here is why the LM showed our surface
    // on no layer while every call returned 0.
    int  (*layerSetRenderOrder)(t_ilm_layer, t_ilm_surface*, t_ilm_uint);   // (layer, surfaces, count)
    int  (*layerSetVisibility)(t_ilm_layer, t_ilm_bool);
    int  (*displaySetRenderOrder)(t_ilm_uint, t_ilm_layer*, t_ilm_uint);   // (display, layers, count)
    int  (*surfaceSetZOrder)(t_ilm_surface, t_ilm_uint);
    int  (*getScreenIDs)(t_ilm_uint*, t_ilm_uint*);   // (pNumberOfIDs, pIDs) -- not (ids, max, &n)
} Ilm;

// dlsym into a typed function pointer without the type-punned store warning.
static void dm(void** slot, void* lib, const char* name) {
    *slot = dlsym(lib, name);
}

static bool ilmOpen(Ilm& g) {
    const char* paths[] = { "/opt/sys/lib/libilmClient.gpp.so.1",
                            "/opt/sys/lib/libilmClient.cpp-ne.so.1" };
    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        g.lib = dlopen(paths[i], RTLD_NOW);
        printf("[probe] dlopen(%s) -> %s\n", paths[i], g.lib ? "OK" : dlerror());
        if (g.lib) break;
    }
    if (!g.lib) return false;
    dm(&(void*&)g.init, g.lib, "ilm_init");
    dm(&(void*&)g.destroy, g.lib, "ilm_destroy");
    dm(&(void*&)g.commit, g.lib, "ilm_commitChanges");
    dm(&(void*&)g.surfaceCreate, g.lib, "ilm_surfaceCreate");
    dm(&(void*&)g.surfaceSetDestinationRectangle, g.lib, "ilm_surfaceSetDestinationRectangle");
    dm(&(void*&)g.surfaceSetSourceRectangle, g.lib, "ilm_surfaceSetSourceRectangle");
    dm(&(void*&)g.surfaceSetVisibility, g.lib, "ilm_surfaceSetVisibility");
    dm(&(void*&)g.surfaceSetOpacity, g.lib, "ilm_surfaceSetOpacity");
    dm(&(void*&)g.layerCreate, g.lib, "ilm_layerCreateWithDimension");
    dm(&(void*&)g.layerAddSurface, g.lib, "ilm_layerAddSurface");
    dm(&(void*&)g.layerRemoveSurface, g.lib, "ilm_layerRemoveSurface");
    dm(&(void*&)g.layerSetDestinationRectangle, g.lib, "ilm_layerSetDestinationRectangle");
    dm(&(void*&)g.layerSetRenderOrder, g.lib, "ilm_layerSetRenderOrder");
    dm(&(void*&)g.layerSetVisibility, g.lib, "ilm_layerSetVisibility");
    dm(&(void*&)g.displaySetRenderOrder, g.lib, "ilm_displaySetRenderOrder");
    dm(&(void*&)g.surfaceSetZOrder, g.lib, "ilm_surfaceSetZOrder");
    dm(&(void*&)g.getScreenIDs, g.lib, "ilm_getScreenIDs");
    bool ok = g.init && g.commit && g.surfaceCreate && g.layerCreate && g.layerAddSurface &&
              g.layerSetRenderOrder && g.layerSetVisibility && g.surfaceSetVisibility;
    printf("[probe] symbols %s\n", ok ? "ok" : "MISSING (see above)");
    return ok;
}

int main(void) {
    printf("osdprobe -- own LM surface for the volume pill\n");
    printf("[probe] pid %d\n", (int)getpid());

    // ---- Screen window ----------------------------------------------------------------------
    screen_context_t ctx = 0;
    if (screen_create_context(&ctx, SCREEN_APPLICATION_CONTEXT) != 0) {
        printf("[probe] screen_create_context failed errno=%d\n", errno);
        return 1;
    }
    screen_window_t win = 0;
    if (screen_create_window_type(&win, ctx, SCREEN_APPLICATION_WINDOW) != 0) {
        printf("[probe] create_window failed errno=%d\n", errno);
        return 1;
    }
    int usage = SCREEN_USAGE_NATIVE | SCREEN_USAGE_WRITE;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_USAGE, &usage);
    int format = SCREEN_FORMAT_RGBA8888;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_FORMAT, &format);
    int size[2] = { kOsdW, kOsdH };
    screen_set_window_property_iv(win, SCREEN_PROPERTY_SOURCE_SIZE, size);
    int pos[2] = { (kScreenW - kOsdW) / 2, kScreenH - kOsdH - 26 };
    screen_set_window_property_iv(win, SCREEN_PROPERTY_POSITION, pos);
    int z = 2010;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_ZORDER, &z);
    if (screen_create_window_buffers(win, 2) != 0) {
        printf("[probe] create_window_buffers failed errno=%d\n", errno);
        return 1;
    }
    void* bufs[2] = { 0, 0 };
    screen_get_window_property_pv(win, SCREEN_PROPERTY_RENDER_BUFFERS, bufs);
    if (!bufs[0] || !bufs[1]) {
        printf("[probe] no render buffers\n");
        return 1;
    }
    int stride = kOsdW;
    void* base = 0;
    for (int i = 0; i < 2; ++i) {
        void* p = 0;
        screen_get_buffer_property_pv((screen_buffer_t)bufs[i], SCREEN_PROPERTY_POINTER, &p);
        if (i == 0) base = p;
        if (p) paint((uint32_t*)p, stride, 40 + i * 10);
    }
    int rect[4] = { 0, 0, kOsdW, kOsdH };
    int prc = screen_post_window(win, (screen_buffer_t)bufs[0], 1, rect, 0);
    printf("[probe] window handle %p buffer %p stride %d post rc=%d errno=%d\n",
           (void*)win, base, stride, prc, errno);

    // ---- own surface on its own layer --------------------------------------------------------
    Ilm ilm;
    memset(&ilm, 0, sizeof(ilm));
    if (!ilmOpen(ilm)) {
        printf("[probe] cannot talk to LayerManager; nothing else to try\n");
        return 1;
    }
    int rc = ilm.init();
    printf("[probe] ilm_init rc=%d\n", rc);
    if (rc != 0) {
        printf("[probe] ilm_init failed: the LM service did not accept us\n");
        return 1;
    }

    // The pixel-format enum is not in any header on this unit. Enumerate it once: the surface that
    // Android Auto lives in reports format 7, and Screen's RGBA8888 is 8, so try those first and then
    // the small range around them, logging every answer instead of assuming one.
    // ilm_surfaceCreate only does anything when the outgoing id is ILM_ID_INVALID (0xFFFFFFFF): the library
    // checks that first and returns without a word otherwise, which is why a run that passed 0 printed rc=0
    // with surface=0 for every pixel format. Seed the id and read it back instead of trusting rc.
    const u32 pfOrder[] = { 7, 8, 6, 9, 5, 4, 3, 2, 1, 0 };
    const u32 kIlmIdInvalid = 0xFFFFFFFFu;
    t_ilm_surface surf = kIlmIdInvalid;
    u32 usedPf = 0;
    for (unsigned i = 0; i < sizeof(pfOrder) / sizeof(pfOrder[0]); ++i) {
        errno = 0;
        surf = kIlmIdInvalid;
        rc = ilm.surfaceCreate((t_ilm_nativehandle)win, kOsdW, kOsdH, pfOrder[i], &surf);
        printf("[probe] ilm_surfaceCreate(pf=%u, native=%p) rc=%d errno=%d surface=%u\n",
               pfOrder[i], (void*)win, rc, errno, surf);
        if (surf != kIlmIdInvalid) { usedPf = pfOrder[i]; break; }
    }
    if (surf == kIlmIdInvalid) {
        printf("[probe] no surface: the LM service refuses a Screen window handle as native content\n");
        ilm.destroy();
        return 1;
    }
    printf("[probe] surface %u created with pixel format %u\n", surf, usedPf);

    t_ilm_layer layer = kIlmIdInvalid;   // same convention: the create is skipped unless this is invalid
    rc = ilm.layerCreate(&layer, kScreenW, kScreenH);
    printf("[probe] ilm_layerCreateWithDimension rc=%d layer=%u\n", rc, layer);
    if (layer == kIlmIdInvalid) {
        printf("[probe] no layer (out of hardware layers?)\n");
        ilm.destroy();
        return 1;
    }

    rc = ilm.layerAddSurface(layer, surf);
    printf("[probe] ilm_layerAddSurface(%u, %u) rc=%d\n", layer, surf, rc);

    rc = ilm.layerSetDestinationRectangle(layer, 0, 0, kScreenW, kScreenH);
    printf("[probe] layerSetDestinationRectangle rc=%d\n", rc);
    rc = ilm.surfaceSetDestinationRectangle(surf, pos[0], pos[1], kOsdW, kOsdH);
    printf("[probe] surfaceSetDestinationRectangle(%d,%d %dx%d) rc=%d\n", pos[0], pos[1], kOsdW, kOsdH, rc);
    rc = ilm.surfaceSetSourceRectangle(surf, 0, 0, kOsdW, kOsdH);
    printf("[probe] surfaceSetSourceRectangle rc=%d\n", rc);
    rc = ilm.surfaceSetVisibility(surf, 1);
    printf("[probe] surfaceSetVisibility rc=%d\n", rc);
    rc = ilm.layerSetVisibility(layer, 1);
    printf("[probe] layerSetVisibility rc=%d\n", rc);
    rc = ilm.commit();
    printf("[probe] ilm_commitChanges rc=%d\n", rc);

    // Everything below is one question: which call actually puts our surface on a layer and our layer on
    // the screen? The last run proved display id 0 is accepted (commit rc=0 with it, rc=2 with the id that
    // ilm_getScreenIDs returns) and still showed "- on layer:" empty, so the membership calls themselves are
    // suspect. ilm_layerAddSurface's body sends its second argument first, so try both argument orders and
    // dump the LM's view after each one. "get screen 0x0" is the only query that behaves (it reported
    // surface 1280's layer correctly), so it is the one that will show whether layer 78 ever joins the screen.
    // The projection's own layer, 0x80000000, is in the screen's render order and carries surface 1280 while
    // Android Auto runs (first LM dump from the car, with AA live: "1280 ... visibility 1 ... on layer
    // 2147483648(0x80000000)"). A layer we create ourselves never enters that order, and forcing it in by
    // replacing the order wipes all 73 layers, so don't: join the projection's layer and raise our surface
    // above 1280 with a surface z-order.
    const t_ilm_layer kProj = 0x80000000u;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "/opt/sys/bin/LayerManagerControl get surface %u", surf);

    printf("\n>>> STEP 1: layerAddSurface(projection=%u, ourSurface=%u)\n", kProj, surf);
    rc = ilm.layerAddSurface(kProj, surf);
    printf("[probe]   rc=%d\n", rc);
    rc = ilm.commit();
    printf("[probe]   commit rc=%d\n", rc);
    fflush(stdout); system(cmd);

    // The order layerAddSurface just produced ({1280, ours}) is a valid order -- and every previous run
    // drew only AFTER one of my layerSetRenderOrder calls had cleared it to empty. Draw first, here.
    printf(">>> PHASE 1: drawing for 10 s with the order layerAddSurface just made\n");
    for (int i2 = 0; i2 < 10; ++i2) {
        sleep(1);
        void* pp = 0;
        screen_get_buffer_property_pv((screen_buffer_t)bufs[i2 % 2], SCREEN_PROPERTY_POINTER, &pp);
        if (pp) paint((uint32_t*)pp, stride, 40 + (i2 * 7) % 60);
        int r2[4] = { 0, 0, kOsdW, kOsdH };
        screen_post_window(win, (screen_buffer_t)bufs[i2 % 2], 1, r2, 0);
        ilm.commit();
    }
    printf(">>> layer order after phase 1:\n");
    fflush(stdout); system("/opt/sys/bin/LayerManagerControl get layer 0x80000000");

    // Force the reverse order, with the explicit count this time. This is the call that was passing a
    // garbage count and clearing the layer's whole list.
    t_ilm_surface ordB[2];
    ordB[0] = surf; ordB[1] = 1280u;
    printf(">>> PHASE 2: explicit {ours, 1280}, count 2, for 10 s\n");
    rc = ilm.layerSetRenderOrder(kProj, ordB, 2);
    printf("[probe]   layerSetRenderOrder rc=%d\n", rc);
    rc = ilm.commit();
    printf("[probe]   commit rc=%d\n", rc);
    for (int i2 = 0; i2 < 10; ++i2) {
        sleep(1);
        void* pp = 0;
        screen_get_buffer_property_pv((screen_buffer_t)bufs[i2 % 2], SCREEN_PROPERTY_POINTER, &pp);
        if (pp) paint((uint32_t*)pp, stride, 40 + (i2 * 7) % 60);
        int r2[4] = { 0, 0, kOsdW, kOsdH };
        screen_post_window(win, (screen_buffer_t)bufs[i2 % 2], 1, r2, 0);
        ilm.commit();
    }
    printf(">>> layer order after phase 2:\n");
    fflush(stdout); system("/opt/sys/bin/LayerManagerControl get layer 0x80000000");

    printf("\n>>> STEP 2: z-order above 1280, visible, at 448,396 384x58\n");
    if (ilm.surfaceSetZOrder) {
        rc = ilm.surfaceSetZOrder(surf, 5000);
        printf("[probe]   surfaceSetZOrder(5000) rc=%d\n", rc);
    }
    rc = ilm.surfaceSetDestinationRectangle(surf, pos[0], pos[1], kOsdW, kOsdH);
    printf("[probe]   surfaceSetDestinationRectangle rc=%d\n", rc);
    rc = ilm.surfaceSetVisibility(surf, 1);
    printf("[probe]   surfaceSetVisibility rc=%d\n", rc);
    rc = ilm.commit();
    printf("[probe]   commit rc=%d\n", rc);

    printf("\n>>> drawing for 18 s -- watch the panel\n");
    for (int i2 = 0; i2 < 18; ++i2) {
        sleep(1);
        void* pp = 0;
        screen_get_buffer_property_pv((screen_buffer_t)bufs[i2 % 2], SCREEN_PROPERTY_POINTER, &pp);
        if (pp) paint((uint32_t*)pp, stride, 40 + (i2 * 7) % 60);
        int r2[4] = { 0, 0, kOsdW, kOsdH };
        screen_post_window(win, (screen_buffer_t)bufs[i2 % 2], 1, r2, 0);
        ilm.commit();
    }

    printf("\n==== LM view of our surface afterwards ====\n");
    fflush(stdout); system(cmd);
    printf("\n==== and the projection layer (is 1280 still on it?) ====\n");
    fflush(stdout); system("/opt/sys/bin/LayerManagerControl get layer 0x80000000");

    printf("[probe] restoring the layer order to {1280, ours}\n");
    t_ilm_surface ordA[2];
    ordA[0] = 1280u; ordA[1] = surf;
    rc = ilm.layerSetRenderOrder(kProj, ordA, 2);
    printf("[probe]   layerSetRenderOrder rc=%d\n", rc);
    rc = ilm.commit();
    printf("[probe]   commit rc=%d\n", rc);

    printf("\n[probe] taking our surface back off the projection layer\n");
    if (ilm.layerRemoveSurface) {
        rc = ilm.layerRemoveSurface(kProj, surf);
        printf("[probe]   layerRemoveSurface rc=%d\n", rc);
        rc = ilm.commit();
        printf("[probe]   commit rc=%d\n", rc);
    }

    printf("[probe] cleaning up\n");
    ilm.layerSetVisibility(layer, 0);
    ilm.commit();
    ilm.destroy();
    screen_destroy_window(win);
    screen_destroy_context(ctx);
    printf("[probe] done\n");
    return 0;
}
