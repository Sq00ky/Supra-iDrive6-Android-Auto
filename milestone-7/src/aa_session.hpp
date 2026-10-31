// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AA_SESSION_HPP
#define AA_SESSION_HPP

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <map>
#include <pthread.h>
#include "aa_cryptor.hpp"
#include "touch_types.hpp"

namespace nbt {

typedef void (*SessionEndedCallback)(void* user_data);
typedef void (*VideoFrameCallback)(uint32_t frame_index, const uint8_t* nal_data, size_t nal_len, uint64_t pts, void* user_data);
typedef void (*CodecConfigCallback)(const uint8_t* config_data, size_t config_len, void* user_data);
// Android Auto PCM (ch 8 speech / ch 9 media, message 0x0000 = 8-byte PTS + PCM) and stream state.
typedef void (*AudioFrameCallback)(uint8_t channel, const uint8_t* pcm, size_t pcm_len, uint64_t pts, void* user_data);
typedef void (*AudioStateCallback)(uint8_t channel, bool started, void* user_data);
// Phone asked for video focus (VideoFocusRequest 0x8007 on the video channel): projected=false is the
// "exit to car" icon (focus_mode UNFOCUSED/NATIVE), projected=true asks to come back. reason = focus_reason.
typedef void (*VideoFocusCallback)(bool projected, uint32_t reason, void* user_data);
// Microphone channel (ch 14): the phone opens (Assistant / voice search) or closes the car mic. Session thread.
typedef void (*MicRequestCallback)(bool open, void* user_data);

class AASession {
public:
    AASession();
    ~AASession();

    bool start(int client_fd);
    void stop();
    void setDisconnectCallback(SessionEndedCallback cb, void* user_data = NULL);
    void setVideoFrameCallback(VideoFrameCallback cb, void* user_data = NULL);
    void setCodecConfigCallback(CodecConfigCallback cb, void* user_data = NULL);
    void setAudioFrameCallback(AudioFrameCallback cb, void* user_data = NULL);
    void setAudioStateCallback(AudioStateCallback cb, void* user_data = NULL);
    void setVideoFocusCallback(VideoFocusCallback cb, void* user_data = NULL);
    void setMicRequestCallback(MicRequestCallback cb, void* user_data = NULL);
    bool isActive() const { return isRunning(); }   // true from start() until the session ended or stop()
    // Touch forwarding (default on, NBT_TOUCH_FWD=0 disables): thread-safe, called from the touch thread.
    // Events are queued and sent by the session thread.
    void postTouchEvent(const TouchEventOut& ev);
    // iDrive controller (default on, NBT_KNOB=0 disables): hard keys / tilt / push as Android key events, rotary as a
    // relative event (keycode 65536 ROTARY_CONTROLLER). Thread-safe; sent on input channel 7.
    void postKeyEvent(uint32_t keycode, bool down);
    void postRelativeEvent(uint32_t keycode, int delta);
    // Tell the phone the car took (projected=false) or gave back (projected=true) the screen: an unrequested
    // VideoFocusIndication, sent by the session thread. Thread-safe. Needs the wake pipe (touch forwarding on).
    void postVideoFocus(bool projected);
    // Car day/night (iDrive's day/night change-over, via the iDrive app): 1 night, 0 day. Kept across sessions;
    // sent to the phone as a NIGHT_MODE sensor event once it has started that sensor. NBT_NIGHT_MODE=0/1 overrides.
    // Thread-safe.
    void postNightMode(bool night);
    // Car microphone PCM (16 kHz mono S16LE) for the phone. Thread-safe (capture thread); queued and sent on
    // ch 14 by the session thread while the phone has the mic open, dropped otherwise.
    void postMicData(const uint8_t* pcm, size_t len);
    bool micOpen();

private:
    static void* sessionThreadStub(void* arg);
    void sessionLoop();

