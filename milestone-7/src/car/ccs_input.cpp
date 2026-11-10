// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "car/ccs_input.hpp"

#include <stdio.h>

namespace nbt {
namespace car {

namespace {

const uint8_t kFBlockController = 0xE5;
const uint8_t kFBlockSteeringWheel = 0x15;
const int kFktPttButton1 = 0x512;

// Registered as device controllers, as test_controls does. Delivery itself is switched on by the client's subscriptions
// (GPS arrived with no key), but E5 and 15 without their keys is untested on the car.
const uint16_t kDevCtrlKeys[] = { 0xE510, 0xE503, 0xE505, 0xE512, 0xE514, 0xE501, 0xE502, 0x1502 };

// MflEvent -> the event form the daemon already handles (kind 1 = key).
CcsEvent makeMflCcsEvent(const MflEvent& e) {
    CcsEvent ev;
    ev.kind = 1;
    ev.keycode = e.keycode;
    ev.down = e.down;
    ev.delta = 0;
    ev.what = e.what;
    ev.cancel = 0;
    return ev;
}

} // namespace

CcsInput::CcsInput() : pttAssistant_(false), pttShort_(false), pttShortFollowsFlag_(false), cb_(NULL), user_(NULL), statusLogged_(0), ccsLogged_(0) {}

bool CcsInput::attach(most::Client& client, const CcsConfig& cfg, CcsEventCb cb, void* user) {
    cb_ = cb;
    user_ = user;
    applyCcsConfig(cfg, dec_);
    pttAssistant_ = cfg.pttAssistant;
    pttShort_ = cfg.pttShort;
    pttShortFollowsFlag_ = cfg.pttShortFollowsFlag;
    printf("[Knob] iDrive controller input: instance %02X (CCS35up C46/C47) or %02X (CCS C10/C20), rotary%s, keys:",
           cfg.inst, cfg.ccsInst, cfg.invert ? " inverted" : "");
    for (int s = 0; s < CCS_SLOTS; ++s) printf(" %s=%u", CcsDecoder::slotName(s), dec_.slotKey(s));
    printf("\n");
    printf("[PTT] voice button Assistant: long=%s, short=%s\n",
           pttAssistant_ ? "on (NBT_PTT_ASSISTANT)" : "off",
           pttShort_ ? (pttShortFollowsFlag_ ? "on (Settings checkbox)" : "on (nbt-aad.env)")
                     : (pttShortFollowsFlag_ ? "off (tick Short press voice button for Assistant, or NBT_PTT_SHORT=1)" : "off (NBT_PTT_SHORT=0)"));

    // Steering wheel media keys (car/mfl.hpp): fkt (+ value for the roller) -> key, read from NBT_MFL_*. One broad route
    // on the block covers whatever the wheel sends; onMfl skips the voice button, which pttTramp handles.
    mfl_.setConfig(readMflConfig());
    {
        const MflConfig& mc = mfl_.config();
        printf("[MFL] steering-wheel keys:");
        if (!mc.n) printf(" none mapped");
        for (int i = 0; i < mc.n; ++i) {
            if (mc.b[i].value < 0) printf(" %s=%03X->%u", mc.b[i].what, mc.b[i].fkt, mc.b[i].key);
            else printf(" %s=%03X:%02X->%u", mc.b[i].what, mc.b[i].fkt, (unsigned)mc.b[i].value, mc.b[i].key);
        }
        printf(", recon log %s\n", mc.log ? "on (NBT_MFL_LOG=0 silences)" : "off");
    }

    bool ok = client.addRoute(kFBlockController, most::kAny, most::kAny, most::kAny, &CcsInput::controllerTramp, this) &&
              client.addRoute(kFBlockSteeringWheel, most::kAny, kFktPttButton1, most::kOpStatus, &CcsInput::pttTramp, this) &&
              client.addRoute(kFBlockSteeringWheel, most::kAny, most::kAny, most::kAny, &CcsInput::mflTramp, this) &&
              client.addLinkListener(&CcsInput::linkTramp, this);
    for (size_t i = 0; ok && i < sizeof(kDevCtrlKeys) / sizeof(kDevCtrlKeys[0]); ++i) ok = client.addDevCtrl(kDevCtrlKeys[i]);
    if (!ok) printf("[Knob] no room on the MOST client: knob/keys disabled\n");
    return ok;
}

void CcsInput::controllerTramp(const most::View& m, void* self) { static_cast<CcsInput*>(self)->onController(m); }
void CcsInput::pttTramp(const most::View& m, void* self) { static_cast<CcsInput*>(self)->onPtt(m); }
void CcsInput::mflTramp(const most::View& m, void* self) { static_cast<CcsInput*>(self)->onMfl(m); }
void CcsInput::linkTramp(most::LinkState s, void* self) { static_cast<CcsInput*>(self)->onLink(s); }

void CcsInput::onLink(most::LinkState s) {
    if (s == most::kUp) return;
    ptt_.reset();
    CcsEvent ev[CCS_MAX_RESET_EVENTS];
    int n = dec_.reset(ev, CCS_MAX_RESET_EVENTS);
    for (int i = 0; i < n; ++i) {
        printf("[Knob] %s released: MOST link %s\n", ev[i].what, most::linkStateName(s));
        if (cb_ && ev[i].keycode) cb_(ev[i], user_);
    }
}

// The steering-wheel voice button: see car/ptt.hpp. Observe-only unless NBT_PTT_ASSISTANT=1 (long press) or the
// Settings checkbox / NBT_PTT_SHORT=1 (short press) is on, sending the Assistant key (AA keycode 84 SEARCH, advertised on input channel 7).
void CcsInput::onPtt(const most::View& m) {
    if (m.len == 0) return;
    unsigned v = m.data[0];
    PttTracker::Result r = ptt_.feed((uint8_t)v, m.rxMs);
    if (r.kind == PttTracker::kPressed) {
        printf("[PTT] voice button pressed (15/%02X fkt 512 = 01)\n", m.inst);
        return;
    }
    if (r.kind == PttTracker::kIgnored) { printf("[PTT] voice button state %u (ignored)\n", v); return; }
    if (r.kind == PttTracker::kCancelled) { printf("[PTT] voice button cancelled\n"); return; }
    bool lng = r.kind == PttTracker::kReleasedLong;
    bool shrt = r.kind == PttTracker::kReleasedShort;
    bool trigger = (lng && pttAssistant_) || (shrt && (pttShortFollowsFlag_ ? nbt::pttShortFlagFileSet() : pttShort_));
    printf("[PTT] voice button released after %u ms (%s press%s)\n", (unsigned)r.heldMs, lng ? "long" : "short",
           trigger ? ": Google Assistant" : (lng ? "; NBT_PTT_ASSISTANT=1 would start the Assistant" : ": iDrive voice"));
    if (trigger && cb_) {
        CcsEvent ev;
        ev.kind = 1; ev.keycode = AAK_SEARCH; ev.delta = 0; ev.what = "PTT"; ev.cancel = 0;
        ev.down = 1; cb_(ev, user_);
        ev.down = 0; cb_(ev, user_);
    }
}

// Steering-wheel buttons other than the voice one: MflDecoder maps the configured fkts to AA media keys and logs every
// function the block carries once, so the mapping can be read off a single drive (car/mfl.hpp).
void CcsInput::onMfl(const most::View& m) {
    if (m.fkt == kFktPttButton1) return;   // pttTramp owns it
    MflEvent ev[2];
    int n = mfl_.feed(m.fkt, m.op, m.len ? m.data : NULL, m.len, ev, 2);
    for (int i = 0; i < n; ++i) {
        printf("[MFL] %s %s -> AA key %u\n", ev[i].what, ev[i].down ? "down" : "up", ev[i].keycode);
        if (cb_) cb_(makeMflCcsEvent(ev[i]), user_);
    }
}

void CcsInput::onController(const most::View& m) {
    if (m.fkt == 0xC10 || m.fkt == 0xC20) {
        // Older CCS controller protocol: layout from the firmware, not yet confirmed on a car -> log raw.
        if (ccsLogged_ < 40) {
            ++ccsLogged_;
            printf("[Knob] E5/%02X fkt=%03X op=%X len=%u (CCS protocol):", m.inst, m.fkt, m.op, (unsigned)m.len);
            for (size_t i = 0; i < m.len && i < 16; ++i) printf(" %02x", m.data[i]);
            printf("\n");
        }
    }
    if (m.fkt == 0xC46 || m.fkt == 0xC47 || m.fkt == 0xC4F) {
        ++statusLogged_;
        if (statusLogged_ <= 3 || (m.fkt == 0xC46 && m.len >= 7 && (m.data[3] || m.data[5] || m.data[6]))) {
            printf("[Knob] E5/%02X fkt=%03X op=%X len=%u:", m.inst, m.fkt, m.op, (unsigned)m.len);
            for (size_t i = 0; i < m.len && i < 16; ++i) printf(" %02x", m.data[i]);
            printf("\n");
        }
    }
    CcsEvent ev[16];
    int n = dec_.feed(m.fblock, m.inst, m.fkt, m.op, m.len ? m.data : NULL, m.len, ev, 16);
    for (int i = 0; i < n; ++i) {
        if (ev[i].kind == 2)
            printf("[Knob] %s delta=%d -> AA relative key %u\n", ev[i].what, ev[i].delta, ev[i].keycode);
        else
            printf("[Knob] %s %s -> AA key %u%s\n", ev[i].what, ev[i].down ? "down" : "up", ev[i].keycode,
                   ev[i].keycode ? "" : " (unmapped, not sent)");
        if (cb_ && (ev[i].kind == 2 || ev[i].keycode)) cb_(ev[i], user_);
    }
}

} // namespace car
} // namespace nbt
