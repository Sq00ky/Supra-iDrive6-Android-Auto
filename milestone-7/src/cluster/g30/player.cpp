// The only component allowed to own the physical PiP session. Reference
// Control, ScreenOutput, FactoryResource and row-0 metadata are shared verbatim
// with the car-confirmed tools/kombi-car-demo implementation.
#include "decoder.hpp"
#include "protocol.hpp"
#include "native/screen_output.hpp"
#include "native/control.hpp"
#ifdef IDRIVER_QNX_SDK
#include <signal.h>
#include "../video_settings.hpp"
#else
extern "C" { int listen(int,int);int accept(int,void*,unsigned*);void (*signal(int,void (*)(int)))(int); }
#endif
using namespace aa_cluster;
namespace {
bool service_mode;
static void keep_service_alive() {
    // Service lifetime belongs to its local socket and the independent helper.
    // Keep finite RPC deadlines; the standalone car trial remains 65 seconds.
    if(service_mode)end_time=millis()+10000;
}
struct Player {
    car_demo::ScreenOutput output;
    Control control;
    Decoder decoder;
    bool active,marked;
    unsigned number,epoch,last_present;
    uint8_t pixels[620*313*4];
    Player():output{},control{-1,-1,-1,0,1,0,0},active(false),marked(false),number(0),epoch(0),last_present(0) {}
    bool stop() {
        bool ok=control.close();ok=output.close()&&ok;decoder.close();active=false;
#ifdef IDRIVER_QNX_SDK
        if(marked&&ok) {unlink(ActiveMarker);marked=false;}
#endif
        output={};control={-1,-1,-1,0,1,0,0};return ok;
    }
    bool pump() {
        keep_service_alive();
        if(expired())return false;
        // AA legitimately stops encoding an unchanged map/onboarding screen.
        // Keep its last frame until focus/session Stop, transport EOF, control
        // failure or the existing 65-second trial deadline. Frame cadence is
        // not a connection-health signal; the AA session owns that watchdog.
        return !active||control.pump();
    }
    bool read(int fd,uint8_t* p,unsigned n) {
        unsigned done=0;uint32_t deadline=millis()+3000;
        while(done<n&&!expired()) {
            if(!pump()) { printf("AA_CLUSTER_READ_STOP reason=control bytes=%u/%u\n",done,n);return false; }
            PollFD pollfd={fd,PollRead,0};int rc=poll(&pollfd,1,20);
            if(rc<0 && *__get_errno_ptr()==4)continue;
            if(rc<0||pollfd.revents&PollErrorOrHangup) {
                printf("AA_CLUSTER_READ_STOP reason=poll bytes=%u/%u events=%d errno=%d\n",done,n,pollfd.revents,*__get_errno_ptr());return false;
            }
            if(!rc) {
                if(done&&int32_t(millis()-deadline)>0) { printf("AA_CLUSTER_READ_STOP reason=partial_timeout bytes=%u/%u\n",done,n);return false; }
                continue;
            }
            rc=recv(fd,p+done,n-done,0);
            if(rc<0 && *__get_errno_ptr()==4)continue;
            if(rc<=0) { printf("AA_CLUSTER_READ_STOP reason=transport bytes=%u/%u rc=%d errno=%d\n",done,n,rc,*__get_errno_ptr());return false; }
            done+=unsigned(rc);
        }
        if(done!=n)printf("AA_CLUSTER_READ_STOP reason=trial_end bytes=%u/%u\n",done,n);
        return done==n;
    }
    bool packet(const uint8_t* h,const uint8_t* payload,bool backlog) {
        unsigned kind=be32(h+4),generation=be32(h+8),n=be32(h+12);
        if(kind==Stop) { epoch=generation;return stop(); }
        if(kind==Reset) { if(!stop())return false;epoch=generation;return decoder.open(); }
        if(generation!=epoch) { printf("AA_CLUSTER_EPOCH_ERROR received=%u expected=%u\n",generation,epoch);return false; }
        bool present=!active||!backlog||millis()-last_present>=200;
        int status=decoder.decode(payload,n,be64(h+16),pixels,present);
        if(status<0) { printf("AA_CLUSTER_DECODE_ERROR await_new_IDR\n");return false; }
        if(!status)return true;
        if(!active) {
#ifdef IDRIVER_QNX_SDK
            // The arrow renderer uses the same Guidance handle. Its normal
            // worker clears the arrow when the map preference becomes enabled.
            unsigned wait=millis();
            while(access("/dev/shmem/nbt-aad-kombi-nav.shown",F_OK)==0) {
                if(expired()||millis()-wait>4000)return false;
                keep_service_alive();usleep(20000);
            }
            int marker=open(ActiveMarker,O_WRONLY|O_CREAT|O_TRUNC,0600);
            if(marker<0)return false;
            char owner[32];int len=snprintf(owner,sizeof(owner),"%ld\n",(long)getpid());
            bool saved=write(marker,owner,len)==len;close(marker);
            marked=true;if(!saved)return false;
#endif
            if(!output.inspect(1)||!output.create()||!control.start()) { printf("AA_CLUSTER_OUTPUT_START_FAILED\n");return false; }
            active=true;
        }
        // No colour swap, exact factory map profile and backing geometry.
        if(!output.present(pixels,(0x5678u+number)&65535,false)) { printf("AA_CLUSTER_PRESENT_FAILED frames=%u\n",number);return false; }
        ++number;last_present=millis();
        if(number==1||number%30==0) {
            printf("AA_CLUSTER_POST frames=%u pts=%llu source=800x480 content=620x313\n",number,(unsigned long long)be64(h+16));fflush(0);
        }
        return true;
    }
};
}
extern "C" int demo_main() {
    signal(1,on_signal);signal(2,on_signal);signal(15,on_signal);
    signal(13,(void (*)(int))1); // SIGPIPE cannot prevent normal cleanup.
    end_time=millis()+65000;
    Player* player=new Player;
    uint8_t* payload=(uint8_t*)malloc(MaxPayload);
    int server=socket(2,1,0),client=-1,result=1;
    Address local={16,2,uint16_t((aa_cluster::Port>>8)|(aa_cluster::Port<<8)),0x0100007f,{}};
    if(!payload||server<0||bind(server,&local,sizeof(local))<0||listen(server,1)<0)goto done;
    printf("AA_CLUSTER_READY loopback_port=%u deadline_seconds=65\n",aa_cluster::Port);fflush(0);
    while(!expired()) {
        keep_service_alive();
        PollFD p={server,PollRead,0};int rc=poll(&p,1,100);
        if(rc<0||p.revents&PollErrorOrHangup)goto done;
        if(rc>0) { client=accept(server,0,0);break; }
    }
    if(client<0)goto done;
    {
        uint8_t hello[8];aa_cluster::put32(hello,0x41435031);aa_cluster::put32(hello+4,1);
        if(send(client,hello,8,0)!=8)goto done;
        uint8_t h[HeaderSize];
        while(!expired()&&player->read(client,h,sizeof(h))) {
            if(!valid_header(h)) { printf("AA_CLUSTER_INVALID_HEADER\n");goto done; }
            unsigned n=be32(h+12);
            if(n&&!player->read(client,payload,n)) { if(expired())result=0;goto done; }
            PollFD pending={client,PollRead,0};
            bool backlog=poll(&pending,1,0)>0 && (pending.revents&PollRead);
            if(!player->packet(h,payload,backlog))goto done;
        }
        result=0;
    }
done:
    if(client>=0)close(client);if(server>=0)close(server);
    if(!player->stop())result=3;
    printf("AA_CLUSTER_DONE status=%d frames=%u unchanged=%u coalesced=%u\n",result,player->number,player->decoder.unchanged(),player->decoder.coalesced());fflush(0);
    delete player;free(payload);return result;
}
#ifdef IDRIVER_QNX_SDK
int main(int argc,char** argv) {
    if(argc==2&&!strcmp(argv[1],"--service"))service_mode=true;
    else if(argc!=1)return 2;
    return demo_main();
}
#endif
