// NBT's Screen 6.5 SP1 binding: its header is absent from the base SDP 6.5.
// Keep these car-verified property values independent of other Screen versions.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "native_metadata.hpp"
#ifdef NBT_CLUSTER_HOST_TEST
#include <qnx_api_stub.hpp>
#else
#include <stdio.h>
#include <unistd.h>
#endif
extern "C" {
int screen_create_context(void**, int);
int screen_destroy_context(void*);
int screen_get_context_property_iv(void*, int, int*);
int screen_get_context_property_pv(void*, int, void**);
int screen_get_display_property_iv(void*, int, int*);
int screen_create_window(void**, void*);
int screen_destroy_window(void*);
int screen_set_window_property_iv(void*, int, const int*);
int screen_set_window_property_pv(void*, int, void**);
int screen_set_window_property_cv(void*, int, int, const char*);
int screen_get_window_property_iv(void*, int, int*);
int screen_get_window_property_pv(void*, int, void**);
int screen_create_window_buffers(void*, int);
int screen_get_buffer_property_iv(void*, int, int*);
int screen_get_buffer_property_pv(void*, int, void**);
int screen_post_window(void*, void*, int, const int*, int);
int screen_flush_context(void*, int);
}

namespace kombi {
namespace pip {
namespace screen {

using nbt::cluster::VideoProfile;

// Full factory backing, visible content below the metadata row, and the
// optional half-size source that present() expands into that content area.
enum {
    Width = VideoProfile::BackingWidth,
    Height = VideoProfile::BackingHeight,
    ContentWidth = VideoProfile::ContentWidth,
    ContentHeight = VideoProfile::ContentHeight,
    ContentY = VideoProfile::ContentY,
    CompactWidth = VideoProfile::CompactWidth,
    CompactHeight = VideoProfile::CompactHeight
};

// Screen 6.5 SP1 property IDs verified on NBT. These are ABI constants, not
// offsets into the pixel buffer or values from a newer Screen SDK.
enum {
    BufferSize = 5,
    Display = 11,
    Format = 14,
    Alpha = 16,
    Pipeline = 17,
    IDString = 20,
    Pointer = 34,
    Position = 35,
    RenderBuffers = 37,
    Size = 40,
    SourceSize = 42,
    Stride = 44,
    Transparency = 46,
    Usage = 48,
    Visible = 51,
    RenderCount = 53,
    ZOrder = 54,
    DisplayCount = 59,
    Displays = 60
};
enum { RGBA8888 = VideoProfile::ScreenFormat };

// Private producer identity; the selected profile retains the factory window.
static const char* const WindowID = VideoProfile::windowId();

struct ScreenOutput {
    // Aggregate initialized by the owner. close() releases a partially created
    // context/window too; display is a borrowed handle owned by the context.
    void *context, *window, *display;
    unsigned posts;
    unsigned metadata_profile;

    bool set(int property, const int* value) {
        int result_code = screen_set_window_property_iv(window, property, value);
        if (result_code) {
            printf("SCREEN_SET_FAILED property=%d rc=%d\n", property, result_code);
        }
        return result_code == 0;
    }

    bool inspect(int context_flags = 0) {
        // The car's existing display manager restricts VISIBLE/ZORDER writes
        // from ordinary application contexts. Flag 1 grants window-management
        // access; use it only for our own window. The launcher verifies both
        // displays already have managers, so this context adopts neither.
        if (screen_create_context(&context, context_flags)) {
            return false;
        }
        int display_count = 0;
        if (screen_get_context_property_iv(context, DisplayCount, &display_count) ||
            display_count != VideoProfile::DisplayCount) {
            printf("REFUSE_DISPLAY_COUNT count=%d expected=%u\n", display_count,
                   unsigned(VideoProfile::DisplayCount));
            return false;
        }
        void* display_handles[VideoProfile::DisplayCount] = {};
        if (screen_get_context_property_pv(context, Displays, display_handles)) {
            return false;
        }
        // Every expected display must have a distinct non-null handle before
        // querying geometry or selecting the profile's output display.
        for (unsigned index = 0; index < VideoProfile::DisplayCount; ++index) {
            if (!display_handles[index]) {
                return false;
            }
            for (unsigned earlier = 0; earlier < index; ++earlier) {
                if (display_handles[index] == display_handles[earlier]) {
                    return false;
                }
            }
        }
        int dimensions[2] = {};
        for (int display_index = 0; display_index < VideoProfile::DisplayCount; ++display_index) {
            if (screen_get_display_property_iv(display_handles[display_index], Size, dimensions)) {
                return false;
            }
            printf("DISPLAY index=%d width=%d height=%d\n", display_index, dimensions[0],
                   dimensions[1]);
            if (display_index == VideoProfile::DisplayIndex &&
                (dimensions[0] != Width || dimensions[1] != Height)) {
                return false;
            }
        }
        display = display_handles[VideoProfile::DisplayIndex];
        return true;
    }