    bool sendFrame(uint8_t channel, uint8_t flags, const uint8_t* payload, size_t length);
    bool sendAll(const uint8_t* data, size_t length);
    bool sendSslHandshake(const std::vector<uint8_t>& tls_bytes);
    bool sendAuthComplete();
    bool sendServiceDiscoveryResponse();
    bool sendChannelOpenRequest(uint8_t channel_id);
    bool sendAvSetupResponse(uint8_t channel);
    bool sendVideoFocusIndication(uint8_t channel, bool focused = true, bool unrequested = false);
    bool sendAvMediaAck(uint8_t channel, uint32_t count);
    bool sendAudioFocusResponse(uint8_t focus_state);
    bool readExact(uint8_t* buffer, size_t length);
    static uint32_t rxTimeoutMs();
    bool isRunning() const;
    void setRunning(bool v);
    bool exchangeRunningOff();   // clear the flag, return the previous value
    void wakeSession();      // shutdown(fd) so a blocked recv() returns
    void releaseSocket();    // session thread only: take + close the fd
    void finishSession();
    void drainTouch();                       // session thread only
    bool sendTouchEvent(const TouchEventOut& ev);  // session thread only
    struct KeyOut { uint32_t keycode; int down; int delta; bool rel; };
    bool sendKeyEvent(const KeyOut& k);            // session thread only
    std::vector<KeyOut> key_queue_;                // guarded by touch_mutex_
    int pending_focus_;                            // guarded by touch_mutex_: 0 none, 1 projected, 2 native
    int night_state_;                              // guarded by touch_mutex_: -1 unknown, 0 day, 1 night (car)
    bool night_started_;                           // guarded by touch_mutex_: phone started sensor 10 this session
    bool pending_night_;                           // guarded by touch_mutex_: send a NIGHT_MODE event on next drain
    VideoFocusCallback focus_cb_;
    void* focus_user_data_;
    MicRequestCallback mic_cb_;
    void* mic_user_data_;
    bool mic_open_;                                // guarded by touch_mutex_
    std::vector<std::vector<uint8_t> > mic_queue_; // guarded by touch_mutex_
    uint32_t mic_sent_;                            // session thread
    bool sendMicFrame(const std::vector<uint8_t>& pcm);   // session thread only
    bool sendMicOpenResponse(uint8_t channel);            // session thread only
    bool sendBindingResponse(uint8_t channel);
    // Sensor channel (ch 1): answer SensorStartRequest and publish night mode / driving status.
    bool sendSensorStartResponse(uint8_t channel, bool ok);
    bool sendSensorEvent(uint32_t sensor_type);
    std::vector<TouchEventOut> touch_queue_;
    pthread_mutex_t touch_mutex_;
    bool input_ready_;                       // guarded by touch_mutex_
    bool touch_fwd_;
    int wake_pipe_[2];    // common tail of sessionLoop (fd release + end-of-session callback)

    // Socket ownership: only the session thread closes the fd (at the end of sessionLoop).
    // stop()/start() merely shutdown() it to wake the thread and then join. fd_mutex_ keeps
    // "shutdown(fd)" from racing with the thread releasing that fd number.
    int client_fd_;
    int running_;                      // guarded by fd_mutex_; use isRunning()/setRunning()
    pthread_t thread_;
    pthread_mutex_t lifecycle_mutex_;   // serialises start()/stop() (accept thread vs main thread)
    pthread_mutex_t fd_mutex_;
    AACryptor cryptor_;
    SessionEndedCallback disconnect_cb_;
    void* disconnect_user_data_;
    VideoFrameCallback video_cb_;
    void* video_user_data_;
    CodecConfigCallback config_cb_;
    void* config_user_data_;
    AudioFrameCallback audio_cb_;
    void* audio_user_data_;
    AudioStateCallback audio_state_cb_;
    void* audio_state_user_data_;
    uint32_t audio_frames_[2];   // [0] = ch 8 speech, [1] = ch 9 media (session thread only)
    uint32_t av_session_[32];    // AV session id per channel from START_INDICATION; echoed in media acks (session thread)

    std::map<uint8_t, std::vector<uint8_t> > reassembly_;
    uint32_t media_frames_;
    std::vector<uint8_t> sps_pps_cache_;

    // TLS records from the phone (session thread): AES-GCM records carry the sender's sequence number as their
    // explicit nonce, so a gap or a repeat shows a record that went missing or arrived out of order.
    void checkRxRecord(uint8_t channel, uint8_t flags, const std::vector<uint8_t>& payload);
    uint64_t rx_seq_next_;
    bool rx_seq_known_;
    uint32_t rx_records_;

    // The last frames from the phone (session thread), printed when the phone goes silent.
    struct RxNote { double t; uint8_t channel; uint8_t flags; uint16_t length; uint16_t msg_id; };
    enum { kRxNotes = 12 };
    RxNote rx_notes_[kRxNotes];
    uint32_t rx_note_count_;
    double rx_start_s_;
    void noteRx(uint8_t channel, uint8_t flags, uint16_t length);
    void noteRxMsg(uint16_t msg_id);
    void dumpRxNotes();
};

} // namespace nbt

#endif // AA_SESSION_HPP
