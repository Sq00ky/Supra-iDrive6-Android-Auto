// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "video_manager.hpp"
#include "car_config.hpp"
#include "lm_geometry.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>
#include <sys/time.h>
#include <time.h>

// Experiment overrides (defaults keep the proven 1280x480 behaviour):
//   NBT_VID_SRC_H = height declared to mdev (coded stream height = the phone's 1280x720 canvas, default 720)
//   NBT_VID_DST_H = MM-Renderer destination height (720 on the 480-row panel shows the top 480 rows 1:1; 480 squashes the canvas)
static int vidEnvInt(const char* name, int def) {
    const char* e = getenv(name);
    return (e && *e) ? atoi(e) : def;
}

// ---- factory CarPlay geometry of LayerManager surface 1280 (see lm_geometry.hpp) ------------------------------------
static const char* const kLmSaved = "/dev/shmem/nbt-aad-lm1280";
static nbt::LmGeometry g_lmFactory;
static bool g_lmHave = false;

static void captureFactoryGeometry() {
    const nbt::CarConfig& cc = nbt::carConfig();
    const char* from = NULL;
    nbt::LmGeometry g;
    if (nbt::lmLoad(kLmSaved, g)) {
        from = kLmSaved;
    } else {
        char out[4096];
        size_t n = 0;
        FILE* p = popen("/opt/sys/bin/LayerManagerControl get surface 1280 2>&1", "r");
        if (p) {
            n = fread(out, 1, sizeof(out) - 1, p);
            pclose(p);
        }
        out[n] = 0;
        for (char* line = strtok(out, "\n"); line; line = strtok(NULL, "\n"))   // log what the car reports
            if (strstr(line, "region") || strstr(line, "size") || strstr(line, "visib") || strstr(line, "opacity") ||
                strstr(line, "rror") || strstr(line, "not"))
                printf("[VideoManager] LayerManager surface 1280 at start:%s\n", line);
        // re-parse: strtok cut the buffer into lines, so join them back
        for (size_t i = 0; i < n; ++i) if (out[i] == 0) out[i] = '\n';
        g = nbt::lmParseSurface(out);
        // A daemon that died while Android Auto was shown leaves OUR panel geometry behind: never take that for the
        // factory one when carplay.ini says the CarPlay picture is a different size.
        bool ours = g.src.valid && g.src.w == cc.screenW && g.src.h == cc.screenH &&
                    cc.iniW > 0 && (cc.iniW != cc.screenW || cc.iniH != cc.screenH);
        if (g.src.valid && g.dst.valid && !ours) {
            from = "LayerManager";
            nbt::lmSave(kLmSaved, g);
        }
    }
    if (!from && cc.iniW > 0 && cc.iniH > 0) {
        g.src.x = g.src.y = g.dst.x = g.dst.y = 0;
        g.src.w = g.dst.w = cc.iniW;
        g.src.h = g.dst.h = cc.iniH;
        g.src.valid = g.dst.valid = true;
        from = "carplay.ini";
    }
    if (!from) {
        printf("[VideoManager] Factory CarPlay geometry of surface 1280 unknown: left as is when Android Auto hides.\n");
        return;
    }
    g_lmFactory = g;
    g_lmHave = true;
    // Also for the iDrive app: if nbt-aad is killed (SIGKILL) or crashes, it restores this geometry itself.
    if (strcmp(from, kLmSaved) != 0) nbt::lmSave(kLmSaved, g);
    printf("[VideoManager] Factory CarPlay geometry of surface 1280 (%s): source %d,%d %dx%d, destination %d,%d %dx%d; "
           "restored whenever Android Auto hides.\n", from, g.src.x, g.src.y, g.src.w, g.src.h, g.dst.x, g.dst.y, g.dst.w, g.dst.h);
}

// Hands surface 1280 back to the factory CarPlay with its own regions (ours are set again by setVisible(true)).
static void restoreFactoryGeometry() {
    if (!g_lmHave) return;
    char cmd[160];
    snprintf(cmd, sizeof(cmd), "/opt/sys/bin/LayerManagerControl set surface 1280 source region %d %d %d %d 2>/dev/null",
             g_lmFactory.src.x, g_lmFactory.src.y, g_lmFactory.src.w, g_lmFactory.src.h);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "/opt/sys/bin/LayerManagerControl set surface 1280 destination region %d %d %d %d 2>/dev/null",
             g_lmFactory.dst.x, g_lmFactory.dst.y, g_lmFactory.dst.w, g_lmFactory.dst.h);
    system(cmd);
}

