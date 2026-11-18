// Prepared for Windows/QNX or host execution later. Not compiled in Codex.
#include "cluster/grafite_oro/artwork6wb.hpp"
#include "cluster/hud6wb_kit.hpp"
#include "hud6wb_kit_test_util.hpp"
#include <assert.h>
#include <algorithm>
#include <math.h>
using namespace nbt;
using namespace nbt::sixwbkit;
void contained(const Scene& scene,const Rect& r) {
    assert(!scene.empty());
    for(size_t i=0;i<scene.size();++i)for(size_t c=0;c<scene[i].contours.size();++c)for(size_t j=0;j<scene[i].contours[c].points.size();++j) {
        const Point& p=scene[i].contours[c].points[j];double radius=scene[i].strokeWidth/2;
        assert(p.x-radius>=r.x-1e-6&&p.y-radius>=r.y-1e-6&&p.x+radius<=r.x+r.width+1e-6&&p.y+radius<=r.y+r.height+1e-6);
    }
}
int main() {
    Rect box(0,0,220,157);const double angles[]={1,15,90,127,180,225,270,359,360};
    for(unsigned cw=0;cw<2;++cw)for(unsigned i=0;i<sizeof(angles)/sizeof(*angles);++i) {
        NavigationEvent e;e.maneuver=cw?Maneuver::RoundaboutCWAngle:Maneuver::RoundaboutCCWAngle;
        e.hasExitAngle=true;e.exitAngle=angles[i];e.hasExitNumber=true;e.exitNumber=2;
        Scene s=renderManeuver(e,box,0);contained(s,box);
        double x0=220,y0=157,x1=0,y1=0;
        for(size_t j=0;j<s.size();++j)for(size_t c=0;c<s[j].contours.size();++c)
            for(size_t k=0;k<s[j].contours[c].points.size();++k) {
                const Point& p=s[j].contours[c].points[k];
                const double half=s[j].strokeWidth/2;
                x0=std::min(x0,p.x-half);y0=std::min(y0,p.y-half);
                x1=std::max(x1,p.x+half);y1=std::max(y1,p.y+half);
            }
        assert(fabs(std::max((x1-x0)/220,(y1-y0)/157)-0.96)<1e-6);
        assert(fabs(x0+x1-220)<1e-6&&fabs(y0+y1-157)<1e-6);
        assert(s[0].contours.size()==2); // hole retained as even-odd geometry
        const std::vector<Point>& outer=s[0].contours[0].points;Point center={0,0};
        for(size_t j=0;j<outer.size();++j){center.x+=outer[j].x/outer.size();center.y+=outer[j].y/outer.size();}
        double rx=outer[0].x-center.x,ry=outer[0].y-center.y,radius=sqrt(rx*rx+ry*ry);
        for(size_t j=0;j<outer.size();++j){double x=outer[j].x-center.x,y=outer[j].y-center.y;
            assert(fabs(sqrt(x*x+y*y)-radius)<1e-6);}
    }
    NavigationEvent generic;generic.maneuver=Maneuver::RoundaboutCCW;contained(renderManeuver(generic,box),box);
    for(unsigned count=2;count<=6;++count) {
        kombinav::NavView v;
        for(unsigned i=0;i<count;++i) {kombinav::AaLane lane;
            lane.push_back(kombinav::LaneDirection(1,i%2==0));lane.push_back(kombinav::LaneDirection(7,false));
            lane.push_back(kombinav::LaneDirection(2,i%2!=0));v.lanes.push_back(lane);}
        NavigationEvent e;assert(hudterm::GrafiteKit::laneEvent(v,e));assert(e.lanes.size()==count);
        assert(e.lanes[0].directions.size()==3&&e.lanes[0].directions[1].shape==7);
        assert(e.lanes[0].directions[0].highlighted&&!e.lanes[0].directions[1].highlighted);
        Scene tips=renderLanes(e.lanes,Rect(0,0,282,39));
        contained(tips,Rect(0,0,282,39));
        unsigned heads=0;
        for(size_t j=0;j<tips.size();++j){
            const Shape& tip=tips[j];assert(tip.contours.size()==1);
            if(tip.contours[0].points.size()!=3)continue; // Work lane separators.
            ++heads;double top=39;
            for(size_t k=0;k<tip.contours[0].points.size();++k)
                top=std::min(top,tip.contours[0].points[k].y);
            assert(top-tip.strokeWidth/2>=4-1e-6);
            // Each fixture lane has at least one highlighted direction.
            assert(tip.fill==Color::White&&tip.stroke==Color::Red);
        }
        assert(heads==count*3);
    }
    hudterm::GrafiteKit kit;hudterm::Bmw6wbProfile fixedProfile;
    for(unsigned type=0;type<=50;++type)for(unsigned which=0;which<2;++which) {
        kombinav::NavView view;view.aa_type=type;view.has_exit_angle=type>=30&&type<=35;
        view.exit_angle=225;view.has_exit_number=view.has_exit_angle;view.exit_number=2;
        hudterm::Batch central;uint16_t handle=which?0x20:0x1f;
        if((type>=1&&type<=29)||type==36){NavigationEvent e;e.maneuver=hudterm::GrafiteKit::maneuver(type);
            Scene flat=renderManeuver(e,box,0);contained(flat,box);assert(!flat.empty());
            const Shape& arrow=flat.back();assert(arrow.fill==Color::White&&arrow.stroke==Color::Red);
            assert(arrow.contours.size()==1&&arrow.contours[0].closed);}
        assert(kit.central(view,handle,fixedProfile.base.glyph,central));
        for(size_t i=0;i<central.calls.size();++i) {
            const hudterm::Call& c=central.calls[i];assert(c.handle()==handle);assert(c.fkt!=hudterm::kSetLineColor&&c.fkt!=hudterm::kSetLineWidth);
            if(c.fkt==hudterm::kAddPolygon)for(unsigned j=0;j<c.payload[5];++j) {
                int x=hudterm::rdBe16(&c.payload[6+j*4]),y=hudterm::rdBe16(&c.payload[8+j*4]);
                assert(x>=0&&x<=220&&y>=0&&y<=157);
            }
        }
        hudterm::Batch rejected;assert(!kit.central(view,0x21,fixedProfile.base.glyph,rejected));assert(rejected.calls.empty());
    }
    kombinav::NavView v;hudterm::KitBarState bar=hudterm::kitBarState(v,1000);assert(!bar.visible);
    v.has_step_m=true;v.step_m=1400;bar=hudterm::kitBarState(v,1000);assert(!bar.visible);
    v.step_m=500;bar=hudterm::kitBarState(v,500);assert(bar.visible&&bar.value==255);
    HudTerminalPalette p;uint8_t index=255;assert(hudColorIndex(p,Color::Gold,index)==HudPaint::Unmapped);
    p.gold=0;assert(hudColorIndex(p,Color::Gold,index)==HudPaint::Mapped&&index==0);
    p.gold=20;assert(hudColorIndex(p,Color::Gold,index)==HudPaint::Unmapped);
    assert(hudPaletteSwatches(Rect(0,0,282,39),10).size()==20);
    // Serializing the parametric ring must retain bounded method payloads.
    NavigationEvent ring;ring.maneuver=Maneuver::RoundaboutCCWAngle;ring.hasExitAngle=true;ring.exitAngle=225;
    HudTerminalPalette measuredFixture=hudterm::provisionalKitPalette();
    hudterm::Batch encoded;hudterm::Rect drawingBounds={0,0,220,157};
    assert(hudterm::GrafiteKit::encode(renderManeuver(ring,box),measuredFixture,0x1f,drawingBounds,encoded));
    hudterm::Bmw6wbProfile profile;
    for(size_t i=0;i<encoded.calls.size();++i)assert(hudterm::callOk(encoded.calls[i],hudterm::handlesOf(profile),true));
    // Missing palette is an explicit failure, not a legacy-style fallback.
    hudterm::Batch unchanged;Scene s;Shape sh;sh.fill=Color::Gold;Contour c;c.closed=true;
    Point a={1,1},b={10,1},d={1,10};c.points.push_back(a);c.points.push_back(b);c.points.push_back(d);sh.contours.push_back(c);s.push_back(sh);
    hudterm::Rect bounds={0,0,20,20};assert(!hudterm::GrafiteKit::encode(s,p,0x1f,bounds,unchanged));assert(unchanged.calls.empty());
}
