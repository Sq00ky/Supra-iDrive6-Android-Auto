// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_AMBIENT_COLOUR_OBSERVER_HPP
#define NBT_AMBIENT_COLOUR_OBSERVER_HPP
#include "dsi_client.hpp"
#include "most/ids.hpp"
#include "most/msg.hpp"
#include "favourite_colour.hpp"
#include "favourite_press.hpp"
#include <pthread.h>
#include <time.h>
namespace nbt {
class AmbientColourObserver {
 DsiClient client_;pthread_mutex_t lock_;AmbientColour colour_;uint32_t changed_;FavouritePress press_;
 static uint32_t now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint32_t)t.tv_sec*1000u+t.tv_nsec/1000000u;}
 static void attached(DsiClient* c,void* self){
  AmbientColourObserver* a=(AmbientColourObserver*)self;
  pthread_mutex_lock(&a->lock_);a->colour_.valid=false;pthread_mutex_unlock(&a->lock_);
  std::vector<uint8_t> p;DsiClient::putString(p,"NBTAAD_AMBIENT");c->sendRequest(most::kReqRegisterClient,p);
  for(unsigned i=0;i<most::kClientSubscriptionCount;++i)c->setNotification(most::kClientSubscriptions[i]);
 }
 static void data(DsiClient* c,uint32_t,uint32_t id,uint32_t,const uint8_t* p,uint32_t len,void* self){
  AmbientColourObserver* a=(AmbientColourObserver*)self;
  if(id==most::kInfoClientResponse&&len>=6){
   uint32_t client=most::rdLe32(p);if(client==most::kClientRefused)return;
   // Fixed Get of the status property validated on this car. No setters,
   // device registration, controller keys or arbitrary car commands.
   most::Msg m;m.txType=0;m.varTag=1;m.varVal=0;m.dst=0x5000;m.src=most::rdLe16(p+4);
   m.fblock=0xc0;m.inst=0x0a;m.fkt=0xcd0;m.opType=most::kOpGet;m.hdrTag=0x414d0001;
   std::vector<uint8_t> req;most::putLe32(req,client);std::vector<uint8_t> body=most::buildMessage(m);
   req.insert(req.end(),body.begin(),body.end());
   printf("[Ambient] initial status Get sent=%d\n",c->sendRequest(most::kReqProcessMessage,req));
  }
  if(id!=most::kInfoMessage)return;
  most::View m;AmbientColour value;
  if(!most::parseView(p,len,m))return;
  pthread_mutex_lock(&a->lock_);a->press_.feed(m.fblock,m.inst,m.fkt,m.op,m.data,m.len);pthread_mutex_unlock(&a->lock_);
  if(!parseAmbientColour(m.fblock,m.inst,m.fkt,m.op,m.data,m.len,value))return;
  pthread_mutex_lock(&a->lock_);
  if(!a->colour_.valid||a->colour_.hue!=value.hue||a->colour_.scheme!=value.scheme){a->colour_=value;a->changed_=now();}
  pthread_mutex_unlock(&a->lock_);
 }
public:
 FavouritePress presses(){pthread_mutex_lock(&lock_);FavouritePress p=press_;pthread_mutex_unlock(&lock_);return p;}
 AmbientColourObserver():changed_(0){pthread_mutex_init(&lock_,NULL);}
 bool start(){client_.setQuiet(true);client_.setQuietTx(true);client_.setCallbacks(attached,data,this);
  return client_.open(most::kServerInterface,most::kServerRole,most::kInterfaceMajor,most::kInterfaceMinor);}
 AmbientColour current(uint32_t at){pthread_mutex_lock(&lock_);AmbientColour c=colour_;
  // The scheme and hue arrive in separate updates ~100 ms apart. Do not show
  // the previous hue under the newly selected scheme during that transition.
  if((uint32_t)(at-changed_)<150u)c.valid=false;
  pthread_mutex_unlock(&lock_);return c;}
};
}
#endif