namespace nbt {

VideoManager::VideoManager()
    : is_initialized_(false),
      mdev_initialized_(false),
      mdev_fd_(-1),
      mmr_lib_(NULL),
      mmr_cnx_(NULL),
      mmr_ctxt_(NULL),
      mmr_output_id_(-1),
      mmr_input_id_(-1),
      worker_thread_(0),
      worker_running_(false),
      accepting_(false),
      reset_pending_(false),
      worker_busy_(false),
      strm_lib_(NULL),
      cp_mode_(false),
      window_ready_(false) {
    memset(&sink_, 0, sizeof(sink_));
    pthread_mutex_init(&queue_mutex_, NULL);
    pthread_mutex_init(&decoder_mutex_, NULL);
    pthread_cond_init(&queue_cond_, NULL);
}

VideoManager::~VideoManager() {
    stop();
    pthread_mutex_destroy(&queue_mutex_);
    pthread_cond_destroy(&queue_cond_);
}

void* VideoManager::workerThreadStub(void* arg) {
    VideoManager* self = reinterpret_cast<VideoManager*>(arg);
    if (self) {
        self->workerLoop();
    }
    return NULL;
}

// Steps 1-13 of the old init(): QNX Screen context + window "1280" in our window group, one black buffer, hidden.
bool VideoManager::createWindow() {
    if (window_ready_) return true;
    const screen_config_t config = sink_.config;
    const uint32_t width = config.width;
    const uint32_t height = config.height;
    // 1. Create QNX Screen Application Context
    if (screen_create_context(&sink_.ctx, SCREEN_APPLICATION_CONTEXT) != 0) {
        perror("[VideoManager] Failed to create screen context");
        return false;
    }

    // 2. Create Window (SCREEN_APPLICATION_WINDOW = 0)
    if (screen_create_window_type(&sink_.win, sink_.ctx, SCREEN_APPLICATION_WINDOW) != 0) {
        perror("[VideoManager] Failed to create screen window");
        screen_destroy_context(sink_.ctx);
        sink_.ctx = NULL;
        return false;
    }

    // 2b. Query Display and explicitly bind Window to Display 0
    // Solves QNX MM-Renderer error: "screen_get_window_property_pv(SCREEN_PROPERTY_DISPLAY) failed errno = 2"
    int num_displays = 0;
    screen_get_context_property_iv(sink_.ctx, SCREEN_PROPERTY_DISPLAY_COUNT, &num_displays);
    if (num_displays > 0) {
        screen_display_t* displays = new screen_display_t[num_displays];
        screen_get_context_property_pv(sink_.ctx, SCREEN_PROPERTY_DISPLAYS, reinterpret_cast<void**>(displays));
        screen_display_t disp = displays[0];
        screen_set_window_property_pv(sink_.win, SCREEN_PROPERTY_DISPLAY, reinterpret_cast<void**>(&disp));
        delete[] displays;
    }

    // 3. Set Window Group (matches factory BMW CarPlay: "cpvideogrp<pid>")
    screen_create_window_group(sink_.win, grp_name_.c_str());

    // 4. Set Usage matching factory CarPlay/MM-Renderer screen_writer path.
    // CPU-writable/native buffers worked for test patterns, but screen_writer's
    // hardware video path expects a VIDEO usage window for decoded-frame scanout.
    int usage = SCREEN_USAGE_VIDEO;
    screen_set_window_property_iv(sink_.win, SCREEN_PROPERTY_USAGE, &usage);

    // 5. Set Window ID String ("1280", cloned from carplay.ini)
    screen_set_window_property_cv(sink_.win, SCREEN_PROPERTY_ID_STRING,
                                  strlen(config.window_id), config.window_id);

    // 6. Set Source Dimensions & Position (1280x480 at 0, 0)
    int size[2] = { static_cast<int>(width), static_cast<int>(height) };
    screen_set_window_property_iv(sink_.win, SCREEN_PROPERTY_SOURCE_SIZE, size);
    int pos[2] = { 0, 0 };
    screen_set_window_property_iv(sink_.win, SCREEN_PROPERTY_POSITION, pos);

    // 7. Set Pixel Format (RGBA8888 = 8, matching factory CarPlay)
    int format = SCREEN_FORMAT_RGBA8888;
    screen_set_window_property_iv(sink_.win, SCREEN_PROPERTY_FORMAT, &format);

    int z_order = 2000;

    // 8. Allocate 1 Window Render Buffer
    if (screen_create_window_buffers(sink_.win, 1) != 0) {
        perror("[VideoManager] Failed to allocate screen buffer");
        screen_destroy_window(sink_.win);
        screen_destroy_context(sink_.ctx);
        sink_.win = NULL;
        sink_.ctx = NULL;
        return false;
    }

    // 9. Retrieve Render Buffer Handle and direct memory pointer
    screen_get_window_property_pv(sink_.win, SCREEN_PROPERTY_RENDER_BUFFERS, reinterpret_cast<void**>(&sink_.buf));

    void* ptr = NULL;
    screen_get_buffer_property_pv(sink_.buf, SCREEN_PROPERTY_POINTER, &ptr);
    // Factory CarPlay uses screen_fill on the hardware video buffer; CPU pointers
    // can be NULL/invalid for VIDEO usage buffers, so use the blitter path.
    int fill_attribs[] = { 0x0c, 0x01000000, 0 };
    screen_fill(sink_.ctx, sink_.buf, fill_attribs);

    // 11. Post Window FIRST using factory full-buffer flush rect.
    int rect[4] = { 0, 0, 0, 0 };
    errno = 0;
    int post_res = screen_post_window(sink_.win, sink_.buf, 1, rect, 0);

    // 12. Set Visibility/Z-order after the initial post, matching factory ordering. The window starts HIDDEN: it
    //     holds only the black fill until the phone's first frame, and showing it here put a black screen over
    //     iDrive for the whole start-up (mm-renderer attach, Bluetooth, DSI) before the main loop hid it again.
    //     The main loop shows it when a phone session is up (setVisible()). mm-renderer attaches to the hidden
    //     window fine: the NBT_MMR_WAIT_S path has hidden it before the attach since v1.5.9 (car-verified).
    int vis = 0;
    z_order = NBT_SCREEN_ZORDER_BACKGROUND;
    int vis_res = screen_set_window_property_iv(sink_.win, SCREEN_PROPERTY_VISIBLE, &vis);
    screen_set_window_property_iv(sink_.win, SCREEN_PROPERTY_ZORDER, &z_order);

    // 13. Surface 1280 in GENIVI LayerManagerService: leave it invisible and leave the stock iDrive Surface 256 alone.
    //     Surface id "1280" is the window name from carplay.ini (WindowName=1280), not a width, and the factory
    //     CarPlay window uses the same surface: its regions are NOT touched here (setVisible(true) sets ours, hiding
    //     puts the factory ones back), so starting the daemon never changes the CarPlay picture.
    system("/opt/sys/bin/LayerManagerControl set surface 1280 visibility 0 2>/dev/null");
    system("/opt/sys/bin/LayerManagerControl set surface 1280 opacity 1.0 2>/dev/null");
    system("/opt/sys/bin/LayerManagerControl set surface 1280 zorder 2000 2>/dev/null");

    printf("[VideoManager] QNX Screen window prepared (buf: %p, ptr: %p, post: %d, zorder: %d, vis: %d [%d]).\n",
           sink_.buf, ptr, post_res, z_order, vis, vis_res);
    if (post_res != 0) {
        printf("[VideoManager] Warning: screen_post_window errno = %d (%s)\n", errno, strerror(errno));
    }

    window_ready_ = true;
    return true;
}

bool VideoManager::init(uint32_t width, uint32_t height, uint32_t fps) {
    printf("[VideoManager] Initializing QNX Screen context for window '%s' (%ux%u @ %ufps)...\n",
           NBT_SCREEN_WINDOW_ID, width, height, fps);

    screen_config_t config;
    config.width     = width;
    config.height    = height;
    config.fps       = fps;
    config.window_id = NBT_SCREEN_WINDOW_ID;
    config.z_order   = NBT_SCREEN_ZORDER_ACTIVE; // 2000: Forces window above HmiMain3D (order 999)

    sink_.config = config;
    sink_.is_visible = false;
    grp_name_.clear();
    {
        char g[64];
        snprintf(g, sizeof(g), "cpvideogrp%d", getpid());   // window group, matches factory CarPlay "cpvideogrp<pid>"
        grp_name_ = g;
    }
    // Surface 1280's factory CarPlay geometry (read-only) and the repair of what a SIGKILLed daemon left behind.
    captureFactoryGeometry();
    restoreFactoryGeometry();   // no-op normally
    // CarPlay hand-off: the window is created only when an Android Auto phone connects. Its id string "1280" is the
    // one Connectivity's CarPlay window uses, and LayerManager binds surface 1280 to the newest window with that id:
    // created at start ("Start automatically" + an iPhone first after boot), it took surface 1280 away from CarPlay,
    // whose picture then stayed black, and after a Force Stop the surface was left without a window until the next
    // reboot (MINI tester, 2026-10-29). NBT_CP_HANDOFF=0 keeps the old order (window at start).
    if (!cpHandoff()) {
        if (!createWindow()) return false;
    } else {
        printf("[VideoManager] screen window '%s' is created when an Android Auto phone connects (surface 1280 stays "
               "with the factory CarPlay until then)\n", NBT_SCREEN_WINDOW_ID);
    }

    is_initialized_ = true;

    // 14. Preload libstrm.so (RTLD_GLOBAL) so strm_dict_* symbols are available to libmmrndclient.so
    const char* strm_paths[] = {
        "/opt/conn/lib/carplay/libstrm.so.1",
        "/opt/conn/lib/carplay/libstrm.so",
        "/opt/mm/lib/libstrm.so.1",
        "/opt/mm/lib/libstrm.so",
        "/fs/sda0/opt/conn/lib/carplay/libstrm.so.1",
        "/fs/sda0/opt/mm/lib/libstrm.so.1",
        NULL
    };
    void* strm_h = NULL;
    for (int i = 0; strm_paths[i]; ++i) {
        strm_h = dlopen(strm_paths[i], RTLD_NOW | RTLD_GLOBAL);
        if (strm_h) {
            printf("[VideoManager] Preloaded stream dictionary symbols: %s\n", strm_paths[i]);
            break;
        }
    }

    // 15. Initialize MM-Renderer Hardware Decoder Pipeline
    const char* mmr_paths[] = {
        "/opt/conn/lib/carplay/libmmrndclient.so.1",
        "/opt/conn/lib/carplay/libmmrndclient.so",
        "/fs/sda0/opt/conn/lib/carplay/libmmrndclient.so.1",
        "/fs/sda0/opt/conn/lib/carplay/libmmrndclient.so",
        "/fs/sda0/repository/istep/opt/conn/lib/carplay/libmmrndclient.so",
        NULL
    };

    for (int i = 0; mmr_paths[i]; ++i) {
        mmr_lib_ = dlopen(mmr_paths[i], RTLD_NOW);
        if (mmr_lib_) {
            printf("[VideoManager] Loaded MM-Renderer client library: %s\n", mmr_paths[i]);
            break;
        }
    }

    if (mmr_lib_ && strm_h) {
        typedef void* (*mmr_connect_t)(const char*);
        mmr_connect_t fn_connect = (mmr_connect_t)dlsym(mmr_lib_, "mmr_connect");
        strm_lib_ = strm_h;

        if (fn_connect) {
            mmr_cnx_ = fn_connect(NULL);
            if (!mmr_cnx_) {
                // Started from the iDrive entry shortly after boot, mm-renderer may not be up yet: mmr_connect()
                // then fails, no decoder context is created, /dev/name/local/mdev never appears and every frame
                // is dropped (black screen for the whole session, car-seen 2026-10-24). Wait for it, with the
                // overlay hidden so iDrive and the reverse camera stay usable. NBT_MMR_WAIT_S (default 90, 0 = no wait).
                int wait_s = vidEnvInt("NBT_MMR_WAIT_S", 90);
                printf("[VideoManager] mm-renderer not reachable yet (mmr_connect failed, errno=%d): waiting up to %d s, overlay hidden.\n",
                       errno, wait_s);
                fflush(stdout);
                for (int t = 0; !mmr_cnx_ && t < wait_s * 2; ++t) {
                    usleep(500000);
                    mmr_cnx_ = fn_connect(NULL);
                    if (mmr_cnx_) printf("[VideoManager] mm-renderer reachable after %d.%d s.\n", (t + 1) / 2, ((t + 1) % 2) * 5);
                }
                if (!mmr_cnx_) printf("[VideoManager] Error: mm-renderer still unreachable: no hardware decoder, video will stay black.\n");
            }
            if (mmr_cnx_) {
                if (cpHandoff()) {
                    printf("[VideoManager] decoder context is created when an Android Auto phone connects "
                           "(the factory CarPlay keeps its own until then; NBT_CP_HANDOFF=0: create it now)\n");
                } else {
                    pthread_mutex_lock(&decoder_mutex_);
                    createDecoder();
                    pthread_mutex_unlock(&decoder_mutex_);
                }
            }
        }
    } else {
        printf("[VideoManager] Notice: libmmrndclient.so not found via dlopen\n");
    }

    // 16. Start Asynchronous Video Ingestion Worker Thread
    pthread_mutex_lock(&queue_mutex_);
    worker_running_ = true;
    pthread_mutex_unlock(&queue_mutex_);
    if (pthread_create(&worker_thread_, NULL, &VideoManager::workerThreadStub, this) != 0) {
        perror("[VideoManager] Warning: Failed to spawn video worker thread");
        pthread_mutex_lock(&queue_mutex_);
        worker_running_ = false;
        pthread_mutex_unlock(&queue_mutex_);
    } else {
        printf("[VideoManager] Asynchronous video ingestion thread active.\n");
    }
    pthread_mutex_lock(&queue_mutex_);
    accepting_ = (mdev_fd_ >= 0);   // false until the decoder exists (created on connect with the CarPlay hand-off)
    pthread_mutex_unlock(&queue_mutex_);

    printf("[VideoManager] QNX Screen window prepared successfully.\n");
    return true;
}

void VideoManager::start() {
    // Nothing to show yet: the main loop owns visibility and raises the overlay for a phone session.
}

void VideoManager::stop() {
    if (!is_initialized_) return;
    // From here on post*() / onVideoFrameReceived() are no-ops, so a late session thread can
    // neither queue work nor touch the objects destroyed below.
    pthread_mutex_lock(&queue_mutex_);
    accepting_ = false;
    pthread_mutex_unlock(&queue_mutex_);

    if (window_ready_) {   // never created (no Android Auto phone this run): surface 1280 belongs to CarPlay, leave it
        setVisible(false);
        system("/opt/sys/bin/LayerManagerControl set surface 256 visibility 1 2>/dev/null");
    }

    pthread_mutex_lock(&queue_mutex_);
    bool had_worker = worker_running_;
    worker_running_ = false;
    pthread_cond_broadcast(&queue_cond_);
    pthread_mutex_unlock(&queue_mutex_);
    if (had_worker) {
        pthread_join(worker_thread_, NULL);
        worker_thread_ = 0;
    }
    pthread_mutex_lock(&queue_mutex_);
    frame_queue_.clear();
    pthread_mutex_unlock(&queue_mutex_);

    // Reverse order of init(): first the MM-Renderer pipeline (its screen_writer renders into our
    // window group), then the mdev connection, and only then the Screen window/context.
    if (mmr_lib_) {
        typedef int (*mmr_stop_t)(void*);
        typedef int (*mmr_context_destroy_t)(void*);
        typedef void (*mmr_disconnect_t)(void*);
        mmr_stop_t fn_stop = (mmr_stop_t)dlsym(mmr_lib_, "mmr_stop");
        mmr_context_destroy_t fn_destroy = (mmr_context_destroy_t)dlsym(mmr_lib_, "mmr_context_destroy");
        mmr_disconnect_t fn_disconn = (mmr_disconnect_t)dlsym(mmr_lib_, "mmr_disconnect");
        if (mmr_ctxt_) {
            if (fn_stop) fn_stop(mmr_ctxt_);
            if (fn_destroy) fn_destroy(mmr_ctxt_);   // also closes the context handle
        }
        if (fn_disconn && mmr_cnx_) fn_disconn(mmr_cnx_);
        dlclose(mmr_lib_);
        mmr_lib_ = NULL;
        mmr_ctxt_ = NULL;
        mmr_cnx_ = NULL;
    }
    if (mdev_fd_ >= 0) {
        name_close(mdev_fd_);
        mdev_fd_ = -1;
    }
    window_ready_ = false;
    if (sink_.win) {
        screen_destroy_window(sink_.win);
        sink_.win = NULL;
    }
    if (sink_.ctx) {
        screen_destroy_context(sink_.ctx);
        sink_.ctx = NULL;
    }
    sink_.buf = NULL;
    is_initialized_ = false;
    printf("[VideoManager] Video sink destroyed (iDrive restored).\n");
}


// ---- decoder context (MM-Renderer + mdev) ------------------------------------------------------------------------
// Created when an Android Auto phone connects (not at start), so the factory CarPlay keeps its own cpvideoctx until
// Android Auto really needs the decoder: an iPhone connecting first after boot gets the factory picture. Our context
// has to replace cpvideoctx (see the purge below), and the factory cannot create it again for the rest of the boot
// (contextCreate err 17 / contextOpen err 3, MINI tester): from then on CarPlay plays through OUR context and window
// 1280. So when an Android Auto session is over, retuneForCarPlay() gives it CarPlay's factory settings, and the next
// Android Auto connection rebuilds it (prepareForSession): mdev otherwise never answers Message 5 again after CarPlay.
// NBT_CP_HANDOFF=0: the old behaviour (context created at start, kept as is).
bool VideoManager::cpHandoff() {
    const char* e = getenv("NBT_CP_HANDOFF");
    return !(e && e[0] == '0');
}

void VideoManager::quiesceWorker() {
    pthread_mutex_lock(&queue_mutex_);
    accepting_ = false;
    frame_queue_.clear();
    pthread_mutex_unlock(&queue_mutex_);
    for (int i = 0; i < 300; ++i) {                    // a stuck mdev send gives up after 1 s
        pthread_mutex_lock(&queue_mutex_);
        bool busy = worker_busy_;
        pthread_mutex_unlock(&queue_mutex_);
        if (!busy) return;
        usleep(10000);
    }
    printf("[VideoManager] Warning: video worker still busy after 3 s\n");
}

void VideoManager::destroyDecoder() {
    quiesceWorker();
    if (mmr_lib_ && mmr_ctxt_) {
        typedef int (*mmr_stop_t)(void*);
        typedef int (*mmr_context_destroy_t)(void*);
        mmr_stop_t fn_stop = (mmr_stop_t)dlsym(mmr_lib_, "mmr_stop");
        mmr_context_destroy_t fn_destroy = (mmr_context_destroy_t)dlsym(mmr_lib_, "mmr_context_destroy");
        if (fn_stop) fn_stop(mmr_ctxt_);
        if (fn_destroy) fn_destroy(mmr_ctxt_);       // also closes the handle
    }
    mmr_ctxt_ = NULL;
    if (mdev_fd_ >= 0) {
        name_close(mdev_fd_);
        mdev_fd_ = -1;
    }
    mdev_initialized_ = false;
}

bool VideoManager::createDecoder() {
    if (!mmr_lib_ || !strm_lib_ || !mmr_cnx_) return false;
    typedef void* (*mmr_connect_t)(const char*);
    typedef void* (*mmr_context_create_t)(void*, const char*, uint32_t, uint32_t);
    typedef void* (*mmr_context_open_t)(void*, const char*);
    typedef int (*mmr_context_close_t)(void*);
    typedef int (*mmr_context_destroy_t)(void*);
    typedef int (*mmr_context_parameters_t)(void*, void*);
    typedef int (*mmr_output_attach_t)(void*, const char*, const char*);
    typedef int (*mmr_output_parameters_t)(void*, int, void*);
    typedef int (*mmr_input_parameters_t)(void*, void*);
    typedef int (*mmr_input_attach_t)(void*, const char*, const char*);
    typedef int (*mmr_play_t)(void*);
    typedef int (*mmr_speed_set_t)(void*, int);
    typedef int (*mmr_seek_t)(void*, const char*);
    typedef int (*mmr_stop_t)(void*);
    typedef const void* (*mmr_error_info_t)(void*);

    typedef void* (*strm_dict_new_t)();
    typedef void (*strm_dict_destroy_t)(void*);
    typedef void* (*strm_dict_set_t)(void*, const char*, const char*);

    mmr_connect_t fn_connect = (mmr_connect_t)dlsym(mmr_lib_, "mmr_connect");
    mmr_context_create_t fn_ctx_create = (mmr_context_create_t)dlsym(mmr_lib_, "mmr_context_create");
    mmr_context_open_t fn_ctx_open = (mmr_context_open_t)dlsym(mmr_lib_, "mmr_context_open");
    mmr_context_close_t fn_ctx_close = (mmr_context_close_t)dlsym(mmr_lib_, "mmr_context_close");
    mmr_context_destroy_t fn_ctx_destroy = (mmr_context_destroy_t)dlsym(mmr_lib_, "mmr_context_destroy");
    mmr_context_parameters_t fn_ctx_params = (mmr_context_parameters_t)dlsym(mmr_lib_, "mmr_context_parameters");
    mmr_output_attach_t fn_out_attach = (mmr_output_attach_t)dlsym(mmr_lib_, "mmr_output_attach");
    mmr_output_parameters_t fn_out_params = (mmr_output_parameters_t)dlsym(mmr_lib_, "mmr_output_parameters");
    mmr_input_parameters_t fn_in_params = (mmr_input_parameters_t)dlsym(mmr_lib_, "mmr_input_parameters");
    mmr_input_attach_t fn_in_attach = (mmr_input_attach_t)dlsym(mmr_lib_, "mmr_input_attach");
    mmr_play_t fn_play = (mmr_play_t)dlsym(mmr_lib_, "mmr_play");
    mmr_speed_set_t fn_speed_set = (mmr_speed_set_t)dlsym(mmr_lib_, "mmr_speed_set");
    mmr_seek_t fn_seek = (mmr_seek_t)dlsym(mmr_lib_, "mmr_seek");
    mmr_stop_t fn_stop = (mmr_stop_t)dlsym(mmr_lib_, "mmr_stop");
    mmr_error_info_t fn_err = (mmr_error_info_t)dlsym(mmr_lib_, "mmr_error_info");

    strm_dict_new_t fn_dict_new = (strm_dict_new_t)dlsym(strm_lib_, "strm_dict_new");
    strm_dict_destroy_t fn_dict_destroy = (strm_dict_destroy_t)dlsym(strm_lib_, "strm_dict_destroy");
    strm_dict_set_t fn_dict_set = (strm_dict_set_t)dlsym(strm_lib_, "strm_dict_set");

    if (!(fn_ctx_create && fn_out_attach && fn_in_attach && fn_play)) return false;
    (void)fn_connect;
    (void)fn_seek;
    // Purge any stale contexts currently holding /dev/name/local/mdev
    DIR* pps_dir = opendir("/pps/services/multimedia/renderer/context");
    if (pps_dir) {
        struct dirent* de;
        while ((de = readdir(pps_dir)) != NULL) {
            if (de->d_name[0] == '.') continue;
            // The factory CarPlay video engine creates "cpvideoctx<pid>" at boot and keeps it
            // attached to the (single) hardware decoder /dev/name/local/mdev. While it exists our
            // frames are accepted by mdev (ret=0) but are rendered into ITS window group, not
            // ours: nothing appears. The known-good build destroyed it at startup (every run
            // logged "Purging stale MM-Renderer context: cpvideoctx<pid>"). The v1.3.35 audit
            // restricted the purge to our own contexts and thereby broke video.
            // Reclaim: our own leftovers + the factory CarPlay video context. Everything else
            // is only listed (NBT_PURGE_ALL=1 restores the old destroy-everything behaviour).
            bool mine = (strncmp(de->d_name, "aadvideoctx", 11) == 0) ||
                        (strncmp(de->d_name, "cpvideoctx", 10) == 0) ||
                        (getenv("NBT_PURGE_ALL") != NULL);
            if (!mine) {
                printf("[VideoManager] Leaving MM-Renderer context alone: %s\n", de->d_name);
                continue;
            }
            printf("[VideoManager] Purging stale MM-Renderer context: %s\n", de->d_name);
            if (fn_ctx_open) {
                void* old_ctx = fn_ctx_open(mmr_cnx_, de->d_name);
                if (old_ctx) {
                    if (fn_stop) fn_stop(old_ctx);
                    // mmr_context_destroy() also closes the handle; calling
                    // mmr_context_close() on it afterwards is a double close.
                    if (fn_ctx_destroy) fn_ctx_destroy(old_ctx);
                    else if (fn_ctx_close) fn_ctx_close(old_ctx);
                }
            }
        }
        closedir(pps_dir);
    }
    usleep(50000); // Allow QNX kernel to complete unregistration

    // Create unique hardware video context
    char ctx_name[64];
    snprintf(ctx_name, sizeof(ctx_name), "aadvideoctx%d", getpid());
    mmr_ctxt_ = fn_ctx_create(mmr_cnx_, ctx_name, 0, 0x1ff);
    if (mmr_ctxt_) {
        // Disable MMF prefetch for mdevpush live input. In mmr-mmf-routing,
        // FUN_00014a38 skips MmResume() while prefetch is active
        // (prefetch_mode >= 0) until the prefetch state reaches 4. For mdev,
        // that deadlocks because mdev_streamer's NextBuffer will not drain
        // until MmResume() promotes its state from 0x40 to 0x100.
        if (fn_ctx_params && fn_dict_new && fn_dict_set && fn_dict_destroy) {
            void* ctx_dict = fn_dict_new();
            ctx_dict = fn_dict_set(ctx_dict, "prefetch_mode", "-1");
            ctx_dict = fn_dict_set(ctx_dict, "VideoDecoderLowLatencyMode", "1");
            ctx_dict = fn_dict_set(ctx_dict, "ColorSpaceMode", "1");
            int ctx_param_res = ctx_dict ? fn_ctx_params(mmr_ctxt_, ctx_dict) : -1;
            printf("[VideoManager] MMR context parameters (prefetch=-1, lowlat=1, colorspace=1) -> %d\n", ctx_param_res);
            // libmmrndcore takes ownership of the dictionary on success and
            // destroys it internally on failure. Do not destroy it here.
        } else {
            printf("[VideoManager] Warning: MMR context parameter API unavailable; prefetch_mode not set.\n");
        }

        // Step 1: Attach Output to Screen Window 1280 and the exact
        // window group registered above, matching factory CarPlay's
        // libconn_carplay_video_mmr.so behavior.
        char out_url[256];
        snprintf(out_url, sizeof(out_url),
                 "screen:/any?winid=%s&nodstviewport=0&dstx=0&dsty=0&dstw=%d&dsth=%d&wingrp=%s",
                 NBT_SCREEN_WINDOW_ID, vidEnvInt("NBT_VID_DST_W", nbt::mmrDstWidth()),
                 vidEnvInt("NBT_VID_DST_H", nbt::mmrDstHeight()), grp_name_.c_str());
        printf("[VideoManager] MM-Renderer output URL: %s\n", out_url);
        mmr_output_id_ = fn_out_attach(mmr_ctxt_, out_url, "video");

        // Step 2: Configure Output Parameters (Low-latency direct scanout)
        if (fn_out_params && fn_dict_new && fn_dict_set && fn_dict_destroy) {
            void* out_dict = fn_dict_new();
            out_dict = fn_dict_set(out_dict, "mmf.SCREEN_WRITER_DISABLE_CADENCE", "1");
            out_dict = fn_dict_set(out_dict, "mmf.SCREEN_WRITER_DISABLE_MEDIACLOCK", "1");
            out_dict = fn_dict_set(out_dict, "mmf.SCREEN_WRITER_POST_BUFFER_NOSYNC", "1");
            int out_param_res = out_dict ? fn_out_params(mmr_ctxt_, mmr_output_id_, out_dict) : -1;
            printf("[VideoManager] MMR output low-latency params -> %d\n", out_param_res);
            // libmmrndcore owns/destroys the dictionary after this call.
        }

        // Step 3: Configure Input Parameters (TI IVA-HD Low Latency Decoder Mode)
        if (fn_in_params && fn_dict_new && fn_dict_set && fn_dict_destroy) {
            void* in_dict = fn_dict_new();
            in_dict = fn_dict_set(in_dict, "VideoDecoderLowLatencyMode", "1");
            in_dict = fn_dict_set(in_dict, "ColorSpaceMode", "1");
            int in_param_res = in_dict ? fn_in_params(mmr_ctxt_, in_dict) : -1;
            printf("[VideoManager] MMR input decoder params -> %d\n", in_param_res);
            // libmmrndcore owns/destroys the dictionary after this call.
        }

        // Seed context speed BEFORE input attach creates the track engine.
        // mmr-track-engine start reads mmr_context_speed_get() and passes it
        // to the MMF routing rate() callback. If this is left at 0, routing
        // starts mdev at 1000 internally but stores requested speed 0, and
        // its start() path skips MmResume(), leaving mdev stuck at 0x40.
        int seed_speed_res = -9999;
        if (fn_speed_set) {
            seed_speed_res = fn_speed_set(mmr_ctxt_, 1000);
        }

        // Step 4: Attach Input to Hardware Video Push Engine ('H264' FourCC)
        // Match the widescreen Android Auto SDP profile. 1280x480 preserves
        // the car display's 8:3 aspect ratio and maps 1:1 onto the 1280x480 panel.
        char in_url[256];
        snprintf(in_url, sizeof(in_url),
                 "mdevpush:/dev/name/local/mdev?fourcc=H264&queue_capacity=60&video.frame_rate=60&video.depth=16&video.width=1280&video.height=%d",
                 vidEnvInt("NBT_VID_SRC_H", 720));
        mmr_input_id_ = fn_in_attach(mmr_ctxt_, in_url, "track");

        // Re-apply parameters AFTER input_attach creates the track engine.
        // libmmrndcore routes post-attach parameter calls through the engine
        // callbacks; pre-attach PPS/default params returned 0 but slog still
        // showed VideoDecoderLowLatencyMode disabled and prefetch active.
        int post_ctx_param_res = -9999;
        if (fn_ctx_params && fn_dict_new && fn_dict_set && fn_dict_destroy) {
            void* post_ctx_dict = fn_dict_new();
            post_ctx_dict = fn_dict_set(post_ctx_dict, "prefetch_mode", "-1");
            post_ctx_dict = fn_dict_set(post_ctx_dict, "VideoDecoderLowLatencyMode", "1");
            post_ctx_dict = fn_dict_set(post_ctx_dict, "ColorSpaceMode", "1");
            post_ctx_param_res = post_ctx_dict ? fn_ctx_params(mmr_ctxt_, post_ctx_dict) : -1;
        }
        printf("[VideoManager] MMR post-attach context params -> %d\n", post_ctx_param_res);

        int post_in_param_res = -9999;
        if (fn_in_params && fn_dict_new && fn_dict_set && fn_dict_destroy) {
            void* post_in_dict = fn_dict_new();
            post_in_dict = fn_dict_set(post_in_dict, "VideoDecoderLowLatencyMode", "1");
            post_in_dict = fn_dict_set(post_in_dict, "ColorSpaceMode", "1");
            post_in_param_res = post_in_dict ? fn_in_params(mmr_ctxt_, post_in_dict) : -1;
        }
        printf("[VideoManager] MMR post-attach input decoder params -> %d\n", post_in_param_res);

        // Step 5: Seed normal speed BEFORE playback, then start playback.
        // mmr-track-engine's start path reads the context speed and calls
        // the routing plugin's rate callback before start(); setting speed
        // only after mmr_play() reaches a different speed() callback and
        // does not promote mdev_streamer from buffering into playing.
        int pre_speed_res = -9999;
        if (fn_speed_set) {
            pre_speed_res = fn_speed_set(mmr_ctxt_, 1000);
        }
        int play_res = fn_play(mmr_ctxt_);
        int post_speed_res = -9999;
        if (fn_speed_set) {
            post_speed_res = fn_speed_set(mmr_ctxt_, 1000);
        }
        printf("[VideoManager] Hardware decoder pipeline active (ctxt: '%s', out: %d, in: %d, seed-speed: %d, pre-speed: %d, play: %d, post-speed: %d)!\n",
               ctx_name, mmr_output_id_, mmr_input_id_, seed_speed_res, pre_speed_res, play_res, post_speed_res);
        if ((mmr_input_id_ < 0 || play_res < 0) && fn_err) {
            const void* err_info = fn_err(mmr_ctxt_);
            if (err_info) {
                printf("[VideoManager] MM-Renderer error info: %s\n", (const char*)err_info + 0x20);
            }
        }
    } else {
        printf("[VideoManager] Notice: mmr_context_create returned NULL\n");
    }

    // Connect to hardware video engine (/dev/name/local/mdev)
    mdev_fd_ = name_open("/dev/name/local/mdev", 0);
    if (mdev_fd_ >= 0) {
        printf("[VideoManager] Connected to hardware video engine (/dev/name/local/mdev, fd: %d)\n", mdev_fd_);
    } else {
        printf("[VideoManager] Notice: /dev/name/local/mdev not available (errno=%d: %s)\n", errno, strerror(errno));
    }
    pthread_mutex_lock(&queue_mutex_);
    reset_pending_ = true;
    accepting_ = (mdev_fd_ >= 0);
    pthread_mutex_unlock(&queue_mutex_);
    cp_mode_ = false;
    return mmr_ctxt_ != NULL && mdev_fd_ >= 0;
}

bool VideoManager::prepareForSession() {
    if (!is_initialized_) return false;
    if (!window_ready_) {
        printf("[VideoManager] Android Auto phone connected: creating the screen window '%s' now\n", NBT_SCREEN_WINDOW_ID);
        if (!createWindow()) printf("[VideoManager] Error: screen window could not be created: video will stay black\n");
    }
    if (!mmr_cnx_) return false;
    pthread_mutex_lock(&decoder_mutex_);
    bool ok = true;
    if (!mmr_ctxt_) {
        printf("[VideoManager] Android Auto phone connected: creating the decoder context now\n");
        ok = createDecoder();
    } else if (cp_mode_) {
        printf("[VideoManager] Android Auto phone connected: rebuilding the decoder context (it carried CarPlay's settings)\n");
        destroyDecoder();
        usleep(50000);
        ok = createDecoder();
    }
    pthread_mutex_unlock(&decoder_mutex_);
    return ok;
}

// CarPlay's factory settings (cpvideoctx on the MINI F56, carplay.ini 1180x480): input AVC1 at the carplay.ini size
// without queue, output at that size, prefetch 0. Without this CarPlay after Android Auto is zoomed (1280x720 output)
// and lags (60-frame queue). The output can only change with the input detached (else err 7).
bool VideoManager::retuneForCarPlay() {
    if (!is_initialized_ || !cpHandoff()) return false;
    pthread_mutex_lock(&decoder_mutex_);
    if (!mmr_ctxt_ || cp_mode_ || !mmr_lib_ || !strm_lib_) { pthread_mutex_unlock(&decoder_mutex_); return false; }
    typedef int (*mmr_input_detach_t)(void*);
    typedef int (*mmr_output_detach_t)(void*, unsigned);
    typedef int (*mmr_context_parameters_t)(void*, void*);
    typedef int (*mmr_output_attach_t)(void*, const char*, const char*);
    typedef int (*mmr_output_parameters_t)(void*, int, void*);
    typedef int (*mmr_input_parameters_t)(void*, void*);
    typedef int (*mmr_input_attach_t)(void*, const char*, const char*);
    typedef int (*mmr_play_t)(void*);
    typedef void* (*strm_dict_new_t)();
    typedef void* (*strm_dict_set_t)(void*, const char*, const char*);
    mmr_input_detach_t fn_in_detach = (mmr_input_detach_t)dlsym(mmr_lib_, "mmr_input_detach");
    mmr_output_detach_t fn_out_detach = (mmr_output_detach_t)dlsym(mmr_lib_, "mmr_output_detach");
    mmr_context_parameters_t fn_ctx_params = (mmr_context_parameters_t)dlsym(mmr_lib_, "mmr_context_parameters");
    mmr_output_attach_t fn_out_attach = (mmr_output_attach_t)dlsym(mmr_lib_, "mmr_output_attach");
    mmr_output_parameters_t fn_out_params = (mmr_output_parameters_t)dlsym(mmr_lib_, "mmr_output_parameters");
    mmr_input_parameters_t fn_in_params = (mmr_input_parameters_t)dlsym(mmr_lib_, "mmr_input_parameters");
    mmr_input_attach_t fn_in_attach = (mmr_input_attach_t)dlsym(mmr_lib_, "mmr_input_attach");
    mmr_play_t fn_play = (mmr_play_t)dlsym(mmr_lib_, "mmr_play");
    strm_dict_new_t fn_dict_new = (strm_dict_new_t)dlsym(strm_lib_, "strm_dict_new");
    strm_dict_set_t fn_dict_set = (strm_dict_set_t)dlsym(strm_lib_, "strm_dict_set");
    if (!(fn_in_detach && fn_out_detach && fn_out_attach && fn_in_attach && fn_play)) {
        printf("[VideoManager] CarPlay settings: MM-Renderer detach API missing, skipped\n");
        pthread_mutex_unlock(&decoder_mutex_);
        return false;
    }
    const nbt::CarConfig& cc = nbt::carConfig();
    int w = cc.iniW > 0 ? cc.iniW : cc.screenW;
    int h = cc.iniH > 0 ? cc.iniH : cc.screenH;
    // Our mdev connection belongs to the Android Auto stream: CarPlay's sender opens its own.
    quiesceWorker();
    if (mdev_fd_ >= 0) { name_close(mdev_fd_); mdev_fd_ = -1; }
    mdev_initialized_ = false;

    int r_in_det = fn_in_detach(mmr_ctxt_);
    int r_out_det = fn_out_detach(mmr_ctxt_, (unsigned)(mmr_output_id_ >= 0 ? mmr_output_id_ : 0));
    int r_ctx = -9999;
    if (fn_ctx_params && fn_dict_new && fn_dict_set) {
        void* d = fn_dict_new();
        d = fn_dict_set(d, "prefetch_mode", "0");
        r_ctx = d ? fn_ctx_params(mmr_ctxt_, d) : -1;
    }
    char out_url[256];
    snprintf(out_url, sizeof(out_url), "screen:/any?winid=%s&nodstviewport=0&dstx=0&dsty=0&dstw=%d&dsth=%d&wingrp=%s",
             NBT_SCREEN_WINDOW_ID, w, h, grp_name_.c_str());
    int r_out = fn_out_attach(mmr_ctxt_, out_url, "video");
    if (r_out >= 0) mmr_output_id_ = r_out;
    int r_outp = -9999;
    if (fn_out_params && fn_dict_new && fn_dict_set && r_out >= 0) {
        void* d = fn_dict_new();
        d = fn_dict_set(d, "mmf.SCREEN_WRITER_DISABLE_CADENCE", "1");
        d = fn_dict_set(d, "mmf.SCREEN_WRITER_DISABLE_MEDIACLOCK", "1");
        d = fn_dict_set(d, "mmf.SCREEN_WRITER_POST_BUFFER_NOSYNC", "1");
        r_outp = d ? fn_out_params(mmr_ctxt_, r_out, d) : -1;
    }
    int r_inp = -9999;
    if (fn_in_params && fn_dict_new && fn_dict_set) {
        void* d = fn_dict_new();
        d = fn_dict_set(d, "ColorSpaceMode", "1");
        d = fn_dict_set(d, "VideoDecoderLowLatencyMode", "1");
        r_inp = d ? fn_in_params(mmr_ctxt_, d) : -1;
    }
    char in_url[256];
    snprintf(in_url, sizeof(in_url),
             "mdevpush:/dev/name/local/mdev?fourcc=AVC1&video.frame_rate=60&video.depth=16&video.width=%d&video.height=%d", w, h);
    int r_in = fn_in_attach(mmr_ctxt_, in_url, "track");
    if (r_in >= 0) mmr_input_id_ = r_in;
    int r_play = fn_play(mmr_ctxt_);
    cp_mode_ = true;
    printf("[VideoManager] decoder context set to CarPlay's settings %dx%d (input detach %d, output detach %d, prefetch 0 %d, "
           "output %d, out params %d, in params %d, input %d, play %d): CarPlay can use it; the next Android Auto "
           "connection rebuilds it\n", w, h, r_in_det, r_out_det, r_ctx, r_out, r_outp, r_inp, r_in, r_play);
    pthread_mutex_unlock(&decoder_mutex_);
    return r_out >= 0 && r_in >= 0;
}

bool VideoManager::decoderInCarPlayMode() {
    pthread_mutex_lock(&decoder_mutex_);
    bool c = cp_mode_;
    pthread_mutex_unlock(&decoder_mutex_);
    return c;
}

// LayerManager layer 0x800d0000 holds surface 1280 once CarPlay has been used since boot. After a CarPlay session
// iDrive leaves it at visibility 0 and offset (x=54 y=-46 on the MINI F56): Android Auto then stays black or only
// covers part of the screen. Not present before the first CarPlay use (the command then just fails).
void VideoManager::fixCarPlayLayer() {
    const nbt::CarConfig& cc = nbt::carConfig();
    char cmd[160];
    snprintf(cmd, sizeof(cmd), "/opt/sys/bin/LayerManagerControl set layer 0x800d0000 source region 0 0 %d %d >/dev/null 2>&1",
             cc.screenW, cc.screenH);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "/opt/sys/bin/LayerManagerControl set layer 0x800d0000 destination region 0 0 %d %d >/dev/null 2>&1",
             cc.screenW, cc.screenH);
    system(cmd);
    system("/opt/sys/bin/LayerManagerControl set layer 0x800d0000 visibility 1 >/dev/null 2>&1");
}

