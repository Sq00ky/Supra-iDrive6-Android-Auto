#pragma once
// Screen test double for public Session lifecycle tests; no host UI/firmware.
namespace screen_peer {
static int display_count, posts, destroys, props[100][2];
static int contexts, windows;
static bool fail_buffers, fail_post;
static void* display;
static uint8_t pixels[800*480*4];
static void reset() {
    display_count=2;posts=destroys=0;display=0;
    contexts=windows=0;fail_buffers=fail_post=false;
    for(unsigned i=0;i<sizeof(props)/sizeof(int);++i)((int*)props)[i]=0;
}
}
extern "C" int screen_create_context(void** p,int) { *p=(void*)1;++screen_peer::contexts;return 0; }
extern "C" int screen_destroy_context(void*) { ++screen_peer::destroys;--screen_peer::contexts;return 0; }
extern "C" int screen_get_context_property_iv(void*,int,int* p) { *p=screen_peer::display_count;return 0; }
extern "C" int screen_get_context_property_pv(void*,int,void** p) { p[0]=(void*)2;p[1]=(void*)3;return 0; }
extern "C" int screen_get_display_property_iv(void* d,int,int* p) { p[0]=d==(void*)3?800:1440;p[1]=d==(void*)3?480:540;return 0; }
extern "C" int screen_create_window(void** p,void*) { *p=(void*)4;++screen_peer::windows;return 0; }
extern "C" int screen_destroy_window(void*) { ++screen_peer::destroys;--screen_peer::windows;return 0; }
extern "C" int screen_set_window_property_iv(void*,int prop,const int* p) {
    screen_peer::props[prop][0]=p[0];
    if(prop==5 || prop==35 || prop==40 || prop==42)screen_peer::props[prop][1]=p[1];return 0;
}
extern "C" int screen_set_window_property_pv(void*,int,void** p) { screen_peer::display=*p;return 0; }
extern "C" int screen_set_window_property_cv(void*,int,int,const char*) { return 0; }
extern "C" int screen_get_window_property_iv(void*,int prop,int* p) {
    if(prop==53){*p=1;return 0;}*p=screen_peer::props[prop][0];
    if(prop==5 || prop==35 || prop==40 || prop==42)p[1]=screen_peer::props[prop][1];return 0;
}
extern "C" int screen_get_window_property_pv(void*,int prop,void** p) { *p=prop==11?screen_peer::display:(void*)screen_peer::pixels;return 0; }
extern "C" int screen_create_window_buffers(void*,int) { return screen_peer::fail_buffers?-1:0; }
extern "C" int screen_get_buffer_property_iv(void*,int prop,int* p) {
    if(prop==44)*p=3200;else if(prop==14)*p=8;else {p[0]=800;p[1]=480;}return 0;
}
extern "C" int screen_get_buffer_property_pv(void*,int,void** p) { *p=screen_peer::pixels;return 0; }
extern "C" int screen_post_window(void*,void*,int,const int*,int) { if(screen_peer::fail_post)return -1;++screen_peer::posts;return 0; }
extern "C" int screen_flush_context(void*,int) { return 0; }
