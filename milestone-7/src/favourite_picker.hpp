// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_FAVOURITE_PICKER_HPP
#define NBT_FAVOURITE_PICKER_HPP
#include "favourite_colour.hpp"
#include <math.h>
namespace nbt {
static const char* const kPickerActivePath="/dev/shmem/nbt-aa-colour-picker.active";
static const char* const kPickerDonePath="/dev/shmem/nbt-aa-colour-picker.done";
// In-process mode (rhmiapp-87): the daemon owns the picker too. The plugin writes a request instead of
// spawning a helper, and the daemon's marker tells it that this is the case.
static const char* const kPickerRequestPath="/dev/shmem/nbt-aa-colour-picker.request";
static const char* const kInProcMarkerPath="/dev/shmem/nbt-aa-inproc-favourites";
inline unsigned pickerClamp(int n,unsigned max){return n<0?0:(unsigned)n>max?max:(unsigned)n;}
inline uint32_t pickerHSV(unsigned h,unsigned s,unsigned v){
 uint32_t pure=favouriteHueRGB(h%360);unsigned r=(pure>>16)&255,g=(pure>>8)&255,b=pure&255;
 r=(255-(255-r)*s/255)*v/255;g=(255-(255-g)*s/255)*v/255;b=(255-(255-b)*s/255)*v/255;
 return (r<<16)|(g<<8)|b;
}
struct FavouritePicker {
 unsigned hue,saturation,value;int drag;bool changed;
 FavouritePicker():hue(210),saturation(220),value(220),drag(0),changed(true){}
 void fromRGB(uint32_t rgb){
  int r=(rgb>>16)&255,g=(rgb>>8)&255,b=rgb&255;
  int hi=r>g?r:g;if(b>hi)hi=b;int lo=r<g?r:g;if(b<lo)lo=b;int delta=hi-lo;
  value=hi;saturation=hi?delta*255/hi:0;
  int h=!delta?0:hi==r?60*(g-b)/delta:hi==g?120+60*(b-r)/delta:240+60*(r-g)/delta;
  hue=(h+360)%360;
 }
 // Reference coordinates 1440x540, scaled from actual panel coordinates.
 // Touch-down chooses a control; moves remain captured to that control.
 int touch(int x,int y,int state){
  if(state==1){drag=x>=230&&x<=1030&&y>=105&&y<=465?1:x>=1080&&x<=1150&&y>=105&&y<=465?2:0;
   if(x>=1210&&x<=1400&&y>=305&&y<=370)return 1;
   if(x>=1210&&x<=1400&&y>=395&&y<=460)return 2;
  }
  if(state==1||state==2){
   if(drag==1){saturation=pickerClamp((x-230)*255/800,255);value=255-pickerClamp((y-105)*255/360,255);changed=true;}
   if(drag==2){hue=pickerClamp((y-105)*359/360,359);changed=true;}
  }
  if(state==3)drag=0;
  return 0;
 }
 uint32_t rgb()const{return pickerHSV(hue,saturation,value);}
};
int favouritePickerMain();
}
#endif
