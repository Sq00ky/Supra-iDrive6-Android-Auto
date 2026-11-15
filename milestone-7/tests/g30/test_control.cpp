// Actual car Control class against a deterministic peer; no real networking.
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

static unsigned next_fd=10, available[16], consumed[16], calls, closed, resource_writes;
static unsigned failure;
static uint8_t replies[16][64];
extern "C" int* __get_errno_ptr() { static int error; return &error; }
extern "C" int fake_clock(int,PipClock* p) { p->tv_sec=p->tv_nsec=0; return 0; }
extern "C" int fake_usleep(unsigned) { return 0; }
extern "C" int fake_socket(int,int,int) { return next_fd++; }
extern "C" int fake_connect(int,const void*,unsigned) { return 0; }
extern "C" int fake_bind(int,const void*,unsigned) { return 0; }
extern "C" int fake_getsockopt(int,int,int,void*,unsigned*) { return 0; }
extern "C" int fake_fcntl(int,int,...) { return 0; }
extern "C" int fake_getsockname(int,void* p,unsigned*) { ((Address*)p)->port=0x3412; return 0; }
extern "C" int fake_close(int) { ++closed; return 0; }
extern "C" int fake_poll(PollFD* p,unsigned,int) { p->revents=p->events; return 1; }
static uint8_t* response(int fd,unsigned size) {
    for(unsigned i=0;i<64;++i) replies[fd][i]=0;
    available[fd]=size;consumed[fd]=0;return replies[fd];
}
extern "C" int fake_sendto(int,const void* data,unsigned n,int,const void*,unsigned) {
    if(((const uint8_t*)data)[35]) {
        uint8_t* r=response(10,29);put32(r,0xb0098001);put32(r+4,21);
        r[12]=r[13]=1;r[14]=2;put16(r+24,620);put16(r+26,313);r[28]=1;
    }
    return n;
}
extern "C" int fake_send(int fd,const void* data,unsigned n,int) {
    const uint8_t* p=(const uint8_t*)data;
    if(fd==10) { if(n!=18 || p[16]!=1 || p[17]!=0) ++failure; return n; }
    if(fd!=12 || n<16) { ++failure; return -1; }
    ++calls; unsigned method=get16(p+2);
    if(method==3 && n==44 && p[24]==2) {
        // Literal 28-byte body captured from the factory, with handle 200.
        const uint8_t expected[]={0,0,0,200, 2,0,0,0, 2, 0,0,0,11,
            0,0,0,3, 2,0,0,0, 2, 3,32,1,224,0,1};
        for(unsigned i=0;i<sizeof(expected);++i) if(p[16+i]!=expected[i]) ++failure;
        ++resource_writes;
    }
    uint8_t* r=response(fd,method==1?24:16);
    for(unsigned i=0;i<16;++i) r[i]=p[i];
    put32(r+4,method==1?16:8);r[14]=0x80;
    if(method==1) put32(r+16,200);
    return n;
}
extern "C" int fake_recv(int fd,void* data,unsigned n,int) {
    if(n>available[fd]-consumed[fd]) return 0;
    for(unsigned i=0;i<n;++i) ((uint8_t*)data)[i]=replies[fd][consumed[fd]++];
    return n;
}
int main() {
    end_time=65000;Control c={-1,-1,-1,0,1,0,0};
    bool started=c.start(),clean=c.close();
    on_signal(0);
    bool ok=started && clean && c.stopped() && !failure && resource_writes==1 && calls==107 && closed==3;
    printf("%s physical resource matches factory packet, sink stays 620x313, cleanup acknowledged\n",ok?"PASS":"FAIL");
    return ok?0:1;
}
