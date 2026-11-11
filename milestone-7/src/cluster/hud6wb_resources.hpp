// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUD6WB_RESOURCES_HPP
#define CLUSTER_HUD6WB_RESOURCES_HPP
#include "cluster/hud6wb_profile.hpp"
#include "cluster/hud_terminal.hpp"
namespace nbt { namespace hudterm {
class BmwResources {
public:
    enum State { kUnknown, kQuerying, kVerified, kIncompatible };
    explicit BmwResources(const Bmw6wbProfile& p) : p_(p), state_(kUnknown) {}
    void forget() { state_=kUnknown; }
    State state() const { return state_; }
    Batch queries() const {
        Batch b("6wb-size-check");
        add(b,p_.base.frame);add(b,p_.main.a);add(b,p_.main.b);
        add(b,p_.icons.a);add(b,p_.icons.b);add(b,p_.lanes.a);add(b,p_.lanes.b);add(b,p_.bar);
        return b;
    }
    // Queries only documented handles. No allocation, discovery scan or guessed initialization.
    bool advance(Port& port) {
        if(state_==kVerified)return true;
        if(state_==kIncompatible || port.status()!=kReady)return false;
        if(state_==kUnknown) {if(port.submit(queries())==kQueued)state_=kQuerying;return false;}
        if(!port.waitIdle(0))return false;
        bool ok=check(port,p_.base.frame,p_.frameWidth,p_.frameHeight) &&
            check(port,p_.main.a,p_.main.width,p_.main.height) && check(port,p_.main.b,p_.main.width,p_.main.height) &&
            check(port,p_.icons.a,p_.icons.width,p_.icons.height) && check(port,p_.icons.b,p_.icons.width,p_.icons.height) &&
            check(port,p_.lanes.a,p_.lanes.width,p_.lanes.height) && check(port,p_.lanes.b,p_.lanes.width,p_.lanes.height) &&
            check(port,p_.bar,p_.barWidth,p_.barHeight);
        state_=ok?kVerified:kIncompatible;return ok;
    }
private:
    static void add(Batch& b,uint16_t h) {std::vector<uint8_t> p=getSize(h);p.push_back(1);b.add(kGetSize,p);}
    static bool check(const Port& port,uint16_t h,uint16_t w,uint16_t height) {
        uint16_t rw=0,rh=0;return port.imageSize(h,rw,rh) && rw==w && rh==height;
    }
    Bmw6wbProfile p_;State state_;
};
} }
#endif
