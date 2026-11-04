// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "car/volume_input.hpp"

#include <stdio.h>

namespace nbt {
namespace car {

VolumeInput::VolumeInput() : cb_(NULL), user_(NULL) {}

bool VolumeInput::attach(most::Client& client, VolumeCb cb, void* user) {
    cb_ = cb;
    user_ = user;
    bool ok = client.addRoute(VolumeLevel::kFBlock, most::kAny, VolumeLevel::kFkt, most::kOpStatus,
                              &VolumeInput::statusTramp, this) &&
              client.addLinkListener(&VolumeInput::linkTramp, this);
    if (!ok) printf("[Volume] no room on the MOST client: the car's volume will not be followed\n");
    return ok;
}

void VolumeInput::statusTramp(const most::View& m, void* self) { static_cast<VolumeInput*>(self)->onStatus(m); }
void VolumeInput::linkTramp(most::LinkState s, void* self) { static_cast<VolumeInput*>(self)->onLink(s); }

void VolumeInput::onLink(most::LinkState s) {
    if (s != most::kUp) level_.reset();
}

void VolumeInput::onStatus(const most::View& m) {
    int level = 0, max = 0;
    if (!level_.feed(m.len ? m.data : NULL, m.len, m.rxMs, level, max)) return;
    printf("[Knob] VOLUME level %d/%d (AudioMaster)\n", level, max);
    if (cb_) cb_(level, max, user_);
}

} // namespace car
} // namespace nbt
