// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef VIDEO_MANAGER_HPP
#define VIDEO_MANAGER_HPP

#include "screen_video.h"
#include "mmr_launch.hpp"
#include "mmr_pps.hpp"
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <string>
#include <vector>

namespace nbt {

class VideoManager {
public:
    VideoManager();
    ~VideoManager();

    bool init(uint32_t width = NBT_SCREEN_WIDTH_SUPRA_WIDESCREEN,
              uint32_t height = NBT_SCREEN_HEIGHT_SUPRA_WIDESCREEN,
              uint32_t fps = NBT_SCREEN_MAX_FPS);
    void start();
    void stop();

    // Toggle overlay visibility (bring forward or hide back to native iDrive 6)
    void setVisible(bool visible);
    bool isVisible() const;

    // Call from the AASession end-of-session callback: drops queued frames and the cached SPS/PPS of
    // the finished session and makes the decoder re-sync. Does not touch the screen (setVisible()).
    void endSession();

    // CarPlay hand-off (NBT_CP_HANDOFF, default on; see video_manager.cpp). prepareForSession(): before an Android
    // Auto session starts (creates or rebuilds the decoder context). retuneForCarPlay(): main thread, a while after a
    // session ended. fixCarPlayLayer(): LayerManager layer 0x800d0000 visible and full screen (setVisible(true)).
    static bool cpHandoff();
    // NBT_CP_PURGE=1: destroy the factory CarPlay context (cpvideoctx) as before v1.9.50 instead of detaching it.
    static bool cpPurge();
    bool prepareForSession();
    bool retuneForCarPlay();
    bool decoderInCarPlayMode();
    // Closes the decoder context (and with it mm-renderer's winid=1280 window) while iDrive has the screen. Needed once
    // the volume overlay has drawn: its window then holds LayerManager surface 1280, so hiding the surface no longer
    // hides the video. prepareForSession() builds a new context (a new, newest 1280 window) when Android Auto returns.
    void parkDecoder();
    bool decoderParked();
    // After a parked context is rebuilt the phone is mid-stream: P-frames without their reference decode as grey,
    // jittery, wrong-colour garbage (G30, v1.9.15). Frames are dropped until the first IDR. releaseKeyframeWait()
    // gives up waiting (the phone never sent one).
    bool waitingForKeyframe();
    // A frame was lost on the way to the decoder (queue full, or mdev did not take it) and no keyframe has been queued
    // since: the picture stays damaged until one comes. The main loop asks the phone for it (keyframe_ask.hpp).
    bool keyframeWanted();
    void clearKeyframeWanted();
    // NBT_MMR_WIN_HIDE=1 (experiment): mm-renderer's winid=1280 window joins our window group, so our context gets a
    // SCREEN_EVENT_CREATE with its handle. pollScreenEvents() (main loop) records it; hideVideoWindow() sets it
    // VISIBLE 0 at the background z-order with the decoder left open, setVisible(true) restores what was read.
    void pollScreenEvents();
    bool videoWindowKnown() const { return mmr_win_ != 0; }
    bool hideVideoWindow();
    void releaseKeyframeWait();
    void fixCarPlayLayer();
    // LayerManager visibility of surface 1280 (the factory CarPlay window's): 1 / 0, -1 = unknown.
    static int surface1280Visible();
    // Android Auto's own LayerManager surface (NBT_AA_SURFACE, default 1282). Our window and mm-renderer's output
    // window carry it as their id string, so LayerManager makes the surface when the first of them posts and binds
    // the newest one that posted. 1280 = the shared-with-CarPlay path of v1.9.19 and earlier (surface 1280, its
    // factory geometry restored on every hide, CarPlay's layer 0x800d0000 fixed up on every show).
    static int aaSurface();
    static const char* aaWindowId();
    static bool sharesCarPlaySurface() { return aaSurface() == 1280; }
    // Main loop, every tick: the surface only exists once one of our windows has posted, so a show that ran before
    // that found nothing to show. Re-applies the current visibility the first time the surface is there, and logs
    // LayerManager's view of it and of the Android Auto layer once Android Auto has been on screen with frames.
    void serviceAaSurface();
    bool hasWindow() const { return window_ready_; }
    // The volume pill's own LayerManager surface (VolumeOsd::kSurfaceId once LayerManager made it, 0 = none). While the
    // picture is shown it goes FIRST in layer 0x80000000's render order: on this unit the first surface of a layer's
    // list is drawn on top (osd1281probe, Supra 2026-11-05: {1280, 1281} left the pill under the video, {1281, 1280}
    // showed it over the picture). The hide order (0x100) holds neither, so the pill always leaves with the picture.
    // Main thread only, like setVisible(). applyOverlayOrder() rewrites the order now when the picture is shown
    // (false when it is hidden: setVisible(true) applies it then). The first two-surface order is read back; if
    // LayerManagerControl did not take it, the plain order is restored and overlayOrderFailed() stays true.
    void setOverlaySurface(int id) { overlay_surface_ = id > 0 ? id : 0; }
    int overlaySurface() const { return overlay_surface_; }
    bool applyOverlayOrder();
    bool overlayOrderFailed() const { return overlay_failed_; }
    // The volume overlay must come up after mm-renderer's own winid=1280 window, never before it: on a G30 (v1.9.08)
    // an overlay window created first kept LayerManager surface 1280 and the panel showed only the pill. A new decoder
    // context (each connection that rebuilds it) recreates that window, so the overlay follows these two counters.
    uint32_t decoderGeneration() const { return decoder_gen_; }
    uint32_t framesSinceDecoder() const { return frames_in_gen_; }
    // The Android Auto context and window, so the volume overlay can be a CHILD of the projection window: a window
    // with its own id string never reaches this panel (no LayerManager surface), and one named "1280" *becomes* the
    // projection surface and covers Android Auto (car 2026-11-03, nbt-aad-68).
    screen_context_t screenContext() const { return sink_.ctx; }
    screen_window_t  screenWindow()  const { return sink_.win; }

