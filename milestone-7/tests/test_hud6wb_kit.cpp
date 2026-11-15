// Prepared for Windows/QNX or host execution later. Not compiled in Codex.
#include "cluster/grafite_oro/artwork6wb.hpp"
#include "cluster/hud6wb_kit.hpp"
#include "hud6wb_kit_test_util.hpp"
#include <assert.h>
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
    Rect box(20,74,180,77);const double angles[]={1,15,90,127,180,225,270,359,360};
    for(unsigned cw=0;cw<2;++cw)for(unsigned i=0;i<sizeof(angles)/sizeof(*angles);++i) {
        NavigationEvent e;e.maneuver=cw?Maneuver::RoundaboutCWAngle:Maneuver::RoundaboutCCWAngle;
        e.hasExitAngle=true;e.exitAngle=angles[i];e.hasExitNumber=true;e.exitNumber=2;
        Scene s=renderManeuver(e,box);contained(s,box);
        assert(s[0].contours.size()==2); // hole retained as even-odd geometry
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
        contained(renderLanes(e.lanes,Rect(0,0,282,39)),Rect(0,0,282,39));
    }
    for(unsigned type=0;type<=46;++type) {
        Maneuver::Value m=hudterm::GrafiteKit::maneuver(type);assert((unsigned)m<Maneuver::Count);
        PlanePolicy::Value policy=describe(m).policy;
        assert(hudterm::kitPlane(policy,false)==hudterm::KitFlat);
        assert(hudterm::kitPlane(policy,true)==(policy==PlanePolicy::DistanceBased?hudterm::KitPerspective:hudterm::KitFlat));
    }
    kombinav::NavView v;hudterm::KitBarState bar=hudterm::kitBarState(v,1000);assert(!bar.visible);
    v.has_step_m=true;v.step_m=1400;bar=hudterm::kitBarState(v,1000);assert(bar.visible&&bar.value==255);
    assert(hudterm::kitPlane(PlanePolicy::DistanceBased,bar.visible)==hudterm::KitPerspective);
    HudTerminalPalette p;uint8_t index=255;assert(hudColorIndex(p,Color::Gold,index)==HudPaint::Unmapped);
    p.gold=0;assert(hudColorIndex(p,Color::Gold,index)==HudPaint::Mapped&&index==0);
    p.gold=20;assert(hudColorIndex(p,Color::Gold,index)==HudPaint::Unmapped);
    assert(hudPaletteSwatches(Rect(0,0,282,39),10).size()==20);
    // Serializing the parametric ring must retain bounded method payloads.
    NavigationEvent ring;ring.maneuver=Maneuver::RoundaboutCCWAngle;ring.hasExitAngle=true;ring.exitAngle=225;
    HudTerminalPalette measuredFixture;measuredFixture.graphite=0;measuredFixture.gold=1;measuredFixture.white=2;
    hudterm::Batch encoded;hudterm::Rect drawingBounds={20,74,200,151};
    assert(hudterm::GrafiteKit::encode(renderManeuver(ring,box),measuredFixture,0x1f,drawingBounds,encoded));
    hudterm::Bmw6wbProfile profile;
    for(size_t i=0;i<encoded.calls.size();++i)assert(hudterm::callOk(encoded.calls[i],hudterm::handlesOf(profile),true));
    // Missing palette is an explicit failure, not a legacy-style fallback.
    hudterm::Batch unchanged;Scene s;Shape sh;sh.fill=Color::Gold;Contour c;c.closed=true;
    Point a={1,1},b={10,1},d={1,10};c.points.push_back(a);c.points.push_back(b);c.points.push_back(d);sh.contours.push_back(c);s.push_back(sh);
    hudterm::Rect bounds={0,0,20,20};assert(!hudterm::GrafiteKit::encode(s,p,0x1f,bounds,unchanged));assert(unchanged.calls.empty());
}
