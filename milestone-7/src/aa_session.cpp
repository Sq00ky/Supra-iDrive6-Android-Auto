// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include <time.h>
#include <fcntl.h>
#include "nbt_env.hpp"
#include "sdr_builder.hpp"
#include "crash_handler.hpp"
#include <sys/select.h>
#include <sys/time.h>
#include <stdlib.h>
#include "aa_session.hpp"
#include "dhu_sdp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <vector>

namespace nbt {

static double monoS() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

AASession::AASession()
    : client_fd_(-1),
      running_(0),
      thread_(0),
      disconnect_cb_(NULL),
      disconnect_user_data_(NULL),
      video_cb_(NULL),
      video_user_data_(NULL),
      config_cb_(NULL),
      config_user_data_(NULL),
      audio_cb_(NULL),
      audio_user_data_(NULL),
      audio_state_cb_(NULL),
      audio_state_user_data_(NULL),
      media_frames_(0),
      rx_seq_next_(0),
      rx_seq_known_(false),
      rx_records_(0),
      rx_note_count_(0),
      rx_start_s_(0) {
    pthread_mutex_init(&lifecycle_mutex_, NULL);
    pthread_mutex_init(&fd_mutex_, NULL);
    pthread_mutex_init(&touch_mutex_, NULL);
    input_ready_ = false;
    memset(av_session_, 0, sizeof(av_session_));
    pending_focus_ = 0;
    night_state_ = -1;
    night_started_ = false;
    pending_night_ = false;
    focus_cb_ = NULL;
    focus_user_data_ = NULL;
    mic_cb_ = NULL;
    mic_user_data_ = NULL;
    mic_open_ = false;
    mic_sent_ = 0;
    audio_frames_[0] = audio_frames_[1] = audio_frames_[2] = 0;
    // Touch events are optional. The wake pipe is not: the knob, the microphone, and video
    // focus are delivered through it, including when NBT_TOUCH_FWD=0.
    touch_fwd_ = nbt::envFlag("NBT_TOUCH_FWD") != 0;
    wake_pipe_[0] = wake_pipe_[1] = -1;
    if (pipe(wake_pipe_) == 0) {
        fcntl(wake_pipe_[1], F_SETFL, fcntl(wake_pipe_[1], F_GETFL, 0) | O_NONBLOCK);
        fcntl(wake_pipe_[0], F_SETFL, fcntl(wake_pipe_[0], F_GETFL, 0) | O_NONBLOCK);
    } else {
        wake_pipe_[0] = wake_pipe_[1] = -1;
    }
}

AASession::~AASession() {
    stop();
    pthread_mutex_destroy(&fd_mutex_);
    pthread_mutex_destroy(&touch_mutex_);
    if (wake_pipe_[0] >= 0) close(wake_pipe_[0]);
    if (wake_pipe_[1] >= 0) close(wake_pipe_[1]);
    pthread_mutex_destroy(&lifecycle_mutex_);
}

void AASession::setDisconnectCallback(SessionEndedCallback cb, void* user_data) {
    disconnect_cb_ = cb;
    disconnect_user_data_ = user_data;
}

void AASession::setVideoFrameCallback(VideoFrameCallback cb, void* user_data) {
    video_cb_ = cb;
    video_user_data_ = user_data;
}

void AASession::setCodecConfigCallback(CodecConfigCallback cb, void* user_data) {
    config_cb_ = cb;
    config_user_data_ = user_data;
}

void AASession::setAudioFrameCallback(AudioFrameCallback cb, void* user_data) {
    audio_cb_ = cb;
    audio_user_data_ = user_data;
}

void AASession::setAudioStateCallback(AudioStateCallback cb, void* user_data) {
    audio_state_cb_ = cb;
    audio_state_user_data_ = user_data;
}

void AASession::setVideoFocusCallback(VideoFocusCallback cb, void* user_data) {
    focus_cb_ = cb;
    focus_user_data_ = user_data;
}

void AASession::postVideoFocus(bool projected) {
    if (wake_pipe_[1] < 0) { printf("[AASession] video focus %s not sent (no wake pipe: NBT_TOUCH_FWD=0)\n", projected ? "PROJECTED" : "NATIVE"); return; }
    pthread_mutex_lock(&touch_mutex_);
    pending_focus_ = projected ? 1 : 2;
    pthread_mutex_unlock(&touch_mutex_);
    char b = 1;
    ssize_t w = write(wake_pipe_[1], &b, 1);
    (void)w;
}

void AASession::postNightMode(bool night) {
    pthread_mutex_lock(&touch_mutex_);
    int v = night ? 1 : 0;
    bool changed = night_state_ != v;
    night_state_ = v;
    if (changed && night_started_) pending_night_ = true;
    bool wake = pending_night_;
    pthread_mutex_unlock(&touch_mutex_);
    if (wake && wake_pipe_[1] >= 0) {
        char b = 1;
        ssize_t w = write(wake_pipe_[1], &b, 1);
        (void)w;
    }
}

void AASession::setMicRequestCallback(MicRequestCallback cb, void* user_data) {
    mic_cb_ = cb;
    mic_user_data_ = user_data;
}

bool AASession::micOpen() {
    pthread_mutex_lock(&touch_mutex_);
    bool o = mic_open_;
    pthread_mutex_unlock(&touch_mutex_);
    return o;
}

void AASession::postMicData(const uint8_t* pcm, size_t len) {
    if (!pcm || len == 0 || len > 8192) return;
    pthread_mutex_lock(&touch_mutex_);
    bool ok = mic_open_ && mic_queue_.size() < 64;   // ~1 s of 16 ms chunks; older audio is useless to the Assistant
    if (ok) mic_queue_.push_back(std::vector<uint8_t>(pcm, pcm + len));
    pthread_mutex_unlock(&touch_mutex_);
    if (ok && wake_pipe_[1] >= 0) {
        char b = 1;
        ssize_t w = write(wake_pipe_[1], &b, 1);
        (void)w;
    }
}

// AVInputOpenResponse 0x8006 {session=1, value=2 (0 = OK)} (aasdk / openauto).
bool AASession::sendMicOpenResponse(uint8_t channel) {
    uint8_t plain[6] = { 0x80, 0x06, 0x08, 0x00, 0x10, 0x00 };
    std::vector<uint8_t> enc;
    if (!cryptor_.encrypt(plain, sizeof(plain), enc)) return false;
    return sendFrame(channel, 0x0B, &enc[0], enc.size());
}

// AV_MEDIA_WITH_TIMESTAMP_INDICATION 0x0000: 8-byte big-endian timestamp (us, monotonic) + PCM, on ch 14.
bool AASession::sendMicFrame(const std::vector<uint8_t>& pcm) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t us = (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000);
    std::vector<uint8_t> msg;
    msg.reserve(pcm.size() + 10);
    msg.push_back(0x00);
    msg.push_back(0x00);
    for (int i = 7; i >= 0; --i) msg.push_back((uint8_t)((us >> (i * 8)) & 0xFF));
    msg.insert(msg.end(), pcm.begin(), pcm.end());
    std::vector<uint8_t> enc;
    if (!cryptor_.encrypt(&msg[0], msg.size(), enc)) return false;
    return sendFrame(14, 0x0B, &enc[0], enc.size());
}

// QNX 6.5 / GCC 4.4 ARM libgcc has no atomic builtins, so the running flag is guarded by
// fd_mutex_ (never held across any other lock or blocking call).
bool AASession::isRunning() const {
    pthread_mutex_t* m = const_cast<pthread_mutex_t*>(&fd_mutex_);
    pthread_mutex_lock(m);
    bool r = (running_ != 0);
    pthread_mutex_unlock(m);
    return r;
}

void AASession::setRunning(bool v) {
    pthread_mutex_lock(&fd_mutex_);
    running_ = v ? 1 : 0;
    pthread_mutex_unlock(&fd_mutex_);
}

