// Exercise late/fragmented native Guidance replies, including cancellation.
#define socket fake_socket
#define connect fake_connect
#define bind fake_bind
#define getsockname fake_getsockname
#define getsockopt fake_getsockopt
#define fcntl fake_fcntl
#define send fake_send
#define sendto fake_sendto
#define recv fake_recv
#define close fake_close
#define poll fake_poll
#define usleep fake_usleep
#define clock_gettime fake_clock
#include "cluster/g30/native/control.hpp"

static unsigned clock_ms, used, consumed, calls, closed, delay_until, fragment;
static unsigned methods[16];
static uint8_t incoming[1024];
static bool missing, mismatch, error_reply, partial_send, interrupt_read;
static void check(bool ok,const char* label) { if(!ok) {printf("FAIL %s\n",label);__builtin_trap();} }
extern "C" int* __get_errno_ptr() { static int error;return &error; }
extern "C" int fake_clock(int,PipClock* p) {p->tv_sec=clock_ms/1000;p->tv_nsec=clock_ms%1000*1000000;return 0;}
extern "C" int fake_usleep(unsigned n) {clock_ms+=n/1000;return 0;}
extern "C" int fake_socket(int,int,int) {return 10;}
extern "C" int fake_connect(int,const void*,unsigned) {return 0;}
extern "C" int fake_bind(int,const void*,unsigned) {return 0;}
extern "C" int fake_getsockopt(int,int,int,void*,unsigned*) {return 0;}
extern "C" int fake_fcntl(int,int,...) {return 0;}
extern "C" int fake_getsockname(int,void*,unsigned*) {return 0;}
extern "C" int fake_close(int) {++closed;return 0;}
extern "C" int fake_sendto(int,const void*,unsigned n,int,const void*,unsigned) {return n;}
extern "C" int fake_poll(PollFD* p,unsigned,int timeout) {
    p->revents=0;
    if((p->events&PollRead) && (consumed==used || missing || (consumed>=fragment && clock_ms<delay_until))) {
        clock_ms+=unsigned(timeout);return 0;
    }
    p->revents=p->events;return 1;
}
extern "C" int fake_send(int,const void* data,unsigned n,int) {
    if(partial_send) {
        if(!calls++)return 8;
        *__get_errno_ptr()=5;return -1;
    }
    check(calls<16 && n>=16,"bounded complete request");
    const uint8_t* p=(const uint8_t*)data;unsigned method=get16(p+2);
    methods[calls++]=method;unsigned size=method==1?24:16;
    check(used+size<=sizeof(incoming),"bounded replies");
    uint8_t* r=incoming+used;used+=size;
    for(unsigned i=0;i<16;++i)r[i]=p[i];
    put32(r+4,size-8);r[14]=0x80;r[15]=error_reply?1:0;
    if(mismatch)put16(r+10,get16(p+10)+1);
    if(method==1)put32(r+16,200);
    return n;
}
extern "C" int fake_recv(int,void* data,unsigned n,int) {
    if(n>used-consumed)n=used-consumed;
    if(consumed<fragment && n>fragment-consumed)n=fragment-consumed;
    for(unsigned i=0;i<n;++i)((uint8_t*)data)[i]=incoming[consumed++];
    if(interrupt_read && consumed==fragment)on_signal(15);
    return n;
}
static Control reset() {
    clock_ms=used=consumed=calls=closed=delay_until=0;fragment=8;
    missing=mismatch=error_reply=partial_send=interrupt_read=false;
    interrupted=0;service_mode=true;end_time=10000;
    return Control{10,11,12,1234,2,200,0};
}
static bool call(Control& c,unsigned method=14) {
    uint8_t body[4]={0,0,0,200};return c.guidance_call(12,method,body,4,1,method==1?&c.handle:0);
}
static void cancelled(Control& c) {
    check(c.close(),"late reply drained and cleanup acknowledged");
    const unsigned expected[]={9,9,9,12,4,9};
    check(calls==7 && closed==3,"six cancellation RPCs and all descriptors closed");
    for(unsigned i=0;i<6;++i)check(methods[i+1]==expected[i],"factory cancellation order unchanged");
    check(consumed==used,"every reply consumed");
}
int main() {
    Control c=reset();delay_until=2200;
    check(!call(c) && c.reply_used==8 && c.pending_reply,"timeout retains partial header");cancelled(c);
    c=reset();delay_until=2200;fragment=19;c.handle=0;
    check(!call(c,1) && c.reply_used==19,"timeout retains partial create-handle body");cancelled(c);
    check(c.handle==200,"late-created handle recovered and cancelled");
    c=reset();interrupt_read=true;
    check(!call(c) && interrupted,"signal interrupts partial reply");cancelled(c);
    c=reset();missing=true;
    check(!call(c) && !c.close(),"missing reply remains failure");
    check(clock_ms<=3000 && calls==1 && closed==3,"missing peer cannot hang cleanup or receive another request");
    c=reset();mismatch=true;
    check(!call(c) && !c.close() && calls==1,"unrelated reply fails closed");
    c=reset();error_reply=true;
    check(!call(c),"negative reply reported");error_reply=false;cancelled(c);
    c=reset();partial_send=true;
    check(!call(c) && c.broken_stream && !c.close(),"partial request never followed by malformed cancellation");
    printf("AA_CONTROL_RECOVERY_OK delayed header/body, late handle, signal, missing peer, mismatch, negative reply, partial send\n");
}
