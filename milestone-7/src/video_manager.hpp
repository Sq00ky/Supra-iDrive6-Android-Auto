// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef VIDEO_MANAGER_HPP
#define VIDEO_MANAGER_HPP

#include "screen_video.h"
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
    bool prepareForSession();
    bool retuneForCarPlay();
    bool decoderInCarPlayMode();
    void fixCarPlayLayer();
    // LayerManager visibility of surface 1280 (shared with the factory CarPlay window): 1 / 0, -1 = unknown.
    static int surface1280Visible();
    bool hasWindow() const { return window_ready_; }

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

    bool createWindow();      // QNX Screen window "1280" (at start, or on the first Android Auto connection)
    bool createDecoder();     // decoder_mutex_ held
    void destroyDecoder();    // decoder_mutex_ held
    void quiesceWorker();
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
    volatile bool window_ready_;    // createWindow() finished (setVisible() is a no-op before)
    std::vector<VideoQueueItem> frame_queue_;
    std::vector<uint8_t> cached_codec_config_;
};

} // namespace nbt

#endif // VIDEO_MANAGER_HPP