// ---- session lifecycle ------------------------------------------------------------------------
// Called from the AASession end-of-session callback (session thread). Memory-only and quick: it never
// touches the screen (setVisible() runs 3-5 system() calls and is owned by the main thread).
void VideoManager::endSession() {
    if (!is_initialized_) return;
    pthread_mutex_lock(&queue_mutex_);
    frame_queue_.clear();            // frames of the finished session must never reach the decoder
    cached_codec_config_.clear();    // the next session brings its own SPS/PPS
    reset_pending_ = true;           // worker redoes Sync/Format before the next frame it sends
    pthread_mutex_unlock(&queue_mutex_);
}

bool VideoManager::isAccepting() {
    pthread_mutex_lock(&queue_mutex_);
    bool a = accepting_;
    pthread_mutex_unlock(&queue_mutex_);
    return a;
}

bool VideoManager::workerRunning() {
    pthread_mutex_lock(&queue_mutex_);
    bool r = worker_running_;
    pthread_mutex_unlock(&queue_mutex_);
    return r;
}

void VideoManager::setVisible(bool visible) {
    sink_.is_visible = visible;
    // No window yet (CarPlay hand-off, no Android Auto phone so far): there is nothing of ours to show or hide, and
    // every LayerManager call below would act on the factory CarPlay's surface 1280.
    if (!window_ready_) return;
    int32_t z_order = visible ? NBT_SCREEN_ZORDER_ACTIVE : NBT_SCREEN_ZORDER_BACKGROUND;
    int vis = visible ? 1 : 0;
    if (sink_.win) {
        screen_set_window_property_iv(sink_.win, SCREEN_PROPERTY_ZORDER, &z_order);
        screen_set_window_property_iv(sink_.win, SCREEN_PROPERTY_VISIBLE, &vis);
    }
    // All LayerManager steps in ONE shell. Each system() is /bin/sh plus LayerManagerControl, and both load the
    // inherited LD_PRELOAD graphics stack (one 'unknown symbol: kdMain' per process): 5-7 shells per change made the
    // driving-mode step-aside visibly late (Supra, 2026-10-29). What the driver sees goes first; the CarPlay
    // bookkeeping (layer 0x800d0000, factory geometry of surface 1280) runs after the switch.
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    const char* const L = "/opt/sys/bin/LayerManagerControl";
    char cmd[768];
    if (visible) {
        const nbt::CarConfig& cc = nbt::carConfig();
        snprintf(cmd, sizeof(cmd),
                 "%s set layer 0x80000000 render order 0x500 2>/dev/null; "
                 "%s set surface 1280 source region 0 0 %d %d 2>/dev/null; "
                 "%s set surface 1280 destination region 0 0 %d %d 2>/dev/null; "
                 "%s set surface 256 visibility 0 2>/dev/null; "
                 "%s set surface 1280 visibility 1 2>/dev/null",
                 L, L, cc.screenW, cc.screenH, L, cc.screenW, cc.screenH, L, L);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "%s set layer 0x80000000 render order 0x100 2>/dev/null; "
                 "%s set surface 256 visibility 1 2>/dev/null; "
                 "%s set surface 1280 visibility 0 2>/dev/null",
                 L, L, L);
    }
    system(cmd);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ms = (long)(t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    printf("[VideoManager] Screen overlay visibility changed: %s (Z-Order: %d, %ld ms)\n",
           visible ? "VISIBLE" : "HIDDEN", z_order, ms);
    if (visible) fixCarPlayLayer();
    else restoreFactoryGeometry();   // the factory CarPlay shares surface 1280: give it its own regions back
}

