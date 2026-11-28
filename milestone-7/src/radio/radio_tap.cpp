// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "radio/radio_tap.hpp"
#include "most/client.hpp"

#include <stdio.h>

namespace nbt {

namespace {

// m.rxMs is CLOCK_MONOTONIC ms like the session's clock.
void onRadioMsg(const most::View& m, void* self) {
    static_cast<RadioTap*>(self)->apply(m.fblock, m.fkt, m.len ? m.data : NULL, m.len, m.rxMs);
}

}  // namespace

bool RadioTap::attach(most::Client& c, ChangeCb cb, void* user) {
    setCallback(cb, user);
    // Every DAB op: the slide (C43) comes as a ResultAck.
    const bool ok =
        c.addRoute(kMostFBlockDabTuner, most::kAny, most::kAny, most::kAny, &onRadioMsg, this) &&
        c.addRoute(kMostFBlockFmTuner, most::kAny, kMostFktFmStation, most::kOpStatus, &onRadioMsg, this) &&
        c.addRoute(kMostFBlockFmTuner, most::kAny, kMostFktRadioText, most::kOpStatus, &onRadioMsg, this) &&
        c.addRoute(kMostFBlockFmTuner, most::kAny, kMostFktDlPlus, most::kOpStatus, &onRadioMsg, this) &&
        c.addRoute(kMostFBlockConnectionMaster, most::kAny, kMostFktConnectionTable, most::kOpStatus, &onRadioMsg,
                   this);
    if (!ok) printf("[RadioTap] cannot add the MOST routes: the radio will not be followed\n");
    return ok;
}

}  // namespace nbt
