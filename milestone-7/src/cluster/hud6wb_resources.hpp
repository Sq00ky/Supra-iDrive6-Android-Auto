// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUD6WB_RESOURCES_HPP
#define CLUSTER_HUD6WB_RESOURCES_HPP
#include "cluster/hud6wb_profile.hpp"
#include "cluster/hud_terminal.hpp"
namespace nbt { namespace hudterm {
// F31 6WB: use the measured surfaces immediately. Transport readiness is
// still mandatory; no C04, reply deadline or automatic discovery fallback.
class BmwResources {
public:
    enum State { kUnknown, kQuerying, kVerified, kIncompatible };
    explicit BmwResources(const Bmw6wbProfile& p) : p_(p) {}
    void forget() {} // reconnect resets transport state, not measured dimensions
    State state() const { return validProfile(p_)?kVerified:kIncompatible; }
    Batch queries() const { return Batch("6wb-known-surfaces"); }
    bool advance(Port& port) { return state()==kVerified && port.status()==kReady; }
    bool iconsUsable(const Port&) const { return state()==kVerified; }
    bool lanesUsable(const Port&) const { return state()==kVerified; }
    bool barUsable(const Port&) const { return state()==kVerified; }
private:
    Bmw6wbProfile p_;
};

// ORIGINAL C04 DISCOVERY - intentionally commented out, not a runtime fallback.
// To restore for another vehicle, replace the active class after explicit review.
// class BmwResources {
// public:
//     enum State { kUnknown, kQuerying, kVerified, kIncompatible };
//     explicit BmwResources(const Bmw6wbProfile& p) : p_(p), state_(kUnknown) {}
//     void forget() { state_=kUnknown; }
//     State state() const { return state_; }
//     Batch queries() const {
//         Batch b("6wb-size-check");
//         add(b,p_.base.frame);add(b,p_.main.a);add(b,p_.main.b);
//         add(b,p_.icons.a);add(b,p_.icons.b);add(b,p_.lanes.a);add(b,p_.lanes.b);add(b,p_.bar);
//         return b;
//     }
//     // Queries only documented handles. No allocation, discovery scan or guessed initialization.
//     bool advance(Port& port) {
//         if(state_==kVerified)return true;
//         if(state_==kIncompatible || port.status()!=kReady)return false;
//         if(state_==kUnknown) {
//             // An OEM yield drops drawing work, not the terminal's size cache.
//             // Reuse already validated sizes instead of repeating same-tag calls.
//             // Core clears this cache when the link or host changes.
//             if(allMeasured(port) && matches(port)) {state_=kVerified;return true;}
//             if(port.submit(queries())==kQueued)state_=kQuerying;
//             return false;
//         }
//         if(!port.waitIdle(0))return false;
//         bool ok=matches(port);
//         state_=ok?kVerified:kIncompatible;return ok;
//     }
// public:
//     bool iconsUsable(const Port& port) const {
//         if(p_.iconHandle)return check(port,p_.iconHandle,p_.icons.width,p_.icons.height);
//         return check(port,p_.icons.a,p_.icons.width,p_.icons.height) &&
//             check(port,p_.icons.b,p_.icons.width,p_.icons.height);
//     }
//     bool lanesUsable(const Port& port) const {
//         return check(port,p_.lanes.a,p_.lanes.width,p_.lanes.height) &&
//             check(port,p_.lanes.b,p_.lanes.width,p_.lanes.height);
//     }
//     bool barUsable(const Port& port) const { return compatibleOrMissing(port,p_.bar,p_.barWidth,p_.barHeight); }
// private:
//     bool allMeasured(const Port& port) const {
//         const Batch b=queries();
//         for(size_t i=0;i<b.calls.size();++i) {
//             uint16_t w=0,h=0;
//             if(!port.imageSize(b.calls[i].handle(),w,h))return false;
//         }
//         return true;
//     }
//     bool compatibleOrMissing(const Port& port,uint16_t handle,uint16_t w,uint16_t h) const {
//         uint16_t rw=0,rh=0;
//         // A missing diagnostic reply uses the established central reference.
//         // Only an actually received, incompatible central size blocks drawing.
//         return !port.imageSize(handle,rw,rh) || (rw==w && rh==h);
//     }
//     bool matches(const Port& port) const {
//         return compatibleOrMissing(port,p_.base.frame,p_.frameWidth,p_.frameHeight) &&
//             compatibleOrMissing(port,p_.main.a,p_.mainImageWidth,p_.mainImageHeight) &&
//             compatibleOrMissing(port,p_.main.b,p_.mainImageWidth,p_.mainImageHeight);
//     }
//     static void add(Batch& b,uint16_t h) {std::vector<uint8_t> p=getSize(h);p.push_back(1);b.add(kGetSize,p);}
//     static bool check(const Port& port,uint16_t h,uint16_t w,uint16_t height) {
//         uint16_t rw=0,rh=0;return port.imageSize(h,rw,rh) && rw==w && rh==height;
//     }
//     Bmw6wbProfile p_;State state_;
// };
} }
#endif