int VideoManager::surface1280Visible() {
    FILE* p = popen("/opt/sys/bin/LayerManagerControl get surface 1280 2>&1", "r");
    if (!p) return -1;
    char line[256];
    int vis = -1;
    while (fgets(line, sizeof(line), p)) {
        const char* k = strstr(line, "visibility:");
        if (k && vis < 0) {
            k += 11;
            while (*k == ' ' || *k == '\t') ++k;
            if (*k == '0' || *k == '1') vis = *k - '0';
        }
    }
    pclose(p);
    return vis;
}

bool VideoManager::isVisible() const {
    return sink_.is_visible;
}

// MsgSendvs_r with a bounded wait: an mdev server that stops replying must not be able to wedge the
// worker (and with it stop()'s pthread_join) forever. Same mechanism the frame path already uses.
static int mdevSendTimed(int coid, const iov_t* iov, int parts) {
    uint64_t timeout_ns = 1000000000ULL; // 1 s
    TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_SEND | _NTO_TIMEOUT_REPLY, NULL, &timeout_ns, NULL);
    return MsgSendvs_r(coid, iov, parts, NULL, 0);
}

bool VideoManager::sendMdevSync() {
    if (mdev_fd_ < 0) return false;
    struct MdevHeader {
        uint32_t rsvd0, rsvd1, rsvd2, rsvd3;
        uint32_t msg_code;
        uint32_t payload_len;
    } hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.msg_code = 5; // Message 5: Sync / Reset
    hdr.payload_len = 4;
    uint32_t val = 1;

    iov_t iov[2];
    SETIOV(&iov[0], &hdr, sizeof(hdr));
    SETIOV(&iov[1], &val, sizeof(val));

    for (int retry = 0; retry < 51 && workerRunning(); ++retry) {
        int ret = mdevSendTimed(mdev_fd_, iov, 2);
        if (ret == 0) {
            printf("[VideoManager] >>> mdev Message 5 (Sync) accepted!\n");
            return true;
        }
        if (ret == -ETIMEDOUT) break;   // server stalled: do not spin 51 x 1 s, let the next frame retry
        usleep(10000);
    }
    printf("[VideoManager] Warning: mdev Message 5 timed out\n");
    return false;
}

