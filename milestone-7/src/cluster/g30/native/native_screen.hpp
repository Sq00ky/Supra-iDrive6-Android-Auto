// NBT's Screen 6.5 SP1 binding: its header is absent from the base SDP 6.5.
// Keep these car-verified property values independent of other Screen versions.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "native_metadata.hpp"
#ifdef NBT_G30_HOST_TEST
#include <qnx_api_stub.hpp>
#else
#include <stdio.h>
#include <unistd.h>
#endif
extern "C" {
int screen_create_context(void**, int);
int screen_destroy_context(void*);
int screen_get_context_property_iv(void*, int, int*);
int screen_get_context_property_pv(void*, int, void**);
int screen_get_display_property_iv(void*, int, int*);
int screen_create_window(void**, void*);
int screen_destroy_window(void*);
int screen_set_window_property_iv(void*, int, const int*);
int screen_set_window_property_pv(void*, int, void**);
int screen_set_window_property_cv(void*, int, int, const char*);
int screen_get_window_property_iv(void*, int, int*);
int screen_get_window_property_pv(void*, int, void**);
int screen_create_window_buffers(void*, int);
int screen_get_buffer_property_iv(void*, int, int*);
int screen_get_buffer_property_pv(void*, int, void**);
int screen_post_window(void*, void*, int, const int*, int);
int screen_flush_context(void*, int);
}

