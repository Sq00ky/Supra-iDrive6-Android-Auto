// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUD6WB_DISPLAY_HPP
#define CLUSTER_HUD6WB_DISPLAY_HPP
#include "cluster/display.hpp"
#include "cluster/hud6wb_resources.hpp"
namespace nbt { namespace kombinav {
class Hud6wbDisplay : public ClusterDisplay {
public:
    Hud6wbDisplay(hudterm::Port& port,const hudterm::Bmw6wbProfile& profile,bool shadow);
    const char* name() const { return "6wb"; }
    void setSettings(const Settings& s) { settings_=s; }
    void setLog(LogFn f,void* u) { log_=f;user_=u; }
    bool sameManeuver(const NavView& v,double now);
    Result showManeuver(const NavView& v,double now,bool isNew,bool first);
    Result updateDistance(const NavView& v,double now);
    Result keepUp(const NavView& v,double now);
    void guidanceGap() {}
    bool clear(std::string* detail);
    void forget();
    bool sentAny() const { return sent_; }
    bool factoryActive() { return !shadow_ && port_.status()==hudterm::kNavActive; }
    bool ready() { return !shadow_ && resources_.state()==hudterm::BmwResources::kVerified && port_.status()==hudterm::kReady; }
    hudterm::Batch render(const NavView& v) const;
    hudterm::Batch clearBatch() const;
private:
    Result health();
    bool canDraw();
    bool submit(const hudterm::Batch& b);
    void log(const std::string& s) const;
    hudterm::Port& port_;hudterm::Bmw6wbProfile p_;hudterm::BmwResources resources_;
    bool shadow_,sent_,fresh_,dirty_;uint32_t epoch_;
    uint16_t mainVisible_,laneVisible_,iconVisible_;
    NavView shown_;Settings settings_;LogFn log_;void* user_;
};
} }
#endif
