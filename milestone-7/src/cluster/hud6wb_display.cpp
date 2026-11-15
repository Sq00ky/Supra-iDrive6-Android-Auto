// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "cluster/hud6wb_display.hpp"
#include "cluster/hud6wb_codec.hpp"
#include "cluster/hud6wa_display.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <algorithm>
namespace nbt { namespace kombinav {
using namespace hudterm;
namespace {
uint16_t other(uint16_t v,const Surface& s) {return v==s.a?s.b:s.a;}
uint32_t kitColor(sixwbkit::Color::Value c) {
    switch(c){case sixwbkit::Color::Graphite:return 0xff353b43;case sixwbkit::Color::Gold:return 0xffc4a36a;
    case sixwbkit::Color::White:return 0xffffffff;case sixwbkit::Color::Gray:return 0xff858d98;case sixwbkit::Color::Black:return 0xff000000;default:return 0;}
}
bool inside(const sixwbkit::Shape& s,double x,double y) {
    bool hit=false;for(size_t c=0;c<s.contours.size();++c){const std::vector<sixwbkit::Point>& ps=s.contours[c].points;
        for(size_t i=0,j=ps.size()?ps.size()-1:0;i<ps.size();j=i++)if((ps[i].y>y)!=(ps[j].y>y)&&
            x<(ps[j].x-ps[i].x)*(y-ps[i].y)/(ps[j].y-ps[i].y)+ps[i].x)hit=!hit;}
    return hit;
}
bool onStroke(const sixwbkit::Shape& s,double x,double y) {
    double radius=s.strokeWidth/2;
    for(size_t c=0;c<s.contours.size();++c){const sixwbkit::Contour& ct=s.contours[c];size_t n=ct.points.size();
        size_t count=ct.closed?n:n?n-1:0;
        for(size_t i=0;i<count;++i){const sixwbkit::Point& a=ct.points[i];const sixwbkit::Point& b=ct.points[(i+1)%n];
            double dx=b.x-a.x,dy=b.y-a.y,d=dx*dx+dy*dy,t=d?((x-a.x)*dx+(y-a.y)*dy)/d:0;
            if(t<0)t=0;if(t>1)t=1;double xx=x-a.x-t*dx,yy=y-a.y-t*dy;
            if(xx*xx+yy*yy<=radius*radius)return true;}
    }return false;
}
void rasterKit(std::vector<uint32_t>& pixels,unsigned w,unsigned h,const sixwbkit::Scene& scene) {
    for(size_t i=0;i<scene.size();++i)for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        const sixwbkit::Shape& s=scene[i];
        if(s.fill!=sixwbkit::Color::None&&inside(s,x+.5,y+.5))pixels[(size_t)y*w+x]=kitColor(s.fill);
        if(s.stroke!=sixwbkit::Color::None&&onStroke(s,x+.5,y+.5))pixels[(size_t)y*w+x]=kitColor(s.stroke);
    }
}
Batch errorBatch(const char* why){Batch b(why,true);return b;}

}
Hud6wbDisplay::Hud6wbDisplay(Port& port,const Bmw6wbProfile& p,bool shadow,const GrafiteKit& kit)
    :calibrationClearQueued_(false),kit_(kit),port_(port),p_(p),resources_(p),shadow_(shadow),sent_(false),fresh_(true),dirty_(false),shownLanes_(true),epoch_(port.epoch()),
    mainVisible_(p.main.b),laneVisible_(p.lanes.b),iconVisible_(p.icons.b),log_(NULL),user_(NULL),queuedCall_(0),kitErrorLogged_(false) {}
void Hud6wbDisplay::log(const std::string& s) const {if(log_)log_(s.c_str(),user_);}
ClusterDisplay::Result Hud6wbDisplay::health() {
    if(!validProfile(p_))return kInternal;
    if(shadow_)return kOk;
    if(port_.status()==kFailed)return kRejected;
    if(port_.epoch()!=epoch_) {
        epoch_=port_.epoch();
        if(calibration_.phase()!=hudterm::PaletteCalibration::Pending)calibration_.cancel();
        calibrationClearQueued_=false;resources_.forget();fresh_=true;dirty_=true;queuedPicture_=Batch();queuedCall_=0;
        mainVisible_=p_.main.b;laneVisible_=p_.lanes.b;iconVisible_=p_.icons.b;
    }
    if(resources_.state()==BmwResources::kIncompatible)return kRejected;
    // A disconnect is a wait, not a permanent session failure. The epoch forces
    // revalidation and a complete redraw after the link becomes available again.
    return kOk;
}
bool Hud6wbDisplay::canDraw() {
    if(shadow_)return true;
    if(port_.status()!=kReady)return false;
    if(!resources_.advance(port_)) {
        if(resources_.state()==BmwResources::kIncompatible)log("6wb: surface dimensions differ from the selected experimental profile; no graphics sent");
        return false;
    }
    // Never replace an unexecuted full batch: the buffer state advances only with an ordered batch.
    return port_.waitIdle(0);
}
bool Hud6wbDisplay::submit(const Batch& b) {
    if(shadow_) {
        for(size_t i=0;i<b.calls.size();++i) {
            const Call& c=b.calls[i];char header[128];
            snprintf(header,sizeof(header),"[6wb shadow] fkt=%03X h=%u tx=%u var=1/0 op=6 tag=registered-client payload:",
                c.fkt,c.handle(),c.txType<0?p_.vectorTx:(uint32_t)c.txType);
            std::string line=header;
            for(size_t j=0;j<c.payload.size();++j) {char h[4];snprintf(h,sizeof(h)," %02x",c.payload[j]);line+=h;}
            log(line);
        }
        return true;
    }
    if(port_.status()!=kReady)return false;
    SubmitResult r=port_.submit(b);
    return r==kQueued;
}
Batch Hud6wbDisplay::calibrationBatch() const {
    Batch b("6wb-palette-calibration",false);
    if(p_.main.a!=0x1f||p_.main.b!=0x20)return errorBatch("6wb-kit-error: calibration requires verified 1F/20 pair");
    uint16_t target=other(mainVisible_,p_.main);
    b.add(kRemoveAllItems,removeAllItems(target));
    b.add(kSetLineWidth,setLineWidth(target,2));
    // Known input region below the ~70px horizon of the verified central pair.
    // 20 separate horizontal lines, ascending indices from top to bottom.
    for(unsigned index=0;index<20;++index) {
        int y=75+(int)((index*74+9)/19); // 75..149, no coincident rows
        std::vector<hudterm::Point> points;hudterm::Point a={22,(int16_t)y},z={198,(int16_t)y};
        points.push_back(a);points.push_back(z);
        b.add(kSetLineColor,setLineColor(target,(uint8_t)index));
        b.add(kAddPolygon,addPolygon(target,(uint16_t)(0x80+index),kPolyline,points));
    }
    // Hide only non-calibration elements. Never show a half-built chart.
    const uint16_t hidden[]={p_.lanes.a,p_.lanes.b,p_.icons.a,p_.icons.b,p_.bar,p_.base.street,p_.base.distance,p_.base.unit};
    for(size_t i=0;i<sizeof(hidden)/sizeof(*hidden);++i)b.add(kSetVisibility,setVisibility(hidden[i],false));
    b.add(kSwitchVisibility,switchVisibility(mainVisible_,target));
    b.add(kSetVisibility,setVisibility(p_.base.frame,true));return b;
}
bool Hud6wbDisplay::interceptNavigation(const NavView*,int status,double now) {
    const hudterm::PaletteCalibration::Phase before=calibration_.phase();
    calibration_.observe(status);
    if(!calibration_.active())return false;
    if(before!=hudterm::PaletteCalibration::Pending && calibration_.phase()==hudterm::PaletteCalibration::Pending) {
        calibrationClearQueued_=false;dirty_=true;
    }
    Result r=health();
    if(r!=kOk){calibration_.cancel();return false;} // existing Core handles failure
    if(!calibration_.active())return false;
    if(calibration_.phase()==hudterm::PaletteCalibration::Pending && !queuedPicture_.calls.empty()) {
        // Finish any previously accepted multi-chunk picture before starting
        // calibration, preserving its predicted A/B state. Never replace it.
        Result pending=showManeuver(queuedView_,now,false,false);
        if(pending!=kOk){calibration_.cancel();return false;}
        return true;
    }
    if(!canDraw())return true; // existing C04 procedure and local dispatch gate
    if(calibration_.phase()==hudterm::PaletteCalibration::Pending) {
        Batch b=calibrationBatch();
        if(b.label.find("6wb-kit-error:")==0){log(b.label);calibration_.cancel();return false;}
        for(size_t i=0;i<b.calls.size();++i)if(!callOk(b.calls[i],handlesOf(p_),true)) {
            log("6wb-kit-error: invalid calibration frame");calibration_.cancel();return false;
        }
        if(!submit(b))return true;
        mainVisible_=other(mainVisible_,p_.main);sent_=!shadow_;calibration_.queued();
        log("6wb palette: indices 0..19 queued; waiting for local dispatch completion");return true;
    }
    if(calibration_.phase()==hudterm::PaletteCalibration::Dispatching) {
        calibration_.dispatched(now);
        log("6wb palette: local dispatch complete; monotonic hold deadline +10000 ms");return true;
    }
    if(calibration_.phase()==hudterm::PaletteCalibration::Holding && !calibration_.due(now))return true;
    if(!calibrationClearQueued_) {
        Batch clear("6wb-palette-clear",false);
        clear.add(kRemoveAllItems,removeAllItems(mainVisible_));
        clear.add(kSetVisibility,setVisibility(mainVisible_,false));
        if(!submit(clear))return true;
        calibrationClearQueued_=true;calibration_.clearQueued();return true;
    }
    // The clear has been locally dispatched. Core will receive THIS tick's
    // latest snapshot, never the maneuver captured at the beginning of the hold.
    calibration_.finished();calibrationClearQueued_=false;fresh_=true;dirty_=true;sent_=false;
    queuedPicture_=Batch();queuedCall_=0;
    log("6wb palette: cleared; resume current navigation snapshot");return false;
}
bool Hud6wbDisplay::mainChanged(const NavView& v) const {
    sixwbkit::PlanePolicy::Value policy=sixwbkit::describe(GrafiteKit::maneuver(v.aa_type)).policy;
    bool planeChanged=kitPlane(policy,kitBarState(v,p_.barRangeMetres).visible)!=
        kitPlane(sixwbkit::describe(GrafiteKit::maneuver(shown_.aa_type)).policy,kitBarState(shown_,p_.barRangeMetres).visible);
    return fresh_ || planeChanged || v.aa_type!=shown_.aa_type || v.has_exit_angle!=shown_.has_exit_angle ||
        (v.has_exit_angle && v.exit_angle!=shown_.exit_angle) || v.has_exit_number!=shown_.has_exit_number ||
        (v.has_exit_number && v.exit_number!=shown_.exit_number);
}
bool Hud6wbDisplay::iconChanged(const NavView& v) const {
    if(fresh_ || v.has_next!=shown_.has_next)return true;
    if(v.has_next && (v.next_type!=shown_.next_type || v.next_has_angle!=shown_.next_has_angle ||
       (v.next_has_angle && v.next_angle!=shown_.next_angle)))return true;
    if(p_.roadShields) {
        RoadShield a=roadShield(v.road),b=roadShield(shown_.road);
        if(a.kind!=b.kind || a.number!=b.number)return true;
    }
    return false;
}
bool Hud6wbDisplay::sameManeuver(const NavView& v,double) {
    health();
    return !queuedPicture_.calls.empty() || dirty_ || mainChanged(v) || iconChanged(v) || (settings_.lanes && v.lanes!=shown_.lanes) || shownLanes_!=settings_.lanes;
}
Batch Hud6wbDisplay::render(const NavView& v) const {
    Batch b("6wb-guidance",false);
    const KitBarState bar=kitBarState(v,p_.barRangeMetres);
    sixwbkit::Maneuver::Value m=GrafiteKit::maneuver(v.aa_type);
    sixwbkit::PlanePolicy::Value policy=sixwbkit::describe(m).policy;
    const bool perspective=kitPlane(policy,bar.visible)==KitPerspective;
    const Surface& plane=perspective?p_.main:p_.flat;
    const Rect& useful=perspective?p_.base.glyph:p_.flatGlyph;
    uint16_t target=plane.a?other(mainVisible_,plane):0;
    uint16_t lane=other(laneVisible_,p_.lanes),icon=other(iconVisible_,p_.icons);
    if(v.aa_type<0||v.aa_type>46)return errorBatch("6wb-kit-error: unsupported AA maneuver code");
    if(policy!=sixwbkit::PlanePolicy::Hidden&&!target)return errorBatch("6wb-kit-error: flat plane is not configured");
    // Preflight the whole picture before any buffer is cleared or hidden.
    if(mainChanged(v)) {
        if(policy==sixwbkit::PlanePolicy::Hidden) {
            b.add(kSetVisibility,setVisibility(mainVisible_,false));
        } else {
            Batch drawing;
            if(!kit_.central(v,target,useful,perspective,drawing))return errorBatch("6wb-kit-error: central palette/geometry not representable");
            b.add(kRemoveAllItems,removeAllItems(target));
            b.calls.insert(b.calls.end(),drawing.calls.begin(),drawing.calls.end());
            b.add(kSwitchVisibility,switchVisibility(mainVisible_,target));
        }
    }
    if(fresh_ || (settings_.lanes && v.lanes!=shown_.lanes) || shownLanes_!=settings_.lanes) {
        Batch drawing;bool visible=settings_.lanes&&!v.lanes.empty();
        if(visible&&!kit_.lanes(v,lane,p_.lanes.width,p_.lanes.height,drawing))
            return errorBatch("6wb-kit-error: lane palette/geometry not representable");
        b.add(kRemoveAllItems,removeAllItems(lane));
        b.calls.insert(b.calls.end(),drawing.calls.begin(),drawing.calls.end());
        b.add(kSwitchVisibility,switchVisibility(laneVisible_,lane));
        b.add(kSetVisibility,setVisibility(lane,visible));
    }
    if(iconChanged(v)) {
        RoadShield shield;if(p_.roadShields)shield=roadShield(v.road);
        bool picture=shield.kind || v.has_next;
        if(picture) {
            std::vector<uint32_t> px=shieldPicture(p_.icons.width,p_.icons.height,shield);
            if(v.has_next && v.next_type>0) {
                Rect r=p_.nextGlyph;
                sixwbkit::NavigationEvent next;next.maneuver=GrafiteKit::maneuver(v.next_type);
                next.hasExitAngle=v.next_has_angle;next.exitAngle=(double)v.next_angle;
                rasterKit(px,p_.icons.width,p_.icons.height,sixwbkit::renderManeuver(next,sixwbkit::Rect(r.x0,r.y0,r.x1-r.x0,r.y1-r.y0)));
            }
            std::vector<uint8_t> payload=bitmap(icon,0,px,p_.icons.width,p_.icons.height,p_.maxBitmapBytes);
            if(!payload.empty()) {Call c(0xC05,payload);c.txType=0;b.calls.push_back(c);
                b.add(kSwitchVisibility,switchVisibility(iconVisible_,icon));}
            else return errorBatch("6wb-kit-error: next icon exceeds verified C05 limit");
        }
        b.add(kSetVisibility,setVisibility(icon,picture));b.add(kSetVisibility,setVisibility(iconVisible_,false));
    }
    std::vector<uint8_t> street=latinText(p_.base.street,v.road,p_.base.streetChars);
    if(fresh_ || street!=latinText(p_.base.street,shown_.road,p_.base.streetChars))b.add(kTextareaSetText,street);
    std::string dist=v.dist_text=="-"?"":v.dist_text,oldDist=shown_.dist_text=="-"?"":shown_.dist_text;
    for(size_t i=0;i<dist.size();++i)if(dist[i]==',')dist[i]='.';
    for(size_t i=0;i<oldDist.size();++i)if(oldDist[i]==',')oldDist[i]='.';
    std::vector<uint8_t> distance=latinText(p_.base.distance,dist,p_.base.distanceChars);
    if(fresh_ || distance!=latinText(p_.base.distance,oldDist,p_.base.distanceChars))b.add(kTextareaSetText,distance);
    std::string unit=dist.empty()?"":hudUnitText(v.aa_unit),oldUnit=oldDist.empty()?"":hudUnitText(shown_.aa_unit);
    if(fresh_ || unit!=oldUnit)b.add(kTextareaSetText,latinText(p_.base.unit,unit,3));
    if(fresh_) {
        b.add(kSetVisibility,setVisibility(p_.base.street,true));b.add(kSetVisibility,setVisibility(p_.base.distance,true));
        b.add(kSetVisibility,setVisibility(p_.base.unit,true));
    }
    KitBarState oldBar=kitBarState(shown_,p_.barRangeMetres);
    if(bar.visible && (fresh_ || !oldBar.visible || bar.value!=oldBar.value))b.add(0x227,distanceBar(p_.bar,bar.value));
    if(fresh_ || bar.visible!=oldBar.visible)b.add(kSetVisibility,setVisibility(p_.bar,bar.visible));
    if(fresh_)b.add(kSetVisibility,setVisibility(p_.base.frame,true));
    if(b.calls.size()>4096)return errorBatch("6wb-kit-error: bounded frame call budget exceeded");
    return b;
}
void Hud6wbDisplay::remember(const NavView& v,const Batch& b) {
    // Commit predicted buffer state only after the batch is accepted. The next
    // batch waits for local dispatch completion; acknowledgement order is irrelevant.
    for(size_t i=0;i<b.calls.size();++i)if(b.calls[i].fkt==kSwitchVisibility) {
        const std::vector<uint8_t>& p=b.calls[i].payload;uint16_t from=rdBe16(&p[0]),to=rdBe16(&p[2]);
        if(from==mainVisible_)mainVisible_=to;
        else if(from==laneVisible_)laneVisible_=to;
        else if(from==iconVisible_)iconVisible_=to;
    }
    if(!b.calls.empty())sent_=!shadow_;
    fresh_=false;dirty_=false;shown_=v;shownLanes_=settings_.lanes;
}
ClusterDisplay::Result Hud6wbDisplay::showManeuver(const NavView& v,double,bool,bool) {
    Result r=health();if(r!=kOk)return r;
    if(shadow_ && fresh_)submit(resources_.queries());
    if(!canDraw()) {dirty_=true;return resources_.state()==BmwResources::kIncompatible?kRejected:kOk;}
    if(queuedPicture_.calls.empty()) {
        Batch b=render(v);
        if(b.label.find("6wb-kit-error:")==0) {
            if(!kitErrorLogged_) {char detail[192];snprintf(detail,sizeof(detail)," | AA type %d, angle %s%lld, exit %s%lld, lanes %u",v.aa_type,
                v.has_exit_angle?"":"absent/",(long long)v.exit_angle,v.has_exit_number?"":"absent/",(long long)v.exit_number,(unsigned)v.lanes.size());
                log(b.label+detail);kitErrorLogged_=true;}
            return kInternal;
        }
        for(size_t i=0;i<b.calls.size();++i)if(!callOk(b.calls[i],handlesOf(p_),true))return kInternal;
        if(b.calls.empty()){remember(v,b);return kOk;}
        queuedPicture_=b;queuedView_=v;queuedCall_=0;
    }
    // Chunking is an adapter concern, not a new MOST queue or retry strategy.
    // One bounded chunk is dispatched at a time. The visible buffer flips only
    // after its ordered geometry commands; never replace a partial picture.
    Batch chunk("6wb-kit-chunk",false);size_t end=std::min(queuedCall_+128,queuedPicture_.calls.size());
    chunk.calls.assign(queuedPicture_.calls.begin()+queuedCall_,queuedPicture_.calls.begin()+end);
    if(!submit(chunk)){dirty_=true;return health();}
    sent_=!shadow_;queuedCall_=end;
    if(queuedCall_==queuedPicture_.calls.size()) {
        remember(queuedView_,queuedPicture_);queuedPicture_=Batch();queuedCall_=0;
    } else dirty_=true;
    return kOk;
}
ClusterDisplay::Result Hud6wbDisplay::updateDistance(const NavView& v,double now) {
    return showManeuver(v,now,false,false);
}
ClusterDisplay::Result Hud6wbDisplay::keepUp(const NavView& v,double now) {
    if(!queuedPicture_.calls.empty() || dirty_ || fresh_ || v.has_step_m!=shown_.has_step_m || (v.has_step_m && v.step_m!=shown_.step_m))
        return showManeuver(v,now,false,false);
    return health();
}
Batch Hud6wbDisplay::clearBatch() const {
    Batch b("6wb-clear",true);b.add(kSetVisibility,setVisibility(p_.base.frame,false));
    const uint16_t hs[]={p_.main.a,p_.main.b,p_.lanes.a,p_.lanes.b};
    if(p_.flat.a){b.add(kSetVisibility,setVisibility(p_.flat.a,false));b.add(kSetVisibility,setVisibility(p_.flat.b,false));}
    for(size_t i=0;i<4;++i)b.add(kRemoveAllItems,removeAllItems(hs[i]));
    b.add(kSetVisibility,setVisibility(p_.icons.a,false));b.add(kSetVisibility,setVisibility(p_.icons.b,false));
    b.add(kSetVisibility,setVisibility(p_.bar,false));
    b.add(kTextareaSetText,latinText(p_.base.street,"",15));b.add(kTextareaSetText,latinText(p_.base.distance,"",5));
    b.add(kTextareaSetText,latinText(p_.base.unit,"",3));return b;
}
bool Hud6wbDisplay::clear(std::string* detail) {
    calibration_.cancel();calibrationClearQueued_=false;
    queuedPicture_=Batch();queuedCall_=0;
    if(shadow_)return submit(clearBatch());
    if(!sent_ || port_.status()==kNavActive)return true;
    // Never reset a failed terminal just to send cleanup; no writes to unverified resources.
    if(!ready() || !port_.waitIdle(3000) || !submit(clearBatch()) || !port_.waitIdle(3000)) {
        if(detail)*detail="6wb: cleanup withheld or unconfirmed; terminal not exclusively ready";
        return false;
    }
    return true;
}
void Hud6wbDisplay::forget() {
    sent_=false;fresh_=true;dirty_=true;shown_=NavView();queuedPicture_=Batch();queuedCall_=0;kitErrorLogged_=false;
    mainVisible_=p_.main.b;laneVisible_=p_.lanes.b;iconVisible_=p_.icons.b;
}
} }
