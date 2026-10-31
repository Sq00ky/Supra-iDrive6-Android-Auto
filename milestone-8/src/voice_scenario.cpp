// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "voice_scenario.hpp"

#ifndef NBT_HOST_TEST
#include "dsi_client.hpp"
#include <pthread.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace nbt {

namespace {
const uint32_t kReqAdd    = 3;            // RequestAddAmxAudioScenario (Connectivity FUN_002a1b28, handsfreed case 3)
const uint32_t kReqRemove = 4;            // RequestRemoveAmxAudioScenario (FUN_0061d4e0, handsfreed case 4)
const uint32_t kRspAdd    = 0x80000003u;  // ResponseAddAmxAudioScenario: u32 result, 0 = OK
const uint32_t kRspRemove = 0x80000004u;  // ResponseRemoveAmxAudioScenario

#ifndef NBT_HOST_TEST
DsiClient g_dsi;
pthread_mutex_t g_txMutex = PTHREAD_MUTEX_INITIALIZER;

void onAttached(DsiClient*, void*) {
    printf("[Voice] attached to Nuance_ATP_Audio_Service.DSCPSpeechHandsfreeAudio (handsfreed)\n");
    fflush(stdout);
}

void onData(DsiClient*, uint32_t type, uint32_t id, uint32_t, const uint8_t* pl, uint32_t len, void*) {
    int res = -1;
    const char* what = VoiceScenario::resultText(id, pl, len, &res);
    if (what) printf("[Voice] %s -> result %d (%s)\n", what, res, res == 0 ? "OK" : "FAILED");
    else printf("[Voice] RX type=0x%x id=0x%x len=%u (ignored)\n", type, id, len);
    fflush(stdout);
}
#endif
} // namespace

VoiceScenario::VoiceScenario() : enabled_(false), active_(false) {
    strcpy(name_, "Carplay_Siri_1mic");
}

void VoiceScenario::buildRequest(std::vector<uint8_t>& out, const char* scenario) {
    // DSI string: u32 length incl. NUL, bytes, pad to 4 (same encoding as BSS_GAP SppUuidRegister).
    out.clear();
    uint32_t n = (uint32_t)strlen(scenario) + 1;
    for (int i = 0; i < 4; ++i) out.push_back((uint8_t)((n >> (8 * i)) & 0xff));
    out.insert(out.end(), scenario, scenario + n);
    while (out.size() % 4) out.push_back(0);
}

const char* VoiceScenario::resultText(uint32_t id, const uint8_t* pl, uint32_t len, int* result) {
    if (id != kRspAdd && id != kRspRemove) return NULL;
    if (result) *result = (pl && len >= 4) ? (int)(pl[0] | (pl[1] << 8) | (pl[2] << 16) | ((uint32_t)pl[3] << 24)) : -1;
    return id == kRspAdd ? "AddAmxAudioScenario" : "RemoveAmxAudioScenario";
}

void VoiceScenario::init() {
    // Default ON since v1.7.6 (car-verified 2026-10-25: without it the car mic delivers pure silence; with it the
    // Assistant hears the driver, and nav/music were unaffected). NBT_VOICE_SCENARIO=0 / off / no / false disables.
    const char* e = getenv("NBT_VOICE_SCENARIO");
    enabled_ = !(e && *e && (strcmp(e, "0") == 0 || strcmp(e, "off") == 0 || strcmp(e, "no") == 0 ||
                             strcmp(e, "false") == 0));
    const char* n = getenv("NBT_VOICE_SCENARIO_NAME");
    if (n && *n && strlen(n) < sizeof(name_)) strcpy(name_, n);
    if (!enabled_) {
        printf("[Voice] car mic scenario: off (NBT_VOICE_SCENARIO=0; the Assistant will hear silence)\n");
        return;
    }
    printf("[Voice] car mic scenario: on, '%s' while the phone has the mic open\n", name_);
#ifndef NBT_HOST_TEST
    g_dsi.setQuiet(true);
    g_dsi.setCallbacks(&onAttached, &onData, NULL);
    if (!g_dsi.open("DSCPSpeechHandsfreeAudio", "Nuance_ATP_Audio_Service", 1, 0)) {
        printf("[Voice] could not open the DSI client: scenario switching disabled\n");
        enabled_ = false;
    }
#endif
    fflush(stdout);
}

bool VoiceScenario::send(uint32_t reqId, const char* what, const char* why) {
#ifndef NBT_HOST_TEST
    if (!g_dsi.attached()) {
        printf("[Voice] %s('%s') skipped (%s): handsfreed not attached\n", what, name_, why);
        fflush(stdout);
        return false;
    }
    std::vector<uint8_t> p;
    buildRequest(p, name_);
    // Sent from the main loop rather than a connection-thread callback: this client's connection thread only
    // receives the two responses, and every send for this client goes through this mutex.
    pthread_mutex_lock(&g_txMutex);
    bool ok = g_dsi.sendRequest(reqId, p);
    pthread_mutex_unlock(&g_txMutex);
    printf("[Voice] %s('%s') sent (%s)%s\n", what, name_, why, ok ? "" : " FAILED");
    fflush(stdout);
    return ok;
#else
    (void)reqId; (void)what; (void)why;
    return true;
#endif
}

void VoiceScenario::activate(const char* why) {
    if (!enabled_ || active_) return;
    if (send(kReqAdd, "AddAmxAudioScenario", why)) active_ = true;
}

void VoiceScenario::deactivate(const char* why) {
    if (!enabled_ || !active_) return;
    send(kReqRemove, "RemoveAmxAudioScenario", why);
    active_ = false;   // never retry a remove in a loop
}

} // namespace nbt
