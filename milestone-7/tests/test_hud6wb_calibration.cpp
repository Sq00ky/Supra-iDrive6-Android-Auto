// Deterministic C++ tests prepared only: no compilation/execution in Codex.
#include "cluster/hud6wb_calibration.hpp"
#include "cluster/hud6wb_display.hpp"
#include "hud6wb_kit_test_util.hpp"
#include <assert.h>
using namespace nbt::hudterm;
struct CalibrationPort:Port {
    bool busy,queried;std::vector<Batch> batches;Bmw6wbProfile p;
    CalibrationPort():busy(false),queried(false){}
    SubmitResult submit(const Batch& b){batches.push_back(b);busy=true;if(b.label=="6wb-size-check")queried=true;return kQueued;}
    Status status()const{return kReady;}uint32_t epoch()const{return 0;}
    bool waiting()const{return busy;}void reset(){}bool waitIdle(uint32_t){return !busy;}
    bool imageSize(uint16_t handle,uint16_t& w,uint16_t& h)const{
        if(!queried)return false;
        if(handle==p.base.frame){w=p.frameWidth;h=p.frameHeight;return true;}
        if(handle==p.bar){w=p.barWidth;h=p.barHeight;return true;}
        if(handle==p.main.a||handle==p.main.b){w=p.mainImageWidth;h=p.mainImageHeight;return true;}
        Surface surfaces[]={p.main,p.icons,p.lanes};
        for(unsigned i=0;i<3;++i)if(handle==surfaces[i].a||handle==surfaces[i].b){w=surfaces[i].width;h=surfaces[i].height;return true;}
        return false;
    }
};
void lifecycleIntegration() {
    CalibrationPort port;Bmw6wbProfile profile;
    nbt::kombinav::Hud6wbDisplay display(port,profile,false);
    nbt::kombinav::NavView first;first.aa_type=8;first.road="First";first.has_step_m=true;first.step_m=250;first.dist_text="250";first.aa_unit=1;
    display.navigationRouteStarted();assert(display.interceptNavigation(&first,1,0));
    assert(port.batches.size()==1&&port.batches.back().label=="6wb-size-check");
    port.busy=false;assert(display.interceptNavigation(&first,1,1));
    assert(port.batches.size()==2&&port.batches.back().label=="6wb-palette-calibration");
    port.busy=false;assert(display.interceptNavigation(&first,1,2)); // hold deadline = 12
    nbt::kombinav::NavView latest=first;latest.aa_type=7;latest.road="Latest";latest.step_m=90;latest.dist_text="90";
    assert(display.interceptNavigation(&latest,1,11.999999));assert(port.batches.size()==2);
    assert(display.interceptNavigation(NULL,3,12)); // rerouting does not freeze the deadline
    assert(port.batches.size()==3&&port.batches.back().label=="6wb-palette-clear");
    port.busy=false;assert(!display.interceptNavigation(&latest,1,12.01));
    assert(!display.interceptNavigation(&latest,1,20));assert(port.batches.size()==3);
    Batch current=display.render(latest);bool foundLatest=false;
    for(size_t i=0;i<current.calls.size();++i)if(current.calls[i].fkt==kTextareaSetText&&current.calls[i].handle()==profile.base.street)
        foundLatest=std::string(current.calls[i].payload.begin()+3,current.calls[i].payload.end()-1)=="Latest";
    assert(foundLatest);
    display.interceptNavigation(NULL,2,21);display.navigationRouteStarted();
    assert(display.interceptNavigation(&latest,1,22));assert(port.batches.back().label=="6wb-palette-calibration");
    // The next route uses the other A/B buffer, with the same filled chart.
    unsigned polygons=0;
    for(size_t i=0;i<port.batches.back().calls.size();++i) {
        const Call& c=port.batches.back().calls[i];
        assert(c.fkt!=kSetLineColor);
        if(c.fkt==kAddPolygon){assert(c.handle()==0x20&&c.payload[4]==0x03&&c.payload[5]==4);
            if(rdBe16(&c.payload[2])>=0x80)++polygons;}
    }
    assert(polygons==20);
}
int main() {
    lifecycleIntegration();
    PaletteCalibration c;c.observe(2);assert(!c.active());
    c.observe(1);assert(c.phase()==PaletteCalibration::Pending);
    c.queued();assert(c.phase()==PaletteCalibration::Dispatching);
    c.dispatched(100);assert(!c.due(109.999999));assert(c.due(110));
    c.observe(1);c.observe(3);c.observe(1);assert(c.phase()==PaletteCalibration::Holding);
    assert(!c.due(109.999999));assert(c.due(110));
    c.clearQueued();c.finished();assert(c.phase()==PaletteCalibration::Done);
    c.observe(1);assert(!c.active());c.observe(2);c.observe(1);assert(c.phase()==PaletteCalibration::Pending);
    c.cancel();c.observe(3);c.observe(1);assert(!c.active());
    c.reset();c.observe(1);assert(c.active());
    PaletteCalibration disabled(false);disabled.observe(1);assert(!disabled.active());
    CalibrationPort port;nbt::hudterm::Bmw6wbProfile profile;
    nbt::kombinav::Hud6wbDisplay display(port,profile,true,testKit());
    Batch chart=display.calibrationBatch();assert(chart.calls.size()==63&&chart.calls.size()<=128);
    unsigned colors=0,rectangles=0,markers=0,references=0;bool seen[20]={false};
    for(size_t i=0;i<chart.calls.size();++i) {
        const Call& call=chart.calls[i];assert(callOk(call,handlesOf(profile),true));
        assert(call.fkt!=kSetLineColor&&call.fkt!=kSetLineWidth);
        if(call.fkt==kSetFillColor) {
            assert(call.payload.size()==3&&call.handle()==0x1f);
            assert(i+1<chart.calls.size()&&chart.calls[i+1].fkt==kAddPolygon);
            uint16_t item=rdBe16(&chart.calls[i+1].payload[2]);
            if(item>=0x80){assert(colors<20&&call.payload[2]==colors&&!seen[colors]);seen[colors]=true;++colors;}
            else {assert(i==1&&item==0x40&&call.payload[2]==2);++references;}
        }
        if(call.fkt==kAddPolygon) {
            assert(call.handle()==0x1f&&call.payload.size()==22&&call.payload[4]==0x03&&call.payload[5]==4);
            uint16_t item=rdBe16(&call.payload[2]);int left,top,right,bottom;
            if(item>=0x80) {
                assert(rectangles<20&&item==0x80+rectangles);
                assert(i>0&&chart.calls[i-1].fkt==kSetFillColor&&chart.calls[i-1].payload[2]==rectangles);
                left=5+(rectangles%4)*58;right=left+48;top=75+(rectangles/4)*27;bottom=top+17;++rectangles;
            } else {
                assert(markers<11&&item==0x40+markers);left=0;right=232;top=70;bottom=205;
                if(markers<5){left=markers==4?230:markers*58;right=left+2;}
                else {unsigned row=markers-5;top=row==5?203:70+row*27;bottom=top+2;}
                ++markers;
            }
            const int expected[4][2]={{left,top},{right,top},{right,bottom},{left,bottom}};
            for(unsigned vertex=0;vertex<4;++vertex) {
                int x=rdBe16(&call.payload[6+vertex*4]),y=rdBe16(&call.payload[8+vertex*4]);
                assert(x==expected[vertex][0]&&y==expected[vertex][1]);
                assert(x>=0&&x<=232&&y>=70&&y<=205);
            }
        }
    }
    assert(colors==20&&rectangles==20&&markers==11&&references==1);
    for(unsigned i=0;i<20;++i)assert(seen[i]);
    nbt::sixwbkit::HudTerminalPalette p=provisionalKitPalette();
    assert(p.white==3&&p.graphite==14&&p.gray==14&&p.black==14&&p.gold==2);
}
