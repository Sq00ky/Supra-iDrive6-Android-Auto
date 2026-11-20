// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "favourite_picker.hpp"
#include "touch_frame.hpp"
#include "screen_video.h"
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>
namespace nbt {
namespace {
volatile sig_atomic_t alive=1;
void stopPicker(int){alive=0;}
const char* LM="/opt/sys/bin/LayerManagerControl";
// Compact, readable 5x7 capitals; no numeric-entry UI.
const unsigned char letters[26][7]={
{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
{14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
{17,17,10,4,4,4,4},{31,1,2,4,8,16,31}};
struct Canvas {
 uint32_t* p;int w,h,stride;
 void box(int x,int y,int width,int height,uint32_t c){
  int x0=x*w/1440,y0=y*h/540,x1=(x+width)*w/1440,y1=(y+height)*h/540;
  for(int j=y0<0?0:y0;j<y1&&j<h;++j)for(int i=x0<0?0:x0;i<x1&&i<w;++i)p[j*stride+i]=0xff000000u|c;
 }
 void text(int x,int y,const char* s,int size=3){
  for(;*s;++s,x+=6*size)if(*s>='A'&&*s<='Z')for(int j=0;j<7;++j)for(int i=0;i<5;++i)
   if(letters[*s-'A'][j]&(1<<(4-i)))box(x+i*size,y+j*size,size,size,0xeeeeee);
 }
 void outline(int x,int y,int w,int h,uint32_t c){box(x,y,w,2,c);box(x,y+h-2,w,2,c);box(x,y,2,h,c);box(x+w-2,y,2,h,c);}
 void draw(const FavouritePicker& f){
  box(0,0,1440,540,0x11161c);text(230,35,"CUSTOM COLOR",4);
  text(230,490,"DRAG TO SELECT",2);
  for(int y=105;y<=465;++y){
   unsigned v=255-(y-105)*255/360;
   for(int x=230;x<=1030;++x)box(x,y,1,1,pickerHSV(f.hue,(x-230)*255/800,v));
   box(1080,y,70,1,pickerHSV((y-105)*359/360,255,255));
  }
  outline(228,103,804,364,0x8d969f);outline(1078,103,74,364,0x8d969f);
  int px=230+f.saturation*800/255,py=105+(255-f.value)*360/255;
  outline(px-8,py-8,17,17,0xffffff);outline(px-6,py-6,13,13,0x000000);
  int hy=105+f.hue*360/359;box(1075,hy-3,80,6,0xffffff);box(1080,hy-1,70,2,0x111111);
  box(1210,105,190,145,f.rgb());outline(1208,103,194,149,0x8d969f);
  box(1210,305,190,65,0x30495f);text(1265,327,"SAVE",3);
  box(1210,395,190,65,0x30373e);text(1250,417,"CANCEL",3);
 }
};
bool saveValue(const char* path,unsigned value,const char* format){
 char next[160];snprintf(next,sizeof(next),"%s.next",path);FILE* f=fopen(next,"w");if(!f)return false;
 bool ok=fprintf(f,format,value)>0;if(fclose(f))ok=false;
 if(ok&&rename(next,path)==0)return true;unlink(next);return false;
}
void visibility(bool show){char cmd[240];snprintf(cmd,sizeof(cmd),"%s set surface 1284 visibility %d >/dev/null 2>&1",LM,show?1:0);system(cmd);}
}
int favouritePickerMain(){
 setvbuf(stdout,NULL,_IOLBF,0);signal(SIGTERM,stopPicker);signal(SIGINT,stopPicker);signal(SIGHUP,SIG_IGN);
 if(access(kPickerActivePath,F_OK))return 1;
 // AA releases this port when iDrive owns the screen. Connect only to the
 // saved touch controller, leaving gesture-camera traffic with native iDrive.
 FILE* peer=fopen(dataFile("touch-peer").c_str(),"r");if(!peer&&packaged())peer=fopen(handDataFile("touch-peer").c_str(),"r");
 char ip[64]={0};if(!peer)return 2;
 fgets(ip,sizeof(ip),peer);fclose(peer);char* colon=strchr(ip,':');if(!colon)return 2;*colon++=0;
 int sock=socket(AF_INET,SOCK_DGRAM,0);if(sock<0)return 2;
 int one=1;setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
#ifdef SO_REUSEPORT
 setsockopt(sock,SOL_SOCKET,SO_REUSEPORT,&one,sizeof(one));
#endif
 sockaddr_in addr;memset(&addr,0,sizeof(addr));addr.sin_family=AF_INET;addr.sin_port=htons(30567);addr.sin_addr.s_addr=INADDR_ANY;
 if(bind(sock,(sockaddr*)&addr,sizeof(addr))){close(sock);return 2;}
 addr.sin_port=htons(atoi(colon));if(!inet_aton(ip,&addr.sin_addr)||connect(sock,(sockaddr*)&addr,sizeof(addr))){close(sock);return 2;}
 int w=1440,h=540;char cmd[512],line[256];
 snprintf(cmd,sizeof(cmd),"%s get surface 256 2>/dev/null",LM);FILE* q=popen(cmd,"r");
 if(q){while(fgets(line,sizeof(line),q)){char* a=strstr(line,"original size:");if(a)sscanf(a,"original size: x=%d, y=%d",&w,&h);}pclose(q);}
 if(w<640||w>4096||h<240||h>2048){close(sock);return 3;}
 screen_context_t ctx=0;screen_window_t win=0;screen_buffer_t buffer=0;void* pixels=0;
 if(screen_create_context(&ctx,0)||screen_create_window_type(&win,ctx,0)){close(sock);return 3;}
 screen_set_window_property_cv(win,SCREEN_PROPERTY_ID_STRING,4,"1284");
 int usage=SCREEN_USAGE_NATIVE|SCREEN_USAGE_WRITE,format=SCREEN_FORMAT_RGBA8888,size[2]={w,h},pos[2]={0,0};
 screen_set_window_property_iv(win,SCREEN_PROPERTY_USAGE,&usage);screen_set_window_property_iv(win,SCREEN_PROPERTY_FORMAT,&format);
 bool ok=!screen_set_window_property_iv(win,5,size)&&!screen_set_window_property_iv(win,40,size)&&!screen_set_window_property_iv(win,42,size)&&!screen_set_window_property_iv(win,41,pos)&&!screen_set_window_property_iv(win,35,pos)&&!screen_create_window_buffers(win,1);
 if(ok){screen_get_window_property_pv(win,SCREEN_PROPERTY_RENDER_BUFFERS,&buffer);if(buffer)screen_get_buffer_property_pv(buffer,SCREEN_PROPERTY_POINTER,&pixels);}
 if(!ok||!pixels){screen_destroy_window(win);screen_destroy_context(ctx);close(sock);return 3;}
 int stride=0;screen_get_buffer_property_iv(buffer,SCREEN_PROPERTY_STRIDE,&stride);if(stride<w*4)stride=w*4;
 FavouritePicker picker;FILE* saved=fopen(favouriteCustomPath().c_str(),"r");unsigned rgb;
 if(saved){if(fscanf(saved,"%x",&rgb)==1&&rgb<=0xffffff)picker.fromRGB(rgb);fclose(saved);}
 Canvas canvas={(uint32_t*)pixels,w,h,stride/4};int rect[4]={0,0,w,h};canvas.draw(picker);screen_post_window(win,buffer,1,rect,0);usleep(150000);
 snprintf(cmd,sizeof(cmd),"%s set surface 1284 source region 0 0 %d %d >/dev/null 2>&1; %s set surface 1284 destination region 0 0 %d %d >/dev/null 2>&1; %s set layer 0x80000000 render order 0x504,0x100 >/dev/null 2>&1",LM,w,h,LM,w,h,LM);system(cmd);visibility(true);
 printf("picker ready %dx%d\n",w,h);
 TouchReassembler reassembler;time_t last=time(NULL);int result=0;bool finish=false;
 // No live car commands; a focus loss always closes the surface and socket.
 while(alive&&!access(kPickerActivePath,F_OK)&&time(NULL)-last<180&&!finish){
  fd_set rf;FD_ZERO(&rf);FD_SET(sock,&rf);timeval tv={0,50000};int rc=select(sock+1,&rf,NULL,NULL,&tv);
  if(rc>0){unsigned char raw[2048];int n=recv(sock,raw,sizeof(raw),0);if(n<=0)continue;
   TouchMsg msgs[kTouchMaxMsgs];int nm=reassembler.feed(raw,n,msgs,kTouchMaxMsgs);
   for(int m=0;m<nm&&!finish;++m){TouchContact contacts[kTouchMaxContacts];int declared=0,nc=parseTouchStream(msgs[m],contacts,kTouchMaxContacts,&declared);
    if(nc==1){last=time(NULL);result=picker.touch(contacts[0].x*1440/w,contacts[0].y*540/h,contacts[0].state);
     if(result==1){if(saveValue(favouriteCustomPath().c_str(),picker.rgb(),"%06X\n")&&saveValue(favouriteColourPath().c_str(),kFavouriteCustom,"%u\n"))finish=true;else puts("picker save failed");}
     else if(result==2)finish=true;
    }
   }
  }
  if(picker.changed){canvas.draw(picker);screen_post_window(win,buffer,1,rect,0);picker.changed=false;}
 }
 visibility(false);screen_destroy_window(win);screen_destroy_context(ctx);close(sock);
 if(finish||time(NULL)-last>=180){FILE* f=fopen(kPickerDonePath,"w");if(f){fputs("done\n",f);fclose(f);}}
 puts("picker closed; native touch restored");return 0;
}
}