    bool create() {
        if (!display || screen_create_window(&window, context)) {
            return false;
        }
        // Keep the profile's verified format/usage flags and double buffering.
        // The window stays hidden until its first post has been processed.
        int dimensions[2] = {Width, Height}, origin[2] = {0, 0}, format = RGBA8888,
            usage = VideoProfile::ScreenUsage;
        int hidden = 0, source_transparency = VideoProfile::ScreenTransparency,
            alpha = VideoProfile::ScreenAlpha;
        // SOURCE transparency (0) preserves metadata in all four channels.
        return !screen_set_window_property_pv(window, Display, &display) && set(Visible, &hidden) &&
               !screen_set_window_property_cv(window, IDString, VideoProfile::WindowIdLength,
                                              WindowID) &&
               set(Format, &format) && set(Usage, &usage) && set(BufferSize, dimensions) &&
               set(Size, dimensions) && set(SourceSize, dimensions) && set(Position, origin) &&
               set(Transparency, &source_transparency) && set(Alpha, &alpha) &&
               !screen_create_window_buffers(window, 2);
    }

    // Re-read the manager-controlled properties before/after every frame.
    // Metadata survives only with the captured size, format and source blend.
    bool contract(bool require_visible) {
        void* actual_display = 0;
        int dimensions[2] = {}, position[2] = {}, format = 0, visible = 0, alpha = 0,
            transparency = -1;
        bool ok = !screen_get_window_property_pv(window, Display, &actual_display) &&
                  actual_display == display &&
                  !screen_get_window_property_iv(window, BufferSize, dimensions) &&
                  dimensions[0] == Width && dimensions[1] == Height &&
                  !screen_get_window_property_iv(window, Size, dimensions) &&
                  dimensions[0] == Width && dimensions[1] == Height &&
                  !screen_get_window_property_iv(window, SourceSize, dimensions) &&
                  dimensions[0] == Width && dimensions[1] == Height &&
                  !screen_get_window_property_iv(window, Position, position) && !position[0] &&
                  !position[1] && !screen_get_window_property_iv(window, Format, &format) &&
                  format == RGBA8888 && !screen_get_window_property_iv(window, Alpha, &alpha) &&
                  alpha == VideoProfile::ScreenAlpha &&
                  !screen_get_window_property_iv(window, Transparency, &transparency) &&
                  transparency == VideoProfile::ScreenTransparency;
        if (require_visible) {
            ok = ok && !screen_get_window_property_iv(window, Visible, &visible) && visible == 1;
        }
        if (!ok) {
            printf("SCREEN_CONTRACT_CHANGED posts=%u\n", posts);
            const int properties[] = {BufferSize, Size,         SourceSize, Position, Format,
                                      Alpha,      Transparency, Visible,    ZOrder,   Pipeline};
            for (unsigned i = 0; i < sizeof(properties) / sizeof(properties[0]); ++i) {
                int value[2] = {-999, -999};
                int result_code = screen_get_window_property_iv(window, properties[i], value);
                printf("SCREEN_PROPERTY property=%d rc=%d value=%d,%d\n", properties[i],
                       result_code, value[0], value[1]);
            }
            actual_display = 0;
            int result_code = screen_get_window_property_pv(window, Display, &actual_display);
            printf("SCREEN_DISPLAY rc=%d matches_secondary=%u\n", result_code,
                   unsigned(actual_display == display));
            fflush(0);
        }
        return ok;
    }

