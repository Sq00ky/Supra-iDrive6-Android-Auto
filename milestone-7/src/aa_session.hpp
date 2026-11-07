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
#include "aa_sensor_msg.hpp"
#include "audio/audio_acks.hpp"
#include "sdr_builder.hpp"
#include "touch_types.hpp"

namespace nbt {

typedef void (*SessionEndedCallback)(void* user_data);
typedef void (*VideoFrameCallback)(uint32_t frame_index, const uint8_t* nal_data, size_t nal_len, uint64_t pts, void* user_data);
typedef void (*CodecConfigCallback)(const uint8_t* config_data, size_t config_len, void* user_data);
// Android Auto PCM (ch 8 speech / ch 9 media / ch 16 system audio, message 0x0000 = 8-byte PTS + PCM) and stream state.
typedef void (*AudioFrameCallback)(uint8_t channel, const uint8_t* pcm, size_t pcm_len, uint64_t pts, void* user_data);
// started: rate / channels = the AudioConfig the phone chose for this stream (START_INDICATION configuration_index
// looked up in what our SDR offered), 0 / 0 when it gave no usable index. Stop: 0 / 0.
typedef void (*AudioStateCallback)(uint8_t channel, bool started, uint32_t rate, uint32_t channels, void* user_data);
// Phone asked for video focus (VideoFocusRequest 0x8007 on the video channel): projected=false is the
// "exit to car" icon (focus_mode UNFOCUSED/NATIVE), projected=true asks to come back. reason = focus_reason.
typedef void (*VideoFocusCallback)(bool projected, uint32_t reason, void* user_data);
// Microphone channel (ch 14): the phone opens (Assistant / voice search) or closes the car mic. Session thread.
typedef void (*MicRequestCallback)(bool open, void* user_data);
// Navigation status channel (ch 10, messages 0x8000 and up): the message id and its protobuf body (after the 2-byte
// message id). Session thread: copy what you need and return, never block.
typedef void (*NavStatusCallback)(uint16_t msg_id, const uint8_t* body, size_t len, void* user_data);

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
    void setNavStatusCallback(NavStatusCallback cb, void* user_data = NULL);
    bool isActive() const { return isRunning(); }   // true from start() until the session ended or stop()
    // How the phone behaved in the session. Written by the session thread: read it in the session-ended callback.
    enum Outcome {
        kEstablished,   // a frame from the phone was decrypted
        kNoAnswer,      // nothing decrypted, and no encrypted frame either: silent, or the handshake never finished
        kStale          // only encrypted frames we can't decrypt: the phone is still in an earlier session
    };
    Outcome outcome() const { return decrypted_any_ ? kEstablished : undecrypted_ ? kStale : kNoAnswer; }
    // The daemon is stopping: asks the phone to end the session (ByeByeRequest, sent by the session thread) and waits
    // up to wait_ms for its answer, which ends the session. true: the session has ended. Thread-safe. Call it while
    // the connection is still up. Before the TLS handshake has finished nothing can be sent, and it just waits.
    bool byeAndWait(uint32_t wait_ms);
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
    // The newest position, about once a second: sent to the phone as a LOCATION (sensor 1) event once it has started that
    // sensor, unless older than kLocationMaxAgeMs by then. Thread-safe.
    void postCarLocation(const LocationFix& fix);
    // The newest satellite count. The session sends it as a GPS_SATELLITE (sensor 21) event about once a second once the
    // phone has started that sensor, whether or not anything was posted: with nothing recent the count is 0. Thread-safe.
    void postCarSatellites(const SatelliteStatus& status);
    // The newest wheel speed: sent to the phone as a CAR_SPEED (sensor 3) event once it has started that sensor, unless
    // carSpeedSendable() says no by then. While the newest is 0 the session repeats it on a timer. Thread-safe.
    void postCarSpeed(const CarSpeedSample& speed);
    // The gear (AaGear) and the parking brake, when they change: sent to the phone as GEAR (sensor 8) and PARKING_BRAKE
    // (sensor 7) events once it has started that sensor, and the newest again whenever it starts one. The car only reports
    // changes, so these have no age limit and are kept across sessions. Thread-safe.
    void postCarGear(int aaGear);
    void postCarParkingBrake(bool on);
    // Car microphone PCM (16 kHz mono S16LE) for the phone. Thread-safe (capture thread); queued and sent on
    // ch 14 by the session thread while the phone has the mic open, dropped otherwise.
    void postMicData(const uint8_t* pcm, size_t len);
    bool micOpen();

