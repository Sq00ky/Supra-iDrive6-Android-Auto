// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "car/input_activity.hpp"

namespace nbt {
namespace car {

InputActivity::InputActivity() : cb_(NULL), user_(NULL) {}

bool InputActivity::attach(most::Client& client, const CcsConfig& cfg, InputActivityCb cb, void* user) {
    cb_ = cb;
    user_ = user;
    applyCcsConfig(cfg, dec_);
    return client.addRoute(0xE5, most::kAny, most::kAny, most::kOpStatus, &InputActivity::controllerTramp, this) &&
           client.addRoute(0x15, most::kAny, 0x401, most::kOpStatus, &InputActivity::mflTramp, this) &&
           client.addRoute(0x15, most::kAny, 0xC01, most::kOpStatus, &InputActivity::mflTramp, this) &&
           client.addRoute(0x15, most::kAny, 0x512, most::kOpStatus, &InputActivity::mflTramp, this) &&
           client.addLinkListener(&InputActivity::linkTramp, this);
}

void InputActivity::controllerTramp(const most::View& m, void* self) {
    InputActivity* a = static_cast<InputActivity*>(self);
    CcsEvent ev[16];
    int n = a->dec_.feed(m.fblock, m.inst, m.fkt, m.op, m.len ? m.data : NULL, m.len, ev, 16);
    for (int i = 0; i < n; ++i) {
        if (ev[i].kind == 1 && !ev[i].down) continue;   // a release adds nothing to the press before it
        if (a->cb_) a->cb_(ev[i].what ? ev[i].what : "KNOB", a->user_);
        return;                                          // one report per message is enough
    }
}

void InputActivity::mflTramp(const most::View& m, void* self) {
    InputActivity* a = static_cast<InputActivity*>(self);
    const char* what = mflPress(m.fkt, m.len ? m.data : NULL, m.len);
    if (what && a->cb_) a->cb_(what, a->user_);
}

void InputActivity::linkTramp(most::LinkState s, void* self) {
    if (s == most::kUp) return;
    InputActivity* a = static_cast<InputActivity*>(self);
    CcsEvent ev[CCS_MAX_RESET_EVENTS];
    a->dec_.reset(ev, CCS_MAX_RESET_EVENTS);             // re-base: the first message after the link returns is no press
}

} // namespace car
} // namespace nbt
