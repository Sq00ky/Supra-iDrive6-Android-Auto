// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>

// QNX Screen definitions
typedef void* screen_context_t;
typedef void* screen_window_t;
typedef void* screen_buffer_t;

#define SCREEN_APPLICATION_CONTEXT 0
#define SCREEN_APPLICATION_WINDOW  0

#define SCREEN_PROPERTY_FORMAT         14
#define SCREEN_PROPERTY_ID_STRING      20
#define SCREEN_PROPERTY_POINTER        34
#define SCREEN_PROPERTY_POSITION       35
#define SCREEN_PROPERTY_RENDER_BUFFERS 37
#define SCREEN_PROPERTY_SOURCE_SIZE    40
#define SCREEN_PROPERTY_VISIBLE        43
#define SCREEN_PROPERTY_USAGE          48
#define SCREEN_PROPERTY_ZORDER         54

#define SCREEN_FORMAT_RGBA8888 8
#define SCREEN_USAGE_NATIVE    (1 << 0)
#define SCREEN_USAGE_READ      (1 << 1)
#define SCREEN_USAGE_WRITE     (1 << 2)

extern "C" {
    int screen_create_context(screen_context_t *ctx, int flags);
    int screen_destroy_context(screen_context_t ctx);
    int screen_create_window_type(screen_window_t *win, screen_context_t ctx, int type);
    int screen_destroy_window(screen_window_t win);
    int screen_create_window_buffers(screen_window_t win, int count);
    int screen_set_window_property_iv(screen_window_t win, int prop, const int *val);
    int screen_set_window_property_cv(screen_window_t win, int prop, int len, const char *val);
    int screen_get_window_property_pv(screen_window_t win, int prop, void **val);
    int screen_get_buffer_property_pv(screen_buffer_t buf, int prop, void **val);
    int screen_post_window(screen_window_t win, screen_buffer_t buf, int count, const int *rects, int flags);
}

int main(int argc, char* argv[]) {
    printf("====================================================\n");
    printf("  NBT Evo Screen Display Proof-of-Concept Test     \n");
    printf("  Target: 1280x480 CID Wide Display (Surface 1280) \n");
    printf("====================================================\n");

    screen_context_t ctx = NULL;
    screen_window_t win = NULL;
    screen_buffer_t buf = NULL;

    // 1. Create Screen Context
    if (screen_create_context(&ctx, SCREEN_APPLICATION_CONTEXT) != 0) {
        perror("screen_create_context failed");
        return 1;
    }
    printf("[1/6] Created Screen context.\n");

    // 2. Create Window
    if (screen_create_window_type(&win, ctx, SCREEN_APPLICATION_WINDOW) != 0) {
        perror("screen_create_window_type failed");
        return 1;
    }
    printf("[2/6] Created Screen window.\n");

    // 3. Set Properties
    const char* win_id = "1280";
    screen_set_window_property_cv(win, SCREEN_PROPERTY_ID_STRING, strlen(win_id), win_id);

    int usage = SCREEN_USAGE_NATIVE | SCREEN_USAGE_WRITE | SCREEN_USAGE_READ;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_USAGE, &usage);

    int format = SCREEN_FORMAT_RGBA8888;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_FORMAT, &format);

    int size[2] = { 1280, 480 };
    screen_set_window_property_iv(win, SCREEN_PROPERTY_SOURCE_SIZE, size);

    int pos[2] = { 0, 0 };
    screen_set_window_property_iv(win, SCREEN_PROPERTY_POSITION, pos);

    int zorder = 2000;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_ZORDER, &zorder);

    printf("[3/6] Set window properties (ID=1280, 1280x480, Z=2000).\n");

    // 4. Allocate Buffer
    if (screen_create_window_buffers(win, 1) != 0) {
        perror("screen_create_window_buffers failed");
        return 1;
    }

    screen_get_window_property_pv(win, SCREEN_PROPERTY_RENDER_BUFFERS, &buf);

    void* ptr = NULL;
    screen_get_buffer_property_pv(buf, SCREEN_PROPERTY_POINTER, &ptr);
    if (!ptr) {
        printf("Error: Could not retrieve buffer direct memory pointer!\n");
        return 1;
    }
    printf("[4/6] Allocated render buffer (ptr: %p).\n", ptr);

    // 5. Fill buffer with Bright Neon Green (0xFF00FF00) and White Borders
    uint32_t* pixels = reinterpret_cast<uint32_t*>(ptr);
    for (int y = 0; y < 480; ++y) {
        for (int x = 0; x < 1280; ++x) {
            // Draw a bright red border (10px) with solid neon green in the middle
            if (x < 10 || x >= 1270 || y < 10 || y >= 470) {
                pixels[y * 1280 + x] = 0xFFFF0000; // Red border (ABGR)
            } else {
                pixels[y * 1280 + x] = 0xFF00FF00; // Neon Green (ABGR)
            }
        }
    }
    printf("[5/6] Painted 1280x480 test pattern (Neon Green with Red border).\n");

    // Post the window
    int rect[4] = { 0, 0, 1280, 480 };
    int post_res = screen_post_window(win, buf, 1, rect, 0);
    int vis = 1;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_VISIBLE, &vis);
    printf("[6/6] screen_post_window result: %d (errno: %d).\n", post_res, errno);

    // 6. LayerManager Switch
    printf("\n>>> SWITCHING DISPLAY: Hiding iDrive (Surface 256) -> Showing Test (Surface 1280)...\n");
    system("/opt/sys/bin/LayerManagerControl set surface 256 visibility 0");
    system("/opt/sys/bin/LayerManagerControl set surface 1280 visibility 1");
    system("/opt/sys/bin/LayerManagerControl set surface 1280 opacity 1.0");

    printf("\n*** SUCCESS! The screen should now be BRIGHT NEON GREEN with RED BORDERS! ***\n");
    printf("Holding display for 15 seconds (press Ctrl+C to abort)...\n");

    for (int i = 15; i > 0; --i) {
        printf("Time remaining: %d seconds...\r", i);
        fflush(stdout);
        sleep(1);
    }
    printf("\n");

    // 7. Cleanup and Restore iDrive
    printf("\n>>> RESTORING IDRIVE DISPLAY: Showing Surface 256...\n");
    system("/opt/sys/bin/LayerManagerControl set surface 256 visibility 1");

    screen_destroy_window(win);
    screen_destroy_context(ctx);
    printf("Done. Screen restored to stock iDrive.\n");
    return 0;
}
