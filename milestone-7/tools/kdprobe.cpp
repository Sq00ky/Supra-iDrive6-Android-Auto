// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// kdprobe -- draw the volume pill the way this car's own UI does: a KD window with an EGL/GLES surface.
//
// Why not LayerManager: surfaces minted through ilm have no backing image on this unit (the service reports
// frame=0 draw=0 update=0 and "surface N has no native content" while Android Auto draws fine), because the
// compositor expects a shared image/vram created per client pid. BrowserRenderer-NBTEVOEXT (the car's own
// browser UI, 140 functions, zero ilm calls) proves the supported path instead:
//
//   eglGetDisplay(0) -> eglInitialize -> eglChooseConfig
//   kd = kdCreateWindow(dpy, cfg, 0)          // KD builds its own Screen context + window
//   kdSetWindowPropertyiv(kd, 66 /*SIZE*/, {w,h})
//   kdSetWindowPropertycv(kd, 0x3ea, "<id>")  // vendor id-string property
//   kdRealizeWindow(kd, &native)              // native == the Screen window (see libKD kdRealizeWindow)
//   eglCreateWindowSurface(dpy, cfg, native, 0) -> eglCreateContext -> eglMakeCurrent -> eglBindAPI
//   glClear... -> eglSwapBuffers
//
// Run: cp /mnt/umass01200t7/kdprobe /tmp/kdprobe; /tmp/kdprobe
// Draws a pill for 20 s then destroys its window. Changes nothing else on the display.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <KD/kd.h>

#include "screen_video.h"

static const int kScreenW = 1280;
static const int kScreenH = 480;
static const int kOsdW    = 384;
static const int kOsdH    = 58;
static const int kPosX    = (1280 - 384) / 2;   // 448
static const int kPosY    = 480 - 58 - 26;      // 396

// The vendor property BrowserRenderer uses for its id string (libKD_X reads it the same way).
static const int kKdPropIdString = 0x3ea;

static const char* eglErr(void) {
    switch (eglGetError()) {
        case EGL_SUCCESS: return "SUCCESS";
        case EGL_NOT_INITIALIZED: return "NOT_INITIALIZED";
        case EGL_BAD_ACCESS: return "BAD_ACCESS";
        case EGL_BAD_ALLOC: return "BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE: return "BAD_ATTRIBUTE";
        case EGL_BAD_CONFIG: return "BAD_CONFIG";
        case EGL_BAD_CONTEXT: return "BAD_CONTEXT";
        case EGL_BAD_CURRENT_SURFACE: return "BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY: return "BAD_DISPLAY";
        case EGL_BAD_MATCH: return "BAD_MATCH";
        case EGL_BAD_NATIVE_PIXMAP: return "BAD_NATIVE_PIXMAP";
        case EGL_BAD_NATIVE_WINDOW: return "BAD_NATIVE_WINDOW";
        case EGL_BAD_PARAMETER: return "BAD_PARAMETER";
        case EGL_BAD_SURFACE: return "BAD_SURFACE";
        default: return "other";
    }
}

