// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "cluster/hud6wb_display.hpp"
#include "cluster/hud6wb_artwork.hpp"
#include "cluster/hud6wb_codec.hpp"
#include "cluster/hud6wa_display.hpp"
#include <stdio.h>
#include <stdlib.h>
namespace nbt { namespace kombinav {
using namespace hudterm;
namespace {
uint16_t other(uint16_t v,const Surface& s) {return v==s.a?s.b:s.a;}
void polygons(Batch& b,uint16_t h,const Shape& shape,unsigned& item) {
    for(size_t i=0;i<shape.size();++i)b.add(kAddPolygon,addPolygon(h,(uint16_t)item++,shape[i].type,shape[i].pts));
}
void plot(std::vector<uint32_t>& px,int w,int h,int x,int y) {if(x>=0&&y>=0&&x<w&&y<h)px[(size_t)y*w+x]=0xffffffff;}
void rasterLines(std::vector<uint32_t>& px,int w,int h,const Shape& s) {
    for(size_t k=0;k<s.size();++k)for(size_t i=1;i<s[k].pts.size();++i) {
        int x=s[k].pts[i-1].x,y=s[k].pts[i-1].y,xx=s[k].pts[i].x,yy=s[k].pts[i].y;
        int dx=abs(xx-x),dy=-abs(yy-y),sx=x<xx?1:-1,sy=y<yy?1:-1,e=dx+dy;
        for(;;) {plot(px,w,h,x,y);if(x==xx&&y==yy)break;int ee=2*e;if(ee>=dy){e+=dy;x+=sx;}if(ee<=dx){e+=dx;y+=sy;}}
    }
}
// Optional software coverage sampling. Opaque grayscale only: no new terminal
// drawing method or unverified partial-alpha interpretation is required.
void raster(std::vector<uint32_t>& px,int w,int h,const Shape& s,bool aa) {
    if(!aa){rasterLines(px,w,h,s);return;}
    std::vector<uint32_t> mask((size_t)w*h*4,0);
    for(int oy=0;oy<2;++oy)for(int ox=0;ox<2;++ox) {
        Shape scaled=s;
        for(size_t k=0;k<scaled.size();++k)for(size_t i=0;i<scaled[k].pts.size();++i) {
            scaled[k].pts[i].x=(int16_t)(scaled[k].pts[i].x*2+ox);
            scaled[k].pts[i].y=(int16_t)(scaled[k].pts[i].y*2+oy);
        }
        rasterLines(mask,w*2,h*2,scaled);
    }
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        unsigned coverage=0;
        for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)if(mask[(size_t)(y*2+dy)*w*2+x*2+dx])++coverage;
        if(!coverage)continue;
        uint32_t old=px[(size_t)y*w+x],color=0xff000000;
        for(int shift=0;shift<24;shift+=8) {
            unsigned v=(old>>shift)&255;v=(v*(4-coverage)+255*coverage+2)/4;color|=v<<shift;
        }
        px[(size_t)y*w+x]=color;
    }
}

}
Hud6wbDisplay::Hud6wbDisplay(Port& port,const Bmw6wbProfile& p,bool shadow)
    :port_(port),p_(p),resources_(p),shadow_(shadow),sent_(false),fresh_(true),dirty_(false),shownLanes_(true),epoch_(port.epoch()),
    mainVisible_(p.main.b),laneVisible_(p.lanes.b),iconVisible_(p.icons.b),log_(NULL),user_(NULL) {}
