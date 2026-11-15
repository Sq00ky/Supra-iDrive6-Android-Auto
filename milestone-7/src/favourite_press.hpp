// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_FAVOURITE_PRESS_HPP
#define NBT_FAVOURITE_PRESS_HPP
#include <stdint.h>
#include <stddef.h>
namespace nbt {
// Car capture: 15/0C/C02 Status, one byte/key: 0 released, 1 touched,
// 2 physically pressed. Count rising presses, including repeats without lift.
struct FavouritePress {
 uint32_t serial;unsigned pressed,last;
 FavouritePress():serial(0),pressed(0),last(0){}
 bool feed(unsigned fb,unsigned inst,unsigned fkt,unsigned op,const uint8_t* p,size_t n){
  if(fb!=0x15||inst!=0x0c||fkt!=0xc02||op!=0xc||!p||n!=8)return false;
  unsigned mask=0;for(unsigned i=0;i<8;++i){if(p[i]>2)return false;if(p[i]==2)mask|=1u<<i;}
  unsigned rise=mask&~pressed;pressed=mask;
  if(rise){last=rise;++serial;return true;}return false;
 }
};
}
#endif
