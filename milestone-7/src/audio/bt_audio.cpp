// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio/bt_audio.hpp"
#include "audio/channels.hpp"
#include "nbt_env.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace nbt {

BtAudio::BtAudio() : src_(0x3a), sink_(0x1), conn_(0xc7), speech_(route_), snaps_(0) {}

void BtAudio::grantedThunk(void* self) {
    static_cast<BtAudio*>(self)->play_.play("car granted the Bluetooth source");
}

// NBT_BT_ROUTE_DELAY_MS (default 1500, 0 = at once): wait this long after the session starts before running
// audioconn. A phone that says ByeBye right after the ServiceDiscovery and reconnects every ~3 s (tester bootloop log,
// 2026-10-29) then never starts an audioconn at all: onSessionEnded() cancels the request while it waits.
int BtAudio::routeDelayMs() {
    const char* e = getenv("NBT_BT_ROUTE_DELAY_MS");
    if (!e || !*e) return 1500;
    int v = atoi(e);
    return v < 0 ? 0 : (v > 10000 ? 10000 : v);
}

// Default ON since v1.7.7 (car-verified nbt-aad-22-2-2: with it the Assistant's reply is heard).
// NBT_BT_REFOCUS=0 / off / no / false disables.
bool BtAudio::refocusEnabled() {
    return envFlag("NBT_BT_REFOCUS") != 0;   // unset stays on
}

bool BtAudio::requestSource() {
    if (conn_) return route_.build(conn_, &grantedThunk, this, routeDelayMs());
    return route_.buildFocus(src_, sink_, &grantedThunk, this, routeDelayMs());
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

void BtAudio::init() {
    src_ = (uint32_t)envHex("NBT_BT_SRC", 0x3a);
    sink_ = (uint32_t)envHex("NBT_BT_SINK", 0x1);
    printf("[BtAudio] NBT_AUDIO_BT on: AA media sink left out of the ServiceDiscovery (NBT_SDR_DROP=%s),\n"
           "[BtAudio] phone audio stays on Bluetooth A2DP; A2I pipeline not opened. Source 0x%x -> sink 0x%x.\n",
           getenv("NBT_SDR_DROP") ? getenv("NBT_SDR_DROP") : "9", src_, sink_);
    if (!route_.configure()) printf("[BtAudio] audioconn not found: select Bluetooth audio in iDrive manually.\n");
    conn_ = (uint32_t)envHex("NBT_BT_CONN", 0xc7);
    if (conn_)
        printf("[BtAudio] Bluetooth source via ConnectionMaster `audioconn -b 0x%x` (NBT_BT_CONN=0: focus form)\n",
               conn_);
    else
        printf("[BtAudio] NBT_BT_CONN=0: Bluetooth source via AudioManagement focus `audioconn -b 0x%x 0x%x`\n",
               src_, sink_);
    play_.init();
    speech_.init();
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
    if (getenv("NBT_BT_RELEASE") && strcmp(getenv("NBT_BT_RELEASE"), "1") == 0) {
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
    // Default on (NBT_BT_REFOCUS=0 disables): the Assistant pauses the music before it listens, so its reply restarts
    // a suspended A2DP stream. Re-request the car's BT audio source when the phone closes the mic, in case the car
    // does not pick the restarted stream up on its own.
    if (refocusEnabled()) {
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
