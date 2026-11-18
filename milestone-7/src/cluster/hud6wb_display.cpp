// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "cluster/hud6wb_display.hpp"
#include "cluster/hud6wb_codec.hpp"
#include "cluster/grafite_oro/miniature_raster.hpp"
#include "cluster/hud6wa_display.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <algorithm>
namespace nbt { namespace kombinav {
using namespace hudterm;
namespace {
uint16_t other(uint16_t v,const Surface& s) {return v==s.a?s.b:s.a;}
Batch errorBatch(const char* why){Batch b(why,true);return b;}

}
Hud6wbDisplay::Hud6wbDisplay(Port& port,const Bmw6wbProfile& p,bool shadow,const GrafiteKit& kit)
    :kit_(kit),port_(port),p_(p),resources_(p),shadow_(shadow),sent_(false),fresh_(true),dirty_(false),shownLanes_(true),epoch_(port.epoch()),
    mainVisible_(p.main.b),laneVisible_(p.lanes.b),iconVisible_(p.icons.b),log_(NULL),user_(NULL),queuedCall_(0),kitErrorLogged_(false) {}
void Hud6wbDisplay::log(const std::string& s) const {if(log_)log_(s.c_str(),user_);}
ClusterDisplay::Result Hud6wbDisplay::health() {
    if(!validProfile(p_))return kInternal;
    if(shadow_)return kOk;
    if(port_.status()==kFailed)return kRejected;
    if(port_.epoch()!=epoch_) {
        epoch_=port_.epoch();
        resources_.forget();fresh_=true;dirty_=true;queuedPicture_=Batch();queuedCall_=0;
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
bool Hud6wbDisplay::mainChanged(const NavView& v) const {
    return fresh_ || v.aa_type!=shown_.aa_type || v.has_exit_angle!=shown_.has_exit_angle ||
        (v.has_exit_angle && v.exit_angle!=shown_.exit_angle) || v.has_exit_number!=shown_.has_exit_number ||
        (v.has_exit_number && v.exit_number!=shown_.exit_number);
}
bool Hud6wbDisplay::iconChanged(const NavView& v) const {
    if(fresh_ || v.has_next!=shown_.has_next)return true;
    if(v.has_next && (v.next_type!=shown_.next_type || v.next_has_angle!=shown_.next_has_angle ||
       (v.next_has_angle && v.next_angle!=shown_.next_angle) || v.next_exit_number!=shown_.next_exit_number))return true;
    if(p_.roadShields) {
        if(road_sign::route(v.road)!=road_sign::route(shown_.road))return true;
    }
    return false;
}
bool Hud6wbDisplay::sameManeuver(const NavView& v,double) {
    health();
    return !queuedPicture_.calls.empty() || dirty_ || (p_.roadShields && v.road!=shown_.road) || mainChanged(v) || iconChanged(v) || (settings_.lanes && v.lanes!=shown_.lanes) || shownLanes_!=settings_.lanes;
}
Batch Hud6wbDisplay::render(const NavView& v) const {
    Batch b("6wb-guidance",false);
    KitBarState bar=kitBarState(v,p_.barRangeMetres);
    bar.visible=bar.visible && (shadow_||resources_.barUsable(port_));
    sixwbkit::Maneuver::Value m=GrafiteKit::maneuver(v.aa_type);
    sixwbkit::PlanePolicy::Value policy=sixwbkit::describe(m).policy;
    const Rect& useful=p_.base.glyph;
    uint16_t target=other(mainVisible_,p_.main);
    uint16_t lane=other(laneVisible_,p_.lanes);
    const uint16_t icon=p_.iconHandle?p_.iconHandle:other(iconVisible_,p_.icons);
    const uint16_t otherIcon=other(icon,p_.icons);
    // Preflight the whole picture before any buffer is cleared or hidden.
    if(mainChanged(v)) {
        if(policy==sixwbkit::PlanePolicy::Hidden) {
            b.add(kSetVisibility,setVisibility(mainVisible_,false));
        } else {
            Batch drawing;
            if(!kit_.central(v,target,useful,drawing))return errorBatch("6wb-kit-error: central palette/geometry not representable");
            b.add(kRemoveAllItems,removeAllItems(target));
            b.calls.insert(b.calls.end(),drawing.calls.begin(),drawing.calls.end());
            b.add(kSwitchVisibility,switchVisibility(mainVisible_,target));
            b.add(kSetVisibility,setVisibility(target,true));
        }
    }
    if(fresh_ || (settings_.lanes && v.lanes!=shown_.lanes) || shownLanes_!=settings_.lanes) {
        Batch drawing;bool visible=settings_.lanes&&(shadow_||resources_.lanesUsable(port_))&&!v.lanes.empty();
        if(visible&&!kit_.lanes(v,lane,p_.lanes.width,p_.lanes.height,drawing)) {
            visible=false;
            drawing=Batch();
            log("6wb: lane geometry omitted; central maneuver continues");
        }
        b.add(kRemoveAllItems,removeAllItems(lane));
        b.calls.insert(b.calls.end(),drawing.calls.begin(),drawing.calls.end());
        b.add(kSwitchVisibility,switchVisibility(laneVisible_,lane));
        b.add(kSetVisibility,setVisibility(lane,visible));
    }
    std::vector<uint8_t> signPayload;
    const bool iconSizeReady=shadow_||resources_.iconsUsable(port_);
    if(p_.roadShields && p_.iconBitmaps && iconSizeReady) {
        std::vector<uint32_t> pixels;
        if(road_sign::draw(road_sign::route(v.road),p_.icons.width,p_.icons.height,p_.signLayout,pixels))
            signPayload=bitmap(icon,0,pixels,p_.icons.width,p_.icons.height,p_.maxBitmapBytes);
    }
    const bool signVisible=!signPayload.empty();
    const bool refreshIcon=iconChanged(v) || (p_.roadShields && dirty_);
    if(refreshIcon) {
        const sixwbkit::Maneuver::Value nextType=GrafiteKit::maneuver(v.next_type);
        const bool nextDrawable=v.has_next && sixwbkit::describe(nextType).policy!=sixwbkit::PlanePolicy::Hidden;
        bool picture=p_.iconBitmaps && iconSizeReady && (signVisible || nextDrawable);
        if(picture) {
            std::vector<uint8_t> payload;
            payload.swap(signPayload);
            // Both images use the same bitmap surface. A road sign takes precedence.
            if(!signVisible && nextDrawable) {
                const Rect& r=p_.nextGlyph;
                std::vector<uint32_t> px((size_t)p_.icons.width*p_.icons.height,0);
                sixwbkit::NavigationEvent next;next.maneuver=nextType;
                next.hasExitAngle=v.next_has_angle;next.exitAngle=(double)v.next_angle;
                next.hasExitNumber=v.next_exit_number>0;next.exitNumber=(unsigned)v.next_exit_number;
                sixwbkit::rasterKit(px,p_.icons.width,p_.icons.height,
                    sixwbkit::renderMiniature(next,sixwbkit::Rect(r.x0,r.y0,r.x1-r.x0+1,r.y1-r.y0+1)));
                payload=bitmap(icon,0,px,p_.icons.width,p_.icons.height,p_.maxBitmapBytes);
            }
            if(!payload.empty()) {
                // A fixed handle may already be visible. Hide it before replacing
                // its contents, keeping the other handle out of this draw path.
                if(p_.iconHandle)b.add(kSetVisibility,setVisibility(icon,false));
                b.add(kRemoveAllItems,removeAllItems(icon));
                Call c(0xC05,payload);c.txType=0;b.calls.push_back(c);
                if(!p_.iconHandle)b.add(kSwitchVisibility,switchVisibility(iconVisible_,icon));
            }
            else {
                picture=false;
                log("6wb icons: bitmap exceeds the C05 payload limit; surfaces hidden");
            }
        }
        b.add(kSetVisibility,setVisibility(otherIcon,false));
        b.add(kSetVisibility,setVisibility(icon,picture));
    }
    if(fresh_ || v.road!=shown_.road || (p_.roadShields && refreshIcon)) {
        const std::string text=road_sign::street(v.road,signVisible,p_.signLayout,p_.base.streetChars);
        b.add(kTextareaSetText,latinText(p_.base.street,text,p_.base.streetChars));
    }
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
    oldBar.visible=oldBar.visible && (shadow_||resources_.barUsable(port_));
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
    for(size_t i=0;i<b.calls.size();++i) {
        const Call& c=b.calls[i];
        if(c.fkt==kSetVisibility && c.payload.size()==3 && c.payload[2] &&
           (c.handle()==p_.icons.a || c.handle()==p_.icons.b))iconVisible_=c.handle();
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
            b=Batch("6wb-unrepresentable",false);
            b.add(kSetVisibility,setVisibility(mainVisible_,false));
            b.add(kSetVisibility,setVisibility(p_.bar,false));
            b.add(kSetVisibility,setVisibility(p_.icons.a,false));
            b.add(kSetVisibility,setVisibility(p_.icons.b,false));
            b.add(kTextareaSetText,latinText(p_.base.street,v.road,p_.base.streetChars));
            b.add(kTextareaSetText,latinText(p_.base.distance,v.dist_text=="-"?"":v.dist_text,p_.base.distanceChars));
            b.add(kTextareaSetText,latinText(p_.base.unit,v.dist_text=="-"?"":hudUnitText(v.aa_unit),3));
            b.add(kSetVisibility,setVisibility(p_.base.street,true));
            b.add(kSetVisibility,setVisibility(p_.base.distance,true));
            b.add(kSetVisibility,setVisibility(p_.base.unit,true));
            b.add(kSetVisibility,setVisibility(p_.base.frame,true));
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
    Batch b("6wb-clear",true,kSlotAll);b.add(kSetVisibility,setVisibility(p_.base.frame,false));
    const uint16_t hs[]={p_.main.a,p_.main.b,p_.lanes.a,p_.lanes.b};
    for(size_t i=0;i<4;++i)b.add(kRemoveAllItems,removeAllItems(hs[i]));
    b.add(kSetVisibility,setVisibility(p_.icons.a,false));b.add(kSetVisibility,setVisibility(p_.icons.b,false));
    b.add(kSetVisibility,setVisibility(p_.bar,false));
    b.add(kTextareaSetText,latinText(p_.base.street,"",p_.base.streetChars));b.add(kTextareaSetText,latinText(p_.base.distance,"",5));
    b.add(kTextareaSetText,latinText(p_.base.unit,"",3));return b;
}
bool Hud6wbDisplay::clear(std::string* detail) {
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
