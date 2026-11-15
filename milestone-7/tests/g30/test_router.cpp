#include "cluster/g30/router.hpp"
#include "cluster/g30/manifest.hpp"
#include "cluster/g30/credits.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
using namespace aa_cluster;
static unsigned setups,focuses,channel,last_epoch,last_len;static bool projected,available=true,overflow=false,broken=false;
static uint64_t timestamp;static std::vector<uint8_t> queued;
static uint32_t clock_ms;
static uint32_t now() {return clock_ms;}
static void setup(void*,unsigned c) { ++setups;channel=c; }
static void focus(void*,unsigned c,bool p,bool) { ++focuses;channel=c;projected=p; }
static bool ready() { return available; }
static bool resync() { bool r=broken;broken=false;return r; }
static unsigned state(bool active) { last_epoch=((last_epoch+2)&~1u)|active;return last_epoch; }
static bool queue(unsigned e,const uint8_t* c,unsigned nc,const uint8_t* p,unsigned n,uint64_t pts) {
    assert(e==last_epoch);if(overflow)return false;last_len=nc+n;timestamp=pts;
    queued.assign(c,c+nc);queued.insert(queued.end(),p,p+n);return true;
}
static void var(std::vector<uint8_t>& o,unsigned n) { do{uint8_t b=n&127;n>>=7;o.push_back(b|(n?128:0));}while(n); }
static void msg(std::vector<uint8_t>& o,unsigned field,const std::vector<uint8_t>& p) {var(o,field*8+2);var(o,p.size());o.insert(o.end(),p.begin(),p.end());}
static std::vector<uint8_t> scalar(unsigned field,unsigned value) {std::vector<uint8_t> v;var(v,field*8);var(v,value);return v;}
static void append(std::vector<uint8_t>& v,const std::vector<uint8_t>& p) {v.insert(v.end(),p.begin(),p.end());}
int main() {
    Credits credits={};
    for(unsigned i=0;i<QueueSize;++i)assert(credits.publish(i<3?7:9));
    assert(!credits.publish(9));assert(credits.take(9)==QueueSize-3);
    assert(!credits.take(9));assert(credits.publish(9));
    assert(!credits.take(11)); // old stream's late worker completion is discarded
    assert(credits.publish(11)&&credits.take(11)==1);
    Router r={};r.enabled=r.allowed=true;r.hooks={setup,focus,ready,resync,state,queue,now};
    const uint8_t set[]={0x80,0},start[]={0x80,1,8,7},stop[]={0x80,2},unfocus[]={0x80,7,0x10,2};
    const uint8_t cfg[]={0,1,0,0,0,1,0x67,1,0,0,0,1,0x68,2};
    // The timestamp deliberately contains a start-code pattern. It isn't H264.
    uint8_t frame[]={0,0,0,0,1,0x65,0,0,0,2,0,0,0,1,0x65,99};
    assert(!r.receive(0,6,set,sizeof(set))&&setups==0);
    assert(r.receive(0,2,set,sizeof(set))&&setups==1&&channel==2&&projected);
    assert(!r.receive(0,2,start,sizeof(start)));
    assert(!r.receive(0,2,cfg,sizeof(cfg)));
    frame[14]=0x41;r.receive(0,2,frame,sizeof(frame));assert(!last_len);
    frame[14]=0x65;r.receive(0,2,frame,sizeof(frame));assert(last_len==sizeof(cfg)-2+6);
    assert(timestamp==be64(frame+2)&&queued[4]==0x67&&queued.back()==99);
    unsigned before=last_epoch;r.receive(0,6,frame,sizeof(frame));assert(last_epoch==before);
    // Overflow invalidates the predictive chain and requires a fresh IDR+config.
    overflow=true;r.receive(0,2,frame,sizeof(frame));assert(r.waiting_idr&&last_epoch!=before);
    overflow=false;last_len=0;frame[14]=0x41;r.receive(0,2,frame,sizeof(frame));assert(!last_len);
    clock_ms+=500;
    frame[14]=0x65;r.receive(0,2,frame,sizeof(frame));assert(last_len==18);
    // Overflow again before the 10 s retry throttle expires. Main/control
    // traffic must eventually restart recovery even with no incoming IDR.
    clock_ms=600;overflow=true;r.receive(0,2,frame,sizeof(frame));overflow=false;
    assert(r.recovery_needed&&!r.recovery_pending&&r.waiting_idr);
    clock_ms=10000;r.receive(0,6,start,sizeof(start));assert(r.recovery_pending&&!projected);
    clock_ms=10500;r.receive(0,2,frame,sizeof(frame));assert(projected&&!r.waiting_idr);
    assert(r.receive(0,2,unfocus,sizeof(unfocus))&&!projected&&!(last_epoch&1));
    r.receive(0,2,frame,sizeof(frame));assert(r.waiting_idr);
    r.receive(0,2,stop,sizeof(stop));r.end();assert(!(last_epoch&1)&&!r.opened);
    available=false;r.receive(0,2,set,sizeof(set));assert(!projected);
    available=true;r.receive(0,6,start,sizeof(start));assert(projected&&channel==2);
    // A settings change is applied by the idle tick: no incoming video, no
    // reconnect, no main-channel focus changes. Late phone packets stay ACKable.
    r.allow(0,false);assert(!projected&&!(last_epoch&1));
    before=last_epoch;r.receive(0,2,start,sizeof(start));r.receive(0,2,frame,sizeof(frame));
    assert(!r.deferred&&!(last_epoch&1));
    r.receive(0,2,unfocus,sizeof(unfocus));
    r.allow(0,true);assert(projected&&r.waiting_idr);
    r.receive(0,2,start,sizeof(start));r.receive(0,2,cfg,sizeof(cfg));
    frame[14]=0x41;r.receive(0,2,frame,sizeof(frame));assert(!r.deferred);
    frame[14]=0x65;r.receive(0,2,frame,sizeof(frame));assert(r.deferred);
    r.allow(0,false);assert(!projected&&!(last_epoch&1));
    available=false;r.allow(0,true);assert(!projected);
    available=true;r.tick(0);assert(projected&&r.waiting_idr);
    r.end();
    // Match the actual SDR nested field path. All other channels must stay exact.
    std::vector<uint8_t> insets;for(unsigned i=1;i<=4;++i)append(insets,scalar(i,i<3?40:200));
    std::vector<uint8_t> extra;msg(extra,1,insets);append(extra,scalar(4,0));
    std::vector<uint8_t> video;append(video,scalar(1,1));append(video,scalar(3,400));append(video,scalar(4,80));msg(video,11,extra);
    std::vector<uint8_t> sink;msg(sink,4,video);append(sink,scalar(6,2));append(sink,scalar(7,1));
    std::vector<uint8_t> cluster=scalar(1,2);msg(cluster,3,sink);
    std::vector<uint8_t> main=scalar(1,6);msg(main,3,sink);
    std::vector<uint8_t> src={0,6};msg(src,1,main);size_t untouched=src.size();msg(src,1,cluster);
    std::vector<uint8_t> dst(src.size());assert(rewrite_manifest(src.data(),src.size(),dst.data(),dst.size()));
    assert(!memcmp(src.data(),dst.data(),untouched));assert(dst!=src);
    unsigned changed=0;assert(rewrite_level(dst.data()+2,dst.size()-2,0,changed));assert(changed==1);
    std::vector<uint8_t> twice(dst.size());assert(rewrite_manifest(dst.data(),dst.size(),twice.data(),twice.size()));assert(twice==dst);
    assert(!rewrite_manifest(src.data(),src.size()-1,dst.data(),dst.size()));
    puts("AA_ROUTER_TEST_OK main isolation, focus, framing, IDR recovery, lifecycle, SDR");
}
