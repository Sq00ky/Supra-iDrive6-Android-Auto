// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host-only: no BMW libraries, device, network or vehicle required.
#include "cluster/nav_core.hpp"
#include "cluster/hud6wb_display.hpp"
#include "cluster/hud6wb_codec.hpp"
#include "cluster/hud6wb_artwork.hpp"
#include "fake_dsi_client.hpp"
#include "kombi_nav_test_util.hpp"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
using namespace nbt;
using namespace nbt::hudterm;
using namespace nbt::kombinav;
namespace {
std::vector<std::string> logs;
void record(const char* s,void*) {logs.push_back(s);}
void route(const most::View&,void*) {}
struct FakePort : Port {
    Status st;uint32_t ep;bool idle,badSize,queried;unsigned resets;Bmw6wbProfile p;std::vector<Batch> batches;
    FakePort():st(kReady),ep(0),idle(true),badSize(false),queried(false),resets(0){}
    SubmitResult submit(const Batch& b){if(st!=kReady)return kNotReady;batches.push_back(b);if(b.label=="6wb-size-check")queried=true;return kQueued;}
    Status status() const{return st;}uint32_t epoch() const{return ep;}
    bool waiting() const{return false;}
    void reset(){++resets;}bool waitIdle(uint32_t){return idle && st==kReady;}
    bool imageSize(uint16_t h,uint16_t& w,uint16_t& height) const {
        if(!queried)return false;
        if(h==p.base.frame){w=p.frameWidth;height=p.frameHeight;}
        else if(h==p.bar){w=p.barWidth;height=p.barHeight;}
        else {
            Surface ss[]={p.main,p.icons,p.lanes};bool found=false;
            for(unsigned i=0;i<3;++i)if(h==ss[i].a||h==ss[i].b){w=ss[i].width;height=ss[i].height;found=true;}
            if(!found)return false;
        }
        if(badSize)++w;
        return true;
    }
};
NavView view(){NavView v;v.aa_type=8;v.road="Test Road";v.dist_text="250";v.aa_unit=1;v.has_step_m=true;v.step_m=250;return v;}
const Call* find(const Batch& b,uint16_t f,uint16_t h=0){for(size_t i=0;i<b.calls.size();++i)if(b.calls[i].fkt==f&&(!h||b.calls[i].handle()==h))return &b.calls[i];return NULL;}
void valid(const Batch& b,const Bmw6wbProfile& p){assert(!b.calls.empty());for(size_t i=0;i<b.calls.size();++i)assert(callOk(b.calls[i],handlesOf(p),true));}
void testCodec(){
    assert(toLatin15("Stra\xc3\x9f" "e \xe2\x82\xac",30)==std::string("Stra\xdf" "e \xa4"));
    assert(toLatin15("\xc2\xa4",2)=="?"); // currency sign is not euro
    assert(toLatin15("\xed\xa0\x80",5)=="?");
    assert(toLatin15("\xe2\x82",5)=="?");
    std::vector<uint8_t> t=latinText(0x1a,"Test",15);
    assert(t.size()==8 && t[0]==0 && t[1]==0x1a && t[2]==1 && t[7]==0);
    std::vector<uint8_t> bar=distanceBar(0x35,999);assert(bar.size()==7&&bar[5]==255&&bar[6]==2);
    bar=distanceBar(0x35,-1);assert(bar[5]==0);
    RoadShield s=roadShield("A 45 / Exit");assert(s.kind=='A'&&s.number=="45");
    assert(!roadShield("Via A45").kind && !roadShield("A45xyz").kind);
    std::vector<uint32_t> image=shieldPicture(305,44,s);assert(image.size()==305*44);
    std::vector<uint8_t> bmp=bitmap(0x18,3,image,305,44,4096);
    assert(!bmp.empty()&&bmp[6]==1&&bmp[7]==1&&bmp[8]==0&&bmp[9]==3);
    assert(bitmap(0x18,0,image,49,44,4096).empty());
    assert(bitmap(0x18,0,image,305,44,20).empty());
    for(unsigned n=1;n<=260;++n){
        std::vector<uint32_t> in;
        for(unsigned i=0;i<n;++i)in.push_back(i%4?0xff00579c:0x12345678);
        if(n%2)in.assign(n,0xff00579c);
        std::vector<uint8_t> r=rle32(in);assert(r.size()%8==0);
        std::vector<uint32_t> out;size_t i=0;
        while(out.size()<in.size()){
            unsigned tag=r[i++],k=tag&127;assert(k);
            if(tag&128){assert(i+4<=r.size());uint32_t c=((uint32_t)r[i]<<24)|((uint32_t)r[i+3]<<16)|((uint32_t)r[i+2]<<8)|r[i+1];i+=4;out.insert(out.end(),k,c);}
            else for(unsigned j=0;j<k;++j){assert(i+4<=r.size());uint32_t c=((uint32_t)r[i]<<24)|((uint32_t)r[i+3]<<16)|((uint32_t)r[i+2]<<8)|r[i+1];i+=4;out.push_back(c);}
        }
        assert(out==in && i<r.size());for(;i<r.size();++i)assert(r[i]==0);
    }
}
void testGeometry(){
    Bmw6wbProfile p;assert(validProfile(p));
    for(int type=1;type<=46;++type)for(int angle=0;angle<=360;angle+=15){
        Shape s=bmwManeuver(shapeFor(type,true,angle),p.base.glyph);assert(!s.empty());
        for(size_t i=0;i<s.size();++i){assert(!s[i].pts.empty()&&s[i].pts.size()<=99);
            for(size_t j=0;j<s[i].pts.size();++j){const Point& q=s[i].pts[j];assert(q.x>=p.base.glyph.x0&&q.x<=p.base.glyph.x1&&q.y>=p.base.glyph.y0&&q.y<=p.base.glyph.y1);}}
    }
    p.main.a=p.base.street;assert(!validProfile(p));
}
void testRendering(){
    FakePort port;Bmw6wbProfile p;p.roadShields=true;
    Hud6wbDisplay d(port,p,true);Settings settings;d.setSettings(settings);NavView v=view();
    logs.clear();d.setLog(&record,NULL);
    v.road="A 45";v.has_next=true;v.next_type=7;
    AaLane l;l.push_back(LaneDirection(1,false));l.push_back(LaneDirection(5,true));v.lanes.push_back(l);
    Batch first=d.render(v);valid(first,p);
    assert(find(first,0x227,0x35)&&find(first,0x210,0x1a)&&find(first,0x21d,0x23));
    assert(find(first,0xc05,0x18)&&find(first,0xc05,0x18)->txType==0);
    const Call* sw=find(first,0x20e);assert(sw&&rdBe16(&sw->payload[0])==0x20&&rdBe16(&sw->payload[2])==0x1f);
    assert(d.showManeuver(v,0,true,true)==ClusterDisplay::kOk);
    assert(logs.size()>=first.calls.size()+8 && logs[0].find("payload:")!=std::string::npos);
    Batch second=d.render(v);sw=find(second,0x20e);assert(rdBe16(&sw->payload[0])==0x1f&&rdBe16(&sw->payload[2])==0x20);
    assert(d.updateDistance(v,1)==ClusterDisplay::kOk);assert(d.clear(NULL));assert(port.batches.empty());
}
void testResourcesAndSafety(){
    FakePort port;Bmw6wbProfile p;Hud6wbDisplay d(port,p,false);NavView v=view();
    d.showManeuver(v,0,true,true);assert(port.batches.size()==1&&port.batches[0].calls.size()==8);
    for(size_t i=0;i<8;++i)assert(port.batches[0].calls[i].fkt==kGetSize&&port.batches[0].calls[i].payload.size()==3);
    d.showManeuver(v,1,true,true);assert(port.batches.size()==2);valid(port.batches.back(),p);
    port.idle=false;v.aa_type=7;d.showManeuver(v,2,true,false);assert(port.batches.size()==2);
    assert(d.sameManeuver(v,2));port.idle=true;d.showManeuver(v,3,true,false);
    const Call* sw=find(port.batches.back(),0x20e);assert(rdBe16(&sw->payload[0])==0x1f&&rdBe16(&sw->payload[2])==0x20);
    size_t n=port.batches.size();port.st=kFailed;assert(!d.clear(NULL));assert(port.batches.size()==n&&port.resets==0);
    port.st=kNavActive;assert(d.factoryActive()&&d.clear(NULL));assert(port.batches.size()==n);
    FakePort bad;bad.badSize=true;Hud6wbDisplay bd(bad,p,false);
    bd.showManeuver(v,0,true,true);assert(bd.showManeuver(v,1,true,true)==ClusterDisplay::kRejected);
    assert(bad.batches.size()==1&&!bd.sentAny());
    FakePort reconnect;Hud6wbDisplay rd(reconnect,p,false);rd.showManeuver(v,0,true,true);rd.showManeuver(v,1,true,true);
    ++reconnect.ep;rd.showManeuver(v,2,true,true);
    assert(reconnect.batches.back().label!="6wb-size-check"); // OEM yield: use existing validated sizes
    reconnect.queried=false; // transport cache cleared on actual reconnect/host change
    ++reconnect.ep;rd.showManeuver(v,3,true,true);assert(reconnect.batches.back().label=="6wb-size-check");
}
most::View message(uint16_t f,const std::vector<uint8_t>& b,uint16_t dst=0x201f,uint8_t op=0xd){
    most::View m={};m.src=0x101;m.dst=dst;m.fblock=0x82;m.inst=0;m.fkt=f;m.op=op;m.hdrTag=89;m.len=b.size();m.data=b.empty()?NULL:&b[0];return m;
}
void testTransport(){
    Bmw6wbProfile p;Config cfg;cfg.handles=handlesOf(p);cfg.navFrame=p.base.frame;
    cfg.initialNavActive=true;cfg.strictAnswers=true;cfg.acceptMirroredAnswers=true;cfg.extendedCalls=true;cfg.maxInFlight=1;
    cfg.txType=4;cfg.useFixedTag=true;cfg.fixedTag=0xffff;
    hudterm::Core core(cfg);core.setReplyTag(89);core.setLink(true,0x2059,0);core.setHost(0x101,0);assert(core.status()==kNavActive);
    most::Msg out;assert(!core.next(0,out));core.tick(60000);assert(core.status()==kNavActive);
    std::vector<uint8_t> off=setVisibility(p.base.frame,false);core.onMessage(message(0x204,off),60001);
    core.tick(65001);assert(core.status()==kReady);
    Batch b("size");std::vector<uint8_t> q=getSize(p.main.a);q.push_back(1);b.add(kGetSize,q);
    assert(core.submit(b,65002)==kQueued&&core.next(65002,out));
    assert(out.dst==0x101&&out.fblock==0x82&&out.inst==0&&out.opType==6&&out.txType==4&&out.hdrTag==0xffff);
    std::vector<uint8_t> ack=getSize(p.main.a);putBe16(ack,221);putBe16(ack,158);
    core.onMessage(message(kGetSize,ack),65003);assert(!core.busy());uint16_t w,h;assert(core.imageSize(p.main.a,w,h)&&w==221&&h==158);
    Batch b2("size-next");q=getSize(p.main.b);q.push_back(1);b2.add(kGetSize,q);
    assert(core.submit(b2,65004)==kQueued&&core.next(65004,out));core.tick(66504);assert(core.status()==kFailed&&core.stats().timeouts==1);
    hudterm::Core fail(cfg);fail.setReplyTag(89);fail.setLink(true,0x2059,0);fail.setHost(0x101,0);fail.onMessage(message(0x204,off),0);fail.tick(5000);
    assert(fail.submit(b,5001)==kQueued&&fail.next(5001,out));std::vector<uint8_t> err=getSize(p.main.a);err.push_back(0x41);
    fail.onMessage(message(kGetSize,err,0x2059,9),5002);assert(fail.status()==kFailed&&!fail.next(5003,out));
    hudterm::Core factory(cfg);factory.setReplyTag(89);factory.setLink(true,0x2059,0);factory.setHost(0x101,0);factory.onMessage(message(0x204,off),0);factory.tick(5000);
    assert(factory.submit(b,5001)==kQueued&&factory.next(5001,out));
    std::vector<uint8_t> foreign=setVisibility(p.base.frame,true);factory.onMessage(message(0x204,foreign),5002);
    assert(factory.status()==kNavActive&&!factory.busy());
    core.setLink(false,0,70000);core.setLink(true,0x2059,70001);core.setHost(0x101,70001);
    core.tick(90000);assert(core.status()==kNavActive&&!core.next(90000,out));
    assert(!core.imageSize(p.main.a,w,h));
    core.onMessage(message(0x204,off),90001);core.tick(95001);assert(core.status()==kReady);
    // Check the final ARM DSI request, including registered return tag and source address.
    fake_dsi::reset();most::Client client;
    assert(client.addRoute(0x82,0,most::kAny,most::kAny,&route,NULL)&&client.allowSend(0x82,0)&&client.start("HEADER"));
    fake_dsi::attach();std::vector<uint8_t> response;most::putLe32(response,77);most::putLe16(response,0x2059);
    fake_dsi::data(most::kInfoClientResponse,&response[0],response.size());
    assert(client.registeredHeaderTag()==77);
    assert(client.send(out,true));const fake_dsi::Call& request=fake_dsi::calls.back();
    most::View wire;assert(most::parseView(&request.payload[4],request.payload.size()-4,wire));
    assert(wire.src==0x2059&&wire.dst==0x101&&wire.hdrTag==77&&wire.txType==4&&request.payload.size()%4==0);
    fake_dsi::detach();
}
// Deterministic replay of the F31 response, not a simulated vehicle success.
void testSizeReplies(){
    Bmw6wbProfile p;Config cfg;cfg.handles=handlesOf(p);cfg.extendedCalls=true;
    cfg.strictAnswers=true;cfg.acceptMirroredAnswers=true;cfg.replyMs=3000;
    cfg.useFixedTag=true;cfg.fixedTag=96;cfg.maxInFlight=1;
    const uint8_t raw[]={0x00,0x17,0x01,0x35,0x01,0x4a};
    std::vector<uint8_t> ack(raw,raw+sizeof(raw));
    Batch b("frame-size");std::vector<uint8_t> q=getSize(0x17);q.push_back(1);b.add(kGetSize,q);
    for(unsigned scenario=0;scenario<18;++scenario) {
        hudterm::Core c(cfg);c.setLink(true,0x2060,0);c.setHost(0x101,0);
        most::Msg out;uint16_t w=0,h=0;
        assert(c.submit(b,100)==kQueued && c.next(100,out));
        most::View m=message(kGetSize,ack,0x2060);m.hdrTag=96;
        std::vector<uint8_t> bad=ack;
        if(scenario==1)bad.insert(bad.begin()+2,1); // old synthetic seven-byte result: unsupported
        if(scenario==2)bad.resize(5);
        if(scenario==3)bad[1]=0x18;
        if(scenario==4)m.fkt=kTextareaSetText;
        if(scenario==5)m.hdrTag=95;
        if(scenario==6)m.op=most::kOpStatus;
        if(scenario==7){bad[2]=bad[3]=0;} // zero width
        if(scenario==8){bad.clear();}
        if(scenario==9){m.src=0x102;}
        if(scenario==10){m.fblock=0x83;}
        if(scenario==11){m.inst=1;}
        if(scenario==14){bad[4]=bad[5]=0;} // zero height
        if(scenario==15){bad.push_back(0);bad.push_back(0);} // trailing bytes
        if(scenario==16){m.op=most::kOpErrorAck;m.hdrTag=95;}
        if(scenario==17){m.op=most::kOpErrorAck;bad[1]=0x18;}
        m.data=bad.empty()?NULL:&bad[0];m.len=bad.size();
        if(scenario==12 || scenario==13) {
            // Both an explicit tick and a delayed send-thread tick must expire.
            if(scenario==12)c.tick(3100);
            c.onMessage(m,3100);
            assert(c.status()==kFailed && c.stats().timeouts==1 && c.stats().answers==0);
            assert(!c.imageSize(0x17,w,h));
            c.reset(3101);assert(c.status()==kReady);
            assert(c.submit(b,3102)==kQueued && !c.next(3102,out));
            assert(c.status()==kFailed && c.stats().calls==1); // no ambiguous same-tag retry
            c.onMessage(m,3103);assert(c.stats().answers==0);
            c.reset(3104);
            Batch next("different-handle");std::vector<uint8_t> nq=getSize(0x1f);nq.push_back(1);next.add(kGetSize,nq);
            assert(c.submit(next,3105)==kQueued && c.next(3105,out));
            c.onMessage(m,3106);assert(c.status()==kReady && c.busy() && c.stats().answers==0);
            assert(!c.imageSize(0x17,w,h));
            continue;
        }
        c.onMessage(m,200);
        if(scenario) {
            assert(c.status()==kReady && c.busy() && c.stats().answers==0);
            assert(!c.imageSize(0x17,w,h));
            // Invalid packets neither consume the request nor prevent a valid result.
            m=message(kGetSize,ack,0x2060);m.hdrTag=96;c.onMessage(m,201);
        }
        assert(c.status()==kReady && !c.busy() && c.stats().answers==1 && c.stats().batchesDone==1);
        assert(c.imageSize(0x17,w,h) && w==309 && h==330);
        c.onMessage(m,202);assert(c.stats().answers==1 && !c.busy()); // duplicate
        // An old frame reply must not complete the next handle's request.
        Batch next("next-size");q=getSize(0x1f);q.push_back(1);next.add(kGetSize,q);
        assert(c.submit(next,203)==kQueued && c.next(203,out));
        c.onMessage(m,204);assert(c.busy() && c.stats().answers==1);
        std::vector<uint8_t> second=getSize(0x1f);putBe16(second,221);putBe16(second,158);
        m=message(kGetSize,second,0x2019);m.hdrTag=96; // mirrored but correlated
        c.onMessage(m,205);assert(!c.busy() && c.stats().answers==2 && c.status()==kReady);
    }
    Config bypassCfg=cfg;bypassCfg.ignoreFactoryNav=true;
    hudterm::Core bypass(bypassCfg);bypass.setLink(true,0x2060,0);bypass.setHost(0x101,0);
    assert(bypass.status()==kReady);most::Msg pending;
    assert(bypass.submit(b,1)==kQueued && bypass.next(1,pending));
    std::vector<uint8_t> on=setVisibility(0x17,true);
    bypass.onMessage(message(kSetVisibility,on),2);
    assert(bypass.status()==kReady && bypass.busy());
    bypass.tick(3001);assert(bypass.status()==kFailed && bypass.stats().timeouts==1);
    bypass.setLink(false,0,3002);bypass.setLink(true,0x2061,3003);bypass.setHost(0x102,3003);
    assert(bypass.status()==kReady);
    hudterm::Core idle(cfg);idle.setLink(true,0x2060,0);idle.setHost(0x101,0);
    most::View unsolicited=message(kGetSize,ack,0x2019);unsolicited.hdrTag=96;
    idle.onMessage(unsolicited,1);uint16_t w,h;
    assert(idle.status()==kReady && idle.stats().answers==0 && !idle.imageSize(0x17,w,h));
}
void testSecondStep(){
    kntest::Bytes first=kntest::navState(8,"First"),second=kntest::navState(7,"Second");
    first.insert(first.end(),second.begin(),second.end());NavMessages m;assert(m.store(0x8006,&first[0],first.size()));
    NavView v;assert(viewFromMessages(m,v)&&v.aa_type==8&&v.has_next&&v.next_type==7);
    // Missing roundabout angle does not create an imaginary exit direction.
    Rect box={0,0,100,100};ShapeSpec spec=shapeFor(34,false,0);
    assert(spec.kind==kShapeRoundabout);
    assert(bmwManeuver(spec,box,false).size()<bmwManeuver(spec,box,true).size());
    assert(!bmwExitNumber(2,box).empty()&&bmwExitNumber(0,box).empty()&&bmwExitNumber(100,box).empty());
}
unsigned graphicRequests(){unsigned n=0;for(size_t i=0;i<fake_dsi::calls.size();++i)if(fake_dsi::calls[i].kind==fake_dsi::Call::kRequest&&fake_dsi::calls[i].id==most::kReqProcessMessage)++n;return n;}
void testRuntime(const char* mode,bool bypass=false){
    if(bypass)setenv("NBT_6WB_OEM_BYPASS","1",1);else unsetenv("NBT_6WB_OEM_BYPASS");
    char dir[]="/tmp/6wb-host-XXXXXX";assert(mkdtemp(dir));std::string marker=std::string(dir)+"/shown";
    KombiNav::kShownFile=marker.c_str();KombiNav::kFlagFile="/nonexistent/6wb-flag";KombiNav::kManualFile="/nonexistent/6wb-manual";
    setenv("NBT_CLUSTER","6wb",1);setenv("NBT_KOMBI_NAV","1",1);setenv("NBT_KOMBI_NAV_YIELD","0",1);
    if(mode)setenv("NBT_HUDTERM",mode,1);else unsetenv("NBT_HUDTERM");fake_dsi::reset();
    {
        most::Client client;KombiNav nav;nav.start(&client);assert(!strcmp(nav.clusterName(),"6wb"));assert(client.start("HOST_6WB"));
        fake_dsi::attach();std::vector<uint8_t> reg;most::putLe32(reg,77);most::putLe16(reg,0x2059);
        fake_dsi::data(most::kInfoClientResponse,&reg[0],reg.size());reg.clear();most::putLe32(reg,1);most::putLe16(reg,0x101);most::putLe16(reg,0x8200);
        fake_dsi::data(most::kAttrRegistry,&reg[0],reg.size());
        kntest::Bytes status=kntest::status(1),step=kntest::navState(8,"Synthetic Road"),pos=kntest::curPos(250,"250",1);
        KombiNav::navStatusCallback(0x8003,&status[0],status.size(),&nav);KombiNav::navStatusCallback(0x8006,&step[0],step.size(),&nav);KombiNav::navStatusCallback(0x8007,&pos[0],pos.size(),&nav);
        usleep(400000);nav.stop();assert((graphicRequests()>0)==(bypass && mode && !strcmp(mode,"on")));
        // Default/log never acquire send rights. Even live cannot draw before an observed factory route end.
        if(!mode || strcmp(mode,"on"))assert(access(marker.c_str(),F_OK)!=0);
        fake_dsi::detach();
    }
    unlink(marker.c_str());rmdir(dir);unsetenv("NBT_CLUSTER");unsetenv("NBT_HUDTERM");unsetenv("NBT_KOMBI_NAV");unsetenv("NBT_KOMBI_NAV_YIELD");
}
}
int main(){testCodec();testGeometry();testRendering();testResourcesAndSafety();testTransport();testSizeReplies();testSecondStep();testRuntime(NULL);testRuntime("log");testRuntime("on");testRuntime(NULL,true);testRuntime("log",true);testRuntime("on",true);unsetenv("NBT_6WB_OEM_BYPASS");puts("test_hud6wb OK");}
