// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef SCREEN_VIDEO_H
#define SCREEN_VIDEO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Native QNX Screen Windowing System Definitions
 * Target: libscreen.so on BMW NBT Evo (Toyota Supra A90)
 * Context: Matches cpvideoctx16838 / carplay.ini
 */

#define NBT_SCREEN_WINDOW_ID            "1280"
#define NBT_SCREEN_WIDTH_SUPRA_WIDESCREEN 1280
#define NBT_SCREEN_HEIGHT_SUPRA_WIDESCREEN 480
#define NBT_SCREEN_WIDTH_STANDARD       1280
#define NBT_SCREEN_HEIGHT_STANDARD      480
#define NBT_SCREEN_MAX_FPS              60

/* Top Z-order to cleanly overlay the native iDrive 6 HMI (HmiMain3D is order 999) */
#define NBT_SCREEN_ZORDER_ACTIVE        2000
#define NBT_SCREEN_ZORDER_BACKGROUND    -1000

/* QNX Screen Types & Property IDs verified via Ghidra MCP */
typedef void* screen_context_t;
typedef void* screen_window_t;
typedef void* screen_buffer_t;
typedef void* screen_display_t;

#define SCREEN_APPLICATION_CONTEXT 0
#define SCREEN_APPLICATION_WINDOW  0

#define SCREEN_PROPERTY_DISPLAY        11
#define SCREEN_PROPERTY_FORMAT         14
#define SCREEN_PROPERTY_ID_STRING      20
#define SCREEN_PROPERTY_POINTER        34
#define SCREEN_PROPERTY_POSITION       35
#define SCREEN_PROPERTY_RENDER_BUFFERS 37
#define SCREEN_PROPERTY_SOURCE_SIZE    40
#define SCREEN_PROPERTY_BUFFER_SIZE    40
#define SCREEN_PROPERTY_VISIBLE        43
#define SCREEN_PROPERTY_USAGE          48
#define SCREEN_PROPERTY_ZORDER         54
#define SCREEN_PROPERTY_STRIDE         58
#define SCREEN_PROPERTY_DISPLAY_COUNT  59
#define SCREEN_PROPERTY_DISPLAYS       60

#define SCREEN_BLIT_COLOR              5
#define SCREEN_BLIT_GLOBAL_ALPHA       9
#define SCREEN_BLIT_END                0

#define SCREEN_FORMAT_RGBA8888 8
#define SCREEN_USAGE_NATIVE    (1 << 0)
#define SCREEN_USAGE_READ      (1 << 1)
#define SCREEN_USAGE_WRITE     (1 << 2)
#define SCREEN_USAGE_VIDEO     (1 << 3)

/* QNX Screen API Functions exported by libscreen.so */
int screen_create_context(screen_context_t *ctx, int flags);
int screen_destroy_context(screen_context_t ctx);
int screen_create_window_type(screen_window_t *win, screen_context_t ctx, int type);
int screen_destroy_window(screen_window_t win);
int screen_create_window_group(screen_window_t win, const char *name);
int screen_create_window_buffers(screen_window_t win, int count);
int screen_get_context_property_iv(screen_context_t ctx, int prop, int *val);
int screen_get_context_property_pv(screen_context_t ctx, int prop, void **val);
int screen_set_window_property_iv(screen_window_t win, int prop, const int *val);
int screen_set_window_property_cv(screen_window_t win, int prop, int len, const char *val);
int screen_set_window_property_pv(screen_window_t win, int prop, void **val);
int screen_get_window_property_pv(screen_window_t win, int prop, void **val);
int screen_get_buffer_property_pv(screen_buffer_t buf, int prop, void **val);
int screen_get_buffer_property_iv(screen_buffer_t buf, int prop, int *val);
int screen_fill(screen_context_t ctx, screen_buffer_t buf, const int *attribs);
int screen_post_window(screen_window_t win, screen_buffer_t buf, int count, const int *rects, int flags);

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    const char* window_id;
    int32_t  z_order;
} screen_config_t;

typedef struct {
    screen_context_t ctx;
    screen_window_t  win;
    screen_buffer_t  buf;
    screen_display_t disp;
    void*            pixels;
    int              stride;
    screen_config_t  config;
    bool             is_visible;
} screen_video_sink_t;

int screen_video_sink_init(screen_video_sink_t* sink, const screen_config_t* config);
int screen_video_sink_set_visible(screen_video_sink_t* sink, bool visible);
int screen_video_sink_post_frame(screen_video_sink_t* sink, const uint8_t* h264_data, size_t length);
void screen_video_sink_destroy(screen_video_sink_t* sink);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_VIDEO_H */
