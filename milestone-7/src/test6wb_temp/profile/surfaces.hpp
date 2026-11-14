// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Profile derived from NBT-AAD hud6wb_profile.hpp; see test6wb_temp/PROVENANCE.md.
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef TEST6WB_SURFACES_HPP
#define TEST6WB_SURFACES_HPP
#include "cluster/hud_terminal.hpp"
#include <stddef.h>
namespace test6wb {
enum Kind { Container, Bitmap, Text, Vector, Bar };
struct Surface { unsigned handle, partner, width, height; Kind kind; const char* name; };
static const Surface surfaces[] = {
    {0x17,0,309,330,Container,"navigation container"},
    {0x18,0x19,305,44,Bitmap,"icons A"}, {0x19,0x18,305,44,Bitmap,"icons B"},
    {0x1a,0,0,0,Text,"street"}, {0x1b,0,0,0,Text,"distance"}, {0x1c,0,0,0,Text,"unit"},
    {0x1f,0x20,221,158,Vector,"main A"}, {0x20,0x1f,221,158,Vector,"main B"},
    {0x23,0x24,283,40,Vector,"lanes A"}, {0x24,0x23,283,40,Vector,"lanes B"},
    {0x35,0,28,113,Bar,"approach bar"}
};
static const size_t surfaceCount=sizeof(surfaces)/sizeof(surfaces[0]);
inline const Surface* surface(unsigned h) {
    for(size_t i=0;i<surfaceCount;++i) if(surfaces[i].handle==h)return &surfaces[i];
    return NULL;
}
inline nbt::hudterm::Config configuration() {
    nbt::hudterm::Config c;
    for(size_t i=0;i<surfaceCount;++i)c.handles.allow(surfaces[i].handle);
    c.navFrame=0x17;c.txType=4;c.fixedTag=0xffff;c.useFixedTag=true;
    c.strictAnswers=true;c.acceptMirroredAnswers=true;c.extendedCalls=true;
    c.ignoreFactoryNav=true;c.initialNavActive=false;
    c.replyMs=3000;c.sizeReplyMs=5000;c.pacedGraphics=true;
    c.graphicIntervalMs=15;c.graphicReplyMs=8000;c.maxInFlight=1;c.maxBatch=128;
    return c;
}
// Application capability gate is stricter than the upstream payload validator.
inline bool compatible(const nbt::hudterm::Call& c) {
    using namespace nbt::hudterm;
    const Surface* s=surface(c.handle());
    if(!s || !callOk(c,configuration().handles,true))return false;
    switch(c.fkt) {
    case kGetSize:return s->kind!=Text;
    case kSetVisibility:return true;
    case kSwitchVisibility:return s->partner && c.payload.size()==4 &&
        nbt::hudterm::rdBe16(&c.payload[2])==s->partner;
    case kRemoveAllItems:case kSetFillColor:case kSetLineWidth:case kAddPolygon:
        return s->kind==Vector;
    case kTextareaSetText: return s->kind==Text;
    case 0x227:return s->kind==Bar;
    case 0xc05:return s->kind==Bitmap && c.txType==0;
    default:return false;
    }
}
}
#endif
