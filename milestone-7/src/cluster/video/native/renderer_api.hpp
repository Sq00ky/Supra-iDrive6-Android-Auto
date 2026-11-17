// Dynamic bindings to the installed NBT services. SDK libc/IPC APIs are used
// normally; these Screen property values are from the verified NBT 6.5 ABI.
#pragma once
#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>

namespace aa_cluster { namespace hardware {

struct RendererApi {
    void *mm, *dict, *screen;
    RendererApi() : mm(0), dict(0), screen(0) {}
    ~RendererApi();
#define API(ret, name, args) typedef ret (*name##_fn) args; name##_fn name;
    API(void*, mmr_connect, (const char*))
    API(void, mmr_disconnect, (void*))
    API(void*, mmr_context_create, (void*, const char*, unsigned, unsigned))
    API(void*, mmr_context_open, (void*, const char*))
    API(void, mmr_context_destroy, (void*))
    API(int, mmr_context_parameters, (void*, void*))
    API(int, mmr_output_attach, (void*, const char*, const char*))
    API(int, mmr_output_parameters, (void*, unsigned, void*))
    API(int, mmr_input_attach, (void*, const char*, const char*))
    API(int, mmr_input_parameters, (void*, void*))
    API(int, mmr_speed_set, (void*, int))
    API(int, mmr_play, (void*))
    API(int, mmr_stop, (void*))
    API(void*, strm_dict_new, ())
    API(void*, strm_dict_set, (void*, const char*, const char*))
    API(void, strm_dict_destroy, (void*))
    API(int, screen_create_context, (void**, int))
    API(int, screen_destroy_context, (void*))
    API(int, screen_create_window, (void**, void*))
    API(int, screen_destroy_window, (void*))
    API(int, screen_create_window_group, (void*, const char*))
    API(int, screen_create_window_buffers, (void*, int))
    API(int, screen_get_context_property_iv, (void*, int, int*))
    API(int, screen_get_context_property_pv, (void*, int, void**))
    API(int, screen_set_window_property_iv, (void*, int, const int*))
    API(int, screen_set_window_property_pv, (void*, int, void**))
    API(int, screen_set_window_property_cv, (void*, int, int, const char*))
    API(int, screen_get_window_property_iv, (void*, int, int*))
    API(int, screen_get_window_property_cv, (void*, int, int, char*))
    API(int, screen_get_window_property_pv, (void*, int, void**))
    API(int, screen_get_buffer_property_pv, (void*, int, void**))
    API(int, screen_get_buffer_property_iv, (void*, int, int*))
    API(int, screen_fill, (void*, void*, const int*))
    API(int, screen_post_window, (void*, void*, int, const int*, int))
    API(int, screen_create_event, (void**))
    API(int, screen_destroy_event, (void*))
    API(int, screen_get_event, (void*, void*, uint64_t))
    API(int, screen_get_event_property_iv, (void*, int, int*))
    API(int, screen_get_event_property_pv, (void*, int, void**))
    API(int, screen_create_pixmap, (void**, void*))
    API(int, screen_destroy_pixmap, (void*))
    API(int, screen_set_pixmap_property_iv, (void*, int, const int*))
    API(int, screen_create_pixmap_buffer, (void*))
    API(int, screen_get_pixmap_property_pv, (void*, int, void**))
    API(int, screen_read_window, (void*, void*, int, const int*, int))
#undef API
    bool load();
    static unsigned long process_id();
    static uint64_t now_us();
    static void sleep_ms(unsigned milliseconds);
    static int open_endpoint(const char* name);
    static void close_endpoint(int handle);
    int read_picture(void* window, void* buffer);
    static int send_packet(int handle, unsigned type, const void* data, unsigned size,
                           const uint64_t* timestamp);
};

} } // namespace aa_cluster::hardware
