// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio/bt_audio.hpp"
#include "audio/channels.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace nbt {

BtAudio::BtAudio()
    : src_(0x3a), sink_(0x1), conn_(0xc7), route_delay_ms_(1500), refocus_(true), release_(false), sdr_drop_("9"),
      speech_(route_), snaps_(0) {}

void BtAudio::grantedThunk(void* self) {
    static_cast<BtAudio*>(self)->play_.play("car granted the Bluetooth source");
}

bool BtAudio::requestSource() {
    // The delay (NBT_BT_ROUTE_DELAY_MS): onSessionEnded() cancels a request still waiting, so a phone that reconnects
    // every few seconds never starts an audioconn at all.
    if (conn_) return route_.build(conn_, &grantedThunk, this, route_delay_ms_);
    return route_.buildFocus(src_, sink_, &grantedThunk, this, route_delay_ms_);
}

void BtAudio::releaseSource() {
    play_.stop("session ended");
    if (conn_) route_.remove(conn_);
    else route_.removeFocus(src_, sink_);
}

void BtAudio::describe(char* out, size_t n) {
    if (conn_) snprintf(out, n, "connection 0x%x (ConnectionMaster)", conn_);
    else snprintf(out, n, "source 0x%x -> sink 0x%x", src_, sink_);
}

void BtAudio::init(const BtConfig& cfg) {
    src_ = cfg.src;
    sink_ = cfg.sink;
    route_delay_ms_ = cfg.route_delay_ms;
    refocus_ = cfg.refocus;
    release_ = cfg.release;
    sdr_drop_ = cfg.sdr_drop;
    printf("[BtAudio] NBT_AUDIO_BT on: AA media sink left out of the ServiceDiscovery (NBT_SDR_DROP=%s),\n"
           "[BtAudio] phone audio stays on Bluetooth A2DP; A2I pipeline not opened. Source 0x%x -> sink 0x%x.\n",
           sdr_drop_, src_, sink_);
    if (!route_.configure()) printf("[BtAudio] audioconn not found: select Bluetooth audio in iDrive manually.\n");
    conn_ = cfg.conn;
    if (conn_)
        printf("[BtAudio] Bluetooth source via ConnectionMaster `audioconn -b 0x%x` (NBT_BT_CONN=0: focus form)\n",
               conn_);
    else
        printf("[BtAudio] NBT_BT_CONN=0: Bluetooth source via AudioManagement focus `audioconn -b 0x%x 0x%x`\n",
               src_, sink_);
    play_.init();
    speech_.init(cfg.speech);
}

void BtAudio::onSessionStarted() {
    char d[64];
    describe(d, sizeof(d));
    printf("[BtAudio] session started: requesting car Bluetooth audio %s\n", d);
    if (!requestSource()) printf("[BtAudio] could not queue audioconn\n");
}

void BtAudio::onFrame(uint8_t channel, const uint8_t* pcm, size_t len, uint64_t pts) {
    (void)pts;
    if (channel == AA_CH_GUIDANCE_AUDIO) speech_.frame(pcm, len);
}

void BtAudio::onStreamState(uint8_t channel, bool started, uint32_t rate, uint32_t channels) {
    (void)rate;
    (void)channels;
    if (channel == AA_CH_GUIDANCE_AUDIO) speech_.state(started);
}

void BtAudio::onSessionEnded() {
    // Drop the Bluetooth-source request (and its retries) this session queued, so a phone that keeps reconnecting
    // cannot stack audioconn processes until the head unit runs out of memory and resets (tester log 2026-10-29).
    route_.cancelPending();
    speech_.onSessionEnded();
    if (release_) {
        char d[64];
        describe(d, sizeof(d));
        printf("[BtAudio] session ended: releasing %s\n", d);
        releaseSource();
    }
}

// Assistant reply loudness (quiet next to music): snapshot the car's audio connections while it listens and while it
// replies, to see whether the car ducks the Bluetooth source during voice. Read-only, first 3 turns.
void BtAudio::snapshot(bool listening) {
    if (snaps_ >= 6) return;
    ++snaps_;
    route_.listConnections(listening ? "Assistant listening" : "Assistant replying", listening ? 1000 : 2500);
}

void BtAudio::onMicOpened() {
    snapshot(true);
}

void BtAudio::onMicClosed() {
    // Default on since v1.7.7 (NBT_BT_REFOCUS=0 disables; car-verified: with it the Assistant's reply is heard): the
    // Assistant pauses the music before it listens, so its reply restarts a suspended A2DP stream. Re-request the
    // car's BT audio source when the phone closes the mic, in case the car does not pick the restarted stream up on
    // its own.
    if (refocus_) {
        char d[64];
        describe(d, sizeof(d));
        printf("[BtAudio] mic closed: re-requesting car Bluetooth audio %s for the reply\n", d);
        requestSource();
    }
    snapshot(false);
}

void BtAudio::cancelPending() {
    route_.cancelPending();
}

void BtAudio::stop() {
    speech_.stop();
}

} // namespace nbt