bool VideoManager::sendMdevFormat(uint32_t width, uint32_t height, uint32_t fps) {
    if (mdev_fd_ < 0) return false;
    struct MdevHeader {
        uint32_t rsvd0, rsvd1, rsvd2, rsvd3;
        uint32_t msg_code;
        uint32_t payload_len;
    } hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.msg_code = 2; // Message 2: Format / Geometry
    hdr.payload_len = 32;

    struct FormatPayload {
        uint32_t magic;    // 2
        uint32_t fourcc;   // 'H264' (0x48323634)
        uint32_t width;    // 1280
        uint32_t height;   // 480 for widescreen AA profile
        uint32_t depth;    // 16
        uint32_t fps;      // 60
        uint32_t rsvd0;    // 0
        uint32_t rsvd1;    // 0
    } fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.magic = 2;
    fmt.fourcc = 0x48323634; // 'H264' (selects init_parser_h264 in ivahd_video_decoder)
    fmt.width = width;
    fmt.height = height;
    fmt.depth = 16;
    fmt.fps = fps;

    iov_t iov[2];
    SETIOV(&iov[0], &hdr, sizeof(hdr));
    SETIOV(&iov[1], &fmt, sizeof(fmt));

    for (int retry = 0; retry < 51 && workerRunning(); ++retry) {
        int ret = mdevSendTimed(mdev_fd_, iov, 2);
        if (ret == 0) {
            printf("[VideoManager] >>> mdev Message 2 (Format %ux%u@%u 'H264') accepted!\n",
                   width, height, fps);
            return true;
        }
        if (ret == -ETIMEDOUT) break;   // server stalled: do not spin 51 x 1 s, let the next frame retry
        usleep(10000);
    }
    printf("[VideoManager] Warning: mdev Message 2 timed out\n");
    return false;
}

