// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_FAVOURITE_RELEASE_HPP
#define NBT_FAVOURITE_RELEASE_HPP
#include "favourite_ready.hpp"
namespace nbt {
// Retain only this session's selected key during the daemon's release grace.
// A final capture is armed once by touch -> release, never by repeated polls.
class FavouriteRelease {
 int owner_;unsigned last_,raw_;uint32_t at_,contacts_;bool pending_;
public:
 FavouriteRelease():owner_(0),last_(0),raw_(0),at_(0),contacts_(0),pending_(false){}
 unsigned update(int owner,bool eligible,unsigned raw,uint32_t now){
  if(owner!=owner_){owner_=owner;last_=raw_=0;pending_=false;}
  if(!eligible||(raw&&!favouriteButtonFromMask(raw))){last_=raw_=0;pending_=false;return raw;}
  if(raw){if(!raw_)++contacts_;last_=raw;raw_=raw;pending_=false;return raw;}
  if(raw_&&last_){at_=now;pending_=true;}
  raw_=0;return last_;
 }
 uint32_t contacts()const{return contacts_;}
 bool pending()const{return pending_;}
 bool due(uint32_t now)const{return pending_&&(uint32_t)(now-at_)>=200u;}
 void consume(){pending_=false;}
};
}
#endif