    // Deferred audio acks (browser audio mode; audio/audio_acks.hpp). On: a PCM frame on ch 8 / 9 / 16 is acked once
    // the consumer reports its bytes taken (postAudioConsumed), or after NBT_AA_ACK_FORCE_MS (default 1000) unacked.
    // Off: acked on arrival (the consumer cannot report). Set before start(); applies from the next session. The ack
    // window audio sinks get is AudioAcks::window() (NBT_AA_MAX_UNACKED).
    void setDeferredAudioAcks(bool on);
    // The audio consumer took `bytes` of `channel`'s PCM, oldest first (played, or dropped). Thread-safe (mixer thread);
    // the acks go out from the session thread, woken through the wake pipe.
    void postAudioConsumed(uint8_t channel, size_t bytes);

    // Underflow reports (browser audio mode): AudioUnderflowNotification (0x800b) to the phone when the consumer ran
    // dry on a started stream, as Google's head unit does. Set before start(). postAudioUnderflow is thread-safe
    // (mixer thread); the message goes out from the session thread.
    void setUnderflowReports(bool on);
    void postAudioUnderflow(uint8_t channel);

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
    // One frame acked. rx_wall_ns != 0: also its arrival time (Ack field 3, wall clock in ns), as the DHU's audio
    // sinks send it; 0 = none (video).
    bool sendAvMediaAck(uint8_t channel, uint64_t rx_wall_ns = 0);
    bool sendAudioFocusResponse(uint8_t focus_state);
    bool sendByeRequest();
    bool pending_bye_;                             // guarded by touch_mutex_: send a ByeByeRequest on next drain
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
    LocationFix car_fix_;                          // guarded by touch_mutex_: latest position
    bool have_fix_;                                // guarded by touch_mutex_: car_fix_ is set (kept across sessions, age-checked)
    bool location_started_;                        // guarded by touch_mutex_: phone started sensor 1 this session
    bool pending_location_;                        // guarded by touch_mutex_: send a LOCATION event on next drain
    uint32_t location_sent_;                       // session thread: LOCATION events this session
    SatelliteStatus car_satellites_;               // guarded by touch_mutex_: latest count
    bool have_satellites_;                         // guarded by touch_mutex_: car_satellites_ is set (kept across sessions, age-checked)
    bool satellites_started_;                      // session thread: phone started sensor 21 this session
    uint32_t satellites_sent_;                     // session thread: GPS_SATELLITE events this session
    uint32_t satellites_last_ms_;                  // session thread: when the last one went out
    uint32_t satellites_last_used_;                // session thread: the count it carried
    void drainSatellites();                        // session thread: send GPS_SATELLITE when one is due
    CarSpeedSample car_speed_;                     // guarded by touch_mutex_: latest speed
    bool have_speed_;                              // guarded by touch_mutex_: car_speed_ is set (kept across sessions)
    bool speed_started_;                           // guarded by touch_mutex_: phone started sensor 3 this session
    bool pending_speed_;                           // guarded by touch_mutex_: send a CAR_SPEED event on next drain
    uint32_t speed_sent_;                          // session thread: CAR_SPEED events this session
    uint32_t speed_last_ms_;                       // session thread: when the last one went out
    bool speed_last_zero_;                         // session thread: it carried 0, so another is due on the timer
    void drainSpeed();                             // session thread: repeat a CAR_SPEED of 0 when one is due
    // A state sensor: gear or parking brake. Guarded by touch_mutex_, apart from sent.
    struct CarState {
        int value;                                 // the newest
        bool have;                                 // value is set (kept across sessions)
        bool started;                              // phone started this sensor this session
        bool pending;                              // send an event on next drain
        uint32_t sent;                             // session thread: events this session
        CarState() : value(0), have(false), started(false), pending(false), sent(0) {}
    };
    CarState gear_;
    CarState parking_brake_;
    void postCarState(CarState& state, int value);
    void startCarState(CarState& state);           // session thread: the phone started this sensor
    void resetCarState(CarState& state);           // with touch_mutex_ held: a new session, the phone has started nothing
    bool takeCarStatePending(CarState& state);     // with touch_mutex_ held: an event is due, and now it is not
    void drainPeriodic();                          // session thread: everything that is due on time, whatever woke it
    uint32_t periodicTickMs();                     // session thread: how long select may sleep before something is due
    uint32_t last_rx_ms_;                          // session thread: when the last byte from the phone arrived
    VideoFocusCallback focus_cb_;
    void* focus_user_data_;
    MicRequestCallback mic_cb_;
    void* mic_user_data_;
    NavStatusCallback nav_cb_;
    void* nav_user_data_;
    bool mic_open_;                                // guarded by touch_mutex_
    std::vector<std::vector<uint8_t> > mic_queue_; // guarded by touch_mutex_
    uint32_t mic_sent_;                            // session thread
    bool sendMicFrame(const std::vector<uint8_t>& pcm);   // session thread only
    bool sendMicOpenResponse(uint8_t channel);            // session thread only
    bool sendBindingResponse(uint8_t channel);
    // Sensor channel (ch 1): answer SensorStartRequest and publish night mode / driving status / location / satellites.
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
    uint32_t audio_frames_[3];   // per AudioStream (audio/channels.hpp: media, guidance, system); session thread only