bool AASession::exchangeRunningOff() {
    pthread_mutex_lock(&fd_mutex_);
    bool was = (running_ != 0);
    running_ = 0;
    pthread_mutex_unlock(&fd_mutex_);
    return was;
}

void AASession::wakeSession() {
    pthread_mutex_lock(&fd_mutex_);
    if (client_fd_ >= 0) {
        shutdown(client_fd_, SHUT_RDWR);   // wakes a recv()/send() blocked in the session thread
    }
    pthread_mutex_unlock(&fd_mutex_);
}

void AASession::releaseSocket() {
    pthread_mutex_lock(&fd_mutex_);
    int fd = client_fd_;
    client_fd_ = -1;
    pthread_mutex_unlock(&fd_mutex_);
    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
}

bool AASession::start(int client_fd) {
    pthread_mutex_lock(&lifecycle_mutex_);

    // Tear down any previous session first (phone reconnected before we noticed it left, or
    // the previous one ended on its own and was never reaped): wake it, then join.
    // Clearing the flag before the join makes finishSession skip its callback, so a session
    // that was still running is cleaned up here. A session that already ended has already run it.
    bool live = isRunning();
    setRunning(false);
    wakeSession();
    if (thread_) {
        pthread_join(thread_, NULL);
        thread_ = 0;
    }
    releaseSocket();   // normally already done by the session thread; harmless if not
    if (live && disconnect_cb_) disconnect_cb_(disconnect_user_data_);

    pthread_mutex_lock(&fd_mutex_);
    client_fd_ = client_fd;
    pthread_mutex_unlock(&fd_mutex_);
    reassembly_.clear();
    reassembly_total_.clear();
    reassembly_wire_.clear();
    sps_pps_cache_.clear();
    pthread_mutex_lock(&touch_mutex_);
    input_ready_ = false;
    touch_queue_.clear();
    key_queue_.clear();
    pending_focus_ = 0;
    night_started_ = false;
    pending_night_ = false;
    mic_open_ = false;
    mic_queue_.clear();
    pthread_mutex_unlock(&touch_mutex_);
    mic_sent_ = 0;
    media_frames_ = 0;
    rx_seq_next_ = 0;
    rx_seq_known_ = false;
    rx_records_ = 0;
    rx_note_count_ = 0;
    rx_start_s_ = monoS();
    memset(av_session_, 0, sizeof(av_session_));
    audio_frames_[0] = audio_frames_[1] = audio_frames_[2] = 0;
    setRunning(true);

    if (pthread_create(&thread_, NULL, &AASession::sessionThreadStub, this) != 0) {
        perror("[AASession] Failed to spawn session thread");
        setRunning(false);
        releaseSocket();
        thread_ = 0;
        pthread_mutex_unlock(&lifecycle_mutex_);
        return false;
    }

    pthread_mutex_unlock(&lifecycle_mutex_);
    return true;
}

void AASession::stop() {
    pthread_mutex_lock(&lifecycle_mutex_);
    setRunning(false);
    wakeSession();
    // Always reap the thread, also when the session ended on its own: otherwise its
    // resources leak. The session thread closes the socket as it exits.
    if (thread_) {
        pthread_join(thread_, NULL);
        thread_ = 0;
    }
    releaseSocket();
    pthread_mutex_unlock(&lifecycle_mutex_);
}

void AASession::noteRx(uint8_t channel, uint8_t flags, uint16_t length) {
    RxNote& n = rx_notes_[rx_note_count_ % kRxNotes];
    n.t = monoS() - rx_start_s_;
    n.channel = channel;
    n.flags = flags;
    n.length = length;
    n.msg_id = 0xFFFF;   // not known until decrypted and complete
    ++rx_note_count_;
}

void AASession::noteRxMsg(uint16_t msg_id) {
    if (rx_note_count_) rx_notes_[(rx_note_count_ - 1) % kRxNotes].msg_id = msg_id;
}

void AASession::dumpRxNotes() {
    uint32_t first = rx_note_count_ > kRxNotes ? rx_note_count_ - kRxNotes : 0;
    printf("[AASession] last %u of %u frames from the phone (s since the session started):\n",
           (unsigned)(rx_note_count_ - first), (unsigned)rx_note_count_);
    for (uint32_t i = first; i < rx_note_count_; ++i) {
        const RxNote& n = rx_notes_[i % kRxNotes];
        if (n.msg_id == 0xFFFF)
            printf("            +%.3f ch %u flags 0x%02X len %u\n", n.t, n.channel, n.flags, n.length);
        else
            printf("            +%.3f ch %u flags 0x%02X len %u msg 0x%04X\n", n.t, n.channel, n.flags, n.length,
                   n.msg_id);
    }
}

void AASession::checkRxRecord(uint8_t channel, uint8_t flags, const std::vector<uint8_t>& payload) {
    ++rx_records_;
    if (payload.size() < 13) {
        printf("[AASession] TLS record %u on ch %u: only %u bytes\n", rx_records_, channel, (unsigned)payload.size());
        return;
    }
    const uint8_t* p = &payload[0];
    unsigned type = p[0];
    unsigned rlen = ((unsigned)p[3] << 8) | p[4];
    uint64_t seq = 0;
    for (int i = 5; i < 13; ++i) seq = (seq << 8) | p[i];
    bool odd = type != 0x17 || p[1] != 0x03 || p[2] != 0x03 || rlen + 5 != payload.size();
    bool jump = rx_seq_known_ && seq != rx_seq_next_;
    if (odd || jump || rx_records_ <= 32) {
        char note[64] = "";
        if (jump) snprintf(note, sizeof(note), " SEQUENCE JUMP (expected %llu)", (unsigned long long)rx_seq_next_);
        else if (odd) snprintf(note, sizeof(note), " UNEXPECTED HEADER");
        printf("[AASession] TLS record %u ch %u flags 0x%02X: type 0x%02X ver %02X%02X len %u (frame %u), seq %llu%s\n",
               rx_records_, channel, flags, type, p[1], p[2], rlen, (unsigned)payload.size(), (unsigned long long)seq,
               note);
    }
    rx_seq_next_ = seq + 1;
    rx_seq_known_ = true;
}

void* AASession::sessionThreadStub(void* arg) {
    reinterpret_cast<AASession*>(arg)->sessionLoop();
    return NULL;
}

// NBT_RX_TIMEOUT_S: seconds of total silence from the phone after which the session is
// considered dead (wireless phones can vanish without closing the socket). 0 = off.
uint32_t AASession::rxTimeoutMs() {
    static uint32_t cached = 0xFFFFFFFFu;
    if (cached == 0xFFFFFFFFu) {
        const char* e = getenv("NBT_RX_TIMEOUT_S");
        // Default 2 s (v1.6.6, was 6 s in v1.6.5): the phone pings exactly once a second (car log 2026-10-24: 78
        // pings, max gap 1.005 s) and sends ch12 status every second even while iDrive owns the screen, so silence
        // means the Wi-Fi link is gone. Without it a sudden drop never ended the session (no RST arrives) and the
        // last frame stayed on screen. The check runs on 1 s select ticks, so the real cut-off is 2-3 s.
        // NBT_RX_TIMEOUT_S=0 disables.
        cached = (e && *e) ? (uint32_t)atoi(e) * 1000u : 2000u;
    }
    return cached;
}

