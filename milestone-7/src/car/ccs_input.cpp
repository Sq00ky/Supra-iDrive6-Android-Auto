// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "car/ccs_input.hpp"
#include "dsi_client.hpp"
#include "most/ids.hpp"
#include "most/msg.hpp"
#include "car/ccs_config.hpp"
#include "car/ptt.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

namespace nbt {
namespace car {

namespace {

DsiClient g_most;
CcsDecoder g_dec;
CcsEventCb g_cb = NULL;
void* g_user = NULL;
uint32_t g_client = most::kClientRefused;
int g_stage = 0;           // 0 init, 1 name sent, 2 devctrls sent (connection thread only)
unsigned g_msgs = 0, g_ccs = 0;

// The steering-wheel voice button is already in our registerDevCtrls list (0x1502). Observe-only unless NBT_PTT_ASSISTANT=1,
// which sends the Assistant key (AA keycode 84 SEARCH, advertised on input channel 7) after a long press.
PttTracker g_ptt;
bool g_pttAssistant = false;
uint32_t pttNowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}
void onPtt(const most::Msg& m) {
    if (m.data.empty()) return;
    unsigned v = m.data[0];
    PttTracker::Result r = g_ptt.feed((uint8_t)v, pttNowMs());
    if (r.kind == PttTracker::kPressed) {
        printf("[PTT] voice button pressed (15/%02X fkt 512 = 01)\n", m.inst);
        return;
    }
    if (r.kind == PttTracker::kIgnored) { printf("[PTT] voice button state %u (ignored)\n", v); return; }
    if (r.kind == PttTracker::kCancelled) { printf("[PTT] voice button cancelled\n"); return; }
    bool lng = r.kind == PttTracker::kReleasedLong;
    printf("[PTT] voice button released after %u ms (%s press%s)\n", (unsigned)r.heldMs, lng ? "long" : "short",
           lng ? (g_pttAssistant ? ": Google Assistant" : "; NBT_PTT_ASSISTANT=1 would start the Assistant") : ": iDrive voice");
    if (lng && g_pttAssistant && g_cb) {
        CcsEvent ev;
        ev.kind = 1; ev.keycode = AAK_SEARCH; ev.delta = 0; ev.what = "PTT";
        ev.down = 1; g_cb(ev, g_user);
        ev.down = 0; g_cb(ev, g_user);
    }
}

void onAttached(DsiClient* c, void*) {
    printf("[Knob] attached to MOSTSubSystem.DSYSMOSTServer: registering MOST client\n");
    std::vector<uint8_t> p;
    DsiClient::putString(p, "NBTAAD_KNOB");
    g_stage = 1;
    c->sendRequest(most::kReqRegisterClient, p);
    // Same subscriptions as the HMI's CHBMOSTClientImpl::componentConnected (car-verified in test_controls v2).
    for (size_t i = 0; i < (size_t)most::kClientSubscriptionCount; ++i) c->setNotification(most::kClientSubscriptions[i]);
}

void onData(DsiClient* c, uint32_t, uint32_t id, uint32_t, const uint8_t* pl, uint32_t len, void*) {
    if (id == most::kInfoClientResponse && len >= 6) {
        g_client = most::rdLe32(pl);
        unsigned dev = most::rdLe16(pl + 4);
        printf("[Knob] registered: clientId=%u deviceId=0x%04X%s\n", g_client, dev,
               g_client == most::kClientRefused ? "  (REGISTRATION REFUSED)" : "");
        if (g_client != most::kClientRefused && g_stage < 2) {
            c->setNotification(most::kAttrCommState);
            static const uint16_t kKeys[] = { 0xE510, 0xE503, 0xE505, 0xE512, 0xE514, 0xE501, 0xE502, 0x1502 };
            std::vector<uint16_t> keys(kKeys, kKeys + sizeof(kKeys) / sizeof(kKeys[0]));
            g_stage = 2;
            c->sendRequest(most::kReqRegisterDevCtrls, most::buildRegisterDevCtrls(g_client, keys));
            printf("[Knob] registerDevCtrls sent (8 keys): waiting for controller events\n");
        }
        return;
    }
    if (id != most::kInfoMessage) return;
    most::Msg m;
    if (!most::parseMessage(pl, len, m)) return;
    ++g_msgs;
    if (m.fblock == 0x15) {
        if (m.fkt == 0x512 && m.opType == most::kOpStatus) onPtt(m);
        return;
    }
    if (m.fblock != 0xE5) return;
    if (m.fkt == 0xC10 || m.fkt == 0xC20) {
        // Older CCS controller protocol: layout from the firmware, not yet confirmed on a car -> log raw.
        static unsigned s_ccsLogged = 0;
        if (s_ccsLogged < 40) {
            ++s_ccsLogged;
            printf("[Knob] E5/%02X fkt=%03X op=%X len=%u (CCS protocol):", m.inst, m.fkt, m.opType, (unsigned)m.data.size());
            for (size_t i = 0; i < m.data.size() && i < 16; ++i) printf(" %02x", m.data[i]);
            printf("\n");
        }
    }
    if (m.fkt == 0xC46 || m.fkt == 0xC47 || m.fkt == 0xC4F) {
        ++g_ccs;
        if (g_ccs <= 3 || (m.fkt == 0xC46 && m.data.size() >= 7 && (m.data[3] || m.data[5] || m.data[6]))) {
            printf("[Knob] E5/%02X fkt=%03X op=%X len=%u:", m.inst, m.fkt, m.opType, (unsigned)m.data.size());
            for (size_t i = 0; i < m.data.size() && i < 16; ++i) printf(" %02x", m.data[i]);
            printf("\n");
        }
    }
    CcsEvent ev[16];
    int n = g_dec.feed(m.fblock, m.inst, m.fkt, m.opType, m.data.empty() ? NULL : &m.data[0], m.data.size(), ev, 16);
    for (int i = 0; i < n; ++i) {
        if (ev[i].kind == 2)
            printf("[Knob] %s delta=%d -> AA relative key %u\n", ev[i].what, ev[i].delta, ev[i].keycode);
        else
            printf("[Knob] %s %s -> AA key %u%s\n", ev[i].what, ev[i].down ? "down" : "up", ev[i].keycode,
                   ev[i].keycode ? "" : " (unmapped, not sent)");
        if (g_cb && (ev[i].kind == 2 || ev[i].keycode)) g_cb(ev[i], g_user);
    }
}

} // namespace

bool startCcsInput(CcsEventCb cb, void* user) {
    g_cb = cb;
    g_user = user;
    CcsConfig cfg = readCcsConfig();
    applyCcsConfig(cfg, g_dec);
    g_pttAssistant = cfg.pttAssistant;
    printf("[Knob] iDrive controller input: instance %02X (CCS35up C46/C47) or %02X (CCS C10/C20), rotary%s, keys:",
           cfg.inst, cfg.ccsInst, cfg.invert ? " inverted" : "");
    for (int s = 0; s < CCS_SLOTS; ++s) printf(" %s=%u", CcsDecoder::slotName(s), g_dec.slotKey(s));
    printf("\n");
    g_most.setQuiet(getenv("NBT_KNOB_RAW") == NULL);
    g_most.setCallbacks(&onAttached, &onData, NULL);
    if (!g_most.open(most::kServerInterface, most::kServerRole, most::kInterfaceMajor, most::kInterfaceMinor)) {
        printf("[Knob] DSI open FAILED: knob/keys disabled\n");
        return false;
    }
    return true;
}

} // namespace car
} // namespace nbt
