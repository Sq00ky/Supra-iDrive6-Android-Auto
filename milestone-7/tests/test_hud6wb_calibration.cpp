// Deterministic C++ tests prepared only: no compilation/execution in Codex.
#include "cluster/hud6wb_calibration.hpp"
#include "cluster/hud6wb_display.hpp"
#include "cluster/hud6wb_codec.hpp"
#include "hud6wb_kit_test_util.hpp"
#include <assert.h>
using namespace nbt::hudterm;
struct CalibrationPort:Port {
    bool busy,queried;std::vector<Batch> batches;Bmw6wbProfile p;
    CalibrationPort():busy(false),queried(false){}
    SubmitResult submit(const Batch& b){batches.push_back(b);busy=true;if(b.label=="6wb-size-check")queried=true;return kQueued;}
    Status status()const{return kReady;}uint32_t epoch()const{return 0;}
    bool waiting(int)const{return busy;}void reset(){}bool waitIdle(uint32_t){return !busy;}
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
    assert(display.interceptNavigation(NULL,3,12)); // stage B queues immediately, no NAV interleaving
    assert(port.batches.size()==3&&port.batches.back().label=="6wb-geometry-calibration");
    port.busy=false;assert(display.interceptNavigation(&latest,1,12.01)); // B deadline 22.01
    assert(display.interceptNavigation(&latest,1,22.009999));assert(port.batches.size()==3);
    assert(display.interceptNavigation(NULL,3,22.01));
    assert(port.batches.size()==4&&port.batches.back().label=="6wb-rgb-calibration");
    const Batch& rgb=port.batches.back();
    assert(rgb.calls[0].fkt==kSetVisibility&&rgb.calls[0].payload[2]==0);
    assert(rgb.calls[2].fkt==0xC05&&rgb.calls[2].txType==0);
    assert(callOk(rgb.calls[2],handlesOf(profile),true));
    port.busy=false;assert(display.interceptNavigation(&latest,1,23));
    assert(display.interceptNavigation(&latest,1,32.999));
    assert(display.interceptNavigation(&latest,1,33));
    assert(port.batches.size()==5&&port.batches.back().label=="6wb-palette-clear");
    port.busy=false;assert(!display.interceptNavigation(&latest,1,34));
    assert(!display.interceptNavigation(&latest,1,35));assert(port.batches.size()==5);
    Batch current=display.render(latest);bool foundLatest=false;
    for(size_t i=0;i<current.calls.size();++i)if(current.calls[i].fkt==kTextareaSetText&&current.calls[i].handle()==profile.base.street)
        foundLatest=std::string(current.calls[i].payload.begin()+3,current.calls[i].payload.end()-1)=="Latest";
    assert(foundLatest);
    display.interceptNavigation(NULL,2,36);display.navigationRouteStarted();
    assert(display.interceptNavigation(&latest,1,37));assert(port.batches.back().label=="6wb-palette-calibration");
    // The next route uses the other A/B buffer, with the same filled chart.
    unsigned polygons=0;
    for(size_t i=0;i<port.batches.back().calls.size();++i) {
        const Call& c=port.batches.back().calls[i];
        assert(c.fkt!=kSetLineColor);
        if(c.fkt==kAddPolygon){assert(c.handle()==0x1f&&c.payload[4]==0x03&&c.payload[5]==4);
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
    assert(c.test()==0);c.nextTest();assert(c.test()==1&&c.phase()==PaletteCalibration::Pending);
    c.queued();c.dispatched(110);assert(!c.due(119.999999));assert(c.due(120));
    c.clearQueued();c.finished();assert(c.phase()==PaletteCalibration::Done);
    c.observe(1);assert(!c.active());c.observe(2);c.observe(1);assert(c.phase()==PaletteCalibration::Pending);
    c.cancel();c.observe(3);c.observe(1);assert(!c.active());
    c.reset();c.observe(1);assert(c.active());
    PaletteCalibration disabled(false);disabled.observe(1);assert(!disabled.active());
    CalibrationPort port;nbt::hudterm::Bmw6wbProfile profile;
    nbt::kombinav::Hud6wbDisplay display(port,profile,true,testKit());
    Batch chart=display.calibrationBatch();assert(chart.calls.size()==64&&chart.calls.size()<=128);
    unsigned colors=0,rectangles=0,markers=0,references=0;bool seen[20]={false};
    for(size_t i=0;i<chart.calls.size();++i) {
        const Call& call=chart.calls[i];assert(callOk(call,handlesOf(profile),true));
        assert(call.fkt!=kSetLineColor&&call.fkt!=kSetLineWidth);
        if(call.fkt==kSetFillColor) {
            assert(call.payload.size()==3&&call.handle()==0x1f);
            assert(i+1<chart.calls.size()&&chart.calls[i+1].fkt==kAddPolygon);
            uint16_t item=rdBe16(&chart.calls[i+1].payload[2]);
            if(item>=0x80){assert(colors<20&&call.payload[2]==colors&&!seen[colors]);seen[colors]=true;++colors;}
            else {assert(i==1&&item==0x40&&call.payload[2]==6);++references;}
        }
        if(call.fkt==kAddPolygon) {
            assert(call.handle()==0x1f&&call.payload.size()==22&&call.payload[4]==0x03&&call.payload[5]==4);
            uint16_t item=rdBe16(&call.payload[2]);int left,top,right,bottom;
            if(item>=0x80) {
                assert(rectangles<20&&item==0x80+rectangles);
                assert(i>0&&chart.calls[i-1].fkt==kSetFillColor&&chart.calls[i-1].payload[2]==rectangles);
                left=25+(rectangles%4)*48;right=left+39;top=(rectangles/4)*37;bottom=top+17;++rectangles;
            } else {
                assert(markers<10&&item==0x40+markers);left=21;right=212;top=0;bottom=169;
                if(markers<5){const int xs[]={21,69,117,165,210};left=xs[markers];right=left+2;}
                else {top=19+(markers-5)*37;bottom=top+2;}
                ++markers;
            }
            const int expected[4][2]={{left,top},{right,top},{right,bottom},{left,bottom}};
            for(unsigned vertex=0;vertex<4;++vertex) {
                int x=rdBe16(&call.payload[6+vertex*4]),y=rdBe16(&call.payload[8+vertex*4]);
                assert(x==expected[vertex][0]&&y==expected[vertex][1]);
                assert(x>=0&&x<=232&&y>=0&&y<=205);
            }
        }
    }
    assert(colors==20&&rectangles==20&&markers==10&&references==1);
    for(unsigned i=0;i<20;++i)assert(seen[i]);
    Batch steps=display.geometryCalibrationBatch();assert(steps.calls.size()==106);
    unsigned left=0,right=0,ticks=0,bitmaps=0;
    for(size_t i=0;i<steps.calls.size();++i) {
        const Call& c=steps.calls[i];assert(callOk(c,handlesOf(profile),true));assert(c.fkt!=kSetLineColor);
        if(c.fkt==0xC05){assert(c.txType==0&&c.handle()==0x18&&c.payload.size()<=4096);++bitmaps;}
        if(c.fkt==kAddPolygon){unsigned item=rdBe16(&c.payload[2]);
            if(item>=0x100&&item<=0x128)++left;
            else if(item>=0x140&&item<=0x168)++right;
            else {assert(item>=0x200&&item<=0x228&&(item-0x200)%5==0);++ticks;}
            for(unsigned j=0;j<c.payload[5];++j){assert(rdBe16(&c.payload[6+4*j])<=232);assert(rdBe16(&c.payload[8+4*j])<=205);}
        }
    }
    assert(left==41&&right==41&&ticks==9&&bitmaps==1);
    for(unsigned test=0;test<2;++test){std::vector<uint32_t> px=calibrationPicture(test);assert(px.size()==305*44);
        assert(px[305+1]==(test?0xffff0000:0xffffff00));assert(px[0]==(test?0xff00ffff:0xff00ff00));
        assert(!bitmap(0x18,0,px,305,44,4096).empty());}
    nbt::sixwbkit::HudTerminalPalette p=provisionalKitPalette();
    assert(p.white==6&&p.graphite==3&&p.gray==2&&p.black==3&&p.gold==2&&p.red==5);
}