bool VideoManager::sendMdevConfig(const uint8_t* data, size_t length) {
    if (mdev_fd_ < 0 || !data || length == 0) return false;
    struct MdevHeader {
        uint32_t rsvd0, rsvd1, rsvd2, rsvd3;
        uint32_t msg_code;
        uint32_t payload_len;
    } hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.msg_code = 4; // Message 4: Codec Config (SPS / PPS)
    hdr.payload_len = static_cast<uint32_t>(length);

    iov_t iov[2];
    SETIOV(&iov[0], &hdr, sizeof(hdr));
    SETIOV(&iov[1], const_cast<uint8_t*>(data), length);

    for (int retry = 0; retry < 51 && workerRunning(); ++retry) {
        int ret = mdevSendTimed(mdev_fd_, iov, 2);
        if (ret == 0) {
            printf("[VideoManager] >>> mdev Message 4 (Codec Config %u bytes) accepted!\n",
                   static_cast<unsigned int>(length));
            return true;
        }
        if (ret == -ETIMEDOUT) break;   // server stalled: do not spin 51 x 1 s, let the next frame retry
        usleep(10000);
    }
    printf("[VideoManager] Warning: mdev Message 4 timed out\n");
    return false;
}

// Debug aid: NBT_VIDEO_DUMP=1 (or a file name) writes the phone's raw H.264 (SPS/PPS + the first frames) to a file
// so the real canvas size, cropping and UI layout can be inspected offline with ffprobe/ffmpeg.
static FILE* g_dump = NULL;
static int g_dumpState = 0;        // 0 = not yet decided, 1 = active, 2 = off
static size_t g_dumpBytes = 0;
static int g_dumpFrames = 0;
static void dumpVideo(const uint8_t* d, size_t n, bool is_config) {
    if (g_dumpState == 0) {
        const char* e = getenv("NBT_VIDEO_DUMP");
        if (!e || !*e) { g_dumpState = 2; return; }
        const char* path = (e[0] == '1' && e[1] == 0) ? "/tmp/aa_video.h264" : e;
        g_dump = fopen(path, "wb");
        g_dumpState = g_dump ? 1 : 2;
        printf("[VideoManager] video dump to %s: %s\n", path, g_dump ? "enabled" : "OPEN FAILED");
    }
    if (g_dumpState != 1 || !g_dump) return;
    if (g_dumpBytes + n > (size_t)4 * 1024 * 1024 || g_dumpFrames > 150) {
        fclose(g_dump); g_dump = NULL; g_dumpState = 2;
        printf("[VideoManager] video dump complete (%d frames, %u bytes)\n", g_dumpFrames, (unsigned)g_dumpBytes);
        return;
    }
    fwrite(d, 1, n, g_dump);
    fflush(g_dump);
    g_dumpBytes += n;
    if (!is_config) ++g_dumpFrames;
}

