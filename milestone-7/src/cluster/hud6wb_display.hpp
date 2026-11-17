// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUD6WB_DISPLAY_HPP
#define CLUSTER_HUD6WB_DISPLAY_HPP
#include "cluster/display.hpp"
#include "cluster/hud6wb_resources.hpp"
#include "cluster/hud6wb_kit.hpp"
#include "cluster/hud6wb_calibration.hpp"
namespace nbt { namespace kombinav {
class Hud6wbDisplay : public ClusterDisplay {
public:
    Hud6wbDisplay(hudterm::Port& port,const hudterm::Bmw6wbProfile& profile,bool shadow,const hudterm::GrafiteKit& kit=hudterm::GrafiteKit());
    const char* name() const { return "6wb"; }
    void setSettings(const Settings& s) { settings_=s; }
    void setLog(LogFn f,void* u) { log_=f;user_=u; }
    bool sameManeuver(const NavView& v,double now);
    Result showManeuver(const NavView& v,double now,bool isNew,bool first);
    bool factoryArbitration() const { return false; }
    bool markerTracksSentCommands() const { return true; }
    bool useFullRoadName() const { return true; }
    bool discardPreviousRoute() const { return true; }
    bool suppressDepartureAtZero() const { return false; }
    Result updateDistance(const NavView& v,double now);
    Result keepUp(const NavView& v,double now);
    void guidanceGap() {}
    bool clear(std::string* detail);
    void forget();
    bool sentAny() const { return sent_; }
    bool factoryActive() { return !shadow_ && port_.status()==hudterm::kNavActive; }
    bool ready() { return !shadow_ && resources_.state()==hudterm::BmwResources::kVerified && port_.status()==hudterm::kReady; }
    bool interceptNavigation(const NavView* v,int status,double now);
    double nextUpdateIn(double now) const {return calibration_.nextIn(now);}
    void navigationSessionEnded() {calibration_.reset();calibrationClearQueued_=false;}
    void navigationRouteStarted() {selectCentralSymbol(NULL);calibration_.reset();calibration_.observe(1);calibrationClearQueued_=false;}
    void navigationPresentationCancelled() {calibration_.cancel();calibrationClearQueued_=false;}
    hudterm::Batch calibrationBatch() const;
    hudterm::Batch geometryCalibrationBatch() const;
    hudterm::Batch render(const NavView& v) const;
    hudterm::Batch clearBatch() const;
private:
    bool appendCalibrationBitmap(hudterm::Batch& b,unsigned test) const;
    Result health();
    bool canDraw();
    bool submit(const hudterm::Batch& b);
    void log(const std::string& s) const;
    hudterm::PaletteCalibration calibration_;bool calibrationClearQueued_;
    hudterm::GrafiteKit kit_;
    hudterm::Port& port_;hudterm::Bmw6wbProfile p_;hudterm::BmwResources resources_;
    void remember(const NavView& v,const hudterm::Batch& b,bool straight);
    void selectCentralSymbol(const NavView* v);
    bool havePolicyStep_,policyHasDistance_,straight_,shownStraight_,queuedStraight_;
    NavView policyStep_;
    double policyMinimumMetres_;
    bool mainChanged(const NavView& v) const;
    bool iconChanged(const NavView& v) const;
    bool shadow_,sent_,fresh_,dirty_,shownLanes_;uint32_t epoch_;
    uint16_t mainVisible_,laneVisible_,iconVisible_;
    NavView shown_;Settings settings_;LogFn log_;void* user_;
    hudterm::Batch queuedPicture_;NavView queuedView_;size_t queuedCall_;bool kitErrorLogged_;
};
} }
#endif
