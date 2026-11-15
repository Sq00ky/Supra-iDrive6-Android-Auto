// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_FAVOURITE_BITMAP_HPP
#define NBT_FAVOURITE_BITMAP_HPP
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <vector>
namespace nbt {
inline uint32_t bmp32(const unsigned char* p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
// Decode only the native top rows; every remaining overlay pixel is transparent.
inline bool favouriteBitmap(FILE* f,uint32_t* dst,int width,int height,int bar,int stride,int outputRows=0) {
 unsigned char h[54];
 if(!f||!dst||width<=0||height<=0||width>4096||height>2048||stride<width||bar<=0||bar>height) return false;
 if(!outputRows)outputRows=height;
 if(outputRows<bar||outputRows>height)return false;
 if(fread(h,1,54,f)!=54||h[0]!='B'||h[1]!='M'||bmp32(h+14)<40) return false;
 int w=(int32_t)bmp32(h+18), sh=(int32_t)bmp32(h+22);
 int bpp=h[28]|(h[29]<<8);
 if(w!=width||(sh!=height&&sh!=-height)||(bpp!=24&&bpp!=32)||h[26]!=1||h[27]!=0||bmp32(h+30)!=0) return false;
 uint32_t offset=bmp32(h+10); if(offset<54||offset>4096) return false;
 int row=((width*(bpp/8)+3)/4)*4;
 std::vector<unsigned char> bytes(row);
 for(int y=0;y<outputRows;y++) memset(dst+(size_t)y*stride,0,width*4);
 for(int y=0;y<bar;y++) {
  int by=sh<0?y:height-1-y;
  if(fseek(f,offset+(long)by*row,SEEK_SET)||fread(&bytes[0],1,row,f)!=(size_t)row) return false;
  for(int x=0;x<width;x++) {
   const unsigned char* p=&bytes[x*(bpp/8)];
   dst[(size_t)y*stride+x]=0xff000000u|((uint32_t)p[0]<<16)|((uint32_t)p[1]<<8)|p[2];
  }
 }
 return true;
}
}
#endif
