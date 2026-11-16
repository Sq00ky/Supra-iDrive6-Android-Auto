// Host fault-injection tests. No QNX, networking or firmware is executed.
#include "cluster/video/native/screen_output.hpp"
using namespace kombi::pip::screen;
static int failures, display_count, display_width, render_count, buffer_stride,
    move_to_primary_after_post;
static int buffer_queries, destroyed_objects, created_windows, posted_frames, context_flags,
    window_properties[100][2];
static void *current_display;
static uint8_t pixels[2][(Width * 4 + 16) * Height];
static void expect(bool yes, const char *label) {
    printf("%s %s\n", yes ? "PASS" : "FAIL", label);
    if (!yes) {
        ++failures;
    }
}
extern "C" int usleep(unsigned) {
    return 0;
}
extern "C" int screen_create_context(void **p, int flags) {
    *p = (void *)1;
    context_flags = flags;
    return 0;
}
extern "C" int screen_destroy_context(void *) {
    ++destroyed_objects;
    return 0;
}
extern "C" int screen_get_context_property_iv(void *, int p, int *values) {
    if (p != DisplayCount) {
        return -1;
    }
    *values = display_count;
    return 0;
}
extern "C" int screen_get_context_property_pv(void *, int p, void **values) {
    if (p != Displays) {
        return -1;
    }
    values[0] = (void *)2;
    values[1] = (void *)3;
    return 0;
}
extern "C" int screen_get_display_property_iv(void *display, int p, int *values) {
    if (p != Size) {
        return -1;
    }
    values[0] = display == (void *)3 ? display_width : 1440;
    values[1] = display == (void *)3 ? Height : 540;
    return 0;
}
extern "C" int screen_create_window(void **p, void *) {
    *p = (void *)4;
    ++created_windows;
    return 0;
}
extern "C" int screen_destroy_window(void *p) {
    if (p != (void *)4) {
        return -1;
    }
    ++destroyed_objects;
    return 0;
}
extern "C" int screen_set_window_property_iv(void *window, int p, const int *values) {
    if (window != (void *)4) {
        return -1;
    }
    if ((p == Visible || p == ZOrder) && context_flags != 1) {
        return 0;
    }
    window_properties[p][0] = *values;
    if (p == Size || p == BufferSize || p == SourceSize || p == Position) {
        window_properties[p][1] = values[1];
    }
    return 0;
}
extern "C" int screen_set_window_property_pv(void *, int p, void **values) {
    if (p != Display) {
        return -1;
    }
    current_display = *values;
    return 0;
}
extern "C" int screen_set_window_property_cv(void *, int property, int length,
                                             const char *identifier) {
    return property == IDString && length == 9 && identifier[0] == '2' ? 0 : -1;
}
extern "C" int screen_get_window_property_iv(void *, int p, int *values) {
    if (p == RenderCount) {
        *values = render_count;
        return 0;
    }
    *values = window_properties[p][0];
    if (p == Size || p == BufferSize || p == SourceSize || p == Position) {
        values[1] = window_properties[p][1];
    }
    return 0;
}
extern "C" int screen_get_window_property_pv(void *, int p, void **values) {
    if (p == Display) {
        *values = current_display;
    } else if (p == RenderBuffers) {
        ++buffer_queries;
        values[0] = (void *)pixels[posted_frames % 2];
        if (render_count == 2) {
            values[1] = (void *)pixels[(posted_frames + 1) % 2];
        }
    } else {
        return -1;
    }
    return 0;
}
extern "C" int screen_create_window_buffers(void *, int n) {
    return n == 2 ? 0 : -1;
}
extern "C" int screen_get_buffer_property_iv(void *, int p, int *values) {
    if (p == Stride) {
        *values = buffer_stride;
    } else if (p == Format) {
        *values = RGBA8888;
    } else if (p == BufferSize) {
        values[0] = Width;
        values[1] = Height;
    } else {
        return -1;
    }
    return 0;
}
extern "C" int screen_get_buffer_property_pv(void *buffer, int p, void **values) {
    if (p != Pointer) {
        return -1;
    }
    *values = buffer;
    return 0;
}
extern "C" int screen_post_window(void *, void *, int n, const int *rect, int) {
    if (n != 1 || rect[2] != Width || rect[3] != Height) {
        return -1;
    }
    ++posted_frames;
    if (move_to_primary_after_post) {
        current_display = (void *)2;
    }
    return 0;
}
extern "C" int screen_flush_context(void *, int) {
    return 0;
}
static void reset_peer() {
    display_count = 2;
    display_width = Width;
    render_count = 2;
    buffer_stride = Width * 4 + 16;
    move_to_primary_after_post = buffer_queries = destroyed_objects = created_windows =
        posted_frames = 0;
    current_display = 0;
    for (unsigned i = 0; i < sizeof(pixels); ++i) {
        ((uint8_t *)pixels)[i] = 0xee;
    }
    for (unsigned i = 0; i < sizeof(window_properties) / sizeof(int); ++i) {
        ((int *)window_properties)[i] = 0;
    }
}
static void test_factory_metadata_and_backing() {
    reset_peer();
    ScreenOutput output = {};
    expect(output.inspect(1) && output.create() && output.present(0, 0x0102, false),
           "privileged owned window, secondary display, private ID, first post");
    expect(pixels[0][0] == 1 && pixels[0][1] == 2 && pixels[0][4] == 1 && pixels[0][7] == 1 &&
               pixels[0][5] == 0 && pixels[0][18] == 0x0f && pixels[0][19] == 0xa2 &&
               pixels[0][24] == 0 && pixels[0][25] == 0,
           "captured map metadata retained; config zero, captured context, reserved bytes zero");
    bool unused_backing_is_zero = true;
    for (unsigned x = 84; x < Width * 4; ++x) {
        unused_backing_is_zero &= pixels[0][x] == 0;
    }
    for (unsigned y = 1; y < Height; ++y) {
        for (unsigned x = 0; x < Width; ++x) {
            if (y > 313 || x >= 620) {
                for (unsigned c = 0; c < 4; ++c) {
                    unused_backing_is_zero &= pixels[0][y * buffer_stride + x * 4 + c] == 0;
                }
            }
        }
    }
    expect(unused_backing_is_zero && pixels[0][buffer_stride + 3] == 255 &&
               pixels[0][313 * buffer_stride + 3] == 255,
           "factory content rows 1 through 313 with transparent unused backing and reserved row0");
    bool row_padding_untouched = true;
    for (unsigned y = 0; y < Height; ++y) {
        for (unsigned x = Width * 4; x < unsigned(buffer_stride); ++x) {
            row_padding_untouched &= pixels[0][y * buffer_stride + x] == 0xee;
        }
    }
    expect(row_padding_untouched, "never write Screen row padding");
    expect(output.present(0, 3, true) && pixels[1][6] == 1 && pixels[1][4] == 0,
           "next render buffer and explicit metadata byte permutation");
    expect(output.close() && destroyed_objects == 2, "destroy only owned window/context");
}