bool VideoManager::postCodecConfig(const uint8_t* data, size_t length) {
    if (!data || length == 0) return false;
    pthread_mutex_lock(&queue_mutex_);
    if (!accepting_) { pthread_mutex_unlock(&queue_mutex_); return false; }
    cached_codec_config_.assign(data, data + length);
    printf("[VideoManager] Cached codec config for in-band prepend (%u bytes):",
           static_cast<unsigned int>(length));
    for (size_t i = 0; i < length && i < 48; ++i) printf(" %02x", data[i]);
    printf("\n");
    dumpVideo(data, length, true);
    VideoQueueItem item;
    item.data.assign(data, data + length);
    item.pts = 0;
    item.is_config = true;
    item.is_idr = false;
    item.has_inline_config = false;
    item.raw_size = length;
    item.inline_config_size = 0;
    frame_queue_.push_back(item);
    pthread_cond_signal(&queue_cond_);
    pthread_mutex_unlock(&queue_mutex_);
    return true;
}

// True if one of the first NAL units of this Annex-B access unit is an IDR slice (type 5).
static bool looksLikeIdr(const uint8_t* d, size_t n) {
    size_t lim = n < 128 ? n : 128;
    for (size_t i = 0; i + 3 < lim; ++i) {
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
            if ((d[i + 3] & 0x1F) == 5) return true;
            i += 2;
        }
    }
    return false;
}

