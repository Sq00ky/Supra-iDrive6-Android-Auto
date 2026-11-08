// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "car/source_key.hpp"

namespace nbt {
namespace car {

SourceKey::SourceKey() : cb_(NULL), user_(NULL) {}

bool SourceKey::attach(most::Client& client, SourceKeyCb cb, void* user) {
    cb_ = cb;
    user_ = user;
    return client.addRoute(0x15, most::kAny, 0x513, most::kOpStatus, &SourceKey::mflTramp, this);
}

void SourceKey::mflTramp(const most::View& m, void* self) {
    SourceKey* k = static_cast<SourceKey*>(self);
    const char* what = sourceKeyPress(m.fkt, m.len ? m.data : NULL, m.len);
    if (what && k->cb_) k->cb_(what, k->user_);
}

} // namespace car
} // namespace nbt