static void test_metadata_profiles() {
    reset_peer();
    ScreenOutput output = {};
    output.metadata_profile = FactoryManeuver;
    expect(output.inspect(1) && output.create() && output.present(0, 0x0059, false) &&
               pixels[0][16] == 0x0f && pixels[0][18] == 0 && pixels[0][19] == 0,
           "maneuver profile keeps its distinct captured context bytes");
    output.close();
    reset_peer();
    output = {};
    output.metadata_profile = 2;
    expect(output.inspect(1) && output.create() && !output.present(0, 1, false) &&
               !buffer_queries && !posted_frames,
           "unknown metadata profile refused before accessing buffers");
    output.close();
}

static void test_full_size_clip() {
    reset_peer();
    ScreenOutput output = {};
    static uint8_t clip_pixels[620 * 313 * 4];
    for (unsigned i = 0; i < sizeof(clip_pixels); ++i) {
        clip_pixels[i] = uint8_t(i * 7);
    }
    bool full_clip_preserved =
        output.inspect(1) && output.create() && output.present(clip_pixels, 1, false);
    for (unsigned y = 0; y < 313; ++y) {
        for (unsigned x = 0; x < 620 * 4; ++x) {
            full_clip_preserved &=
                pixels[0][(y + 1) * buffer_stride + x] == clip_pixels[y * 620 * 4 + x];
        }
    }
    expect(full_clip_preserved, "clip pixels including last row survive the metadata-row offset");
    output.close();
}

