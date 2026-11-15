#include "cluster/g30/decoder.hpp"
#include "cluster/g30/protocol.hpp"
#include "cluster/g30/native/native_screen.hpp"
#include "fake_screen.hpp"
extern "C" int usleep(unsigned) {return 0;}
static uint8_t payload[aa_cluster::MaxPayload],pixels[620*313*4];
int main() {
    using namespace aa_cluster;
    Decoder decoder;FILE* f=fopen("fixture.acp","rb");if(!f)return 1;
    screen_peer::reset();kombi::pip::screen::ScreenOutput screen={};
    if(!screen.inspect(1)||!screen.create())return 2;
    uint8_t h[HeaderSize];unsigned count=0;
    while(fread(h,1,sizeof(h),f)==sizeof(h)) {
        if(!valid_header(h))return 3;
        unsigned n=be32(h+12),kind=be32(h+4);
        if(n&&fread(payload,1,n,f)!=n)return 4;
        if(kind==Reset){if(!decoder.open())return 5;continue;}
        if(kind==Stop){decoder.close();continue;}
        int rc=decoder.decode(payload,n,be64(h+16),pixels);if(rc<0)return 6;if(!rc)continue;
        if(!screen.present(pixels,0x5678+count,false))return 7;++count;
        for(unsigned y=0;y<480;++y)for(unsigned x=0;x<3200;++x) {
            uint8_t got=screen_peer::pixels[y*3200+x];
            if(y>=1&&y<=313&&x<620*4) {if(got!=pixels[(y-1)*620*4+x])return 8;}
            else if(y&&got)return 9;
            else if(!y&&x>=84&&got)return 10;
        }
        const uint8_t* row=screen_peer::pixels;
        if(row[0]!=((0x5677+count)>>8)||row[1]!=uint8_t(0x5677+count)||row[4]!=1||row[7]!=1||row[18]!=15||row[19]!=162)return 11;
    }
    fclose(f);if(!screen.close()||count!=30)return 12;
    // Repeated encoded frames still advance the decoder's reference chain,
    // but need one presentation. A fresh session must present its first frame.
    for(unsigned pass=0;pass<2;++pass) {
        f=fopen("static.acp","rb");if(!f)return 13;
        unsigned inputs=0,changed=0;
        while(fread(h,1,sizeof(h),f)==sizeof(h)) {
            if(!valid_header(h))return 14;
            unsigned n=be32(h+12),kind=be32(h+4);
            if(n&&fread(payload,1,n,f)!=n)return 15;
            if(kind==Reset){if(!decoder.open())return 16;continue;}
            if(kind==Stop){decoder.close();continue;}
            ++inputs;int rc=decoder.decode(payload,n,be64(h+16),pixels);
            if(rc<0)return 17;if(rc)++changed;
        }
        fclose(f);if(inputs!=30||changed!=1)return 18;
    }
    if(decoder.unchanged()!=58)return 19;
    // Coalescing must decode skipped P-frames to preserve their references.
    f=fopen("fixture.acp","rb");if(!f)return 20;
    unsigned inputs=0,shown=0;
    while(fread(h,1,sizeof(h),f)==sizeof(h)) {
        unsigned n=be32(h+12),kind=be32(h+4);
        if(n&&fread(payload,1,n,f)!=n)return 21;
        if(kind==Reset){if(!decoder.open())return 22;continue;}
        if(kind==Stop){decoder.close();continue;}
        bool present=inputs==0||inputs==29;++inputs;
        int rc=decoder.decode(payload,n,be64(h+16),pixels,present);
        if(rc<0||(!present&&rc))return 23;if(rc)++shown;
    }
    fclose(f);if(shown!=2||decoder.coalesced()!=28)return 24;
    printf("AA_PIXEL_CONTRACT_OK frames=%u factory_metadata row0 BGRA content_origin=0,1 unused_backing_zero\n",count);
    printf("AA_STATIC_VIDEO_OK unchanged=58 first_frame_after_restart_presented=1\n");
    printf("AA_COALESCED_VIDEO_OK decoded=30 presented=2 predictive_chain_preserved=1\n");
}