bool AASession::readExact(uint8_t* buffer, size_t length) {
    size_t total = 0;
    uint32_t idle_ms = 0;
    const uint32_t limit_ms = rxTimeoutMs();
    const bool have_wake = wake_pipe_[0] >= 0;
    const bool need_select = (limit_ms != 0) || have_wake;
    while (total < length && isRunning()) {
        int fd = client_fd_;
        if (fd < 0) return false;
        if (need_select) {
            fd_set rf;
            FD_ZERO(&rf);
            FD_SET(fd, &rf);
            int maxfd = fd;
            if (have_wake) {
                FD_SET(wake_pipe_[0], &rf);
                if (wake_pipe_[0] > maxfd) maxfd = wake_pipe_[0];
            }
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            int r = select(maxfd + 1, &rf, NULL, NULL, &tv);
            if (r < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            if (r == 0) {
                if (limit_ms) {
                    idle_ms += 1000;
                    if (idle_ms >= limit_ms) {
                        printf("[AASession] No data from phone for %u s: treating session as dead.\n",
                               (unsigned)(idle_ms / 1000));
                        dumpRxNotes();
                        return false;
                    }
                }
                continue;
            }
            if (have_wake && FD_ISSET(wake_pipe_[0], &rf)) {
                char junk[64];
                while (read(wake_pipe_[0], junk, sizeof(junk)) > 0) { }
                drainTouch();
                if (!FD_ISSET(fd, &rf)) continue;
            }
            idle_ms = 0;
        }
        ssize_t n = recv(fd, buffer + total, length - total, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            return false;
        }
        total += static_cast<size_t>(n);
    }
    return total == length;
}

void AASession::postTouchEvent(const TouchEventOut& ev) {
    if (!touch_fwd_) return;
    pthread_mutex_lock(&touch_mutex_);
    if (!input_ready_) { pthread_mutex_unlock(&touch_mutex_); return; }
    // Coalesce consecutive moves: only the newest positions matter.
    if (ev.action == 2 && !touch_queue_.empty() && touch_queue_.back().action == 2) {
        touch_queue_.back() = ev;
    } else if (touch_queue_.size() < 256) {
        touch_queue_.push_back(ev);
    }
    pthread_mutex_unlock(&touch_mutex_);
    char b = 1;
    ssize_t w = write(wake_pipe_[1], &b, 1);
    (void)w;
}

static void putVar(std::vector<uint8_t>& o, uint64_t v) {
    while (v >= 0x80) { o.push_back((uint8_t)(v | 0x80)); v >>= 7; }
    o.push_back((uint8_t)v);
}

void AASession::postKeyEvent(uint32_t keycode, bool down) {
    KeyOut k; k.keycode = keycode; k.down = down ? 1 : 0; k.delta = 0; k.rel = false;
    pthread_mutex_lock(&touch_mutex_);
    bool ok = input_ready_ && key_queue_.size() < 64;
    if (ok) key_queue_.push_back(k);
    pthread_mutex_unlock(&touch_mutex_);
    if (!ok) {
        static int drops = 0;
        if (++drops <= 5) printf("[AASession] key %u %s dropped (input channel not bound)\n", keycode, down ? "down" : "up");
        return;
    }
    char b = 1;
    ssize_t w = write(wake_pipe_[1], &b, 1);
    (void)w;
}

void AASession::postRelativeEvent(uint32_t keycode, int delta) {
    KeyOut k; k.keycode = keycode; k.down = 0; k.delta = delta; k.rel = true;
    pthread_mutex_lock(&touch_mutex_);
    bool ok = input_ready_ && key_queue_.size() < 64;
    if (ok) {
        // Coalesce consecutive rotary steps that have not been sent yet.
        if (!key_queue_.empty() && key_queue_.back().rel && key_queue_.back().keycode == keycode) key_queue_.back().delta += delta;
        else key_queue_.push_back(k);
    }
    pthread_mutex_unlock(&touch_mutex_);
    if (!ok) {
        static int drops = 0;
        if (++drops <= 5) printf("[AASession] rotary %d dropped (input channel not bound)\n", delta);
        return;
    }
    char b = 1;
    ssize_t w = write(wake_pipe_[1], &b, 1);
    (void)w;
}

// InputEventIndication (0x8001) on input channel 7 (aasdk InputEventIndication):
//   key:      {1: timestamp(us), 4: ButtonEvents{1: ButtonEvent{1: scan_code, 2: is_pressed, 3: meta=0, 4: long_press=0}}}
//   relative: {1: timestamp(us), 6: RelativeInputEvents{1: RelativeInputEvent{1: scan_code, 2: delta (int32)}}}
bool AASession::sendKeyEvent(const KeyOut& k) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t us = (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000);
    std::vector<uint8_t> ev;
    ev.push_back(0x08); putVar(ev, k.keycode);
    if (k.rel) {
        ev.push_back(0x10); putVar(ev, (uint64_t)(int64_t)k.delta);   // int32 negative -> 10-byte varint
    } else {
        ev.push_back(0x10); putVar(ev, k.down ? 1 : 0);
        ev.push_back(0x18); putVar(ev, 0);
        ev.push_back(0x20); putVar(ev, 0);
    }
    std::vector<uint8_t> list;
    list.push_back(0x0A); putVar(list, ev.size());
    list.insert(list.end(), ev.begin(), ev.end());
    std::vector<uint8_t> msg;
    msg.push_back(0x80); msg.push_back(0x01);
    msg.push_back(0x08); putVar(msg, us);
    msg.push_back(k.rel ? 0x32 : 0x22); putVar(msg, list.size());
    msg.insert(msg.end(), list.begin(), list.end());
    std::vector<uint8_t> enc;
    if (!cryptor_.encrypt(&msg[0], msg.size(), enc)) return false;
    return sendFrame(7, 0x0B, &enc[0], enc.size());
}

// InputEventIndication (0x8001) on the touchscreen input channel:
//   {1: timestamp(us), 3: TouchEvent{1 (repeated): Pointer{1:x 2:y 3:pointer_id}, 2: action_index, 3: action}}
bool AASession::sendTouchEvent(const TouchEventOut& ev) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t us = (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000);

    std::vector<uint8_t> te;
    for (int i = 0; i < ev.n; ++i) {
        std::vector<uint8_t> ptr;
        ptr.push_back(0x08); putVar(ptr, (uint32_t)ev.p[i].x);
        ptr.push_back(0x10); putVar(ptr, (uint32_t)ev.p[i].y);
        ptr.push_back(0x18); putVar(ptr, (uint32_t)ev.p[i].id);
        te.push_back(0x0A); putVar(te, ptr.size());
        te.insert(te.end(), ptr.begin(), ptr.end());
    }
    te.push_back(0x10); putVar(te, (uint32_t)ev.index);    // action_index
    te.push_back(0x18); putVar(te, (uint32_t)ev.action);
    std::vector<uint8_t> msg;
    msg.push_back(0x80); msg.push_back(0x01);          // InputEventIndication
    msg.push_back(0x08); putVar(msg, us);
    msg.push_back(0x1A); putVar(msg, te.size());
    msg.insert(msg.end(), te.begin(), te.end());
    std::vector<uint8_t> enc;
    if (!cryptor_.encrypt(&msg[0], msg.size(), enc)) return false;
    return sendFrame(7, 0x0B, &enc[0], enc.size());
}

void AASession::drainTouch() {
    std::vector<TouchEventOut> work;
    std::vector<KeyOut> keys;
    pthread_mutex_lock(&touch_mutex_);
    work.swap(touch_queue_);
    keys.swap(key_queue_);
    int focus = pending_focus_;
    pending_focus_ = 0;
    bool night = pending_night_ && night_started_;
    pending_night_ = false;
    std::vector<std::vector<uint8_t> > mic;
    mic.swap(mic_queue_);
    pthread_mutex_unlock(&touch_mutex_);
    for (size_t i = 0; i < mic.size(); ++i) {
        if (!sendMicFrame(mic[i])) { printf("[AASession] mic -> phone: SEND FAILED\n"); break; }
        if ((mic_sent_++ % 250) == 0) printf("[AASession] mic -> phone: %u chunks sent (%u bytes each)\n", mic_sent_, (unsigned)mic[i].size());
    }
    if (night && !sendSensorEvent(10)) printf("[AASession] NIGHT_MODE event: SEND FAILED\n");
    if (focus) {
        bool ok = sendVideoFocusIndication(6, focus == 1, true);
        printf("[AASession] video focus -> phone: %s (unrequested) %s\n", focus == 1 ? "FOCUSED/projected" : "UNFOCUSED/native", ok ? "sent" : "SEND FAILED");
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        bool ok = sendKeyEvent(keys[i]);
        if (keys[i].rel)
            printf("[AASession] rotary -> phone: key=%u delta=%d (%s)\n", keys[i].keycode, keys[i].delta, ok ? "sent" : "SEND FAILED");
        else
            printf("[AASession] key -> phone: key=%u %s (%s)\n", keys[i].keycode, keys[i].down ? "down" : "up", ok ? "sent" : "SEND FAILED");
        if (!ok) return;
    }
    static int logged = 0;
    for (size_t i = 0; i < work.size(); ++i) {
        bool ok = sendTouchEvent(work[i]);
        if (work[i].action != 2 || logged < 40) {
            if (work[i].action == 2) ++logged;
            printf("[AASession] touch -> phone: action=%d idx=%d n=%d first=(%d,%d) (%s)\n",
                   work[i].action, work[i].index, work[i].n,
                   work[i].n ? work[i].p[0].x : -1, work[i].n ? work[i].p[0].y : -1, ok ? "sent" : "SEND FAILED");
        }
        if (!ok) break;
    }
}

