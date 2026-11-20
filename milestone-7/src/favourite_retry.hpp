// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_FAVOURITE_RETRY_HPP
#define NBT_FAVOURITE_RETRY_HPP
#include <stdint.h>
namespace nbt {
// Retry transient capture failures without exiting the helper or blocking release handling.
class FavouriteRetry {
    uint32_t due_;unsigned failures_;
public:
    FavouriteRetry():due_(0),failures_(0){}
    bool ready(uint32_t now)const{return !failures_||(int32_t)(now-due_)>=0;}
    unsigned failed(uint32_t now){if(failures_<1000)++failures_;due_=now+(failures_<3?100u:500u);return failures_;}
    void reset(){due_=0;failures_=0;}
};
}
#endif
