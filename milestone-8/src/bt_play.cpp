// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "bt_play.hpp"

#ifndef NBT_HOST_TEST
#include "dsi_client.hpp"
#include <pthread.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace nbt {

namespace {
const uint32_t kReqActivate    = 2;            // DMMAudioSource requestActivate (NBTMediaMainApp FUN_0050f518)
const uint32_t kRspActivate    = 0x80000003u;  // responseActivate(activity) FUN_0050f42c
const uint32_t kActPlay        = 0;            // DSI activity: 0 PLAY (-> internal On), 1 STOP (Off), 2 PAUSE
const uint32_t kActStop        = 1;
const uint32_t kReqSendCommand = 0;            // SGMedia SendCommand (libdsiservice FUN_00193534)
const uint32_t kRspSendCommand = 0x80000003u;
const uint32_t kCmdStart       = 0;            // MEDIA_COMMAND_START (libmediaservice FUN_00023a64: AVRCP PLAY)

#ifndef NBT_HOST_TEST
DsiClient g_src;      // AuxInBTAudio.DMMAudioSource 9.0 (NBTMediaMainApp)
DsiClient g_media;    // BlueMedia.SGMedia 2.1 (Connectivity / libdsiservice)
pthread_mutex_t g_txMutex = PTHREAD_MUTEX_INITIALIZER;

void onSrcAttached(DsiClient*, void*) {
    printf("[BtPlay] attached to AuxInBTAudio.DMMAudioSource\n");
    fflush(stdout);
}
void onMediaAttached(DsiClient*, void*) {
    printf("[BtPlay] attached to BlueMedia.SGMedia\n");
    fflush(stdout);
}
void onData(bool sg, uint32_t type, uint32_t id, const uint8_t* pl, uint32_t len) {
    int v = -1;
    const char* what = BtPlay::responseText(sg, id, pl, len, &v);
    if (what) printf("[BtPlay] %s %d%s%s\n", what, v, sg ? " " : "", sg ? BtPlay::sgResultName(v) : "");
    else printf("[BtPlay] %s RX type=0x%x id=0x%x len=%u\n", sg ? "SGMedia" : "DMMAudioSource", type, id, len);
    fflush(stdout);
}
void onSrcData(DsiClient*, uint32_t type, uint32_t id, uint32_t, const uint8_t* pl, uint32_t len, void*) {
    onData(false, type, id, pl, len);
}
void onMediaData(DsiClient*, uint32_t type, uint32_t id, uint32_t, const uint8_t* pl, uint32_t len, void*) {
    onData(true, type, id, pl, len);
}

struct Job { BtPlay* self; const char* why; };
void* playWorker(void* arg) {
    Job* j = static_cast<Job*>(arg);
    int ms = j->self->delayMs();
    if (ms > 0) usleep((useconds_t)ms * 1000u);
    j->self->sendNow(true, j->why);
    delete j;
    return NULL;
}
#endif
} // namespace

BtPlay::BtPlay() : mode_(0), delay_(1500) {}

void BtPlay::buildU32(std::vector<uint8_t>& out, uint32_t v) {
    out.clear();
    for (int i = 0; i < 4; ++i) out.push_back((uint8_t)((v >> (8 * i)) & 0xff));
}

const char* BtPlay::sgResultName(int r) {
    switch (r) {
    case 0: return "(OK)";
    case 1: return "(TIMEOUT)";
    case 2: return "(NOT_SUPPORTED)";
    case 3: return "(NOT_ALLOWED: A2DP connecting/disconnecting)";
    case 5: return "(SYSTEM_ERROR: no A2DP stream connected)";
    default: return "(?)";
    }
}

const char* BtPlay::responseText(bool sgmedia, uint32_t id, const uint8_t* pl, uint32_t len, int* value) {
    if (value) *value = (pl && len >= 4) ? (int)(pl[0] | (pl[1] << 8) | (pl[2] << 16) | ((uint32_t)pl[3] << 24)) : -1;
    if (!sgmedia && id == kRspActivate) return "DMMAudioSource responseActivate: activity";
    if (sgmedia && id == kRspSendCommand) return "SGMedia SendCommand(START) result";
    return NULL;
}

void BtPlay::init() {
    const char* e = getenv("NBT_BT_PLAY");
    mode_ = (e && (e[0] == '1' || e[0] == '2')) ? e[0] - '0' : 0;
    const char* d = getenv("NBT_BT_PLAY_DELAY");
    if (d && *d) delay_ = atoi(d);
    if (!mode_) return;
    printf("[BtPlay] NBT_BT_PLAY=%d (experiment): SourceActivity PLAY on AuxInBTAudio.DMMAudioSource%s, %d ms after "
           "the Bluetooth source request\n", mode_, mode_ == 2 ? " + SGMedia START" : "", delay_);
#ifndef NBT_HOST_TEST
    g_src.setCallbacks(&onSrcAttached, &onSrcData, NULL);
    if (!g_src.open("DMMAudioSource", "AuxInBTAudio", 9, 0)) printf("[BtPlay] DMMAudioSource client could not be opened\n");
    if (mode_ == 2) {
        g_media.setCallbacks(&onMediaAttached, &onMediaData, NULL);
        if (!g_media.open("SGMedia", "BlueMedia", 2, 1)) printf("[BtPlay] SGMedia client could not be opened\n");
    }
#endif
    fflush(stdout);
}

void BtPlay::sendNow(bool playing, const char* why) {
#ifndef NBT_HOST_TEST
    std::vector<uint8_t> p;
    // Sent off the connection thread like VoiceScenario (car-verified pattern): these clients' connection threads
    // only receive responses, and every send goes through this mutex.
    pthread_mutex_lock(&g_txMutex);
    if (g_src.attached()) {
        buildU32(p, playing ? kActPlay : kActStop);
        bool ok = g_src.sendRequest(kReqActivate, p);
        printf("[BtPlay] DMMAudioSource requestActivate(%s) sent (%s)%s\n", playing ? "PLAY" : "STOP", why,
               ok ? "" : " FAILED");
    } else {
        printf("[BtPlay] requestActivate(%s) skipped (%s): AuxInBTAudio.DMMAudioSource not attached "
               "(interface version 9.0 refused?)\n", playing ? "PLAY" : "STOP", why);
    }
    if (playing && mode_ == 2) {
        if (g_media.attached()) {
            buildU32(p, kCmdStart);
            bool ok = g_media.sendRequest(kReqSendCommand, p);
            printf("[BtPlay] SGMedia SendCommand(START) sent (%s)%s\n", why, ok ? "" : " FAILED");
        } else {
            printf("[BtPlay] SendCommand(START) skipped (%s): BlueMedia.SGMedia not attached\n", why);
        }
    }
    pthread_mutex_unlock(&g_txMutex);
    fflush(stdout);
#else
    (void)playing; (void)why;
#endif
}

void BtPlay::play(const char* why) {
    if (!mode_) return;
#ifndef NBT_HOST_TEST
    Job* j = new Job;
    j->self = this;
    j->why = why;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    if (pthread_create(&t, &at, &playWorker, j) != 0) {
        delete j;
        printf("[BtPlay] could not start the play worker\n");
    }
    pthread_attr_destroy(&at);
#else
    (void)why;
#endif
}

void BtPlay::stop(const char* why) {
    if (!mode_) return;
    sendNow(false, why);
}

} // namespace nbt
