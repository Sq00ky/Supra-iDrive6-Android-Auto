// Host fault-injection tests. No QNX, networking or firmware is executed.
#include "../../src/cluster/g30/native/screen_output.hpp"
using namespace car_demo;
static int failures, display_count, display_width, render_count, stride, display_changed;
static int reads, destroys, creates, submitted, context_flags, props[100][2];
static void* current_display;
static uint8_t pixels[2][(Width*4+16)*Height];
static void expect(bool yes,const char* label) { printf("%s %s\n",yes?"PASS":"FAIL",label); if(!yes) ++failures; }
extern "C" int usleep(unsigned) { return 0; }
extern "C" int screen_create_context(void** p,int flags) { *p=(void*)1; context_flags=flags; return 0; }
extern "C" int screen_destroy_context(void*) { ++destroys; return 0; }
extern "C" int screen_get_context_property_iv(void*,int p,int* out) { if(p!=DisplayCount) return -1; *out=display_count; return 0; }
extern "C" int screen_get_context_property_pv(void*,int p,void** out) { if(p!=Displays) return -1; out[0]=(void*)2; out[1]=(void*)3; return 0; }
extern "C" int screen_get_display_property_iv(void* d,int p,int* out) { if(p!=Size) return -1; out[0]=d==(void*)3?display_width:1440; out[1]=d==(void*)3?Height:540; return 0; }
extern "C" int screen_create_window(void** p,void*) { *p=(void*)4; ++creates; return 0; }
extern "C" int screen_destroy_window(void* p) { if(p!=(void*)4) return -1; ++destroys; return 0; }
extern "C" int screen_set_window_property_iv(void* w,int p,const int* val) { if(w!=(void*)4) return -1; if((p==Visible || p==ZOrder) && context_flags!=1) return 0; props[p][0]=*val; if(p==Size || p==BufferSize || p==SourceSize || p==Position) props[p][1]=val[1]; return 0; }
extern "C" int screen_set_window_property_pv(void*,int p,void** val) { if(p!=Display) return -1; current_display=*val; return 0; }
extern "C" int screen_set_window_property_cv(void*,int p,int n,const char* s) { return p==IDString && n==9 && s[0]=='2'?0:-1; }
extern "C" int screen_get_window_property_iv(void*,int p,int* val) { if(p==RenderCount) { *val=render_count; return 0; } *val=props[p][0]; if(p==Size || p==BufferSize || p==SourceSize || p==Position) val[1]=props[p][1]; return 0; }
extern "C" int screen_get_window_property_pv(void*,int p,void** val) { if(p==Display) *val=current_display; else if(p==RenderBuffers) { ++reads; val[0]=(void*)pixels[submitted%2]; if(render_count==2) val[1]=(void*)pixels[(submitted+1)%2]; } else return -1; return 0; }
extern "C" int screen_create_window_buffers(void*,int n) { return n==2?0:-1; }
extern "C" int screen_get_buffer_property_iv(void*,int p,int* val) { if(p==Stride) *val=stride; else if(p==Format) *val=RGBA8888; else if(p==BufferSize) { val[0]=Width; val[1]=Height; } else return -1; return 0; }
extern "C" int screen_get_buffer_property_pv(void* b,int p,void** val) { if(p!=Pointer) return -1; *val=b; return 0; }
extern "C" int screen_post_window(void*,void*,int n,const int* rect,int) { if(n!=1 || rect[2]!=Width || rect[3]!=Height) return -1; ++submitted; if(display_changed) current_display=(void*)2; return 0; }
extern "C" int screen_flush_context(void*,int) { return 0; }
static void reset() {
    display_count=2; display_width=Width; render_count=2; stride=Width*4+16;
    display_changed=reads=destroys=creates=submitted=0; current_display=0;
    for(unsigned i=0;i<sizeof(pixels);++i) ((uint8_t*)pixels)[i]=0xee;
    for(unsigned i=0;i<sizeof(props)/sizeof(int);++i) ((int*)props)[i]=0;
}
int main() {
    reset(); ScreenOutput s={};
    expect(s.inspect(1) && s.create() && s.present(0,0x0102,false),"privileged owned window, secondary display, private ID, first post");
    expect(pixels[0][0]==1 && pixels[0][1]==2 && pixels[0][4]==1 && pixels[0][7]==1
           && pixels[0][5]==0 && pixels[0][18]==0x0f && pixels[0][19]==0xa2
           && pixels[0][24]==0 && pixels[0][25]==0,"captured map metadata retained; config zero, captured context, reserved bytes zero");
    bool reserved=true;
    for(unsigned x=84;x<Width*4;++x) reserved&=pixels[0][x]==0;
    for(unsigned y=1;y<Height;++y) for(unsigned x=0;x<Width;++x)
        if(y>313 || x>=620) for(unsigned c=0;c<4;++c) reserved&=pixels[0][y*stride+x*4+c]==0;
    expect(reserved && pixels[0][stride+3]==255 && pixels[0][313*stride+3]==255,
           "factory content rows 1 through 313 with transparent unused backing and reserved row0");
    bool padding=true; for(unsigned y=0;y<Height;++y) for(unsigned x=Width*4;x<unsigned(stride);++x) padding&=pixels[0][y*stride+x]==0xee;
    expect(padding,"never write Screen row padding");
    expect(s.present(0,3,true) && pixels[1][6]==1 && pixels[1][4]==0,"next render buffer and explicit metadata byte permutation");
    expect(s.close() && destroys==2,"destroy only owned window/context");
    reset(); s={}; s.metadata_profile=FactoryManeuver;
    expect(s.inspect(1) && s.create() && s.present(0,0x0059,false)
           && pixels[0][16]==0x0f && pixels[0][18]==0 && pixels[0][19]==0,
           "maneuver profile keeps its distinct captured context bytes"); s.close();
    reset(); s={}; s.metadata_profile=2;
    expect(s.inspect(1) && s.create() && !s.present(0,1,false) && !reads && !submitted,
           "unknown metadata profile refused before accessing buffers"); s.close();
    reset(); s={}; static uint8_t clip[620*313*4];
    for(unsigned i=0;i<sizeof(clip);++i) clip[i]=uint8_t(i*7);
    bool clip_ok=s.inspect(1) && s.create() && s.present(clip,1,false);
    for(unsigned y=0;y<313;++y) for(unsigned x=0;x<620*4;++x)
        clip_ok&=pixels[0][(y+1)*stride+x]==clip[y*620*4+x];
    expect(clip_ok,"clip pixels including last row survive the metadata-row offset"); s.close();
    reset(); s={}; static uint8_t small[310*157*4];
    for(unsigned y=0;y<157;++y) for(unsigned x=0;x<310;++x) {
        unsigned i=(y*310+x)*4;small[i]=uint8_t(x);small[i+1]=uint8_t(y);small[i+2]=100;small[i+3]=255;
    }
    bool small_ok=s.inspect(1) && s.create() && s.present(small,0x1234,false,true);
    const uint8_t* middle=pixels[0]+2*stride+4; // content (1,1): average four neighbors
    const uint8_t* last=pixels[0]+313*stride+619*4;
    expect(small_ok && middle[0]==1 && middle[1]==1 && middle[2]==100 && middle[3]==255
           && last[0]==53 && last[1]==156 && last[2]==100 && last[3]==255
           && pixels[0][0]==0x12 && pixels[0][1]==0x34,
           "compact clip interpolates pixels, clamps last column/row and retains metadata"); s.close();
    reset(); s={};
    expect(s.inspect() && s.create() && !s.present(0,1,false),"silent factory-manager rejection detected for ordinary app context"); s.close();
    reset(); s={}; display_count=1;
    expect(!s.inspect() && !creates,"never fall back to primary display"); s.close();
    reset(); s={}; display_width=1440;
    expect(!s.inspect() && !creates,"unexpected secondary geometry rejected"); s.close();
    reset(); s={}; expect(s.inspect() && s.create(),"buffer bound fixture"); render_count=3;
    expect(!s.present(0,1,false) && !reads && !submitted,"oversized handle count rejected before pointer-array query"); s.close();
    reset(); s={}; s.inspect(); s.create(); stride=Width*4-1;
    expect(!s.present(0,1,false) && !submitted,"short stride rejected"); s.close();
    reset(); s={}; s.inspect(); s.create(); display_changed=1;
    expect(!s.present(0,1,false),"manager moving window to primary aborts"); s.close();
    reset(); s={}; s.inspect(); s.create(); props[Transparency][0]=3;
    expect(!s.present(0,1,false) && !submitted,"alpha-blended metadata rejected before posting"); s.close();
    return failures?1:0;
}
