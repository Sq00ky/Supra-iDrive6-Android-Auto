#include "hardware_decoder.hpp"
#include "hardware_capture.hpp"
#include "native/renderer_api.hpp"
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/neutrino.h>
#include <sys/iomsg.h>
#include <sys/dispatch.h>
#include <signal.h>
#include <limits.h>

namespace aa_cluster {
namespace hardware {

bool RendererApi::load() {
    // This factory client imports strm_dict_* without declaring libstrm in
    // DT_NEEDED. Make that dependency global before loading the client.
    dict = dlopen("/opt/conn/lib/carplay/libstrm.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!dict) dict = dlopen("libstrm.so.1", RTLD_NOW | RTLD_GLOBAL);
    mm = dlopen("/opt/conn/lib/carplay/libmmrndclient.so.1", RTLD_NOW | RTLD_LOCAL);
    screen = dlopen("libscreen.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!dict || !mm || !screen) {
        printf("AA_HW_FAILED stage=libraries detail=%s\n", dlerror()); return false;
    }
#define BIND(library, name) name = (name##_fn)dlsym(library, #name); \
    if (!name) { printf("AA_HW_FAILED stage=symbol name=%s\n", #name); return false; }
    BIND(mm, mmr_connect) BIND(mm, mmr_disconnect)
    BIND(mm, mmr_context_create) BIND(mm, mmr_context_open) BIND(mm, mmr_context_destroy)
    BIND(mm, mmr_context_parameters) BIND(mm, mmr_output_attach) BIND(mm, mmr_output_parameters)
    BIND(mm, mmr_input_attach) BIND(mm, mmr_input_parameters)
    BIND(mm, mmr_speed_set) BIND(mm, mmr_play) BIND(mm, mmr_stop)
    BIND(dict, strm_dict_new) BIND(dict, strm_dict_set) BIND(dict, strm_dict_destroy)
    BIND(screen, screen_create_context) BIND(screen, screen_destroy_context)
    BIND(screen, screen_get_context_property_iv) BIND(screen, screen_get_context_property_pv)
    BIND(screen, screen_create_window) BIND(screen, screen_destroy_window)
    BIND(screen, screen_create_window_group) BIND(screen, screen_create_window_buffers)
    BIND(screen, screen_set_window_property_iv) BIND(screen, screen_set_window_property_pv)
    BIND(screen, screen_get_window_property_iv) BIND(screen, screen_get_window_property_cv)
    BIND(screen, screen_get_window_property_pv) BIND(screen, screen_post_window)
    BIND(screen, screen_create_event) BIND(screen, screen_destroy_event)
    BIND(screen, screen_get_event) BIND(screen, screen_get_event_property_iv) BIND(screen, screen_get_event_property_pv)
    BIND(screen, screen_create_pixmap) BIND(screen, screen_destroy_pixmap)
    BIND(screen, screen_set_pixmap_property_iv) BIND(screen, screen_create_pixmap_buffer)
    BIND(screen, screen_get_pixmap_property_pv) BIND(screen, screen_get_buffer_property_iv)
    BIND(screen, screen_get_buffer_property_pv) BIND(screen, screen_read_window)
#undef BIND
    return true;
}
RendererApi::~RendererApi() {
    if (mm) dlclose(mm);
    if (screen) dlclose(screen);
    if (dict) dlclose(dict);
}
unsigned long RendererApi::process_id() { return (unsigned long)getpid(); }
uint64_t RendererApi::now_us() {
    timespec now = {}; clock_gettime(CLOCK_MONOTONIC, &now);
    return uint64_t(now.tv_sec) * 1000000 + now.tv_nsec / 1000;
}
void RendererApi::sleep_ms(unsigned ms) { usleep(ms * 1000); }
int RendererApi::open_endpoint(const char* name) { return name_open(name, 0); }
void RendererApi::close_endpoint(int handle) { name_close(handle); }
int RendererApi::read_picture(void* window, void* buffer) {
    uint64_t timeout = 1000000000;
    const int states = _NTO_TIMEOUT_SEND | _NTO_TIMEOUT_REPLY;
    // Success returns the PREVIOUS flags, which need not be zero.
    if (TimerTimeout(CLOCK_MONOTONIC, states, 0, &timeout, 0) < 0) return -1;
    int result = screen_read_window(window, buffer, 0, 0, 0);
    const int saved = errno;
    // Zero flags cancel. Keeping SEND/REPLY with a null duration instead arms
    // an immediate timeout on the next IPC (including logging/socket poll).
    if (TimerTimeout(CLOCK_MONOTONIC, 0, 0, 0, 0) < 0 && result == 0) return -1;
    errno = saved;
    return result;
}
int RendererApi::send_packet(int handle, unsigned type, const void* data, unsigned size,
                             const uint64_t* timestamp) {
    uint32_t header[] = {0, 0, 0, 0, type, size + (timestamp ? 8u : 0u)};
    iov_t parts[3]; SETIOV(&parts[0], header, sizeof(header));
    if (timestamp) { SETIOV(&parts[1], timestamp, 8); SETIOV(&parts[2], data, size); }
    else SETIOV(&parts[1], data, size);
    uint64_t timeout = 250000000; // one input cannot stall the player indefinitely
    if (TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_SEND | _NTO_TIMEOUT_REPLY, 0, &timeout, 0) < 0) return -errno;
    const int result = MsgSendvs_r(handle, parts, timestamp ? 3 : 2, 0, 0);
    const int saved = errno;
    if (TimerTimeout(CLOCK_MONOTONIC, 0, 0, 0, 0) < 0 && result >= 0) return -errno;
    errno = saved;
    return result;
}
} // namespace hardware

struct HardwareDecoder::Implementation {
    hardware::RendererApi api;
    hardware::AsyncSession<hardware::RendererApi> session;
    bool loaded;
    Implementation() : session(api), loaded(api.load()) {}
};
HardwareDecoder::HardwareDecoder() : implementation_(0) {}
HardwareDecoder::~HardwareDecoder() { delete implementation_; }
bool HardwareDecoder::open() {
    if (!implementation_) implementation_ = new Implementation;
    return implementation_->loaded && implementation_->session.open();
}
void HardwareDecoder::close() { if (implementation_) implementation_->session.close(); }
int HardwareDecoder::decode(const uint8_t* data, unsigned size, uint64_t pts, uint8_t* pixels, bool present) {
    return implementation_ ? implementation_->session.decode(data, size, pts, pixels, present) : -1;
}
int HardwareDecoder::poll_picture(uint8_t* pixels, bool present) {
    return implementation_ ? implementation_->session.poll(pixels, present) : 0;
}
unsigned HardwareDecoder::unchanged() const { return implementation_ ? implementation_->session.unchanged() : 0; }
unsigned HardwareDecoder::coalesced() const { return implementation_ ? implementation_->session.coalesced() : 0; }
int HardwareDecoder::cleanup_owner(const char* text) {
    char* end = 0; errno = 0;
    long pid = strtol(text, &end, 10);
    if (errno || !text[0] || *end || pid <= 1 || pid > INT_MAX) return 2;
    if (kill(pid_t(pid), 0) == 0 || errno != ESRCH) return 2;
    char name[64], path[160];
    snprintf(name, sizeof(name), "kombi_hw_%ld", pid);
    snprintf(path, sizeof(path), "/pps/services/multimedia/renderer/context/%s", name);
    if (access(path, F_OK)) return errno == ENOENT ? 0 : 1;
    hardware::RendererApi api;
    if (!api.load()) return 1;
    void* connection = api.mmr_connect(0);
    if (!connection) return 1;
    void* context = api.mmr_context_open(connection, name);
    if (context) { api.mmr_stop(context); api.mmr_context_destroy(context); }
    api.mmr_disconnect(connection);
    for (unsigned i = 0; i < 50; ++i) {
        if (access(path, F_OK) && errno == ENOENT) {
            printf("AA_HW_RECOVERED context=%s\n", name); return 0;
        }
        usleep(20000);
    }
    printf("AA_HW_FAILED stage=orphan_context_cleanup context=%s\n", name);
    return 1;
}
} // namespace aa_cluster