void Hud6wbDisplay::log(const std::string& s) const {if(log_)log_(s.c_str(),user_);}
ClusterDisplay::Result Hud6wbDisplay::health() {
    if(!validProfile(p_))return kInternal;
    if(shadow_)return kOk;
    if(port_.status()==kFailed)return kRejected;
    if(port_.epoch()!=epoch_) {
        epoch_=port_.epoch();resources_.forget();fresh_=true;dirty_=true;
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
       (v.next_has_angle && v.next_angle!=shown_.next_angle)))return true;
    if(p_.roadShields) {
        RoadShield a=roadShield(v.road),b=roadShield(shown_.road);
        if(a.kind!=b.kind || a.number!=b.number)return true;
    }
    return false;
}
bool Hud6wbDisplay::sameManeuver(const NavView& v,double) {
    health();
    return dirty_ || mainChanged(v) || iconChanged(v) || (settings_.lanes && v.lanes!=shown_.lanes) || shownLanes_!=settings_.lanes;
}
Batch Hud6wbDisplay::render(const NavView& v) const {
    Batch b("6wb-guidance",true);
    uint16_t target=other(mainVisible_,p_.main),lane=other(laneVisible_,p_.lanes),icon=other(iconVisible_,p_.icons);
    if(fresh_) {
        b.add(kSetVisibility,setVisibility(p_.main.a,false));b.add(kSetVisibility,setVisibility(p_.main.b,false));
        b.add(kSetVisibility,setVisibility(p_.lanes.a,false));b.add(kSetVisibility,setVisibility(p_.lanes.b,false));
        b.add(kSetVisibility,setVisibility(p_.icons.a,false));b.add(kSetVisibility,setVisibility(p_.icons.b,false));
    }
    if(mainChanged(v)) {
        b.add(kRemoveAllItems,removeAllItems(target));b.add(kSetFillColor,setFillColor(target,p_.base.fill));
        b.add(kSetLineWidth,setLineWidth(target,p_.lineWidth));unsigned item=0x80;
        ShapeSpec mainSpec=shapeFor(v.aa_type,v.has_exit_angle,v.exit_angle);
        if(v.aa_type>0)polygons(b,target,bmwManeuver(mainSpec,p_.base.glyph,v.has_exit_angle,p_.roundaboutSegments),item);
        if(mainSpec.kind==kShapeRoundabout && v.has_exit_number) {
            const Rect& g=p_.base.glyph;int cx=(g.x0+g.x1)/2,cy=g.y0+(g.y1-g.y0)*43/100;
            Rect r={(int16_t)(cx-9),(int16_t)(cy-6),(int16_t)(cx+9),(int16_t)(cy+6)};
            polygons(b,target,bmwExitNumber((unsigned)v.exit_number,r),item);
        }
        b.add(kSwitchVisibility,switchVisibility(mainVisible_,target));
    }
    if(fresh_ || (settings_.lanes && v.lanes!=shown_.lanes) || shownLanes_!=settings_.lanes) {
        b.add(kRemoveAllItems,removeAllItems(lane));b.add(kSetFillColor,setFillColor(lane,p_.base.fill));
        b.add(kSetLineWidth,setLineWidth(lane,p_.laneLineWidth));unsigned item=0x80;
        bool valid=settings_.lanes && !v.lanes.empty() && v.lanes.size()<=8;
        for(size_t i=0;i<v.lanes.size() && valid;++i) {
            if(v.lanes[i].empty())valid=false;
            for(size_t j=0;j<v.lanes[i].size();++j)if(v.lanes[i][j].shape<1 || v.lanes[i][j].shape>9)valid=false;
        }
        if(valid) {
            int cell=(p_.lanes.width-8)/(int)v.lanes.size();
            for(size_t i=0;i<v.lanes.size();++i) {
                // Preserve every direction of a multi-direction lane; do not invent a recommended one.
                Rect r={(int16_t)(4+i*cell+2),3,(int16_t)(4+(i+1)*cell-3),(int16_t)(p_.lanes.height-4)};
                for(size_t j=0;j<v.lanes[i].size();++j)
                    polygons(b,lane,bmwLane(v.lanes[i][j].shape,v.lanes[i][j].highlighted,r),item);
            }
        }
        b.add(kSwitchVisibility,switchVisibility(laneVisible_,lane));
        b.add(kSetVisibility,setVisibility(lane,valid));
    }
    if(iconChanged(v)) {
        RoadShield shield;if(p_.roadShields)shield=roadShield(v.road);
        bool picture=shield.kind || v.has_next;
        if(picture) {
            std::vector<uint32_t> px=shieldPicture(p_.icons.width,p_.icons.height,shield);
            if(v.has_next && v.next_type>0) {
                Rect r=p_.nextGlyph;
                raster(px,p_.icons.width,p_.icons.height,bmwManeuver(shapeFor(v.next_type,v.next_has_angle,v.next_angle),r,v.next_has_angle,p_.roundaboutSegments),p_.iconAntialias);
            }
            std::vector<uint8_t> payload=bitmap(icon,0,px,p_.icons.width,p_.icons.height,p_.maxBitmapBytes);
            if(!payload.empty()) {Call c(0xC05,payload);c.txType=0;b.calls.push_back(c);
                b.add(kSwitchVisibility,switchVisibility(iconVisible_,icon));}
            else picture=false; // Oversize images are omitted, never fragmented with an unverified format.
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
    double metres=0,oldMetres=0;bool has=distanceMetres(v,metres),oldHas=distanceMetres(shown_,oldMetres);
    int value=metres<=0?0:metres>=p_.barRangeMetres?255:(int)(metres*255/p_.barRangeMetres);
    int oldValue=oldMetres<=0?0:oldMetres>=p_.barRangeMetres?255:(int)(oldMetres*255/p_.barRangeMetres);
    if(has && (fresh_ || !oldHas || value!=oldValue))b.add(0x227,distanceBar(p_.bar,value));
    if(fresh_ || has!=oldHas)b.add(kSetVisibility,setVisibility(p_.bar,has));
    if(fresh_)b.add(kSetVisibility,setVisibility(p_.base.frame,true));
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
    Batch b=render(v);
    for(size_t i=0;i<b.calls.size();++i)if(!callOk(b.calls[i],handlesOf(p_),true))return kInternal;
    if(!b.calls.empty() && !submit(b)) {dirty_=true;return health();}
    remember(v,b);return kOk;
}
ClusterDisplay::Result Hud6wbDisplay::updateDistance(const NavView& v,double now) {
    return showManeuver(v,now,false,false);
}
ClusterDisplay::Result Hud6wbDisplay::keepUp(const NavView& v,double now) {
    if(dirty_ || fresh_ || v.has_step_m!=shown_.has_step_m || (v.has_step_m && v.step_m!=shown_.step_m))
        return showManeuver(v,now,false,false);
    return health();
}
Batch Hud6wbDisplay::clearBatch() const {
    Batch b("6wb-clear",true);b.add(kSetVisibility,setVisibility(p_.base.frame,false));
    const uint16_t hs[]={p_.main.a,p_.main.b,p_.lanes.a,p_.lanes.b};
    for(size_t i=0;i<4;++i)b.add(kRemoveAllItems,removeAllItems(hs[i]));
    b.add(kSetVisibility,setVisibility(p_.icons.a,false));b.add(kSetVisibility,setVisibility(p_.icons.b,false));
    b.add(kSetVisibility,setVisibility(p_.bar,false));
    b.add(kTextareaSetText,latinText(p_.base.street,"",15));b.add(kTextareaSetText,latinText(p_.base.distance,"",5));
    b.add(kTextareaSetText,latinText(p_.base.unit,"",3));return b;
}
bool Hud6wbDisplay::clear(std::string* detail) {
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
    sent_=false;fresh_=true;dirty_=true;shown_=NavView();
    mainVisible_=p_.main.b;laneVisible_=p_.lanes.b;iconVisible_=p_.icons.b;
}
} }