    bool present(const uint8_t* bgra, unsigned sequence, bool swap_rb, bool compact = false) {
        if (metadata_profile > FactoryManeuver || !window || !contract(posts != 0)) {
            return false;
        }
        int buffer_count = 0, buffer_stride = 0, format = 0, dimensions[2] = {};
        void* buffers[2] = {};
        void* memory = 0;
        if (screen_get_window_property_iv(window, RenderCount, &buffer_count) || buffer_count < 1 ||
            buffer_count > 2 || screen_get_window_property_pv(window, RenderBuffers, buffers) ||
            !buffers[0] || screen_get_buffer_property_iv(buffers[0], Stride, &buffer_stride) ||
            buffer_stride < Width * 4 || buffer_stride > 16384 ||
            screen_get_buffer_property_iv(buffers[0], BufferSize, dimensions) ||
            dimensions[0] != Width || dimensions[1] != Height ||
            screen_get_buffer_property_iv(buffers[0], Format, &format) || format != RGBA8888 ||
            screen_get_buffer_property_pv(buffers[0], Pointer, &memory) || !memory) {
            return false;
        }

        // Clear the full backing each frame. Copy/scale only the visible area;
        // all other pixels, including the reserved first row, start as zero.
        for (unsigned y = 0; y < Height; ++y) {
            uint8_t* row = (uint8_t*)memory + y * buffer_stride;
            for (unsigned x = 0; x < Width; ++x) {
                uint8_t* pixel = row + x * 4;
                pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
                if (x < ContentWidth && y >= ContentY && y < ContentY + ContentHeight) {
                    const unsigned content_y = y - ContentY;
                    if (bgra && compact) {
                        // Twofold bilinear expansion; crop the final expanded
                        // row when the negotiated content height is odd.
                        unsigned source_x0 = x / 2, source_y0 = content_y / 2;
                        unsigned source_x1 = source_x0 + (source_x0 + 1 < CompactWidth),
                                 source_y1 = source_y0 + (source_y0 + 1 < CompactHeight);
                        unsigned horizontal_weight = x & 1, vertical_weight = content_y & 1;
                        for (unsigned channel = 0; channel < 4; ++channel) {
                            unsigned top =
                                bgra[(source_y0 * CompactWidth + source_x0) * 4 + channel] *
                                    (2 - horizontal_weight) +
                                bgra[(source_y0 * CompactWidth + source_x1) * 4 + channel] *
                                    horizontal_weight;
                            unsigned bottom =
                                bgra[(source_y1 * CompactWidth + source_x0) * 4 + channel] *
                                    (2 - horizontal_weight) +
                                bgra[(source_y1 * CompactWidth + source_x1) * 4 + channel] *
                                    horizontal_weight;
                            pixel[channel] = uint8_t(
                                (top * (2 - vertical_weight) + bottom * vertical_weight + 2) / 4);
                        }
                    } else if (bgra) {
                        for (unsigned channel = 0; channel < 4; ++channel) {
                            pixel[channel] = bgra[(content_y * ContentWidth + x) * 4 + channel];
                        }
                    } else {
                        // No input frame: produce the original moving test bars.
                        const unsigned first_bar_end = (ContentWidth + 2) / 3;
                        const unsigned second_bar_end = 2 * first_bar_end;
                        pixel[3] = 255;
                        pixel[0] = x < first_bar_end ? 220 : 20;
                        pixel[1] = (x >= first_bar_end && x < second_bar_end) ? 220 : 20;
                        pixel[2] = x >= second_bar_end ? 220 : 20;
                        if (((x + sequence * 9) % ContentWidth) < 28 || content_y < 4 ||
                            content_y > ContentHeight - 5) {
                            pixel[0] = pixel[1] = pixel[2] = 255;
                        }
                    }
                }
            }
        }
        // The reserved first row holds metadata followed by transparent zeros.
        // Profile geometry places the visible image below that row; all unused
        // backing pixels remain zero.
        if (!factory_metadata_bgra((uint8_t*)memory, uint16_t(sequence), metadata_profile)) {
            return false;
        }

        // Swap the entire backing after metadata insertion: metadata channels
        // follow exactly the same byte conversion as the displayed pixels.
        if (swap_rb) {
            for (unsigned y = 0; y < Height; ++y) {
                for (unsigned x = 0; x < Width; ++x) {
                    uint8_t* pixel = (uint8_t*)memory + y * buffer_stride + x * 4;
                    uint8_t first_channel = pixel[0];
                    pixel[0] = pixel[2];
                    pixel[2] = first_channel;
                }
            }
        }
        int damage[4] = {0, 0, Width, Height};
        if (screen_post_window(window, buffers[0], 1, damage, 0)) {
            return false;
        }
        ++posts;
        if (posts == 1) {
            // Allow the manager's first POST handler to finish before checking
            // our own window. Do not select a pipeline or modify factory windows.
            usleep(200000);
            void* actual_display = 0;
            if (screen_get_window_property_pv(window, Display, &actual_display) ||
                actual_display != display) {
                return false;
            }
            int source_transparency = VideoProfile::ScreenTransparency,
                alpha = VideoProfile::ScreenAlpha, visible = 1,
                z_order = VideoProfile::ScreenZOrder;
            if (!set(Transparency, &source_transparency) || !set(Alpha, &alpha) ||
                !set(ZOrder, &z_order) || !set(Visible, &visible) ||
                screen_flush_context(context, 1)) {
                return false;
            }
            int pipeline = -1;
            screen_get_window_property_iv(window, Pipeline, &pipeline);
            printf(
                "FIRST_POST id=%s display_index=%u pipeline=%d stride=%d metadata=factory%u-bgra "
                "profile=%s content_y=%u swap_rb=%u\n",
                WindowID, unsigned(VideoProfile::DisplayIndex), pipeline, buffer_stride,
                unsigned(MetadataBytes), metadata_profile == FactoryMap ? "map" : "maneuver",
                unsigned(ContentY), unsigned(swap_rb));
            fflush(0);
        }
        return contract(true);
    }

    bool close() {
        bool ok = true;
        if (window) {
            int hide = 0;
            set(Visible, &hide);
            ok = screen_destroy_window(window) == 0;
            window = 0;
        }
        if (context) {
            ok = (screen_destroy_context(context) == 0) && ok;
            context = 0;
        }
        display = 0;
        return ok;
    }
};
} // namespace screen
} // namespace pip
} // namespace kombi
