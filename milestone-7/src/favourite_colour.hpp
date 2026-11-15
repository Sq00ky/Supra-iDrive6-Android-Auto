// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_FAVOURITE_COLOUR_HPP
#define NBT_FAVOURITE_COLOUR_HPP
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
namespace nbt {
enum FavouriteColourMode { kFavouriteBMW=0,kFavouriteAmbient,kFavouriteBronze,kFavouriteWhite,
    kFavouriteOrange,kFavouriteBlue,kFavouriteGreen,kFavouriteLilac,kFavouriteCustom,kFavouriteColourCount };
static const char* const kFavouriteColourPath="/var/opt/aa/favourite-bar-colour";
static const char* const kFavouriteDisabledPath="/var/opt/aa/favourite-bar.disabled";
inline int loadFavouriteColour(const char* path=kFavouriteColourPath) {
 FILE* f=fopen(path,"r");if(!f)return kFavouriteBMW;
 int n=-1;char extra=0;int count=fscanf(f,"%d %c",&n,&extra);fclose(f);
 return count==1&&n>=0&&n<kFavouriteColourCount?n:kFavouriteBMW;
}
struct AmbientColour {unsigned hue,scheme;bool valid;AmbientColour():hue(0),scheme(0),valid(false){} };
inline AmbientColour loadFavouriteAmbient(){
 AmbientColour a;FILE* f=fopen("/dev/shmem/nbt-aa-ambient-colour","r");
 if(f){if(fscanf(f,"%u %u",&a.hue,&a.scheme)==2&&a.hue<=4095&&a.scheme<=15)a.valid=true;fclose(f);}return a;
}
inline bool parseAmbientColour(uint8_t fb,uint8_t inst,unsigned function,unsigned op,const uint8_t* p,size_t n,AmbientColour& out) {
 if(fb!=0xc0||inst!=0x0a||function!=0xcd0||op!=0xc||!p||n!=12)return false;
 unsigned hue=((unsigned)p[0]<<8)|p[1];if(hue>4095||p[2]>15)return false;
 out.hue=hue;out.scheme=p[2];out.valid=true;return true;
}
inline uint32_t favouriteHueRGB(unsigned hue) {
 if(hue>=360)return 0;
 unsigned sector=hue/60,t=(hue%60)*255/60;
 switch(sector){
 case 0:return 0xff0000u|(t<<8);
 case 1:return ((255-t)<<16)|0xff00u;
 case 2:return 0xff00u|t;
 case 3:return ((255-t)<<8)|0xffu;
 case 4:return (t<<16)|0xffu;
 default:return 0xff0000u|(255-t);
 }
}
// BMW LED-library RGB references, not fully saturated HSV approximations.
// White remains neutral on the display: LED white's green compensation is
// specific to the physical LEDs. RGB zero preserves the native BMW gradient.
inline uint32_t favouriteColourRGB(int mode,const AmbientColour& ambient) {
 if(mode==kFavouriteCustom){
  FILE* f=fopen("/var/opt/aa/favourite-bar-custom","r");unsigned rgb=0x2080d0;char extra;
  if(f){unsigned n;if(fscanf(f,"%x %c",&n,&extra)==1&&n<=0xffffff)rgb=n;fclose(f);}
  return rgb?rgb:0x1000000u; // Zero is reserved for the untouched native gradient.
 }
 if(mode==kFavouriteAmbient){
  if(!ambient.valid)return 0;
  switch(ambient.scheme){
   case 3:case 4:mode=kFavouriteBronze;break;
   case 5:case 6:mode=kFavouriteOrange;break;
   case 7:mode=kFavouriteWhite;break;
   case 8:case 9:mode=kFavouriteBlue;break;
   case 10:case 11:mode=kFavouriteGreen;break;
   case 12:case 13:mode=kFavouriteLilac;break;
   default:return 0;
  }
 }
 switch(mode){
 case kFavouriteBronze:return 0xd76e14u;
 case kFavouriteWhite:return 0xffffffu;
 case kFavouriteOrange:return 0x931a01u;
 case kFavouriteBlue:return 0x0020feu;
 case kFavouriteGreen:return 0x10fe32u;
 case kFavouriteLilac:return 0x783cfeu;
 default:return 0;
 }
}
// Canonical decoded strips are RGBA bytes (ABGR words); this unit's display
// buffer uses BGRA bytes. Correct the channel swap while applying an optional
// hue only to the native orange accent, preserving neutral labels and icons.
inline void renderFavouriteColour(uint32_t* dst,const uint32_t* src,size_t count,uint32_t rgb) {
 for(size_t i=0;i<count;++i){
  uint32_t p=src[i];unsigned r=p&255,g=(p>>8)&255,b=(p>>16)&255;
  if(rgb&&r>=16&&r*100>=g*95&&r>b+8&&g>=b+4){
   // Include yellow edge highlights and dim antialiased accent pixels.
   // Retain their neutral component so lettering stays legible.
   unsigned neutral=b,shade=r-b;
   r=neutral+((rgb>>16)&255)*shade/255;
   g=neutral+((rgb>>8)&255)*shade/255;
   b=neutral+(rgb&255)*shade/255;
  }
  dst[i]=(p&0xff000000u)|(r<<16)|(g<<8)|b;
 }
}
}
#endif
