#pragma once
#include "hardware_pixels.hpp"
#include "protocol.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

namespace aa_cluster {
namespace hardware {

// NBT Screen 6.5 SP1 ABI, shared with the car-tested native producer.
enum Property {
    BufferSize = 5, Display = 11, Format = 14, IdString = 20, Pointer = 34,
    Position = 35, RenderBuffers = 37, Size = 40, SourcePosition = 41, SourceSize = 42, Stride = 44,
    Type = 47, Usage = 48, Visible = 51, Window = 52, ZOrder = 54,
    DisplayCount = 59, Displays = 60
};

// Api is the native renderer/Screen binding in production and a deterministic
// peer in lifecycle tests. No phone, factory context or main endpoint is owned.
// After open, one input thread may call decode while one capture thread calls
// poll. Join capture before close/open; Screen objects belong to capture only.
template<class Api> class Session {
    typedef nbt::cluster::VideoProfile P;
    Api& api_;
    void *connection_, *renderer_, *screen_, *parent_, *window_, *event_;
    void *pixmap_, *buffer_, *pixels_;
    int endpoint_, stride_;
    bool opened_, configured_, have_picture_;
    unsigned generation_, snapshots_;
    uint64_t last_sample_, last_report_;

    // Only small counters/status cross the input/capture boundary. Never hold
    // this mutex across renderer IPC, Screen readback, fitting or logging.
    struct Progress {
        mutable pthread_mutex_t mutex;
        bool ready, failed;
        unsigned accepted, unchanged, coalesced;
        uint64_t first_input;
        Progress() : ready(pthread_mutex_init(&mutex, 0) == 0) { reset(); }
        ~Progress() { if (ready) pthread_mutex_destroy(&mutex); }
        void reset() { failed = false; accepted = unchanged = coalesced = 0; first_input = 0; }
        bool snapshot(unsigned& inputs, uint64_t& first) const {
            pthread_mutex_lock(&mutex);
            inputs = accepted; first = first_input;
            const bool healthy = !failed;
            pthread_mutex_unlock(&mutex);
            return healthy;
        }
        void fail() {
            pthread_mutex_lock(&mutex); failed = true; pthread_mutex_unlock(&mutex);
        }
        unsigned input(uint64_t time, bool present) {
            pthread_mutex_lock(&mutex);
            if (!accepted) first_input = time;
            const unsigned count = ++accepted;
            if (!present) ++coalesced;
            pthread_mutex_unlock(&mutex);
            return count;
        }
        void same_picture() {
            pthread_mutex_lock(&mutex); ++unchanged; pthread_mutex_unlock(&mutex);
        }
        unsigned counter(bool same) const {
            if (!ready) return 0;
            pthread_mutex_lock(&mutex);
            const unsigned value = same ? unchanged : coalesced;
            pthread_mutex_unlock(&mutex);
            return value;
        }
    } progress_;
    uint8_t *previous_, *fitted_;
    char name_[64], group_[64], endpoint_name_[96], window_id_[32];

    bool ok(int result, const char* stage) {
        if (result < 0) printf("AA_HW_FAILED stage=%s rc=%d\n", stage, result);
        return result >= 0;
    }
    bool parameters(unsigned target, unsigned output = 0) {
        void* values = api_.strm_dict_new();
        if (!values) return false;
        const char* input_keys[] = {"prefetch_mode", "VideoDecoderLowLatencyMode", "ColorSpaceMode"};
        const char* input_values[] = {"-1", "1", "1"};
        const char* output_keys[] = {"mmf.SCREEN_WRITER_DISABLE_CADENCE",
            "mmf.SCREEN_WRITER_DISABLE_MEDIACLOCK", "mmf.SCREEN_WRITER_POST_BUFFER_NOSYNC"};
        for (unsigned i = 0; i < 3; ++i) {
            void* next = api_.strm_dict_set(values, target == 2 ? output_keys[i] : input_keys[i],
                                          target == 2 ? "1" : input_values[i]);
            if (!next) { api_.strm_dict_destroy(values); return false; }
            values = next;
        }
        // The factory client consumes the dictionary on success and failure.
        return ok(target == 2 ? api_.mmr_output_parameters(renderer_, output, values) :
                  target == 1 ? api_.mmr_input_parameters(renderer_, values) :
                                api_.mmr_context_parameters(renderer_, values), "parameters");
    }
    bool make_screen() {
        if (!ok(api_.screen_create_context(&screen_, 1), "screen_context")) return false;
        int count = 0; void* displays[8] = {};
        if (!ok(api_.screen_get_context_property_iv(screen_, DisplayCount, &count), "display_count") ||
            count < 1 || count > 8 ||
            !ok(api_.screen_get_context_property_pv(screen_, Displays, displays), "displays") ||
            !ok(api_.screen_create_window(&parent_, screen_), "parent")) return false;
        int dimensions[] = {P::SourceWidth, P::SourceHeight}, origin[] = {0, 0};
        int format = 8, usage = 8, hidden = 0, z = -1000;
        if (!ok(api_.screen_set_window_property_pv(parent_, Display, &displays[0]), "parent_display") ||
            !ok(api_.screen_create_window_group(parent_, group_), "group") ||
            !ok(api_.screen_set_window_property_iv(parent_, Usage, &usage), "parent_usage") ||
            !ok(api_.screen_set_window_property_iv(parent_, Format, &format), "parent_format") ||
            !ok(api_.screen_set_window_property_iv(parent_, BufferSize, dimensions), "parent_buffer") ||
            !ok(api_.screen_set_window_property_iv(parent_, Size, dimensions), "parent_size") ||
            !ok(api_.screen_set_window_property_iv(parent_, SourceSize, dimensions), "parent_source") ||
            !ok(api_.screen_set_window_property_iv(parent_, Position, origin), "parent_position") ||
            !ok(api_.screen_set_window_property_iv(parent_, Visible, &hidden), "parent_hidden") ||
            !ok(api_.screen_set_window_property_iv(parent_, ZOrder, &z), "parent_z") ||
            !ok(api_.screen_create_window_buffers(parent_, 1), "parent_allocate") ||
            !ok(api_.screen_create_event(&event_), "event")) return false;
        // Allocate/flush the group anchor before the renderer joins it. It is
        // never used as the decoded picture and remains hidden on display 0.
        void* anchor = 0; int rect[] = {0, 0, P::SourceWidth, P::SourceHeight};
        if (!ok(api_.screen_get_window_property_pv(parent_, RenderBuffers, &anchor), "anchor") ||
            !anchor || !ok(api_.screen_post_window(parent_, anchor, 1, rect, 0), "anchor_post")) return false;
        // The decoder/window stays 800x480. Screen scales its whole image into
        // this smaller destination, combining conversion and resizing in one call.
        int capture_size[] = {P::FittedWidth, P::ContentHeight};
        usage = 14; // READ | WRITE | NATIVE, required for server-side readback.
        if (!ok(api_.screen_create_pixmap(&pixmap_, screen_), "pixmap") ||
            !ok(api_.screen_set_pixmap_property_iv(pixmap_, Format, &format), "capture_format") ||
            !ok(api_.screen_set_pixmap_property_iv(pixmap_, BufferSize, capture_size), "capture_size") ||
            !ok(api_.screen_set_pixmap_property_iv(pixmap_, Usage, &usage), "capture_usage") ||
            !ok(api_.screen_create_pixmap_buffer(pixmap_), "capture_allocate") ||
            !ok(api_.screen_get_pixmap_property_pv(pixmap_, RenderBuffers, &buffer_), "capture_buffer") ||
            !buffer_ || !ok(api_.screen_get_buffer_property_pv(buffer_, Pointer, &pixels_), "capture_pointer") ||
            !pixels_ || !ok(api_.screen_get_buffer_property_iv(buffer_, Stride, &stride_), "capture_stride")) return false;
        int size[2] = {}, actual_format = 0;
        if (!ok(api_.screen_get_buffer_property_iv(buffer_, BufferSize, size), "capture_geometry") ||
            !ok(api_.screen_get_buffer_property_iv(buffer_, Format, &actual_format), "capture_format")) return false;
        if (size[0] != P::FittedWidth || size[1] != P::ContentHeight || actual_format != 8 ||
            stride_ < P::FittedWidth * 4 || stride_ > 65536) {
            printf("AA_HW_FAILED stage=capture_geometry requested=%ux%u actual=%dx%d format=%d stride=%d\n",
                   unsigned(P::FittedWidth), unsigned(P::ContentHeight), size[0], size[1], actual_format, stride_);
            return false;
        }
        return true;
    }
    bool events() {
        for (unsigned i = 0; i < 32; ++i) {
            if (api_.screen_get_event(screen_, event_, 0)) return false;
            int type = 0; void* candidate = 0;
            if (api_.screen_get_event_property_iv(event_, Type, &type)) return false;
            if (!type) break;
            if (type != 1 && type != 2 && type != 3 && type != 9) continue;
            if (api_.screen_get_event_property_pv(event_, Window, &candidate) || !candidate || candidate == parent_) continue;
            if (type == 3 && candidate == window_) {
                api_.screen_destroy_window(window_); window_ = 0;
                printf("AA_HW_FAILED stage=renderer_window_closed\n"); return false;
            }
            // Managers can see foreign windows. Require the exact ID that our
            // renderer URL requested, including when CREATE preceded ID setup.
            char id[64] = {};
            if (type == 3 || api_.screen_get_window_property_cv(candidate, IdString, sizeof(id) - 1, id) ||
                strcmp(id, window_id_) || candidate == window_) continue;
            if (window_) { printf("AA_HW_FAILED stage=duplicate_output_window\n"); return false; }
            window_ = candidate; int hidden = 0, z = -1000;
            if (api_.screen_set_window_property_iv(window_, Visible, &hidden) ||
                api_.screen_set_window_property_iv(window_, ZOrder, &z)) return false;
            printf("AA_HW_WINDOW id=%s\n", id);
        }
        return true;
    }

public:
    explicit Session(Api& api) : api_(api), connection_(0), renderer_(0), screen_(0), parent_(0),
        window_(0), event_(0), pixmap_(0), buffer_(0), pixels_(0), endpoint_(-1), stride_(0),
        opened_(false), configured_(false), have_picture_(false), generation_(0),
        snapshots_(0), last_sample_(0),
        last_report_(0), previous_(0), fitted_(0) { name_[0] = 0; }
    ~Session() { close(); }
    void close() {
        if (endpoint_ >= 0) api_.close_endpoint(endpoint_);
        endpoint_ = -1;
        if (renderer_) { api_.mmr_stop(renderer_); api_.mmr_context_destroy(renderer_); renderer_ = 0; }
        if (connection_) { api_.mmr_disconnect(connection_); connection_ = 0; }
        if (window_) { api_.screen_destroy_window(window_); window_ = 0; }
        if (event_) { api_.screen_destroy_event(event_); event_ = 0; }
        if (pixmap_) { api_.screen_destroy_pixmap(pixmap_); pixmap_ = 0; }
        if (parent_) { api_.screen_destroy_window(parent_); parent_ = 0; }
        if (screen_) { api_.screen_destroy_context(screen_); screen_ = 0; }
        free(previous_); free(fitted_); previous_ = fitted_ = 0;
        pixels_ = buffer_ = 0; opened_ = configured_ = have_picture_ = false;
        if (name_[0]) printf("AA_HW_CLEANUP context=%s inputs=%u snapshots=%u\n", name_, progress_.accepted, snapshots_);
        name_[0] = 0;
    }
    bool open() {
        close();
        if (!progress_.ready) return false;
        progress_.reset(); snapshots_ = 0;
        last_sample_ = last_report_ = 0; ++generation_;
        snprintf(name_, sizeof(name_), "kombi_hw_%lu", api_.process_id());
        snprintf(group_, sizeof(group_), "kombi_hw_group_%lu_%u", api_.process_id(), generation_);
        snprintf(endpoint_name_, sizeof(endpoint_name_), "/dev/name/local/%s", name_);
        snprintf(window_id_, sizeof(window_id_), "%lu", 50000ul + api_.process_id() % 10000);
        previous_ = (uint8_t*)malloc(P::ContentWidth * P::ContentHeight * 4);
        fitted_ = (uint8_t*)malloc(P::ContentWidth * P::ContentHeight * 4);
        if (!previous_ || !fitted_ || !make_screen()) return false;
        connection_ = api_.mmr_connect(0);
        if (!connection_) { printf("AA_HW_FAILED stage=connect_existing_renderer\n"); return false; }
        renderer_ = api_.mmr_context_create(connection_, name_, 0, 0600);
        if (!renderer_ || !parameters(0)) return false;
        char url[512];
        snprintf(url, sizeof(url), "screen:/any?winid=%s&nodstviewport=0&dstx=0&dsty=0&dstw=%u&dsth=%u&wingrp=%s",
                 window_id_, unsigned(P::SourceWidth), unsigned(P::SourceHeight), group_);
        int output = api_.mmr_output_attach(renderer_, url, "video");
        if (!ok(output, "output_attach") || !parameters(2, unsigned(output)) || !parameters(1) ||
            !ok(api_.mmr_speed_set(renderer_, 1000), "speed_before_attach")) return false;
        snprintf(url, sizeof(url), "mdevpush:%s?fourcc=H264&queue_capacity=8&video.frame_rate=30&video.depth=16&video.width=%u&video.height=%u",
                 endpoint_name_, unsigned(P::SourceWidth), unsigned(P::SourceHeight));
        if (!ok(api_.mmr_input_attach(renderer_, url, "track"), "input_attach") ||
            !parameters(0) || !parameters(1) || !ok(api_.mmr_speed_set(renderer_, 1000), "speed_before_play") ||
            !ok(api_.mmr_play(renderer_), "play")) return false;
        const uint64_t until = api_.now_us() + 3000000;
        do {
            endpoint_ = api_.open_endpoint(endpoint_name_);
            if (endpoint_ >= 0) break;
            api_.sleep_ms(20);
        } while (api_.now_us() < until);
        if (endpoint_ < 0) { printf("AA_HW_FAILED stage=mdev_endpoint\n"); return false; }
        uint32_t sync = 1, format[] = {2, 0x48323634, P::SourceWidth, P::SourceHeight, 16, 30, 0, 0};
        if (api_.send_packet(endpoint_, 5, &sync, sizeof(sync), 0) ||
            api_.send_packet(endpoint_, 2, format, sizeof(format), 0)) return false;
        opened_ = true;
        printf("AA_HW_READY context=%s endpoint=%s source=%ux%u capture=%ux%u scaling=screen software_fallback=0\n",
               name_, endpoint_name_, unsigned(P::SourceWidth), unsigned(P::SourceHeight),
               unsigned(P::FittedWidth), unsigned(P::ContentHeight));
        return true;
    }
    int decode(const uint8_t* bytes, unsigned size, uint64_t phone_pts, uint8_t*, bool present) {
        unsigned inputs = 0; uint64_t first = 0;
        if (!opened_ || !bytes || !size || size > MaxPayload ||
            !progress_.snapshot(inputs, first)) return -1;
        if (!configured_) {
            uint8_t config[MaxConfig];
            unsigned length = hardware_configuration(bytes, size, config, sizeof(config));
            if (!length || !annexb_idr(bytes, size)) {
                printf("AA_HW_FAILED stage=initial_SPS_PPS_IDR\n"); return -1;
            }
            if (api_.send_packet(endpoint_, 4, config, length, 0)) return -1;
            configured_ = true;
        }
        // Native mdev/GraphClock uses QNX monotonic microseconds, not the
        // phone's clock origin. Phone PTS remains diagnostic only.
        const uint64_t timestamp = api_.now_us();
        int result = api_.send_packet(endpoint_, 3, bytes, size, &timestamp);
        if (result) { printf("AA_HW_FAILED stage=send rc=%d bytes=%u\n", result, size); return -1; }
        const unsigned accepted = progress_.input(timestamp, present);
        if (accepted == 1 || accepted % 300 == 0)
            printf("AA_HW_INPUT accepted=%u phone_pts=%llu bytes=%u\n", accepted,
                   (unsigned long long)phone_pts, size);
        return 0; // Renderer output is asynchronous; poll drains it independently.
    }
    int poll(uint8_t* destination, bool present = true) {
        if (!opened_) return 0;
        unsigned inputs = 0; uint64_t first_input = 0;
        if (!progress_.snapshot(inputs, first_input)) return -1;
        if (!events()) { progress_.fail(); return -1; }
        const uint64_t now = api_.now_us();
        if (!inputs || !present || now - last_sample_ < 66666) return 0; // at most 15 readbacks/s
        last_sample_ = now;
        if (!window_) {
            if (now - first_input < 5000000) return 0;
            printf("AA_HW_FAILED stage=no_output_window\n"); progress_.fail(); return -1;
        }
        int allocation[2] = {}, origin[2] = {}, source[2] = {}, size[2] = {}, format = 0;
        if (api_.screen_get_window_property_iv(window_, BufferSize, allocation) ||
            api_.screen_get_window_property_iv(window_, SourcePosition, origin) ||
            api_.screen_get_window_property_iv(window_, SourceSize, source) ||
            api_.screen_get_window_property_iv(window_, Size, size) ||
            api_.screen_get_window_property_iv(window_, Format, &format) ||
            source[0] != P::SourceWidth || source[1] != P::SourceHeight ||
            size[0] != P::SourceWidth || size[1] != P::SourceHeight ||
            allocation[0] < source[0] || allocation[1] < source[1] ||
            origin[0] < 0 || origin[1] < 0 ||
            origin[0] > allocation[0] - source[0] || origin[1] > allocation[1] - source[1] ||
            (format != 12 && format != 8)) {
            if (!have_picture_ && now - first_input < 5000000) return 0;
            printf("AA_HW_FAILED stage=source_geometry allocation=%dx%d source=%d,%d/%dx%d window=%dx%d format=%d\n",
                   allocation[0], allocation[1], origin[0], origin[1], source[0], source[1], size[0], size[1], format);
            progress_.fail(); return -1;
        }
        // IVA-HD pads its allocation (896x576, source 32,24/800x480 on the car).
        // Read the whole 800x480 WINDOW into the 522x313 pixmap. Screen applies
        // its source viewport, colour conversion and scaling to the destination.
        // read_window rectangles use screen coordinates, not buffer offsets;
        // passing origin here would crop the decoded picture a second time.
        const uint64_t began = api_.now_us();
        for (unsigned y = 0; y < P::ContentHeight; ++y)
            memset((uint8_t*)pixels_ + y * stride_, 0xa5, P::FittedWidth * 4);
        const uint64_t read_started = api_.now_us();
        if (api_.read_picture(window_, buffer_)) {
            if (!have_picture_ && now - first_input < 5000000) return 0;
            printf("AA_HW_FAILED stage=scaled_readback\n"); progress_.fail(); return -1;
        }
        const uint64_t read_finished = api_.now_us();
        bool written = false;
        for (unsigned y = 0; y < P::ContentHeight && !written; ++y)
            for (unsigned x = 0; x < P::FittedWidth * 4; ++x)
                if (((uint8_t*)pixels_)[y * stride_ + x] != 0xa5) { written = true; break; }
        if (!written) {
            if (!have_picture_ && now - first_input < 5000000) return 0;
            printf("AA_HW_FAILED stage=readback_no_pixels\n"); progress_.fail(); return -1;
        }
        const uint64_t pack_started = api_.now_us();
        if (!pack_scaled_hardware_picture((const uint8_t*)pixels_, P::FittedWidth, P::ContentHeight, stride_, fitted_)) return -1;
        const uint64_t fitted = api_.now_us();
        // Space samples from completion. A slow copy must not immediately
        // qualify for another capture just because its start was long ago.
        last_sample_ = fitted;
        ++snapshots_;
        if (snapshots_ == 1 || now - last_report_ >= 5000000) {
            last_report_ = now;
            printf("AA_HW_FRAME snapshots=%u inputs=%u format=%d allocation=%dx%d source=%d,%d/%dx%d capture=%ux%u scaling=screen stride=%d copy_pack_us=%llu readback_us=%llu pack_us=%llu sample_bgra=%02x%02x%02x%02x\n",
                   snapshots_, inputs, format, allocation[0], allocation[1], origin[0], origin[1], source[0], source[1],
                   unsigned(P::FittedWidth), unsigned(P::ContentHeight), stride_, (unsigned long long)(fitted - began),
                   (unsigned long long)(read_finished - read_started), (unsigned long long)(fitted - pack_started),
                   fitted_[(P::ContentWidth + P::PaddingX) * 4], fitted_[(P::ContentWidth + P::PaddingX) * 4 + 1],
                   fitted_[(P::ContentWidth + P::PaddingX) * 4 + 2], fitted_[(P::ContentWidth + P::PaddingX) * 4 + 3]);
        }
        const unsigned bytes = P::ContentWidth * P::ContentHeight * 4;
        if (have_picture_ && !memcmp(previous_, fitted_, bytes)) { progress_.same_picture(); return 0; }
        memcpy(previous_, fitted_, bytes); memcpy(destination, fitted_, bytes); have_picture_ = true;
        return 1;
    }
    unsigned unchanged() const { return progress_.counter(true); }
    unsigned coalesced() const { return progress_.counter(false); }
};
} // namespace hardware
} // namespace aa_cluster
