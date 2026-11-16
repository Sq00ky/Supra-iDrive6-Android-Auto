#pragma once
// Screen test double for public Session lifecycle tests; no host UI/firmware.
namespace screen_peer {
// Literal Screen property IDs keep this peer independent of native wrappers.
enum Property {
    WindowSize = 5,
    WindowDisplay = 11,
    BufferFormat = 14,
    WindowBufferSize = 35,
    WindowPosition = 40,
    WindowSourceSize = 42,
    BufferStride = 44,
    WindowRenderCount = 53
};
static int display_count, posts, destroys, window_properties[100][2];
static int contexts, windows;
static bool fail_buffers, fail_post;
static void *display;
static uint8_t pixels[800 * 480 * 4];
static void reset() {
    display_count = 2;
    posts = destroys = 0;
    display = 0;
    contexts = windows = 0;
    fail_buffers = fail_post = false;
    for (unsigned i = 0; i < sizeof(window_properties) / sizeof(int); ++i) {
        ((int *)window_properties)[i] = 0;
    }
}
} // namespace screen_peer
extern "C" int screen_create_context(void **values, int) {
    *values = (void *)1;
    ++screen_peer::contexts;
    return 0;
}
extern "C" int screen_destroy_context(void *) {
    ++screen_peer::destroys;
    --screen_peer::contexts;
    return 0;
}
extern "C" int screen_get_context_property_iv(void *, int, int *values) {
    *values = screen_peer::display_count;
    return 0;
}
extern "C" int screen_get_context_property_pv(void *, int, void **values) {
    values[0] = (void *)2;
    values[1] = (void *)3;
    return 0;
}
extern "C" int screen_get_display_property_iv(void *display, int, int *values) {
    values[0] = display == (void *)3 ? 800 : 1440;
    values[1] = display == (void *)3 ? 480 : 540;
    return 0;
}
extern "C" int screen_create_window(void **values, void *) {
    *values = (void *)4;
    ++screen_peer::windows;
    return 0;
}
extern "C" int screen_destroy_window(void *) {
    ++screen_peer::destroys;
    --screen_peer::windows;
    return 0;
}
extern "C" int screen_set_window_property_iv(void *, int property, const int *values) {
    screen_peer::window_properties[property][0] = values[0];
    if (property == screen_peer::WindowSize || property == screen_peer::WindowBufferSize ||
        property == screen_peer::WindowPosition || property == screen_peer::WindowSourceSize) {
        screen_peer::window_properties[property][1] = values[1];
    }
    return 0;
}
extern "C" int screen_set_window_property_pv(void *, int, void **values) {
    screen_peer::display = *values;
    return 0;
}
extern "C" int screen_set_window_property_cv(void *, int, int, const char *) {
    return 0;
}
extern "C" int screen_get_window_property_iv(void *, int property, int *values) {
    if (property == screen_peer::WindowRenderCount) {
        *values = 1;
        return 0;
    }
    *values = screen_peer::window_properties[property][0];
    if (property == screen_peer::WindowSize || property == screen_peer::WindowBufferSize ||
        property == screen_peer::WindowPosition || property == screen_peer::WindowSourceSize) {
        values[1] = screen_peer::window_properties[property][1];
    }
    return 0;
}
extern "C" int screen_get_window_property_pv(void *, int property, void **values) {
    *values =
        property == screen_peer::WindowDisplay ? screen_peer::display : (void *)screen_peer::pixels;
    return 0;
}
extern "C" int screen_create_window_buffers(void *, int) {
    return screen_peer::fail_buffers ? -1 : 0;
}
extern "C" int screen_get_buffer_property_iv(void *, int property, int *values) {
    if (property == screen_peer::BufferStride) {
        *values = 3200;
    } else if (property == screen_peer::BufferFormat) {
        *values = 8;
    } else {
        values[0] = 800;
        values[1] = 480;
    }
    return 0;
}
extern "C" int screen_get_buffer_property_pv(void *, int, void **values) {
    *values = screen_peer::pixels;
    return 0;
}
extern "C" int screen_post_window(void *, void *, int, const int *, int) {
    if (screen_peer::fail_post) {
        return -1;
    }
    ++screen_peer::posts;
    return 0;
}
extern "C" int screen_flush_context(void *, int) {
    return 0;
}