static void test_compact_clip_interpolation() {
    reset_peer();
    ScreenOutput output = {};
    static uint8_t compact_pixels[310 * 157 * 4];
    for (unsigned y = 0; y < 157; ++y) {
        for (unsigned x = 0; x < 310; ++x) {
            unsigned i = (y * 310 + x) * 4;
            compact_pixels[i] = uint8_t(x);
            compact_pixels[i + 1] = uint8_t(y);
            compact_pixels[i + 2] = 100;
            compact_pixels[i + 3] = 255;
        }
    }
    bool compact_clip_presented =
        output.inspect(1) && output.create() && output.present(compact_pixels, 0x1234, false, true);
    const uint8_t *middle =
        pixels[0] + 2 * buffer_stride + 4; // content (1,1): average four neighbors
    const uint8_t *last = pixels[0] + 313 * buffer_stride + 619 * 4;
    expect(compact_clip_presented && middle[0] == 1 && middle[1] == 1 && middle[2] == 100 &&
               middle[3] == 255 && last[0] == 53 && last[1] == 156 && last[2] == 100 &&
               last[3] == 255 && pixels[0][0] == 0x12 && pixels[0][1] == 0x34,
           "compact clip interpolates pixels, clamps last column/row and retains metadata");
    output.close();
}

static void test_window_and_display_ownership() {
    reset_peer();
    ScreenOutput output = {};
    expect(output.inspect() && output.create() && !output.present(0, 1, false),
           "silent factory-manager rejection detected for ordinary app context");
    output.close();
    reset_peer();
    output = {};
    display_count = 1;
    expect(!output.inspect() && !created_windows, "never fall back to primary display");
    output.close();
    reset_peer();
    output = {};
    display_width = 1440;
    expect(!output.inspect() && !created_windows, "unexpected secondary geometry rejected");
    output.close();
}

static void test_buffer_bounds_and_manager_changes() {
    reset_peer();
    ScreenOutput output = {};
    expect(output.inspect() && output.create(), "buffer bound fixture");
    render_count = 3;
    expect(!output.present(0, 1, false) && !buffer_queries && !posted_frames,
           "oversized handle count rejected before pointer-array query");
    output.close();
    reset_peer();
    output = {};
    output.inspect();
    output.create();
    buffer_stride = Width * 4 - 1;
    expect(!output.present(0, 1, false) && !posted_frames, "short stride rejected");
    output.close();
    reset_peer();
    output = {};
    output.inspect();
    output.create();
    move_to_primary_after_post = 1;
    expect(!output.present(0, 1, false), "manager moving window to primary aborts");
    output.close();
    reset_peer();
    output = {};
    output.inspect();
    output.create();
    window_properties[Transparency][0] = 3;
    expect(!output.present(0, 1, false) && !posted_frames,
           "alpha-blended metadata rejected before posting");
    output.close();
}

int main() {
    test_factory_metadata_and_backing();
    test_metadata_profiles();
    test_full_size_clip();
    test_compact_clip_interpolation();
    test_window_and_display_ownership();
    test_buffer_bounds_and_manager_changes();
    return failures ? 1 : 0;
}
