// Real AASession parsing/reassembly/ACK dispatch, scripted phone, stub TLS.
#include "aa_session.hpp"
#include "cluster/g30/router.hpp"
#include "cluster/g30/bridge.hpp"
#include "cluster/g30/credits.hpp"
#include <assert.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <stdio.h>
#include <vector>
using namespace aa_cluster;
using Bytes=std::vector<uint8_t>;
static Router router;
static unsigned state_value,queued_count,main_frames,main_configs,last_len;
static ClusterAck credit_callback;
static Credits credits;
extern "C" void idriver_cluster_configure_acks(ClusterAck cb) {credit_callback=cb;}
extern "C" unsigned idriver_cluster_ack_window() {return router.enabled?4:0;}
extern "C" void idriver_cluster_drain_acks(void* s) {
    unsigned n=credits.take(state_value);while(n--)credit_callback(s,2);
}
extern "C" void idriver_cluster_tick(void* s) {router.tick(s);}
static bool ready() {return true;}
static bool resync() {return false;}
static uint32_t now() {return 0;}
static unsigned state(bool b) {state_value=((state_value+2)&~1u)|b;return state_value;}
static bool queue(unsigned,const uint8_t* c,unsigned nc,const uint8_t* p,unsigned n,uint64_t pts) {
    assert(nc==6&&c[4]==0x67&&n==6&&p[4]==0x65&&pts==0x16500000002ULL);
    last_len=nc+n;__sync_fetch_and_add(&queued_count,1);return true;
}
extern "C" void idriver_cluster_init(ClusterSetup s,ClusterFocus f) {router.hooks={s,f,ready,resync,state,queue,now};router.enabled=router.allowed=true;}
extern "C" int idriver_cluster_receive(void* s,unsigned ch,const uint8_t* p,size_t n) {return router.receive(s,ch,p,n)?1:(router.deferred?2:0);}
extern "C" void idriver_cluster_end() {router.end();}
extern "C" const uint8_t* idriver_cluster_manifest(const uint8_t* p,size_t) {return p;}
static void readn(int fd,void* out,size_t n) {size_t at=0;while(at<n){ssize_t r=read(fd,(char*)out+at,n-at);assert(r>0);at+=r;}}
static void send(int fd,unsigned ch,const Bytes& p,unsigned flags=11,unsigned total=0) {
    uint8_t h[8]={uint8_t(ch),uint8_t(flags),uint8_t(p.size()>>8),uint8_t(p.size()),0,0,0,0};
    put32(h+4,total);unsigned n=(flags&3)==1?8:4;
    assert(write(fd,h,n)==n);assert(write(fd,p.data(),p.size())==ssize_t(p.size()));
}
static Bytes reply(int fd,unsigned channel) {uint8_t h[4];readn(fd,h,4);assert(h[0]==channel);Bytes b(h[2]*256+h[3]);readn(fd,b.data(),b.size());return b;}
static void ack(int fd,unsigned ch,unsigned session) {
    Bytes b=reply(fd,ch);assert(b.size()>=6&&b[0]==0x80&&b[1]==4&&field(b.data()+2,b.size()-2,1,99)==session&&field(b.data()+2,b.size()-2,2,99)==1);
}
static void main_frame(uint32_t,const uint8_t*,size_t,uint64_t,void*) {__sync_fetch_and_add(&main_frames,1);}
static void main_config(const uint8_t*,size_t,void*) {__sync_fetch_and_add(&main_configs,1);}
int main() {
    alarm(15);signal(SIGPIPE,SIG_IGN);int fd[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,fd));
    nbt::AASession s;s.setVideoFrameCallback(main_frame);s.setCodecConfigCallback(main_config);assert(s.start(fd[0]));
    uint8_t version[12];readn(fd[1],version,12);
    for(unsigned ch:{2u,6u}) {
        send(fd[1],ch,{0x80,0});Bytes setup=reply(fd[1],ch),focus=reply(fd[1],ch);
        assert(setup[0]==0x80&&setup[1]==3);assert(focus[0]==0x80&&focus[1]==8&&focus[3]==1);
        assert(field(setup.data()+2,setup.size()-2,2,0)==(ch==2?4u:10u));
        send(fd[1],ch,{0x80,1,8,uint8_t(ch==2?7:9)});
    }
    Bytes config={0,1,0,0,0,1,0x67,42};
    send(fd[1],2,config);ack(fd[1],2,7);send(fd[1],6,config);ack(fd[1],6,9);
    Bytes frame={0,0,0,0,1,0x65,0,0,0,2,0,0,0,1,0x65,99};
    // Split inside the timestamp; no consumer may see an incomplete access unit.
    send(fd[1],2,Bytes(frame.begin(),frame.begin()+7),9,frame.size());
    send(fd[1],2,Bytes(frame.begin()+7,frame.end()),10);
    struct pollfd waiting={fd[1],POLLIN,0};assert(poll(&waiting,1,100)==0);
    // The cluster consumer is deliberately slow: main video must still be
    // dispatched and acknowledged while the cluster has no returned credit.
    send(fd[1],6,frame);ack(fd[1],6,9);
    assert(credits.publish(state_value));ack(fd[1],2,7);
    assert(__sync_fetch_and_add(&queued_count,0)==1&&__sync_fetch_and_add(&main_frames,0)==1&&__sync_fetch_and_add(&main_configs,0)==1);
    // A late completion from the previous cluster stream must not ACK the
    // replacement phone session, even though both use the same channel.
    unsigned old_epoch=state_value;
    send(fd[1],2,{0x80,1,8,11});send(fd[1],2,config);ack(fd[1],2,11);
    assert(credits.publish(old_epoch));waiting.revents=0;assert(poll(&waiting,1,100)==0);
    send(fd[1],2,frame);send(fd[1],6,frame);ack(fd[1],6,9);
    assert(credits.publish(state_value));ack(fd[1],2,11);
    assert(__sync_fetch_and_add(&queued_count,0)==2);
    send(fd[1],2,{0x80,7,0x10,2});Bytes focus=reply(fd[1],2);assert(focus[3]==2);
    send(fd[1],2,{0x80,2});s.stop();close(fd[1]);
    assert(!(state_value&1)&&!router.opened&&last_len==12);
    puts("AA_SOURCE_SESSION_OK independent streams, fragmentation, deferred cluster credits, main isolation, session-specific ACKs, focus and teardown");
}