    uint16_t aa_minor_;              // protocol minor version this session asked for (NBT_AA_MINOR, default 6)

    // Deferred audio acks and underflow reports (audio/audio_acks.hpp): their state lives in acks_ (thread-safe); the
    // session thread records frames into it and sends what it releases.
    AudioAcks acks_;
    void drainAcks();                // session thread: send the acks acks_ releases
    void drainUnderflows();          // session thread: send the underflow reports posted since the last drain
    void wakeSessionThread();        // a byte down the wake pipe (consumer thread)
    static bool sendAckThunk(uint8_t channel, uint64_t rx_wall_ns, void* self);
    static bool sendUnderflowThunk(uint8_t channel, void* self);
    uint32_t av_session_[32];    // AV session id per channel from START_INDICATION; echoed in media acks (session thread)
    // AudioConfigs our ServiceDiscoveryResponse offered per audio sink channel, in order (session thread).
    std::vector<AaAudioConfig> offered_audio_[32];

    std::map<uint8_t, std::vector<uint8_t> > reassembly_;
    std::map<uint8_t, uint32_t> reassembly_total_;   // size the phone announced on the FIRST fragment
    std::map<uint8_t, uint32_t> reassembly_wire_;    // sum of the fragment length fields
    uint32_t media_frames_;
    std::vector<uint8_t> sps_pps_cache_;

    // TLS records from the phone (session thread): AES-GCM records carry the sender's sequence number as their
    // explicit nonce, so a gap or a repeat shows a record that went missing or arrived out of order.
    void checkRxRecord(uint8_t channel, uint8_t flags, const std::vector<uint8_t>& payload);
    uint64_t rx_seq_next_;
    bool rx_seq_known_;
    uint32_t rx_records_;
    bool decrypted_any_;       // session thread: see outcome()
    uint32_t undecrypted_;     // session thread: frames that failed to decrypt before the first one that didn't

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