namespace kombi { namespace pip { namespace screen {
enum { Width=800, Height=480, ContentWidth=620, ContentHeight=313, ContentY=1,
       CompactWidth=310, CompactHeight=157,
       BufferSize=5, Display=11, Format=14, Alpha=16, Pipeline=17, IDString=20,
       Pointer=34, Position=35, RenderBuffers=37, Size=40, SourceSize=42,
       Stride=44, Transparency=46, Usage=48, Visible=51, RenderCount=53,
       ZOrder=54, DisplayCount=59, Displays=60, RGBA8888=8 };
// Decimal ID with scene display index 1 in the high nibble, surface 9000.
// Absent from the pinned factory table; never impersonate window 1056.
static const char WindowID[]="268444456";

struct ScreenOutput {
    void *context, *window, *display;
    unsigned posts;
    unsigned metadata_profile;
    bool set(int prop, const int* value) {
        int rc=screen_set_window_property_iv(window,prop,value);
        if(rc) printf("SCREEN_SET_FAILED property=%d rc=%d\n",prop,rc);
        return rc==0;
    }
    bool inspect(int context_flags=0) {
        // The car's existing display manager restricts VISIBLE/ZORDER writes
        // from ordinary application contexts. Flag 1 grants window-management
        // access; use it only for our own window. The launcher verifies both
        // displays already have managers, so this context adopts neither.
        if(screen_create_context(&context,context_flags)) return false;
        int count=0;
        if(screen_get_context_property_iv(context,DisplayCount,&count) || count!=2) {
            printf("REFUSE_DISPLAY_COUNT count=%d expected=2\n",count); return false;
        }
        void* handles[2]={};
        if(screen_get_context_property_pv(context,Displays,handles) || !handles[0]
           || !handles[1] || handles[0]==handles[1]) return false;
        int dimensions[2]={};
        for(int i=0;i<2;++i) {
            if(screen_get_display_property_iv(handles[i],Size,dimensions)) return false;
            printf("DISPLAY index=%d width=%d height=%d\n",i,dimensions[0],dimensions[1]);
            if(i==1 && (dimensions[0]!=Width || dimensions[1]!=Height)) return false;
        }
        display=handles[1]; return true;
    }
    bool create() {
        if(!display || screen_create_window(&window,context)) return false;
        int size[2]={Width,Height}, origin[2]={0,0}, format=RGBA8888, usage=6;
        int hidden=0, source=0, alpha=255;
        return !screen_set_window_property_pv(window,Display,&display)
            && set(Visible,&hidden)
            && !screen_set_window_property_cv(window,IDString,sizeof(WindowID)-1,WindowID)
            && set(Format,&format) && set(Usage,&usage) && set(BufferSize,size)
            && set(Size,size) && set(SourceSize,size) && set(Position,origin)
            // SOURCE preserves all four bytes, including metadata in alpha.
            && set(Transparency,&source) && set(Alpha,&alpha)
            && !screen_create_window_buffers(window,2);
    }
    bool contract(bool require_visible) {
        void* actual=0; int size[2]={}, pos[2]={}, format=0, visible=0, alpha=0, blend=-1;
        bool ok=!screen_get_window_property_pv(window,Display,&actual) && actual==display
            && !screen_get_window_property_iv(window,BufferSize,size) && size[0]==Width && size[1]==Height
            && !screen_get_window_property_iv(window,Size,size) && size[0]==Width && size[1]==Height
            && !screen_get_window_property_iv(window,SourceSize,size) && size[0]==Width && size[1]==Height
            && !screen_get_window_property_iv(window,Position,pos) && !pos[0] && !pos[1]
            && !screen_get_window_property_iv(window,Format,&format) && format==RGBA8888
            && !screen_get_window_property_iv(window,Alpha,&alpha) && alpha==255
            && !screen_get_window_property_iv(window,Transparency,&blend) && blend==0;
        if(require_visible) ok=ok && !screen_get_window_property_iv(window,Visible,&visible) && visible==1;
        if(!ok) {
            printf("SCREEN_CONTRACT_CHANGED posts=%u\n",posts);
            const int properties[]={BufferSize,Size,SourceSize,Position,Format,Alpha,Transparency,Visible,ZOrder,Pipeline};
            for(unsigned i=0;i<sizeof(properties)/sizeof(properties[0]);++i) {
                int value[2]={-999,-999};
                int rc=screen_get_window_property_iv(window,properties[i],value);
                printf("SCREEN_PROPERTY property=%d rc=%d value=%d,%d\n",properties[i],rc,value[0],value[1]);
            }
            actual=0; int rc=screen_get_window_property_pv(window,Display,&actual);
            printf("SCREEN_DISPLAY rc=%d matches_secondary=%u\n",rc,unsigned(actual==display));
            fflush(0);
        }
        return ok;
    }
    bool present(const uint8_t* bgra, unsigned sequence, bool swap_rb, bool compact=false) {
        if(metadata_profile>FactoryManeuver || !window || !contract(posts!=0)) return false;
        int count=0, stride=0, format=0, dimensions[2]={}; void* buffers[2]={}; void* memory=0;
        if(screen_get_window_property_iv(window,RenderCount,&count) || count<1 || count>2
           || screen_get_window_property_pv(window,RenderBuffers,buffers) || !buffers[0]
           || screen_get_buffer_property_iv(buffers[0],Stride,&stride) || stride<Width*4 || stride>16384
           || screen_get_buffer_property_iv(buffers[0],BufferSize,dimensions)
           || dimensions[0]!=Width || dimensions[1]!=Height
           || screen_get_buffer_property_iv(buffers[0],Format,&format) || format!=RGBA8888
           || screen_get_buffer_property_pv(buffers[0],Pointer,&memory) || !memory) return false;
        for(unsigned y=0;y<Height;++y) {
            uint8_t* row=(uint8_t*)memory+y*stride;
            for(unsigned x=0;x<Width;++x) {
                uint8_t* p=row+x*4; p[0]=p[1]=p[2]=p[3]=0;
                if(x<ContentWidth && y>=ContentY && y<ContentY+ContentHeight) {
                    const unsigned cy=y-ContentY;
                    if(bgra && compact) {
                        // Twofold bilinear expansion; crop only the final row
                        // from 620x314 to the negotiated 620x313 content area.
                        unsigned x0=x/2,y0=cy/2;
                        unsigned x1=x0+(x0+1<CompactWidth),y1=y0+(y0+1<CompactHeight);
                        unsigned fx=x&1,fy=cy&1;
                        for(unsigned c=0;c<4;++c) {
                            unsigned top=bgra[(y0*CompactWidth+x0)*4+c]*(2-fx)+bgra[(y0*CompactWidth+x1)*4+c]*fx;
                            unsigned bottom=bgra[(y1*CompactWidth+x0)*4+c]*(2-fx)+bgra[(y1*CompactWidth+x1)*4+c]*fx;
                            p[c]=uint8_t((top*(2-fy)+bottom*fy+2)/4);
                        }
                    } else if(bgra) for(unsigned c=0;c<4;++c) p[c]=bgra[(cy*ContentWidth+x)*4+c];
                    else {
                        p[3]=255;
                        p[0]=x<207?220:20; p[1]=(x>=207 && x<414)?220:20; p[2]=x>=414?220:20;
                        if(((x+sequence*9)%ContentWidth)<28 || cy<4 || cy>ContentHeight-5) p[0]=p[1]=p[2]=255;
                    }
                }
            }
        }
        // Factory row0 is metadata followed by transparent zeros. The visible
        // 620x313 image occupies rows1..313; all unused backing pixels are zero.
        if(!factory_metadata_bgra((uint8_t*)memory,uint16_t(sequence),metadata_profile)) return false;
        if(swap_rb) for(unsigned y=0;y<Height;++y) for(unsigned x=0;x<Width;++x) {
            uint8_t* p=(uint8_t*)memory+y*stride+x*4; uint8_t v=p[0]; p[0]=p[2]; p[2]=v;
        }
        int damage[4]={0,0,Width,Height};
        if(screen_post_window(window,buffers[0],1,damage,0)) return false;
        ++posts;
        if(posts==1) {
            // Allow the manager's first POST handler to finish before checking
            // our own window. Do not select a pipeline or modify factory windows.
            usleep(200000);
            void* actual=0;
            if(screen_get_window_property_pv(window,Display,&actual) || actual!=display) return false;
            int source=0, alpha=255, visible=1, zorder=1000;
            if(!set(Transparency,&source) || !set(Alpha,&alpha) || !set(ZOrder,&zorder)
               || !set(Visible,&visible) || screen_flush_context(context,1)) return false;
            int pipeline=-1; screen_get_window_property_iv(window,Pipeline,&pipeline);
            printf("FIRST_POST id=%s display_index=1 pipeline=%d stride=%d metadata=factory84-bgra profile=%s content_y=1 swap_rb=%u\n",
                   WindowID,pipeline,stride,metadata_profile==FactoryMap?"map":"maneuver",unsigned(swap_rb)); fflush(0);
        }
        return contract(true);
    }
    bool close() {
        bool ok=true;
        if(window) { int hide=0; set(Visible,&hide); ok=screen_destroy_window(window)==0; window=0; }
        if(context) { ok=(screen_destroy_context(context)==0)&&ok; context=0; }
        display=0; return ok;
    }
};
}}}
