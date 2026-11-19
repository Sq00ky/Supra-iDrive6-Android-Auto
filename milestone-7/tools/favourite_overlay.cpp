// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Live native favourite labels in an independent, transparent, full-panel Screen window.
// No writes to factory files, no native-window resizing and no AA resizing in normal operation.
#include "screen_video.h"
#include "favourite_bitmap.hpp"
#include "favourite_strip.hpp"
#include "favourite_ready.hpp"
#include "favourite_retry.hpp"
#include "favourite_colour.hpp"
#include "ambient_colour_observer.hpp"
#include "favourite_picker.hpp"
#include "favourite_release.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <spawn.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <vector>
extern char** environ;
static unsigned long ms() { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (unsigned long)t.tv_sec*1000+t.tv_nsec/1000000; }
static volatile sig_atomic_t running=1;
static void stop(int) { running=0; }
static const char* L="/opt/sys/bin/LayerManagerControl";
static const char* capture="/dev/shmem/nbt-aa-favourite-frame.bmp";
// Match the lean SSH capture environment; HMI's graphics preload is unnecessary for the CLI.
static int lmCommand(const char* const* input) {
 std::vector<char*> args,env;
 for(int i=0;input[i];i++)args.push_back(const_cast<char*>(input[i]));args.push_back(NULL);
 for(char** e=environ;e&&*e;e++)if(strncmp(*e,"LD_PRELOAD=",11))env.push_back(*e);env.push_back(NULL);
 posix_spawn_file_actions_t actions;
 if(posix_spawn_file_actions_init(&actions))return -1;
 int fd=open("/dev/null",O_WRONLY);if(fd<0){posix_spawn_file_actions_destroy(&actions);return -1;}
 posix_spawn_file_actions_adddup2(&actions,fd,1);posix_spawn_file_actions_adddup2(&actions,fd,2);
 pid_t pid=-1;int rc=posix_spawn(&pid,L,&actions,NULL,&args[0],&env[0]);
 posix_spawn_file_actions_destroy(&actions);close(fd);if(rc)return -1;
 int status=0;while(waitpid(pid,&status,0)<0){if(errno!=EINTR)return -1;}
 return WIFEXITED(status)?WEXITSTATUS(status):-1;
}
// popen() forks, and forking this daemon from a worker thread can deadlock (the thread simply never returns:
// that is why the overlay printed its start line and then nothing at all). Ask LayerManagerControl the same way
// lmCommand() does - posix_spawn - but let it write to a file we read afterwards.
static int lmCapture(const char* const* input, const char* outPath, char* buf, size_t bufsz) {
 if (buf && bufsz) buf[0] = 0;
 std::vector<char*> args, env;
 for (int i = 0; input[i]; i++) args.push_back(const_cast<char*>(input[i]));
 args.push_back(NULL);
 for (char** e = environ; e && *e; e++) if (strncmp(*e, "LD_PRELOAD=", 11)) env.push_back(*e);
 env.push_back(NULL);
 posix_spawn_file_actions_t actions;
 if (posix_spawn_file_actions_init(&actions)) return -1;
 int fd = open(outPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
 if (fd < 0) { posix_spawn_file_actions_destroy(&actions); return -1; }
 posix_spawn_file_actions_adddup2(&actions, fd, 1);
 posix_spawn_file_actions_adddup2(&actions, fd, 2);
 pid_t pid = -1;
 int rc = posix_spawn(&pid, input[0], &actions, NULL, &args[0], &env[0]);
 posix_spawn_file_actions_destroy(&actions);
 close(fd);
 if (rc) { printf("[Favourites] spawn %s failed rc=%d\n", input[0], rc); return -1; }
 int status = 0;
 while (waitpid(pid, &status, 0) < 0) if (errno != EINTR) return -1;
 if (buf && bufsz) {
  FILE* f = fopen(outPath, "r");
  if (f) { size_t n = fread(buf, 1, bufsz - 1, f); buf[n] = 0; fclose(f); }
 }
 return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
static void ownVisibility(bool visible) {
 unsigned long t=ms(); const char* args[]={L,"set","surface","1283","visibility",visible?"1":"0",NULL}; int rc=lmCommand(args); printf("TIMING %lu visibility=%d rc=%d duration=%lu\n",ms(),visible,rc,ms()-t);
}
static nbt::FavouriteRelease releaseCaptureState;
struct State { char text[16]; int pid,overlay,favourite; unsigned mask; };
static State state() {
 State s={{0},0,0,0,0}; FILE* f=fopen("/dev/shmem/nbt-aad-state","r");
    if(f) { if(fscanf(f,"%15s %d %d %d %u",s.text,&s.pid,&s.overlay,&s.favourite,&s.mask)!=5) s.pid=0; fclose(f); }
 s.mask=releaseCaptureState.update(s.pid,s.pid>0&&!strcmp(s.text,"connected")&&s.overlay>=1&&s.overlay<=5&&s.favourite==1,s.mask,(uint32_t)ms());
 return s;
}
static bool wanted(const State& s) { return s.pid>0&&!strcmp(s.text,"connected")&&s.overlay>=1&&s.overlay<=5&&s.favourite==1&&nbt::favouriteButtonFromMask(s.mask)>0; }
// While a finger slides 1-2-3-4 the mask can read 0 for a frame between keys. The daemon's peek flag stays set
// through that (450 ms release grace), so "armed" keeps the strip on screen instead of hiding and re-showing it.
static bool armed(const State& s) { return s.pid>0&&!strcmp(s.text,"connected")&&s.overlay>=1&&s.overlay<=5&&s.favourite==1; }
static void order(bool shown,int aa) {
 char cmd[512];
 snprintf(cmd,sizeof(cmd),"%s set layer 0x80000000 render order %s0x%x%s >/dev/null 2>&1; %s set surface 256 visibility %d >/dev/null 2>&1; %s set surface 1283 visibility %d >/dev/null 2>&1",
          L,shown?"0x501,0x503,":"0x501,",aa,shown?",0x100":"",L,shown?1:0,L,shown?1:0);
 system(cmd);
}
// Embedded (NBT_FAVOURITE_NO_MAIN, compiled into nbt-aad) or standalone (the old helper binary).
// favouriteOverlayStop() is the only way out of the loop when it runs as a thread: no signals there.
namespace nbt {
void favouriteOverlayStop() { running=0; }
int favouriteOverlayRun(int argc,char** argv) {
 running=1;
 if(argc>1&&!strcmp(argv[1],"picker"))return favouritePickerMain();
 bool follow=argc>1&&!strcmp(argv[1],"follow");
 int seconds=follow?0:90,owner=follow&&argc>3?atoi(argv[3]):0;
 bool compatibility=argc>1&&!strcmp(argv[1],"probe-v3");
 int aa=1282; FILE* af=fopen("/dev/shmem/nbt-aad-surface","r");if(af){fscanf(af,"%d",&aa);fclose(af);}
 printf("[Favourites] overlay start: aa surface=%d follow=%d owner=%d\n", aa, (int)follow, owner);
 // 1280 is CarPlay's window and 1281 the volume pill, so only refuse the impossible values here: a silent
 // return cost a whole debugging round when the file said 1280 on this unit.
 if(aa<1280||aa>65535||aa==1283) { printf("[Favourites] refusing aa surface %d (need 1280..65535, not 1283)\n", aa); return 1; }
 char cmd[1024],line[256];int w=0,h=0;long long existing=0;
 char ask[16], grey[8192];
 snprintf(ask,sizeof(ask),"%d",aa);
 const char* aaArgs[]={L,"get","surface",ask,NULL};
 printf("[Favourites] reading surface %d\n", aa);
 int arc=lmCapture(aaArgs,"/dev/shmem/nbt-aa-lm-aa.txt",grey,sizeof(grey));
 printf("[Favourites] surface %d read rc=%d\n", aa, arc);
 // Any "x=N, y=N" pair will do, and the largest one wins: `get surface` does not necessarily label it
 // "original size" the way `get layer` does, and a strict label match left the thread dead in setup.
 for(char* sp=grey; sp&&*sp; ) {
  char* nl=strchr(sp,'\n'); if(nl)*nl=0;
  int px=0,py=0;
  if(sscanf(sp,"%*[^x]x=%d, y=%d",&px,&py)==2 || (strstr(sp,"x=")&&sscanf(strstr(sp,"x="),"x=%d, y=%d",&px,&py)==2)) {
   if(px>w&&px<=4096&&py>0&&py<=2048){w=px;h=py;}
  }
  sp = nl ? nl+1 : NULL;
 }
 if(w<=0||h<=0) {
  const char* pv=getenv("NBT_FAVOURITE_PANEL");
  if(pv&&sscanf(pv,"%dx%d",&w,&h)==2) printf("[Favourites] panel from NBT_FAVOURITE_PANEL: %dx%d\n",w,h);
 }
 if(w<=0||h<=0) {
  printf("[Favourites] no panel size parsed; using 1280x480. lm output was [%s]\n",
         grey[0]?grey:"(empty)");
  w=1280;h=480;
 }
 if(w>4096||h>2048||h<240) {printf("[Favourites] panel %dx%d is not sane; staying out\n",w,h);return 1;}
 const char* ownArgs[]={L,"get","surface","1283",NULL};
 if(lmCapture(ownArgs,"/dev/shmem/nbt-aa-lm-1283.txt",grey,sizeof(grey))==0) {
  for(char* sp=grey; sp&&*sp; ) {
   char* nl=strchr(sp,'\n'); if(nl)*nl=0;
   char* k=strstr(sp,"native surface:"); if(k)existing=strtoll(k+15,NULL,10);
   sp = nl ? nl+1 : NULL;
  }
 }
 if(existing>0&&existing!=4294967295LL){printf("[Favourites] surface 1283 already owned (%lld); refusing\n", existing);return 1;}
 printf("[Favourites] panel %dx%d\n", w, h);
 int bar=nbt::favouriteStripRegion(w,h,true).y;
 screen_context_t ctx=0;screen_window_t win=0;
 if(screen_create_context(&ctx,0)||screen_create_window_type(&win,ctx,0)) {printf("Screen creation failed %d\n",errno);return 1;}
 screen_set_window_property_cv(win,SCREEN_PROPERTY_ID_STRING,4,"1283");
 int usage=SCREEN_USAGE_NATIVE|SCREEN_USAGE_WRITE,format=SCREEN_FORMAT_RGBA8888,sz[2]={w,h},pos[2]={0,0};
 screen_set_window_property_iv(win,SCREEN_PROPERTY_USAGE,&usage);
 screen_set_window_property_iv(win,SCREEN_PROPERTY_FORMAT,&format);
 // Unit ABI: 5 buffer dimensions, 40 window dimensions, 42 source dimensions.
 // The old shared header aliases all three to 40; a full-panel window hid that
 // mistake. A short window otherwise keeps its default full-panel source size.
 const int bufferSize=5,windowSize=40,sourceSize=42,sourcePosition=41;
 int before[2]={0,0};screen_get_window_property_iv(win,sourceSize,before);
 printf("Screen source before setup %dx%d\n",before[0],before[1]);
 if(screen_set_window_property_iv(win,bufferSize,sz)||
    screen_set_window_property_iv(win,windowSize,sz)||
    screen_set_window_property_iv(win,sourceSize,sz)||
    screen_set_window_property_iv(win,sourcePosition,pos)) {
  printf("strip dimensions failed errno=%d\n",errno);screen_destroy_window(win);screen_destroy_context(ctx);return 1;
 }
 screen_set_window_property_iv(win,SCREEN_PROPERTY_POSITION,pos);
 // One buffer, as in the build that displayed: the flash is fixed by not hiding between keys, not by
 // double buffering (the second buffer was NULL on this unit, which made every post fail silently).
 if(screen_create_window_buffers(win,1)) {printf("buffers failed %d\n",errno);screen_destroy_context(ctx);return 1;}
 int bs[2]={0,0},ws[2]={0,0},ss[2]={0,0};
 int br=screen_get_window_property_iv(win,bufferSize,bs);
 int wr=screen_get_window_property_iv(win,windowSize,ws);
 int sr=screen_get_window_property_iv(win,sourceSize,ss);
 printf("Screen strip buffer=%dx%d rc=%d window=%dx%d rc=%d source=%dx%d rc=%d\n",bs[0],bs[1],br,ws[0],ws[1],wr,ss[0],ss[1],sr);
 if(br||wr||sr||bs[0]!=w||bs[1]!=h||ws[0]!=w||ws[1]!=h||ss[0]!=w||ss[1]!=h) {
  puts("strip size verification failed");screen_destroy_window(win);screen_destroy_context(ctx);return 1;
 }
 void* buffers[2]={0,0};void* pixels[2]={0,0};
 screen_get_window_property_pv(win,SCREEN_PROPERTY_RENDER_BUFFERS,buffers);
 if(buffers[0])screen_get_buffer_property_pv(buffers[0],SCREEN_PROPERTY_POINTER,&pixels[0]);
 if(buffers[1])screen_get_buffer_property_pv(buffers[1],SCREEN_PROPERTY_POINTER,&pixels[1]);
 if(!pixels[0]) {puts("no buffer pointers");screen_destroy_context(ctx);return 1;}
 if(!pixels[1]) pixels[1]=pixels[0];
 printf("strip buffers: single (%p), strip %dx%d top=%d\n", buffers[0], w, h, bar);
 int cur=0;
 int rect[4]={0,0,w,h};memset(pixels[0],0,(size_t)w*h*4);
 screen_post_window(win,buffers[0],1,rect,0);usleep(150000);
 snprintf(cmd,sizeof(cmd),"%s set surface 1283 source region 0 0 %d %d >/dev/null 2>&1; %s set surface 1283 destination region 0 0 %d %d >/dev/null 2>&1; %s set surface 1283 visibility 0 >/dev/null 2>&1",L,w,h,L,w,h,L);
 system(cmd);
 int frame=0;bool shown=false,cached=false;
 nbt::FavouriteRetry retry;
 std::vector<uint32_t> buttonCache[8],pendingCache; int shownButton=0,pendingButton=0;
 std::vector<uint32_t> decoded((size_t)w*bar);
 std::vector<uint32_t> lastNative;
 nbt::AmbientColourObserver* ambient=new nbt::AmbientColourObserver;
 printf("[Ambient] observer started=%d\n",ambient->start());
 nbt::AmbientColour ambientColour;
 int colourMode=nbt::loadFavouriteColour();bool barEnabled=access(nbt::kFavouriteDisabledPath,F_OK)!=0;
 uint32_t settingsAt=0,paintedRGB=0xffffffffu;
 unsigned refreshMask=0;bool refreshDone=false;
 uint32_t seenPress=0,releasedAt=0;unsigned pressBudget=0;
 uint32_t seenContact=0;
 time_t started=time(NULL);
 printf("[Favourites] overlay live: %dx%d strip top=%d, entering loop\n",w,h,bar);
 while(running&&(!seconds||time(NULL)-started<seconds)) {
  State s=state();if(owner&&(s.pid!=owner||kill(owner,0)))break;
  uint32_t now=(uint32_t)ms();
  if(seenContact!=releaseCaptureState.contacts()){
   seenContact=releaseCaptureState.contacts();refreshDone=false;pendingCache.clear();pendingButton=0;
  }
  if(releaseCaptureState.pending()){refreshDone=false;pressBudget=0;}
  nbt::FavouritePress press=ambient->presses();
  if(press.serial!=seenPress){
   seenPress=press.serial;releasedAt=0;pressBudget=1;refreshDone=false;
   pendingCache.clear();pendingButton=0;retry.reset();
   // Keep the last valid cache as a fallback; a failed capture must not erase it.
   printf("TIMING %lu physical favourite press serial=%u mask=%u; bounded refresh\n",ms(),(unsigned)seenPress,press.last);
  }
  if(!settingsAt||(uint32_t)(now-settingsAt)>=200u){colourMode=nbt::loadFavouriteColour();barEnabled=access(nbt::kFavouriteDisabledPath,F_OK)!=0;settingsAt=now;}
  nbt::AmbientColour currentAmbient=ambient->current(now);
  if(currentAmbient.valid&&(!ambientColour.valid||ambientColour.hue!=currentAmbient.hue||ambientColour.scheme!=currentAmbient.scheme)){
   ambientColour=currentAmbient;
   FILE* f=fopen("/dev/shmem/nbt-aa-ambient-colour.next","w");
   if(f){fprintf(f,"%u %u\n",ambientColour.hue,ambientColour.scheme);if(!fclose(f))rename("/dev/shmem/nbt-aa-ambient-colour.next","/dev/shmem/nbt-aa-ambient-colour");}
  }
  uint32_t rgb=nbt::favouriteColourRGB(colourMode,ambientColour);
  bool show=barEnabled&&(wanted(s)||(shown&&armed(s))); static int previous=-1; if(previous!=(int)show) { printf("TIMING %lu wanted=%d cached=%d shown=%d\n",ms(),show,cached,shown); previous=show; }
  if(!show) {
   refreshMask=0;refreshDone=false;pressBudget=0;
   retry.reset();
   pendingCache.clear();pendingButton=0;
   if(shown) { if(compatibility&&s.overlay)order(false,aa);else ownVisibility(false); shown=false; }
   if(strcmp(s.text,"connected")) { cached=false; for(int k=0;k<8;++k)buttonCache[k].clear(); } // cache belongs to this phone session
   usleep(10000);continue;
  }
  int requested=nbt::favouriteButtonFromMask(s.mask);
  if(refreshMask!=s.mask){refreshMask=s.mask;refreshDone=false;pendingCache.clear();pendingButton=0;}
  if(shown&&requested>0&&shownButton!=requested) {
   // Swipe to another key: paint the next strip into the back buffer and post it, keeping the surface visible.
   if(!buttonCache[requested-1].empty()) {
    const int n=0;
    nbt::renderFavouriteColour((uint32_t*)pixels[n],&buttonCache[requested-1][0],(size_t)w*bar,rgb);
    if(screen_post_window(win,buffers[n],1,rect,0)==0) {
     cur=n;shownButton=requested;lastNative=buttonCache[requested-1];paintedRGB=rgb;
     printf("TIMING %lu swipe to favourite %d from cache (no visibility change)\n",ms(),requested);
    }
   } else shownButton=requested;        // nothing cached yet: the capture below fills it
  }
  if(shown&&paintedRGB!=rgb&&!lastNative.empty()){
   nbt::renderFavouriteColour((uint32_t*)pixels[cur],&lastNative[0],lastNative.size(),rgb);
   if(screen_post_window(win,buffers[cur],1,rect,0)==0)paintedRGB=rgb;
   printf("[Colour] mode=%d RGB=%06X ambient scheme=%u hue=%u\n",colourMode,(unsigned)rgb,ambientColour.scheme,ambientColour.hue);
  }
  if(!shown&&requested>0&&!buttonCache[requested-1].empty()&&!compatibility) {
   const int n=0;
   nbt::renderFavouriteColour((uint32_t*)pixels[n],&buttonCache[requested-1][0],(size_t)w*bar,rgb);
   State latest=state();
   if(armed(latest)&&barEnabled&&screen_post_window(win,buffers[n],1,rect,0)==0) {
    cur=n;ownVisibility(true);shown=true;shownButton=requested;frame++;
    lastNative=buttonCache[requested-1];paintedRGB=rgb;
    printf("TIMING %lu cached favourite %d shown; refreshing live labels\n",ms(),requested);
   }
  }
  {
   if(compatibility) {
    snprintf(cmd,sizeof(cmd),"%s set surface %d viewport 0 0 0 0 >/dev/null 2>&1; %s set surface %d source region 0 0 %d %d >/dev/null 2>&1; %s set surface %d destination region 0 0 %d %d >/dev/null 2>&1",L,aa,L,aa,w,h,L,aa,w,h);system(cmd);
   }
   // Native is rendered BEHIND full-size AA, only to supply the current pixels.
   if(compatibility&&shown) order(true,aa);
   else if(compatibility) {
    snprintf(cmd,sizeof(cmd),"%s set layer 0x80000000 render order 0x501,0x%x,0x100 >/dev/null 2>&1; %s set surface 256 visibility 1 >/dev/null 2>&1",L,aa,L);system(cmd);
    usleep(150000);
   }
  }
  // Toggle shortcuts can change their checkmark without a capacitive-touch
  // transition. Revalidate while held, retaining the displayed/cache buffer.
  if(releaseCaptureState.pending()&&!releaseCaptureState.due((uint32_t)ms())){usleep(10000);continue;}
  if(pressBudget){
   // The native toggle is committed on physical button release. Do not copy
   // its previous checkmark while the key is still down (touch may stay on).
   if(press.pressed&press.last){usleep(10000);continue;}
   if(!releasedAt)releasedAt=(uint32_t)ms();
   if((uint32_t)((uint32_t)ms()-releasedAt)<100u){usleep(10000);continue;}
  }
  if(refreshDone||!retry.ready((uint32_t)ms())) {usleep(10000);continue;}
  bool finalReleaseCapture=releaseCaptureState.pending();
  if(finalReleaseCapture){releaseCaptureState.consume();refreshDone=true;puts("final release capture; retain result before hiding");}
  unlink(capture);
  const char* captureArgs[]={L,"dump","surface","256","to",capture,NULL};
  unsigned long ct=ms(); int result=lmCommand(captureArgs); printf("TIMING %lu capture rc=%d duration=%lu\n",ms(),result,ms()-ct);FILE* bmp=result==0?fopen(capture,"rb"):NULL;
  const int n=0;bool ok=bmp&&nbt::favouriteBitmap(bmp,&decoded[0],w,h,bar,w,bar);if(bmp)fclose(bmp);
  bool pressCapture=pressBudget!=0;
  if(pressBudget&&--pressBudget==0){refreshDone=true;puts("press capture budget exhausted; valid cache retained until next press/touch");}
  // Recheck after capture: never show stale labels over a camera or after release.
  s=state();
  if((!wanted(s)&&!armed(s))||access(nbt::kFavouriteDisabledPath,F_OK)==0) { if(shown) { ownVisibility(false);shown=false; } continue; }
  if(ok)retry.reset();
  requested=nbt::favouriteButtonFromMask(s.mask);
  int selected=ok?nbt::favouriteSelectedButton(&decoded[0],w,bar,w):0;
  if(ok&&(selected==0||selected!=requested)) {
   static unsigned waiting=0; if((waiting++%10)==0)printf("TIMING %lu waiting for native favourite banner; waiting for matching selected button; header/other selection not displayed\n",ms());
   usleep(30000);continue;
  }
  if(ok) {
   // Validate in CPU memory before touching the sole displayed buffer. In
   // particular, a rejected app-header capture cannot overwrite a cached label.
   nbt::renderFavouriteColour((uint32_t*)pixels[n],&decoded[0],(size_t)w*bar,rgb);
   int prc=screen_post_window(win,buffers[n],1,rect,0);
   if(prc) printf("[Favourites] strip post failed rc=%d errno=%d\n", prc, errno);
   if(prc==0) {if(!shown) {if(compatibility)order(true,aa);else ownVisibility(true);puts("live native strip shown");}shown=true;shownButton=requested;cached=true;
    lastNative=decoded;paintedRGB=rgb;
    // A validated post-press snapshot replaces the cached checkmark immediately;
    // this path deliberately takes only one capture, not a settling pair.
    if(pressCapture||finalReleaseCapture)buttonCache[requested-1]=decoded;
    const uint32_t* strip=&decoded[0]; size_t count=(size_t)w*bar;
    // Cache a settled pair, never the first partial slide-in or an in-progress fade.
    if(pendingButton==requested&&pendingCache.size()==count&&
       !memcmp(&pendingCache[0],strip,count*sizeof(uint32_t))) {
     buttonCache[requested-1]=pendingCache;
     refreshDone=true;pressBudget=0;
     printf("TIMING %lu favourite %d settled; waiting for next press or touch\n",ms(),requested);
    }
    pendingCache.assign(strip,strip+count);pendingButton=requested;
    printf("TIMING %lu live favourite %d posted\n",ms(),requested);frame++;}
  } else {
   unsigned failures=retry.failed((uint32_t)ms());
   pendingCache.clear();pendingButton=0;
   if(failures<=3||failures%10==0)
    printf("TIMING %lu capture unavailable (%u); keeping matching cache and retrying; AA composition untouched\n",ms(),failures);
  }
  usleep(30000);
 }
 State s=state();if(s.overlay)order(false,aa);
 else {snprintf(cmd,sizeof(cmd),"%s set surface 1283 visibility 0 >/dev/null 2>&1",L);system(cmd);}
 unlink(capture);screen_destroy_window(win);screen_destroy_context(ctx);puts("overlay stopped; AA retained");return 0;
}
} // namespace nbt

#ifndef NBT_FAVOURITE_NO_MAIN
int main(int argc,char** argv) {
 setvbuf(stdout,NULL,_IOLBF,0); signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,SIG_IGN);
 return nbt::favouriteOverlayRun(argc,argv);
}
#endif
