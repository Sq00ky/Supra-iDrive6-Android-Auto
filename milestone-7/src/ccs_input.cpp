// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "ccs_input.hpp"
#include "dsi_client.hpp"
#include "most_msg.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

namespace nbt {

namespace {

DsiClient g_most;
CcsDecoder g_dec;
CcsEventCb g_cb = NULL;
void* g_user = NULL;
uint32_t g_client = 0xffffffffu;
int g_stage = 0;           // 0 init, 1 name sent, 2 devctrls sent (connection thread only)
unsigned g_msgs = 0, g_ccs = 0;

// Steering-wheel voice (PTT) button: MOST FBlock 0x15 (MFL) inst 02, fkt 0x512 (PTTButton1), Status, 1 byte
// (1 pressed, 0 released, 3 cancel, 2 ignored). Already in our registerDevCtrls list (0x1502). Copies only: the HMI
// still gets it (short press = BMW voice dialog, long press = phone voice session). The HMI times a long press
// itself (750 ms, HmiMain3D onPTTButtonStatus @0x252334); so do we. Observe-only unless NBT_PTT_ASSISTANT=1, which
// sends the Assistant key (AA keycode 84 SEARCH, advertised on input channel 7) after a long press.
uint32_t g_pttDownMs = 0;      // 0 = not held
uint32_t pttNowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}
bool pttAssistant() {
    const char* e = getenv("NBT_PTT_ASSISTANT");
    return e && e[0] == '1';
}
void onPtt(const MostMsg& m) {
    if (m.data.empty()) return;
    unsigned v = m.data[0];
    if (v == 1) {
        g_pttDownMs = pttNowMs() | 1u;
        printf("[PTT] voice button pressed (15/%02X fkt 512 = 01)\n", m.inst);
        return;
    }
    if (v != 0 && v != 3) { printf("[PTT] voice button state %u (ignored)\n", v); return; }
    uint32_t held = g_pttDownMs ? pttNowMs() - g_pttDownMs : 0;
    bool wasDown = g_pttDownMs != 0;
    g_pttDownMs = 0;
    if (v == 3) { printf("[PTT] voice button cancelled\n"); return; }
    bool lng = wasDown && held >= 750;
    printf("[PTT] voice button released after %u ms (%s press%s)\n", (unsigned)held, lng ? "long" : "short",
           lng ? (pttAssistant() ? ": Google Assistant" : "; NBT_PTT_ASSISTANT=1 would start the Assistant") : ": iDrive voice");
    if (lng && pttAssistant() && g_cb) {
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
    c->sendRequest(0xF, p);
    // Same subscriptions as the HMI's CHBMOSTClientImpl::componentConnected (car-verified in test_controls v2).
    static const uint32_t kAttrs[] = { 0xC0000002u, 0xC0000003u, 0xC0000001u, 0x80000001u, 0x80000002u,
                                       0x80000003u, 0x80000004u, 0x80000005u, 0x80000006u, 0x80000007u };
    for (size_t i = 0; i < sizeof(kAttrs) / sizeof(kAttrs[0]); ++i) c->setNotification(kAttrs[i]);
}

void onData(DsiClient* c, uint32_t, uint32_t id, uint32_t, const uint8_t* pl, uint32_t len, void*) {
    if (id == 0x80000000u && len >= 6) {
        g_client = mostRdU32(pl);
        unsigned dev = mostRdU16(pl + 4);
        printf("[Knob] registered: clientId=%u deviceId=0x%04X%s\n", g_client, dev,
               g_client == 0xffffffffu ? "  (REGISTRATION REFUSED)" : "");
        if (g_client != 0xffffffffu && g_stage < 2) {
            c->setNotification(0xC0000000u);
            static const uint16_t kKeys[] = { 0xE510, 0xE503, 0xE505, 0xE512, 0xE514, 0xE501, 0xE502, 0x1502 };
            std::vector<uint16_t> keys(kKeys, kKeys + sizeof(kKeys) / sizeof(kKeys[0]));
            g_stage = 2;
            c->sendRequest(1, buildRegisterDevCtrls(g_client, keys));
            printf("[Knob] registerDevCtrls sent (8 keys): waiting for controller events\n");
        }
        return;
    }
    if (id != 0x80000001u) return;
    MostMsg m;
    if (!parseMostMessage(pl, len, m)) return;
    ++g_msgs;
    if (m.fblock == 0x15) {
        if (m.fkt == 0x512 && m.opType == 0xC) onPtt(m);
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
    const char* e = getenv("NBT_KNOB_INST");
    if (e && *e) g_dec.setInstance((uint8_t)strtoul(e, NULL, 16));
    if (getenv("NBT_KNOB_INVERT")) g_dec.setInvert(true);
    const char* ci = getenv("NBT_KNOB_CCS_INST");   // older CCS controller (fkt C10/C20), default 01
    if (ci && *ci) g_dec.setCcsInstance((uint8_t)strtoul(ci, NULL, 16));
    for (int s = 0; s < CCS_SLOTS; ++s) {
        char name[32];
        snprintf(name, sizeof(name), "NBT_KEY_%s", CcsDecoder::slotName(s));
        const char* v = getenv(name);
        if (v && *v) g_dec.setSlotKey(s, (uint32_t)strtoul(v, NULL, 0));
    }
    printf("[Knob] iDrive controller input: instance %s (CCS35up C46/C47) or %s (CCS C10/C20), rotary%s, keys:",
           (e && *e) ? e : "03", (ci && *ci) ? ci : "01", getenv("NBT_KNOB_INVERT") ? " inverted" : "");
    for (int s = 0; s < CCS_SLOTS; ++s) printf(" %s=%u", CcsDecoder::slotName(s), g_dec.slotKey(s));
    printf("\n");
    g_most.setQuiet(getenv("NBT_KNOB_RAW") == NULL);
    g_most.setCallbacks(&onAttached, &onData, NULL);
    if (!g_most.open("DSYSMOSTServer", "MOSTSubSystem", 4, 5)) {
        printf("[Knob] DSI open FAILED: knob/keys disabled\n");
        return false;
    }
    return true;
}

} // namespace nbt
