// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef A2I_STREAM_HPP
#define A2I_STREAM_HPP

// Playback / capture streams that follow the factory CarPlay A2I protocol
// (reverse-engineered from libconn_carplay_audio_a2i.so):
//
//   playback:  Open -> [thread] prime every free buffer with silence
//              (GetBuffer until rc==0x14, memset 0, Out_Write)
//              -> Start -> loop { GetNotification; GetBuffer; fill; Out_Write }
//   capture:   Open -> [thread] Start -> loop { GetNotification; GetBuffer; consume }
//   teardown:  stop flag + join; playback = Close only, capture = Stop + Close.
//
// Out_Write is ONLY ever handed a pointer that GetBuffer returned. Passing a
// foreign pointer is the suspected cause of the earlier _list_memalign crashes.

#include "a2i_client.h"
#include "pcm_utils.hpp"
#include <pthread.h>

namespace nbt {

struct A2IStreamConfig {
    const char* endpoint;            // e.g. "/input_stereo_AirPlayMain"
    const char* fallback_endpoint;   // optional, NULL if none
    uint32_t    sample_rate;
    uint32_t    channels;
    uint32_t    buffer_size;         // bytes per A2I buffer (0x400 stereo / 0x200 mono)
    uint32_t    cookie_primary;      // cfg+0x34, tried first
    uint32_t    cookie_alternate;    // tried second, A2I_COOKIE_NONE to disable
    uint32_t    timeout_ms;
};

// Opens an A2I endpoint, trying endpoint x cookie combinations in order.
// Returns the A2I rc of the last attempt (0 on success).
int a2iOpenWithFallback(uint32_t direction, const A2IStreamConfig& cfg,
                        a2i_client_handle_t* handle,
                        const char** used_endpoint, uint32_t* used_cookie);

class A2IPlaybackStream {
public:
    explicit A2IPlaybackStream(const char* log_name, size_t ring_bytes);
    ~A2IPlaybackStream();

    bool open(const A2IStreamConfig& cfg);
    // Spawns the worker (prime -> Start -> feed loop). If feed is false the
    // worker only writes silence (bounded bring-up stage).
    bool start(bool feed);
    void setFeedEnabled(bool feed) { feed_ = feed; }
    void flush() { ring_.clear(); }   // drop queued PCM (pause / stream stop)
    void stop();                      // join + Close (safe to call repeatedly)

    // Queue PCM already converted to the endpoint format. Never blocks.
    void write(const uint8_t* pcm, size_t len);

    bool isOpen() const { return handle_ != NULL; }
    bool isRunning() const { return running_; }
    const char* endpoint() const { return used_endpoint_; }
    uint32_t sampleRate() const { return cfg_.sample_rate; }
    uint32_t channels() const { return cfg_.channels; }
    uint32_t bufferSize() const { return cfg_.buffer_size; }

    // Counters (monitoring / tests)
    uint32_t writes() const { return writes_; }
    uint32_t primed() const { return primed_; }
    uint32_t underruns() const { return underruns_; }

private:
    A2IPlaybackStream(const A2IPlaybackStream&);
    A2IPlaybackStream& operator=(const A2IPlaybackStream&);

    static void* threadStub(void* arg);
    void workerLoop();

    const char* name_;
    A2IStreamConfig cfg_;
    a2i_client_handle_t handle_;
    const char* used_endpoint_;
    uint32_t used_cookie_;
    PcmRing ring_;
    pthread_t thread_;
    bool thread_valid_;
    volatile bool stop_;
    volatile bool running_;
    volatile bool feed_;
    volatile uint32_t writes_;
    volatile uint32_t primed_;
    volatile uint32_t underruns_;
    uint32_t notify_fail_;
    uint32_t getbuf_fail_;
    uint32_t write_fail_;
};

typedef void (*A2ICaptureCallback)(const uint8_t* pcm, size_t size, void* user_data);

class A2ICaptureStream {
public:
    explicit A2ICaptureStream(const char* log_name);
    ~A2ICaptureStream();

    bool open(const A2IStreamConfig& cfg);
    bool start(A2ICaptureCallback cb, void* user_data);
    void stop();                      // join + Stop + Close

    bool isOpen() const { return handle_ != NULL; }
    bool isRunning() const { return running_; }
    uint32_t chunks() const { return chunks_; }

private:
    A2ICaptureStream(const A2ICaptureStream&);
    A2ICaptureStream& operator=(const A2ICaptureStream&);

    static void* threadStub(void* arg);
    void workerLoop();

    const char* name_;
    A2IStreamConfig cfg_;
    a2i_client_handle_t handle_;
    const char* used_endpoint_;
    uint32_t used_cookie_;
    pthread_t thread_;
    bool thread_valid_;
    volatile bool stop_;
    volatile bool running_;
    bool started_;
    A2ICaptureCallback cb_;
    void* cb_user_;
    volatile uint32_t chunks_;
};

} // namespace nbt

#endif // A2I_STREAM_HPP