bool AASession::sendBindingResponse(uint8_t channel) {
    uint8_t resp[4] = { 0x80, 0x03, 0x08, 0x00 };      // BindingResponse{status = SUCCESS}
    std::vector<uint8_t> enc;
    if (!cryptor_.encrypt(resp, sizeof(resp), enc)) return false;
    return sendFrame(channel, 0x0B, &enc[0], enc.size());
}

bool AASession::sendAll(const uint8_t* data, size_t length) {
    size_t total = 0;
    while (total < length && isRunning()) {
        int fd = client_fd_;
        if (fd < 0) return false;
        ssize_t n = send(fd, data + total, length - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        total += static_cast<size_t>(n);
    }
    return total == length;
}

bool AASession::sendFrame(uint8_t channel, uint8_t flags, const uint8_t* payload, size_t length) {
    if (length > 0xFFFFU) return false;
    std::vector<uint8_t> frame;
    frame.reserve(length + 4);
    frame.push_back(channel);
    frame.push_back(flags);
    frame.push_back(static_cast<uint8_t>((length >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(length & 0xFF));
    if (length > 0) {
        if (!payload) return false;
        frame.insert(frame.end(), payload, payload + length);
    }
    return sendAll(&frame[0], frame.size());
}

bool AASession::sendSslHandshake(const std::vector<uint8_t>& tls_bytes) {
    if (tls_bytes.empty()) return true;

    // Control Message ID 0x0003 (MESSAGE_SSL_HANDSHAKE)
    std::vector<uint8_t> payload;
    payload.reserve(tls_bytes.size() + 2);
    payload.push_back(0x00);
    payload.push_back(0x03);
    payload.insert(payload.end(), tls_bytes.begin(), tls_bytes.end());

    return sendFrame(0x00, 0x03, &payload[0], payload.size());
}

bool AASession::sendAuthComplete() {
    // Control Message ID 0x0004 (MESSAGE_AUTH_COMPLETE)
    // Per canonical aasdk specification (ControlServiceChannel.cpp:65):
    // AUTH_COMPLETE is transmitted as messenger::EncryptionType::PLAIN (Flags = 0x03)!
    // Protobuf payload: 08 00 (field 1 = 0, status SUCCESS)
    uint8_t auth_payload[4] = { 0x00, 0x04, 0x08, 0x00 };
    printf("[AASession] Sent AuthCompleteNotification (Flags=0x03, 4 bytes).\n");
    return sendFrame(0x00, 0x03, auth_payload, sizeof(auth_payload));
}

bool AASession::sendServiceDiscoveryResponse() {
    // Control Message ID 0x0006 (MESSAGE_SERVICE_DISCOVERY_RESPONSE)
    // Uses Google's canonical reference Desktop Head Unit (DHU) capability manifest
    // including all 9 mandatory channels: Video, Media Audio, Speech, System Audio,
    // Input (touch/rotary), Sensors (Driving Status, Night Mode), Mic, and Nav.
    std::vector<uint8_t> plain_sdr;
    plain_sdr.push_back(0x00);
    plain_sdr.push_back(0x06); // Message ID: 6 (ServiceDiscoveryResponse)
    if (nbt::wirelessEnabled() || getenv("NBT_SDR_IDENTITY")) {
        // Wireless / real-car identity: same channels as the DHU manifest, but the
        // head unit identifies as the Toyota (override with NBT_CAR_* / NBT_HU_* env).
        SdrIdentity ident;
        ident.loadEnv();
        std::vector<uint8_t> sdr = buildServiceDiscovery(kDhuSdpProtobuf, kDhuSdpProtobufLen, ident);
        printf("[AASession] ServiceDiscovery identity: %s %s %s (BT channel: %s)\n",
               ident.carMake.c_str(), ident.carModel.c_str(), ident.carYear.c_str(),
               ident.btAdapterAddress.empty() ? "none" : ident.btAdapterAddress.c_str());
        patchMainVideo(sdr);
        patchTouchConfig(sdr);
        dropSdrChannels(sdr);
        retypeGuidanceSink(sdr);
        addSystemAudioSink(sdr);
        plain_sdr.insert(plain_sdr.end(), sdr.begin(), sdr.end());
    } else {
        std::vector<uint8_t> sdr(kDhuSdpProtobuf, kDhuSdpProtobuf + kDhuSdpProtobufLen);
        patchMainVideo(sdr);
        patchTouchConfig(sdr);
        dropSdrChannels(sdr);
        retypeGuidanceSink(sdr);
        addSystemAudioSink(sdr);
        plain_sdr.insert(plain_sdr.end(), sdr.begin(), sdr.end());
    }

    std::vector<uint8_t> encrypted_payload;
    if (cryptor_.encrypt(&plain_sdr[0], plain_sdr.size(), encrypted_payload)) {
        printf("[AASession] >>> Sent Google DHU ServiceDiscoveryResponse (%u bytes: All Channels, Audio, Video, Sensors, Input)!\n",
               static_cast<unsigned int>(encrypted_payload.size()));
        return sendFrame(0x00, 0x0B, &encrypted_payload[0], encrypted_payload.size());
    }
    return false;
}

bool AASession::sendChannelOpenRequest(uint8_t channel_id) {
    uint8_t plain[4] = { 0x00, 0x07, 0x10, channel_id };
    std::vector<uint8_t> enc;
    if (cryptor_.encrypt(plain, sizeof(plain), enc)) {
        printf("[AASession] >>> Sent ChannelOpenRequest for Channel %u!\n", channel_id);
        return sendFrame(0x00, 0x0B, &enc[0], enc.size());
    }
    return false;
}

bool AASession::sendAvSetupResponse(uint8_t channel) {
    // AVChannelSetupResponse: media_status=OK(2), max_unacked=10, configs[0]=0
    uint8_t plain[8] = { 0x80, 0x03, 0x08, 0x02, 0x10, 0x0A, 0x18, 0x00 };
    std::vector<uint8_t> enc;
    if (cryptor_.encrypt(plain, sizeof(plain), enc)) {
        printf("[AASession] >>> Sent AV SETUP_RESPONSE on Channel %u (max_unacked=10)!\n", channel);
        return sendFrame(channel, 0x0B, &enc[0], enc.size());
    }
    return false;
}

bool AASession::sendVideoFocusIndication(uint8_t channel, bool focused, bool unrequested) {
    // VideoFocusIndication {focus_mode=1: FOCUSED(1) / UNFOCUSED(2), unrequested=2}
    uint8_t plain[6] = { 0x80, 0x08, 0x08, (uint8_t)(focused ? 0x01 : 0x02), 0x10, (uint8_t)(unrequested ? 1 : 0) };
    std::vector<uint8_t> enc;
    if (cryptor_.encrypt(plain, sizeof(plain), enc)) {
        printf("[AASession] >>> Sent VideoFocusIndication (%s%s) on Channel %u!\n", focused ? "FOCUSED" : "UNFOCUSED",
               unrequested ? ", unrequested" : "", channel);
        return sendFrame(channel, 0x0B, &enc[0], enc.size());
    }
    return false;
}

bool AASession::sendAvMediaAck(uint8_t channel, uint32_t count) {
    (void)count;
    // AVMediaAckIndication {session=1, value=2 (frames acked)}: the session must be the one the phone gave in this
    // channel's START_INDICATION. It is 0 for the first start (video, media) but a stream that is stopped and
    // restarted (navigation guidance, assistant) gets a new id; acking with 0 then leaves the phone's
    // "max unacked" window full and the stream stalls.
    uint32_t sess = av_session_[channel & 31];
    uint8_t plain[12];
    size_t n = 0;
    plain[n++] = 0x80; plain[n++] = 0x04; plain[n++] = 0x08;
    do { uint8_t b = sess & 0x7F; sess >>= 7; plain[n++] = b | (sess ? 0x80 : 0); } while (sess);
    plain[n++] = 0x10; plain[n++] = 0x01;
    std::vector<uint8_t> enc;
    if (cryptor_.encrypt(plain, n, enc)) {
        return sendFrame(channel, 0x0B, &enc[0], enc.size());
    }
    return false;
}

bool AASession::sendAudioFocusResponse(uint8_t focus_state) {
    uint8_t plain[4] = { 0x00, 0x13, 0x08, focus_state };
    std::vector<uint8_t> enc;
    if (cryptor_.encrypt(plain, sizeof(plain), enc)) {
        printf("[AASession] >>> Sent AudioFocusResponse (state=%u)!\n", focus_state);
        return sendFrame(0x00, 0x0B, &enc[0], enc.size());
    }
    return false;
}

// ---- Sensor channel (AA channel 1) -------------------------------------------
// Wire layout (aasdk): SensorStartRequest 0x8001 {sensor_type=1 varint, min_period=2},
// SensorStartResponse 0x8002 {status=1}, SensorEventIndication 0x8003 with repeated
// NightMode night_mode=10 {is_night=1} and DrivingStatus driving_status=13 {status=1}.
// Env: NBT_NIGHT_MODE (0/1, default 0), NBT_DRIVING_STATUS (0=unrestricted, default 0).
static uint32_t pbFirstVarint(const std::vector<uint8_t>& d, size_t off, uint32_t field) {
    size_t i = off;
    while (i < d.size()) {
        uint32_t tag = 0; int sh = 0;
        while (i < d.size()) { uint8_t b = d[i++]; tag |= (uint32_t)(b & 0x7F) << sh; sh += 7; if (!(b & 0x80)) break; }
        uint32_t f = tag >> 3, w = tag & 7;
        if (w == 0) {
            uint32_t v = 0; sh = 0;
            while (i < d.size()) { uint8_t b = d[i++]; v |= (uint32_t)(b & 0x7F) << sh; sh += 7; if (!(b & 0x80)) break; }
            if (f == field) return v;
        } else if (w == 2) {
            uint32_t len = 0; sh = 0;
            while (i < d.size()) { uint8_t b = d[i++]; len |= (uint32_t)(b & 0x7F) << sh; sh += 7; if (!(b & 0x80)) break; }
            i += len;
        } else if (w == 1) { i += 8; } else if (w == 5) { i += 4; } else { break; }
    }
    return 0xFFFFFFFFu;
}

bool AASession::sendSensorStartResponse(uint8_t channel, bool ok) {
    uint8_t plain[4] = { 0x80, 0x02, 0x08, (uint8_t)(ok ? 0x00 : 0x01) };
    std::vector<uint8_t> enc;
    if (cryptor_.encrypt(plain, sizeof(plain), enc)) {
        return sendFrame(channel, 0x0B, &enc[0], enc.size());
    }
    return false;
}

bool AASession::sendSensorEvent(uint32_t sensor_type) {
    std::vector<uint8_t> plain;
    plain.push_back(0x80); plain.push_back(0x03);
    if (sensor_type == 10) { // NIGHT_MODE
        const char* e = getenv("NBT_NIGHT_MODE");
        uint8_t night;
        const char* src;
        if (e && (e[0] == '0' || e[0] == '1')) { night = (e[0] == '1') ? 1 : 0; src = "NBT_NIGHT_MODE"; }
        else {
            pthread_mutex_lock(&touch_mutex_);
            int s = night_state_;
            pthread_mutex_unlock(&touch_mutex_);
            night = s > 0 ? 1 : 0;
            src = s < 0 ? "car state unknown yet: day" : "car day/night";
        }
        plain.push_back(0x52); plain.push_back(0x02); plain.push_back(0x08); plain.push_back(night);
        printf("[AASession] >>> Sensor NIGHT_MODE event (is_night=%u, %s)\n", night, src);
    } else if (sensor_type == 13) { // DRIVING_STATUS
        const char* e = getenv("NBT_DRIVING_STATUS");
        uint8_t st = e ? (uint8_t)atoi(e) : 0;
        plain.push_back(0x6A); plain.push_back(0x02); plain.push_back(0x08); plain.push_back(st);
        printf("[AASession] >>> Sensor DRIVING_STATUS event (status=%u)\n", st);
    } else {
        return true; // no data source for other sensors: ack only
    }
    std::vector<uint8_t> enc;
    if (cryptor_.encrypt(&plain[0], plain.size(), enc)) {
        return sendFrame(1, 0x0B, &enc[0], enc.size());
    }
    return false;
}

void AASession::sessionLoop() {
    printf("[AASession] Protocol thread active on fd %d.\n", client_fd_);

    // Step 1: Send Android Auto VersionRequest (Major 1, Minor 6, Status 0)
    // Frame: Channel 0 (Control), Flags 0x03 (First | Last, Unencrypted)
    uint8_t version_req[8] = {
        0x00, 0x01, // Message ID: 1 (VersionRequest)
        0x00, 0x01, // Major version: 1
        0x00, 0x06, // Minor version: 6
        0x00, 0x00  // Status: 0
    };

    printf("[AASession] Sending VersionRequest (v1.6)...\n");
    if (!sendFrame(0x00, 0x03, version_req, sizeof(version_req))) {
        printf("[AASession] Error: Failed to send VersionRequest.\n");
        finishSession();
        return;
    }

    // Step 2: Read incoming frames from the phone
    while (isRunning()) {
        uint8_t header[4];
        if (!readExact(header, 4)) {
            if (isRunning()) {
                printf("[AASession] Phone closed socket or read error.\n");
            }
            break;
        }

        uint8_t channel = header[0];
        uint8_t flags   = header[1];
        uint16_t length = (static_cast<uint16_t>(header[2]) << 8) | header[3];
        noteRx(channel, flags, length);
        g_crumb_stage = 1;
        g_crumb_channel = channel;

        uint32_t total_size = 0;
        // Check if frame has FrameType::FIRST (bit 0 set, bit 1 clear: flags & 0x03 == 1)
        // Per canonical aasdk specification (FrameHeader.cpp:33, FrameSize.cpp:52):
        // Per-frame logging is throttled on the media channels (video/audio/metadata): hundreds of
        // lines per second to a USB stick lost log data on the car. NBT_VERBOSE=1 logs everything.
        bool logit = true;
        {
            static unsigned frame_count[256];
            static int verbose = -1;
            if (verbose < 0) verbose = getenv("NBT_VERBOSE") ? 1 : 0;
            if (!verbose && (channel == 6 || channel == 9 || channel == 12 || channel == 14 ||
                             channel == AA_CH_SYSTEM_AUDIO)) {
                unsigned n = frame_count[channel]++;
                logit = (n < 30) || ((n % 300) == 0);
            }
        }
        // FrameType::FIRST carries an extended 6-byte size header (2 bytes length + 4 bytes totalSize).
        if ((flags & 0x03) == 0x01) {
            uint8_t total_buf[4];
            if (!readExact(total_buf, 4)) {
                printf("[AASession] Failed to read extended total_size header.\n");
                break;
            }
            total_size = (static_cast<uint32_t>(total_buf[0]) << 24) |
                         (static_cast<uint32_t>(total_buf[1]) << 16) |
                         (static_cast<uint32_t>(total_buf[2]) << 8)  |
                          static_cast<uint32_t>(total_buf[3]);
            if (logit) printf("[AASession] Frame RX (EXTENDED FIRST): Ch=%u, Flags=0x%02X, Len=%u, Total=%u\n",
                   channel, flags, length, total_size);
        } else {
            if (logit) printf("[AASession] Frame RX: Ch=%u, Flags=0x%02X, Len=%u\n",
                   channel, flags, length);
        }

        if (length > 0) {
            std::vector<uint8_t> payload(length);
            if (!readExact(&payload[0], length)) {
                printf("[AASession] Failed to read payload bytes (%u bytes).\n", length);
                break;
            }

            // Decrypt payload if frame has encryption flag (0x08)
            g_crumb_stage = 2;
            std::vector<uint8_t> decrypted_chunk;
            if (flags & 0x08) {
                checkRxRecord(channel, flags, payload);
                if (!cryptor_.decrypt(&payload[0], payload.size(), decrypted_chunk)) {
                    printf("[AASession] Failed to decrypt encrypted frame (%u bytes): %s; %u bytes queued for the phone\n",
                           length, cryptor_.lastError(), (unsigned)cryptor_.pendingOut());
                    printf("            Raw: ");
                    for (size_t i = 0; i < payload.size() && i < 24; ++i) printf("%02X ", payload[i]);
                    printf("\n");
                    reassembly_[channel].clear();
                    reassembly_total_.erase(channel);
                    reassembly_wire_.erase(channel);
                    continue;
                }
            } else {
                decrypted_chunk = payload;
            }

            // Reassemble fragmented frames per channel
            uint8_t frame_type = flags & 0x03; // 0=MIDDLE, 1=FIRST, 2=LAST, 3=BULK
            std::vector<uint8_t> frame_data;

            // Bound reassembly memory: a lost LAST fragment or garbage must not grow it forever.
            if (reassembly_[channel].size() + decrypted_chunk.size() > (8u * 1024u * 1024u)) {
                printf("[AASession] Reassembly overflow on channel %u: dropping fragments.\n", channel);
                reassembly_[channel].clear();
                reassembly_total_.erase(channel);
                reassembly_wire_.erase(channel);
                continue;
            }

            if (frame_type == 0x01) { // FIRST
                reassembly_[channel].clear();
                reassembly_[channel].insert(reassembly_[channel].end(),
                                             decrypted_chunk.begin(), decrypted_chunk.end());
                reassembly_total_[channel] = total_size;
                reassembly_wire_[channel] = length;
                continue; // Wait for remaining fragments
            } else if (frame_type == 0x00) { // MIDDLE
                reassembly_[channel].insert(reassembly_[channel].end(),
                                             decrypted_chunk.begin(), decrypted_chunk.end());
                reassembly_wire_[channel] += length;
                continue; // Wait for remaining fragments
            } else if (frame_type == 0x02) { // LAST
                bool had_first = reassembly_total_.find(channel) != reassembly_total_.end() ||
                                 !reassembly_[channel].empty();
                reassembly_[channel].insert(reassembly_[channel].end(),
                                             decrypted_chunk.begin(), decrypted_chunk.end());
                reassembly_wire_[channel] += length;
                uint32_t expect = reassembly_total_[channel];
                uint32_t wire = reassembly_wire_[channel];
                frame_data = reassembly_[channel];
                reassembly_[channel].clear();
                reassembly_total_.erase(channel);
                reassembly_wire_.erase(channel);
                if (!had_first) {
                    printf("[AASession] LAST fragment on channel %u with no FIRST: dropping\n", channel);
                    continue;
                }
                // total_size is the size the phone announced. The length fields are the bytes on the
                // wire (larger once encrypted). A shortfall means this LAST closed an incomplete message.
                if (expect != 0 && wire < expect) {
                    printf("[AASession] channel %u reassembled %u wire bytes, phone announced %u: dropping\n",
                           channel, wire, expect);
                    continue;
                }
            } else { // 0x03 = BULK (Single unfragmented message)
                frame_data = decrypted_chunk;
            }

            // Dump first bytes in hex
            if (logit) {
                printf("            Data: ");
                for (size_t i = 0; i < frame_data.size() && i < 16; ++i) {
                    printf("%02X ", frame_data[i]);
                }
                if (frame_data.size() > 16) printf("...");
                printf("\n");
            }

            // Universal Message Dispatcher based on protocol wire captures
            uint16_t msg_id = frame_data.size() >= 2 ? ((static_cast<uint16_t>(frame_data[0]) << 8) | frame_data[1]) : 0;
            noteRxMsg(msg_id);
            g_crumb_stage = 3;
            g_crumb_msg = msg_id;

            if (channel == 1 && msg_id == 0x8001) { // SensorStartRequest
                uint32_t st = pbFirstVarint(frame_data, 2, 1);
                printf("[AASession] >>> SensorStartRequest (type=%u)\n", st);
                sendSensorStartResponse(channel, true);
                if (st == 10) {
                    pthread_mutex_lock(&touch_mutex_);
                    night_started_ = true;
                    pending_night_ = false;
                    pthread_mutex_unlock(&touch_mutex_);
                }
                if (st != 0xFFFFFFFFu) sendSensorEvent(st);
            } else if (channel == 14 && msg_id == 0x8005) { // AVInputOpenRequest {open=1, anc=2, ec=3, max_unacked=4}
                uint32_t open = pbFirstVarint(frame_data, 2, 1);
                uint32_t maxu = pbFirstVarint(frame_data, 2, 4);
                bool on = (open == 1);
                printf("[AASession] >>> Microphone %s request (max_unacked=%d)\n", on ? "OPEN" : "CLOSE",
                       maxu == 0xFFFFFFFFu ? -1 : (int)maxu);
                pthread_mutex_lock(&touch_mutex_);
                mic_open_ = on;
                if (!on) mic_queue_.clear();
                pthread_mutex_unlock(&touch_mutex_);
                if (on) { mic_sent_ = 0; sendMicOpenResponse(channel); }
                if (mic_cb_) mic_cb_(on, mic_user_data_);
            } else if (channel == 14 && msg_id == 0x8004) { // AVMediaAck for our mic frames: nothing to do
            } else if (msg_id == 7) { // ChannelOpenRequest (sent by phone on the channel being opened!)
                printf("[AASession] >>> Received ChannelOpenRequest on Channel %u! Sending ChannelOpenResponse (SUCCESS)...\n",
                       channel);
                uint8_t open_resp[4] = { 0x00, 0x08, 0x08, 0x00 }; // Msg ID 8 (ChannelOpenResponse), Status 0 (OK)
                std::vector<uint8_t> enc_resp;
                if (cryptor_.encrypt(open_resp, sizeof(open_resp), enc_resp)) {
                    sendFrame(channel, 0x0F, &enc_resp[0], enc_resp.size()); // Specific channel flag 0x0F
                }
            } else if (channel == 10 && msg_id >= 0x8000) {
                // Navigation status channel: guidance state (0x8003), next turn (0x8006), turn distance (0x8007), ...
                // Informational only, nothing is answered. Before v1.6.3 the generic handlers below caught 0x8007 as a
                // VideoFocusRequest and answered with a video message on ch 10: the phone dropped the session as soon
                // as guidance started and kept reconnecting (car-seen 2026-10-24, followed by a head-unit reset).
                static unsigned nav_msgs = 0;
                if ((nav_msgs++ % 50) == 0 || msg_id == 0x8003)
                    printf("[AASession] nav status ch10: msg 0x%04X (%u bytes)%s\n", msg_id, (unsigned)frame_data.size(),
                           msg_id == 0x8003 ? (pbFirstVarint(frame_data, 2, 1) == 1 ? " guidance ACTIVE" : " guidance inactive") : "");
            } else if (channel == 12 && msg_id >= 0x8000) {
                // Media playback status channel: 0x8001 = {state, source name e.g. "Apple Music", position} every
                // second, 0x8003 = track metadata + album art. Informational only. Before v1.6.6 the generic AV
                // handler below logged every 0x8001 as "AV START_INDICATION on Channel 12". Log state changes only.
                static uint32_t last_state = 0xFFFFFFFFu;
                if (msg_id == 0x8001) {
                    uint32_t st = pbFirstVarint(frame_data, 2, 1);
                    if (st != last_state) {
                        last_state = st;
                        printf("[AASession] media status ch12: state %u\n", st);
                    }
                }
            } else if (msg_id == 0x8000) { // AV SETUP_REQUEST (Phone requests AV configuration on Video/Audio channel)
                // Only focus/open the main dashboard display. Advertising/focusing all
                // DHU display channels makes the phone encode several H.264 streams; we
                // only ingest Channel 6, so keep Channels 2/4 unfocused to reduce load.
                printf("[AASession] >>> Received AV SETUP_REQUEST on Channel %u! Sending SETUP_RESPONSE%s...\n",
                       channel, (channel == 6) ? " and VIDEO_FOCUS" : "");
                sendAvSetupResponse(channel);
                if (channel == 6) {
                    sendVideoFocusIndication(channel);
                }
            } else if (msg_id == 0x8001) { // AV START_INDICATION
                uint32_t sess = pbFirstVarint(frame_data, 2, 1);
                if (sess != 0xFFFFFFFFu) av_session_[channel & 31] = sess;
                printf("[AASession] >>> Received AV START_INDICATION on Channel %u (session %u)! Projection ACTIVE!\n", channel,
                       sess == 0xFFFFFFFFu ? 0u : sess);
                if (isAaAudioSinkChannel(channel) && audio_state_cb_) {
                    audio_state_cb_(channel, true, audio_state_user_data_);
                }
            } else if (msg_id == 0x8002 && isAaAudioSinkChannel(channel)) { // AV STOP_INDICATION (audio)
                printf("[AASession] >>> Received AV STOP_INDICATION on Channel %u.\n", channel);
                if (audio_state_cb_) audio_state_cb_(channel, false, audio_state_user_data_);
            } else if (msg_id == 0x8002 && (channel == 3 || channel == 5 || channel == 7)) {
                // Input BindingRequest: accept it even when touch forwarding is off. The knob, the
                // microphone wake-up, and video focus share channel 7, and the phone waits for this.
                printf("[AASession] >>> Input BindingRequest on Channel %u: accepting.\n", channel);
                sendBindingResponse(channel);
                if (channel == 7) {   // channel 7 = main touchscreen (1280x480) in our service discovery
                    pthread_mutex_lock(&touch_mutex_);
                    input_ready_ = true;
                    touch_queue_.clear();
                    key_queue_.clear();
                    pthread_mutex_unlock(&touch_mutex_);
                    printf("[AASession] Touch forwarding armed.\n");
                }
            } else if (channel == 6 && msg_id == 0x8007) { // VIDEO_FOCUS_REQUEST {disp_index=1, focus_mode=2, focus_reason=3}
                // The phone's "exit to car" icon asks for UNFOCUSED (native UI); answer with the same mode and let
                // the owner hide the overlay. A request for FOCUSED (projection) is granted the same way.
                uint32_t mode = pbFirstVarint(frame_data, 2, 2);
                uint32_t reason = pbFirstVarint(frame_data, 2, 3);
                bool projected = (mode != 2);
                printf("[AASession] >>> VideoFocusRequest on Channel %u: mode=%u (%s) reason=%u\n", channel, mode,
                       projected ? "projected" : "native / exit to car", reason);
                sendVideoFocusIndication(channel, projected, false);
                if (focus_cb_) focus_cb_(projected, reason, focus_user_data_);
            } else if (channel == 6 && msg_id == 0x8008) { // VIDEO_FOCUS_INDICATION
                printf("[AASession] >>> Received VideoFocusIndication on Channel %u.\n", channel);
            } else if (msg_id == 0x0000 || msg_id == 0x0001) { // AV Media Data (Live H.264 video frames)
                media_frames_++;
                if ((media_frames_ % 30) == 1) {
                    printf("[AASession] >>> Streaming Media Frame %u on Channel %u (%u bytes)\n",
                           media_frames_, channel, static_cast<unsigned int>(frame_data.size()));
                }
                if (isAaAudioSinkChannel(channel) && msg_id == 0x0000) {
                    // Audio PCM: 2-byte ID + 8-byte PTS + interleaved s16le (ch 9 48 kHz stereo, ch 8 guidance and
                    // ch 16 system audio 16 kHz mono).
                    if (frame_data.size() > 10) {
                        const size_t pcm_len = frame_data.size() - 10;
                        uint64_t pts = 0;
                        for (int b = 2; b < 10; ++b) pts = (pts << 8) | frame_data[b];
                        uint32_t& n = audio_frames_[channel == AA_CH_MEDIA_AUDIO ? 1 : (channel == AA_CH_SYSTEM_AUDIO ? 2 : 0)];
                        ++n;
                        if (n <= 3 || (n % 500) == 0) {
                            printf("[AASession] audio %s frame #%u: %u bytes PCM, pts=%llu%s\n",
                                   channel == AA_CH_MEDIA_AUDIO ? "media(ch9)" :
                                   (channel == AA_CH_SYSTEM_AUDIO ? "system(ch16)" : "speech(ch8)"), n, (unsigned)pcm_len,
                                   (unsigned long long)pts, audio_cb_ ? "" : " (no consumer)");
                        }
                        if (audio_cb_) {
                            g_crumb_stage = 5;
                            audio_cb_(channel, &frame_data[10], pcm_len, pts, audio_user_data_);
                            g_crumb_stage = 3;
                        }
                    }
                } else if (channel == 6) { // Channel 6 is EXCLUSIVELY the Main Dashboard Screen (display_id=0, display_type=MAIN)
                    // Use protocol boundaries, never scan the message ID or timestamp:
                    // 0x0001 = 2-byte ID + codec data; 0x0000 = ID + 8-byte PTS + video.
                    // Timestamps can contain 00 00 01 and look like Annex-B start codes.
                    const uint8_t* p = frame_data.empty() ? NULL : &frame_data[0];
                    size_t sz = frame_data.size();
                    const size_t offset = (msg_id == 0x0001) ? 2 : 10;
                    if (sz <= offset) {
                        printf("[AASession] Dropping truncated/empty video message 0x%04X (%u bytes).\n",
                               msg_id, static_cast<unsigned int>(sz));
                        continue;
                    }
                    const uint8_t* nal_ptr = p + offset;
                    size_t nal_len = sz - offset;

                    if (msg_id == 0x0001) { // AV_MEDIA_CONFIG (SPS / PPS)
                        sps_pps_cache_.assign(nal_ptr, nal_ptr + nal_len);
                        printf("[AASession] >>> Received H.264 SPS/PPS codec config (%u bytes)!\n",
                               static_cast<unsigned int>(nal_len));
                        if (config_cb_) {
                            config_cb_(nal_ptr, nal_len, config_user_data_);
                        }
                    } else { // 0x0000 = AV_MEDIA_DATA (Video Slices with 8-byte PTS)
                        uint64_t pts = 0;
                        if (sz >= 10) {
                            pts = (static_cast<uint64_t>(p[2]) << 56) |
                                  (static_cast<uint64_t>(p[3]) << 48) |
                                  (static_cast<uint64_t>(p[4]) << 40) |
                                  (static_cast<uint64_t>(p[5]) << 32) |
                                  (static_cast<uint64_t>(p[6]) << 24) |
                                  (static_cast<uint64_t>(p[7]) << 16) |
                                  (static_cast<uint64_t>(p[8]) << 8)  |
                                   static_cast<uint64_t>(p[9]);
                        }

                        // VideoManager owns the hardware decoder feed and performs any
                        // in-band SPS/PPS prepend next to the mdev Message 3 send. Keep
                        // AASession limited to protocol parsing and fixed-offset slicing.
                        if (video_cb_) {
                            g_crumb_stage = 4;
                            video_cb_(media_frames_, nal_ptr, nal_len, pts, video_user_data_);
                            g_crumb_stage = 3;
                        }
                    }
                }
                sendAvMediaAck(channel, media_frames_);
            } else if (channel == 0) {
                if (msg_id == 2) { // VersionResponse
                    if (frame_data.size() >= 8) {
                        uint16_t major  = (frame_data[2] << 8) | frame_data[3];
                        uint16_t minor  = (frame_data[4] << 8) | frame_data[5];
                        uint16_t status = (frame_data[6] << 8) | frame_data[7];
                        printf("[AASession] >>> VersionResponse: Major=%u, Minor=%u, Status=%u (SUCCESS)\n",
                               major, minor, status);

                        // Step 2: Initialize Cryptor with mTLS cert and send TLS ClientHello
                        if (cryptor_.init()) {
                            std::vector<uint8_t> tls_client_hello;
                            if (cryptor_.startHandshake(tls_client_hello)) {
                                sendSslHandshake(tls_client_hello);
                                printf("[AASession] Sent TLS ClientHello (with mTLS client cert) to phone.\n");
                            }
                        }
                    }
                } else if (msg_id == 3) { // SSL Handshake
                    if (frame_data.size() < 3) {
                        printf("[AASession] Ignoring empty SSL handshake message.\n");
                        continue;
                    }
                    printf("[AASession] >>> Received SSL Handshake from phone (%u bytes)\n",
                           static_cast<unsigned int>(frame_data.size() - 2));

                    std::vector<uint8_t> tls_out;
                    cryptor_.processHandshakeData(&frame_data[2], frame_data.size() - 2, tls_out);

                    if (!tls_out.empty()) {
                        sendSslHandshake(tls_out);
                        printf("[AASession] Sent TLS Handshake response (%u bytes).\n",
                               static_cast<unsigned int>(tls_out.size()));
                    }

                    if (cryptor_.isHandshakeComplete()) {
                        printf("[AASession] >>> TLS Handshake Complete! Sending AuthCompleteNotification...\n");
                        sendAuthComplete();
                        printf("[AASession] >>> Awaiting ServiceDiscoveryRequest from phone...\n");
                    }
                } else if (msg_id == 5) { // ServiceDiscoveryRequest
                    printf("[AASession] >>> Received ServiceDiscoveryRequest from phone! Sending ServiceDiscoveryResponse...\n");
                    sendServiceDiscoveryResponse();
                } else if (msg_id == 0x0012) { // AudioFocusRequest
                    uint8_t requested_focus = (frame_data.size() >= 4) ? frame_data[3] : 0;
                    // AudioFocusType 1 GAIN, 2 GAIN_TRANSIENT, 3 GAIN_NAVI (guidance prompts), 4 RELEASE. Like openauto, answer
                    // RELEASE with LOSS (3) and everything else with GAIN (1); answering RELEASE with NONE (0) left the
                    // phone without a clean end of each guidance prompt.
                    uint8_t response_focus = (requested_focus == 4) ? 3 : 1;
                    printf("[AASession] >>> Received AudioFocusRequest on Channel 0 (request=%u)! Sending AudioFocusResponse(state=%u)...\n",
                           requested_focus, response_focus);
                    sendAudioFocusResponse(response_focus);
                } else if (msg_id == 0x000D) { // NavigationFocusRequest {type=1}: 1 NATIVE, 2 PROJECTED
                    // Maps asks for navigation focus when guidance starts (2) and ends (1). Unanswered it never gets
                    // it. Like openauto, grant PROJECTED: this head unit has no native guidance of its own running.
                    uint32_t want = pbFirstVarint(frame_data, 2, 1);
                    uint8_t resp[4] = { 0x00, 0x0E, 0x08, 0x02 };   // NavigationFocusResponse {type=2 PROJECTED}
                    std::vector<uint8_t> enc;
                    bool ok = cryptor_.encrypt(resp, sizeof(resp), enc) && sendFrame(0x00, 0x0B, &enc[0], enc.size());
                    printf("[AASession] >>> NavigationFocusRequest (type=%d) -> NavigationFocusResponse(PROJECTED) %s\n",
                           want == 0xFFFFFFFFu ? -1 : (int)want, ok ? "sent" : "SEND FAILED");
                } else if (msg_id == 0x0011) { // VoiceSessionRequest {type=1}: 1 START, 2 STOP (informational)
                    uint32_t vs = pbFirstVarint(frame_data, 2, 1);
                    printf("[AASession] >>> VoiceSessionRequest: %s\n", vs == 1 ? "START" : vs == 2 ? "STOP" : "?");
                } else if (msg_id == 0x000F) { // ByeByeRequest {reason=1}: the phone ends the session
                    // Answer with ByeByeResponse (0x0010, empty) and end the session ourselves, as aasdk/openauto do.
                    // Up to v1.8.41 this was only logged as an unknown control message and the session waited for the
                    // phone to drop the socket (tester bootloop log 2026-10-29: 27 ByeByes, reason 1, mostly right
                    // after the ServiceDiscoveryResponse, phone reconnecting every ~3 s).
                    uint32_t reason = pbFirstVarint(frame_data, 2, 1);
                    uint8_t resp[2] = { 0x00, 0x10 };
                    std::vector<uint8_t> enc;
                    bool ok = cryptor_.encrypt(resp, sizeof(resp), enc) && sendFrame(0x00, 0x0B, &enc[0], enc.size());
                    printf("[AASession] >>> ByeByeRequest from the phone (reason=%d%s, %u media frames this session): "
                           "ByeByeResponse %s, ending the session\n",
                           reason == 0xFFFFFFFFu ? -1 : (int)reason, reason == 1 ? " = quit" : "",
                           (unsigned)media_frames_, ok ? "sent" : "SEND FAILED");
                    break;
                } else if (msg_id == 11) { // PingRequest
                    printf("[AASession] >>> Received PingRequest. Replying with PingResponse...\n");
                    // PingResponse {timestamp = field 1}: echo the request's timestamp varint.
                    std::vector<uint8_t> ping_resp;
                    ping_resp.push_back(0x00);
                    ping_resp.push_back(0x0C); // Msg ID 12 (PingResponse)
                    if (frame_data.size() > 3 && frame_data[2] == 0x08) {
                        size_t i = 3;
                        ping_resp.push_back(0x08);
                        while (i < frame_data.size() && ping_resp.size() < 16) {
                            uint8_t b = frame_data[i++];
                            ping_resp.push_back(b);
                            if (!(b & 0x80)) break;
                        }
                    }
                    sendFrame(0x00, 0x03, &ping_resp[0], ping_resp.size()); // Pings are unencrypted
                } else {
                    printf("[AASession] Control Message RX: ID=0x%04X, Len=%u\n",
                           msg_id, static_cast<unsigned int>(frame_data.size()));
                }
            } else {
                printf("[AASession] Channel %u Message: ID=0x%04X, Len=%u\n",
                       channel, msg_id, static_cast<unsigned int>(frame_data.size()));
            }
        }
    }

    printf("[AASession] Session loop terminated.\n");
    g_crumb_stage = 0;
    finishSession();
}

void AASession::finishSession() {
    // Only this thread closes the socket. Whether the session ended on its own or stop()
    // asked it to, release the fd here, then tell the owner if nobody requested the stop.
    bool ended_on_its_own = exchangeRunningOff();
    releaseSocket();
    if (ended_on_its_own && disconnect_cb_) {
        disconnect_cb_(disconnect_user_data_);
    }
}

} // namespace nbt