// Solid rounded-ish pill body plus a progress bar, drawn with scissor rectangles: no shaders, no fonts.
static void drawPill(int pct) {
    glViewport(0, 0, kOsdW, kOsdH);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);      // transparent corners
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_SCISSOR_TEST);

    // body: navy, #0A0D08
    glScissor(0, 0, kOsdW, kOsdH);
    glClearColor(0.039f, 0.051f, 0.031f, 0.82f);
    glClear(GL_COLOR_BUFFER_BIT);

    // track: white at 14%
    const int barX = 96, barY = 26, barW = kOsdW - 96 - 40, barH = 6;
    glScissor(barX, barY, barW, barH);
    glClearColor(1.0f, 1.0f, 1.0f, 0.14f);
    glClear(GL_COLOR_BUFFER_BIT);

    // fill: AA blue #4A90D9, width follows the level
    int fillW = barW * pct / 100;
    if (fillW > 0) {
        glScissor(barX, barY, fillW, barH);
        glClearColor(0.290f, 0.565f, 0.851f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    // speaker block, so the pill is recognisable even without text
    glScissor(24, kOsdH / 2 - 9, 18, 18);
    glClearColor(0.812f, 0.855f, 0.902f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_SCISSOR_TEST);
}

static int kdProbeRun(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("kdprobe -- KD window + EGL surface, %dx%d at %d,%d\n", kOsdW, kOsdH, kPosX, kPosY);

    // eglGetDisplay(EGL_DEFAULT_DISPLAY) returns null here with eglGetError()=SUCCESS, so the default display id
    // is not mapped on this build. The other documented form is a real native display: QNX EGL takes a Screen
    // display handle. Try both and log which one works.
    EGLDisplay dpy = EGL_NO_DISPLAY;
    screen_context_t sctx = 0;
    screen_display_t sdisp[4];
    int ndisp = 0;
    memset(sdisp, 0, sizeof(sdisp));
    if (screen_create_context(&sctx, SCREEN_APPLICATION_CONTEXT) == 0) {
        screen_get_context_property_iv(sctx, SCREEN_PROPERTY_DISPLAY_COUNT, &ndisp);
        if (ndisp > 0 && ndisp <= 4)
            screen_get_context_property_pv(sctx, SCREEN_PROPERTY_DISPLAYS, (void**)sdisp);
        printf("[kd] screen displays: %d (first %p)\n", ndisp, (void*)sdisp[0]);
    } else {
        printf("[kd] screen_create_context failed errno=%d\n", errno);
    }

    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    printf("[kd] eglGetDisplay(EGL_DEFAULT_DISPLAY) -> %p (%s)\n", (void*)dpy, eglErr());
    if (dpy == EGL_NO_DISPLAY && ndisp > 0) {
        dpy = eglGetDisplay((EGLNativeDisplayType)sdisp[0]);
        printf("[kd] eglGetDisplay(screen display %p) -> %p (%s)\n", (void*)sdisp[0], (void*)dpy, eglErr());
    }
    if (dpy == EGL_NO_DISPLAY) { printf("[kd] no EGL display in either form -- stopping\n"); return 1; }

    EGLint maj = 0, min = 0;
    if (!eglInitialize(dpy, &maj, &min)) {
        printf("[kd] eglInitialize failed (%s)\n", eglErr());
        return 1;
    }
    printf("[kd] EGL %d.%d\n", (int)maj, (int)min);

    const EGLint cfgAttr[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,   8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg = 0;
    EGLint num = 0;
    if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &num) || num < 1) {
        printf("[kd] eglChooseConfig failed (%s) num=%d\n", eglErr(), (int)num);
        return 1;
    }
    printf("[kd] config ok (%d)\n", (int)num);

    KDWindow* kd = kdCreateWindow(dpy, cfg, NULL);
    printf("[kd] kdCreateWindow -> %p\n", (void*)kd);
    if (!kd) { printf("[kd] kdCreateWindow failed err=%d\n", (int)kdGetError()); return 1; }

    KDint32 size[2] = { kOsdW, kOsdH };
    int rc = kdSetWindowPropertyiv(kd, KD_WINDOWPROPERTY_SIZE, size);
    printf("[kd] set SIZE %dx%d rc=%d\n", kOsdW, kOsdH, rc);
    rc = kdSetWindowPropertycv(kd, kKdPropIdString, "nbtosd");
    printf("[kd] set id_string rc=%d err=%d\n", rc, (int)kdGetError());

    EGLNativeWindowType native = 0;
    rc = kdRealizeWindow(kd, &native);
    printf("[kd] kdRealizeWindow rc=%d native=%p err=%d\n", rc, (void*)native, (int)kdGetError());
    if (rc != 0 || !native) { printf("[kd] realize failed\n"); kdDestroyWindow(kd); return 1; }

    // kdRealizeWindow hands back the Screen window itself (libKD: *native = screen window), so the Screen
    // window properties still apply to it: place it where the pill belongs.
    {
        screen_window_t sw = (screen_window_t)native;
        int pos[2] = { kPosX, kPosY };
        int z = NBT_SCREEN_ZORDER_ACTIVE + 10;
        int vis = 1;
        int r1 = screen_set_window_property_iv(sw, SCREEN_PROPERTY_POSITION, pos);
        int r2 = screen_set_window_property_iv(sw, SCREEN_PROPERTY_ZORDER, &z);
        int r3 = screen_set_window_property_iv(sw, SCREEN_PROPERTY_VISIBLE, &vis);
        printf("[kd] screen position rc=%d zorder rc=%d visible rc=%d (errno=%d)\n", r1, r2, r3, errno);
    }

    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, native, NULL);
    printf("[kd] eglCreateWindowSurface -> %p (%s)\n", (void*)surf, surf ? "ok" : eglErr());
    if (!surf) { kdDestroyWindow(kd); return 1; }

    const EGLint ctxAttr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
    printf("[kd] eglCreateContext -> %p (%s)\n", (void*)ctx, ctx ? "ok" : eglErr());
    if (!ctx) { eglDestroySurface(dpy, surf); kdDestroyWindow(kd); return 1; }

    if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
        printf("[kd] eglMakeCurrent failed (%s)\n", eglErr());
        eglDestroyContext(dpy, ctx); eglDestroySurface(dpy, surf); kdDestroyWindow(kd);
        return 1;
    }
    printf("[kd] makeCurrent ok; GL_API=%d\n", (int)eglBindAPI(EGL_OPENGL_ES_API));
    printf("[kd] GL_RENDERER: %s\n", (const char*)glGetString(GL_RENDERER));
    printf("[kd] GL_VERSION:  %s\n", (const char*)glGetString(GL_VERSION));

    printf(">>> drawing for 20 s -- a dark pill with a blue bar, bottom centre\n");
    for (int i = 0; i < 20; ++i) {
        int pct = 15 + (i * 4) % 80;
        drawPill(pct);
        EGLBoolean ok = eglSwapBuffers(dpy, surf);
        if (i % 5 == 0) printf("[kd] frame %d pct=%d swap=%d err=%s\n", i, pct, (int)ok, eglErr());
        usleep(500 * 1000);
    }

    printf("[kd] cleaning up\n");
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, surf);
    kdDestroyWindow(kd);
    eglTerminate(dpy);
    printf("kdprobe done\n");
    return 0;
}

// libKD.so references kdMain (openkode apps export it); provide it so the link resolves, and keep main().
extern "C" KDint kdMain(KDint argc, const KDchar* const* argv) {
    (void)argc; (void)argv;
    return kdProbeRun();
}

int main(void) {
    return kdProbeRun();
}
