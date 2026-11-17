#pragma once
#include "cluster/video/hardware_session.hpp"
#include <assert.h>
#include <set>
#include <string>
#include <vector>

// A renderer/Screen protocol peer, not a claim of IVA-HD emulation. It models
// asynchronous output, padded allocations/viewports, scanlines and partial startup.
struct HardwarePeer {
    typedef nbt::cluster::VideoProfile P;
    uint64_t clock;
    unsigned calls, fail_at, next, event_type, reads, input_count;
    int send_error;
    int allocation[2], origin[2], source[2], window_size[2], source_format, capture_usage;
    int capture_size[2], capture_stride;
    bool no_pixels, read_error, output_event, close_event;
    std::set<void*> objects;
    std::vector<uint8_t> pixels;
    std::vector<unsigned> packets;
    std::string context_name, endpoint_name, output_id;
    void *parent, *child, *foreign;
    HardwarePeer() : clock(1000000), calls(0), fail_at(0), next(100), event_type(0), reads(0),
        input_count(0), send_error(0), no_pixels(false), read_error(false), output_event(false), close_event(false),
        pixels((P::FittedWidth * 4 + 64) * P::ContentHeight, 0xe7), parent(0), child((void*)900), foreign((void*)901) {
        allocation[0]=source[0]=window_size[0]=P::SourceWidth;
        allocation[1]=source[1]=window_size[1]=P::SourceHeight;
        origin[0]=origin[1]=0;source_format=12;capture_usage=0;
        capture_size[0]=capture_size[1]=0;capture_stride=P::FittedWidth*4+64;
    }
    int result() { return ++calls == fail_at ? -1 : 0; }
    int create(void** out) { if (result()) return -1; *out=(void*)(uintptr_t)++next; objects.insert(*out); return 0; }
    int destroy(void* p) { assert(objects.erase(p)==1); return 0; }
    unsigned long process_id() { return 4242; }
    uint64_t now_us() { return clock; }
    void sleep_ms(unsigned n) { clock += n*1000; }
    int open_endpoint(const char* name) { endpoint_name=name; return result() ? -1 : 7; }
    void close_endpoint(int n) { assert(n==7); }
    int send_packet(int n, unsigned type, const void*, unsigned, const uint64_t* pts) {
        assert(n==7); packets.push_back(type);
        if (type==3) { assert(pts && *pts==clock); ++input_count; output_event=true; }
        else assert(!pts);
        return send_error;
    }
    void* mmr_connect(const char*) { void* p=0; create(&p); return p; }
    void mmr_disconnect(void* p) { destroy(p); }
    void* mmr_context_create(void*, const char* name, unsigned, unsigned) {
        context_name=name; void* p=0; create(&p); return p;
    }
    void mmr_context_destroy(void* p) { destroy(p); }
    int mmr_stop(void*) { return 0; }
    void* strm_dict_new() { void* p=0; create(&p); return p; }
    void* strm_dict_set(void* p,const char*,const char*) { return result()?0:p; }
    void strm_dict_destroy(void* p) { destroy(p); }
    int parameters(void* p) { destroy(p); return result(); }
    int mmr_context_parameters(void*,void* p) { return parameters(p); }
    int mmr_input_parameters(void*,void* p) { return parameters(p); }
    int mmr_output_parameters(void*,unsigned id,void* p) { assert(id==0);return parameters(p); }
    int mmr_output_attach(void*,const char* url,const char*) {
        std::string s(url);size_t a=s.find("winid=")+6,b=s.find('&',a);output_id=s.substr(a,b-a);
        assert(s.find("wingrp=kombi_hw_group_4242_")!=std::string::npos); return result();
    }
    int mmr_input_attach(void*,const char* url,const char*) {
        assert(strstr(url,"mdevpush:/dev/name/local/kombi_hw_4242?"));
        assert(strstr(url,"video.width=800&video.height=480"));return result();
    }
    int mmr_speed_set(void*,int n) { assert(n==1000);return result(); }
    int mmr_play(void*) { return result(); }
    int screen_create_context(void** out,int) { return create(out); }
    int screen_destroy_context(void* p) { return destroy(p); }
    int screen_create_window(void** out,void*) { int rc=create(out);if(!rc)parent=*out;return rc; }
    int screen_destroy_window(void* p) { return p==child?0:destroy(p); }
    int screen_create_window_group(void*,const char*) { return result(); }
    int screen_set_window_property_iv(void*,int,const int*) { return result(); }
    int screen_set_window_property_pv(void*,int,void**) { return result(); }
    int screen_create_window_buffers(void*,int) { return result(); }
    int screen_create_event(void** out) { return create(out); }
    int screen_destroy_event(void* p) { return destroy(p); }
    int screen_create_pixmap(void** out,void*) { return create(out); }
    int screen_destroy_pixmap(void* p) { return destroy(p); }
    int screen_set_pixmap_property_iv(void*,int property,const int* value) {
        if(property==aa_cluster::hardware::Usage)capture_usage=*value;
        if(property==aa_cluster::hardware::BufferSize) {
            capture_size[0]=value[0];capture_size[1]=value[1];
            assert(value[0]==522 && value[1]==313); // Literal requested scaled destination.
        }
        return result();
    }
    int screen_create_pixmap_buffer(void*) {
        assert(capture_size[0]==522 && capture_size[1]==313);
        return result();
    }
    int screen_get_context_property_iv(void*,int,int* out) { *out=2;return result(); }
    int screen_get_context_property_pv(void*,int,void** out) { out[0]=(void*)1;out[1]=(void*)2;return result(); }
    int screen_get_window_property_pv(void*,int,void** out) { *out=(void*)10;return result(); }
    int screen_get_pixmap_property_pv(void*,int,void** out) { *out=(void*)11;return result(); }
    int screen_get_buffer_property_pv(void*,int,void** out) { *out=&pixels[0];return result(); }
    int screen_get_buffer_property_iv(void*,int property,int* out) {
        if(property==aa_cluster::hardware::Stride)*out=capture_stride;
        else if(property==aa_cluster::hardware::Format)*out=8;
        else {out[0]=capture_size[0];out[1]=capture_size[1];}return result();
    }
    int screen_get_window_property_iv(void* w,int property,int* out) {
        assert(w==child);
        using namespace aa_cluster::hardware;
        if(property==Format)*out=source_format;
        else {
            const int* pair=property==BufferSize?allocation:property==SourcePosition?origin:
                            property==SourceSize?source:window_size;
            assert(property==BufferSize || property==SourcePosition || property==SourceSize || property==Size);
            out[0]=pair[0];out[1]=pair[1];
        }
        return result();
    }
    int screen_get_window_property_cv(void* w,int,int,char* out) {
        strcpy(out,w==child?output_id.c_str():"1282");return result();
    }
    int screen_post_window(void*,void*,int,const int*,int) { return result(); }
    int screen_get_event(void*,void*,uint64_t) {
        event_type=close_event?3:output_event?1:0; close_event=output_event=false;return result();
    }
    int screen_get_event_property_iv(void*,int,int* out) { *out=event_type;return result(); }
    int screen_get_event_property_pv(void*,int,void** out) { *out=child;return result(); }
    int screen_read_window(void* w,void*,int count,const int* rect,int) {
        assert(w==child && count==0 && !rect);++reads;
        assert(capture_usage==14); // READ | WRITE | NATIVE
        if(read_error)return -1;
        if(!no_pixels) {
            // Synthetic decoded allocation: poison padding, then paint the
            // active image at its reported offset. Screen's whole-window copy
            // applies that viewport, scales and converts into the capture pixmap.
            // Nearest-neighbour is only this fixture's sampling rule; production
            // delegates filtering to Screen and makes no quality guarantee here.
            const unsigned source_stride=allocation[0]*4+32;
            std::vector<uint8_t> decoded(source_stride*allocation[1],0xe7);
            for(int y=0;y<source[1];++y)for(int x=0;x<source[0];++x) {
                uint8_t* p=&decoded[(y+origin[1])*source_stride+(x+origin[0])*4];
                p[0]=x%256;p[1]=y%256;p[2]=input_count;p[3]=255;
            }
            for(int y=0;y<capture_size[1];++y)for(int x=0;x<capture_size[0];++x) {
                const unsigned sx=unsigned(x)*(source[0]-1)/(capture_size[0]-1);
                const unsigned sy=unsigned(y)*(source[1]-1)/(capture_size[1]-1);
                memcpy(&pixels[y*capture_stride+x*4],
                       &decoded[(sy+origin[1])*source_stride+(sx+origin[0])*4],4);
            }
        }
        return result();
    }
    int read_picture(void* window,void* buffer) { return screen_read_window(window,buffer,0,0,0); }
};