    // Push raw H.264 NAL units from aasdk
    bool postVideoFrame(const uint8_t* data, size_t length, uint64_t pts = 0);
    bool postCodecConfig(const uint8_t* data, size_t length);
    void onVideoFrameReceived(uint32_t frame_index, size_t byte_len);

private:
    struct VideoQueueItem {
        std::vector<uint8_t> data;
        uint64_t pts;
        bool is_config;
        bool is_idr;
        bool has_inline_config;
        size_t raw_size;
        size_t inline_config_size;
    };

    static void* workerThreadStub(void* arg);
    void workerLoop();

    bool isAccepting();      // takes queue_mutex_
    bool workerRunning();    // takes queue_mutex_

    bool createWindow();      // QNX Screen window aaWindowId() (at start, or on the first Android Auto connection)
    bool createDecoder();     // decoder_mutex_ held
    void destroyDecoder();    // decoder_mutex_ held
    void quiesceWorker();
    bool detachFactoryContext(const char* name);   // decoder_mutex_ held
    bool reattachFactoryContext(const char* why);  // decoder_mutex_ held; our own context already gone
    void* makeDict(const std::vector<PpsAttr>& attrs);
    bool sendMdevSync();
    bool sendMdevFormat(uint32_t width, uint32_t height, uint32_t fps);
    bool sendMdevConfig(const uint8_t* data, size_t length);

    screen_video_sink_t sink_;
    bool is_initialized_;
    bool mdev_initialized_;
    int mdev_fd_;

    void* mmr_lib_;
    void* mmr_cnx_;
    void* mmr_ctxt_;
    int mmr_output_id_;
    int mmr_input_id_;
    mmr::Launcher mmr_launch_;      // the mm-renderer we started (none on a car with CarPlay enabled)

    pthread_t worker_thread_;
    pthread_mutex_t queue_mutex_;
    pthread_cond_t queue_cond_;
    bool worker_running_;           // guarded by queue_mutex_
    bool accepting_;                // guarded by queue_mutex_: false once stop() began / no mdev
    bool reset_pending_;            // guarded by queue_mutex_: endSession() asked for a fresh Sync/Format
    bool worker_busy_;              // guarded by queue_mutex_: the worker is handling a popped item
    void* strm_lib_;
    std::string grp_name_;
    pthread_mutex_t decoder_mutex_; // createDecoder / destroyDecoder / retuneForCarPlay
    bool cp_mode_;                  // guarded by decoder_mutex_: context carries CarPlay's settings
    bool parked_;                   // guarded by decoder_mutex_: context closed while iDrive has the screen
    bool wait_idr_;                 // guarded by queue_mutex_: drop frames until an IDR (rebuilt mid-stream)
    void* screen_ev_;               // main thread only (pollScreenEvents / hideVideoWindow / setVisible)
    void* mmr_win_;                 // mm-renderer's window in our group (from SCREEN_EVENT_CREATE), or 0
    bool mmr_hidden_;               // hideVideoWindow() took it down: setVisible(true) restores
    int mmr_saved_vis_, mmr_saved_z_;
    int screen_events_logged_;
    uint32_t idr_dropped_;          // guarded by queue_mutex_
    bool idr_wanted_;               // guarded by queue_mutex_: a frame was lost and no keyframe was queued since
    volatile uint32_t decoder_gen_;     // +1 per decoder context created
    volatile uint32_t frames_in_gen_;   // frames sent to mdev since the current context was created
    volatile bool window_ready_;    // createWindow() finished (setVisible() is a no-op before)
    int overlay_surface_;           // main thread only: the pill's surface, on top of the picture while shown (0 = none)
    bool overlay_verified_;         // the two-surface render order was read back once
    bool overlay_failed_;           // ... and LayerManagerControl did not take it: not tried again this run
    bool aa_surface_seen_;          // main thread only: LayerManager has surface aaSurface() (it persists for the boot)
    bool aa_surface_logged_;        // ... and its state + the layer's render order were logged once while shown
    unsigned aa_surface_ticks_;     // serviceAaSurface() calls since the window was created
    void* cp_ctx_;                  // decoder_mutex_: our handle on the factory cpvideoctx<pid> (never destroyed)
    bool cp_detached_;              // decoder_mutex_: its input/outputs are detached while Android Auto has mdev
    std::string cp_name_;           // decoder_mutex_: its name
    MmrCtxSnapshot cp_snap_;        // decoder_mutex_: what it had attached (PPS) before we detached it
    void verifyOverlayOrder();
    std::vector<VideoQueueItem> frame_queue_;
    std::vector<uint8_t> cached_codec_config_;
};

} // namespace nbt

#endif // VIDEO_MANAGER_HPP