bool VideoManager::postVideoFrame(const uint8_t* data, size_t length, uint64_t pts) {
    if (!data || length == 0) return false;

    // Non-blocking enqueue: network thread pushes frame and returns immediately!
    pthread_mutex_lock(&queue_mutex_);
    if (!accepting_) { pthread_mutex_unlock(&queue_mutex_); return false; }
    if (frame_queue_.size() >= 15) {
        bool dropped = false;
        size_t dropped_size = 0;
        for (std::vector<VideoQueueItem>::iterator it = frame_queue_.begin(); it != frame_queue_.end(); ++it) {
            if (!it->is_config && !it->is_idr) {   // never sacrifice a keyframe
                dropped_size = it->data.size();
                frame_queue_.erase(it);
                dropped = true;
                break;
            }
        }
        if (!dropped) {
            printf("[VideoManager] Warning: video queue full of codec config packets; rejecting %u byte frame.\n",
                   static_cast<unsigned int>(length));
            pthread_mutex_unlock(&queue_mutex_);
            return false;
        }
        printf("[VideoManager] Warning: video queue full; dropped oldest non-config frame (%u bytes), queued=%u.\n",
               static_cast<unsigned int>(dropped_size),
               static_cast<unsigned int>(frame_queue_.size()));
    }
    dumpVideo(data, length, false);
    VideoQueueItem item;
    item.raw_size = length;
    item.inline_config_size = cached_codec_config_.size();
    item.has_inline_config = item.inline_config_size != 0;
    if (item.has_inline_config) {
        item.data.resize(item.inline_config_size + length);
        memcpy(&item.data[0], &cached_codec_config_[0], item.inline_config_size);
        memcpy(&item.data[item.inline_config_size], data, length);
    } else {
        item.data.assign(data, data + length);
    }
    item.pts = pts;
    item.is_config = false;
    item.is_idr = looksLikeIdr(data, length);
    frame_queue_.push_back(item);
    pthread_cond_signal(&queue_cond_);
    pthread_mutex_unlock(&queue_mutex_);
    return true;
}

void VideoManager::workerLoop() {
    uint32_t frame_count = 0;
    for (;;) {
        VideoQueueItem item;
        pthread_mutex_lock(&queue_mutex_);
        worker_busy_ = false;
        while (worker_running_ && frame_queue_.empty()) {
            pthread_cond_wait(&queue_cond_, &queue_mutex_);
        }
        if (!worker_running_) {
            pthread_mutex_unlock(&queue_mutex_);
            break;
        }
        item = frame_queue_.front();
        frame_queue_.erase(frame_queue_.begin());
        size_t queued_after_pop = frame_queue_.size();
        bool do_reset = reset_pending_;
        reset_pending_ = false;
        worker_busy_ = true;
        pthread_mutex_unlock(&queue_mutex_);
        if (do_reset) mdev_initialized_ = false;   // endSession() ran: redo Sync/Format first

        if (mdev_fd_ >= 0 && !item.data.empty()) {
            if (item.is_config) {
                // v1.3.14: send Message 2 again even though the URL declares the same
                // final H264 geometry. v1.3.13 avoided Message 2 and mdev never logged
                // mdemux_process_packet; the format packet is our only observed trigger
                // into that dispatcher, even when it is later reported unchanged.
                bool ok = sendMdevSync() &&
                          sendMdevFormat(1280, (uint32_t)vidEnvInt("NBT_VID_SRC_H", 720), 60) &&
                          sendMdevConfig(&item.data[0], item.data.size());
                mdev_initialized_ = ok;
                if (!ok) {
                    printf("[VideoManager] Warning: mdev initialization handshake failed; frames will wait for retry.\n");
                }
            } else {
                if (!mdev_initialized_) {
                    bool ok = sendMdevSync() && sendMdevFormat(1280, (uint32_t)vidEnvInt("NBT_VID_SRC_H", 720), 60);
                    mdev_initialized_ = ok;
                    if (!ok) {
                        printf("[VideoManager] Warning: mdev format handshake failed; dropping frame until retry.\n");
                        continue;
                    }
                }

                struct MdevVideoHeader {
                    uint32_t rsvd0;
                    uint32_t rsvd1;
                    uint32_t rsvd2;
                    uint32_t rsvd3;
                    uint32_t msg_code;
                    uint32_t payload_len;
                } hdr;

                memset(&hdr, 0, sizeof(hdr));
                hdr.msg_code = 3; // 3 = MDEV_PACKET_DATA
                hdr.payload_len = static_cast<uint32_t>(item.data.size() + 8);

                // Always calculate microsecond PTS matching local QNX CLOCK_MONOTONIC timebase
                // (Matches mdev_streamer FUN_00015828 clock_gettime(2, &ts) / GraphClock)
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                uint64_t pts = static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + ts.tv_nsec / 1000ULL;

                iov_t iov[3];
                SETIOV(&iov[0], &hdr, sizeof(hdr));
                SETIOV(&iov[1], &pts, sizeof(pts));
                SETIOV(&iov[2], &item.data[0], item.data.size());

                uint32_t next_frame = frame_count + 1;
                if (next_frame <= 35 || (next_frame % 30) == 0) {
                    printf("[VideoManager] mdev frame #%u BEGIN (%u bytes%s, raw=%u, cfg=%u, queue_after_pop=%u)\n",
                           next_frame,
                           static_cast<unsigned int>(item.data.size()),
                           item.has_inline_config ? " incl-config" : "",
                           static_cast<unsigned int>(item.raw_size),
                           static_cast<unsigned int>(item.inline_config_size),
                           static_cast<unsigned int>(queued_after_pop));
                }

                int ret = -1;
                int err = 0;
                uint64_t start_us = pts;
                // One bounded send attempt per frame. In v1.3.12 a full downstream mdev
                // queue made retries hold the worker for ~100s and starve fresh frames;
                // if mdev returns EBUSY, drop this frame and move on.
                for (int retry = 0; retry < 1; ++retry) {
                    uint64_t send_timeout_ns = 250000000ULL; // 250ms
                    TimerTimeout(CLOCK_MONOTONIC,
                                 _NTO_TIMEOUT_SEND | _NTO_TIMEOUT_REPLY,
                                 NULL, &send_timeout_ns, NULL);
                    errno = 0;
                    ret = MsgSendvs_r(mdev_fd_, iov, 3, NULL, 0);
                    if (ret == 0) break;
                    err = (ret < 0) ? -ret : errno;
                    break;
                }
                struct timespec ts_done;
                clock_gettime(CLOCK_MONOTONIC, &ts_done);
                uint64_t done_us = static_cast<uint64_t>(ts_done.tv_sec) * 1000000ULL + ts_done.tv_nsec / 1000ULL;
                uint64_t elapsed_us = (done_us >= start_us) ? (done_us - start_us) : 0;

                frame_count++;
                if (ret != 0 || frame_count <= 35 || (frame_count % 30) == 0) {
                    printf("[VideoManager] mdev frame #%u END (%u bytes%s, raw=%u, cfg=%u, pts=%llu, elapsed=%llu us) -> MsgSendvs_r ret=%d (status: %s)\n",
                           frame_count, static_cast<unsigned int>(item.data.size()),
                           item.has_inline_config ? " incl-config" : "",
                           static_cast<unsigned int>(item.raw_size),
                           static_cast<unsigned int>(item.inline_config_size),
                           static_cast<unsigned long long>(pts),
                           static_cast<unsigned long long>(elapsed_us), ret, strerror(err));
                }
            }
        }
    }
}

void VideoManager::onVideoFrameReceived(uint32_t frame_index, size_t byte_len) {
    // When MM-Renderer hardware decoder is active, it manages direct scanout to Window 1280.
    // Do not draw software overlays on top of the hardware decoder scanout!
    if (mmr_ctxt_) return;
    if (!isAccepting()) return;   // stop() in progress / done: window and context are going away

    if (!sink_.buf || !sink_.win) return;
    void* ptr = NULL;
    screen_get_buffer_property_pv(sink_.buf, SCREEN_PROPERTY_POINTER, &ptr);
    if (!ptr) return;

    // Software test pattern fallback only if hardware decoder is not active
    uint32_t* pixels = reinterpret_cast<uint32_t*>(ptr);
    uint32_t pulse_color = ((frame_index / 10) % 2 == 0) ? 0xFF00E5FF : 0xFF2979FF;

    // Window buffer is panel-sized (carConfig): index with the panel width, never a fixed 1280.
    const int pw = nbt::carConfig().screenW, ph = nbt::carConfig().screenH;
    int bar_w = pw / 3;
    int bar_h = 40;
    int start_x = (pw - bar_w) / 2;
    int start_y = (ph - bar_h) / 2;

    for (int y = start_y; y < start_y + bar_h; ++y) {
        for (int x = start_x; x < start_x + bar_w; ++x) {
            pixels[y * pw + x] = pulse_color;
        }
    }

    int dirty_rect[4] = { start_x, start_y, bar_w, bar_h };
    screen_post_window(sink_.win, sink_.buf, 1, dirty_rect, 0);
}

} // namespace nbt
