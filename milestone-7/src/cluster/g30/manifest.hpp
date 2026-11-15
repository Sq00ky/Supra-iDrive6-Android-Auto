#pragma once
#include "protocol.hpp"
#include <string.h>
namespace aa_cluster {
// Preserve wire length and all unrelated fields byte for byte. A multi-byte
// protobuf zero is valid; this avoids depending on Dinkumware vector layouts.
struct Field {
    unsigned number,wire;size_t start,value,end;
};
inline bool next(const uint8_t* p,size_t n,size_t& pos,Field& f) {
    f.start=pos;uint64_t tag=0,value=0;unsigned shift=0;
    do { if(pos>=n||shift>63)return false;uint8_t b=p[pos++];tag|=uint64_t(b&127)<<shift;shift+=7;if(!(b&128))break; } while(true);
    if(tag<8||tag>0xffffffffu)return false;
    f.number=unsigned(tag)>>3;f.wire=unsigned(tag)&7;f.value=pos;
    if(f.wire==0||f.wire==2) {
        shift=0;
        do { if(pos>=n||shift>63)return false;uint8_t b=p[pos++];value|=uint64_t(b&127)<<shift;shift+=7;if(!(b&128))break; } while(true);
        if(f.wire==2) { if(value>n-pos)return false;f.value=pos;pos+=size_t(value); }
    } else if(f.wire==1||f.wire==5) { size_t len=f.wire==1?8:4;if(len>n-pos)return false;pos+=len; }
    else return false;
    f.end=pos;return true;
}
inline void zero(uint8_t* p,const Field& f) {
    for(size_t i=f.value;i<f.end;++i)p[i]=i+1==f.end?0:128;
}
inline bool rewrite_level(uint8_t* p,size_t n,unsigned level,unsigned& changed) {
    size_t i=0;Field f;
    while(i<n) {
        if(!next(p,n,i,f))return false;
        bool descend=false;
        if(level==0 && f.number==1 && f.wire==2 && field(p+f.value,f.end-f.value,1,0)==2) { descend=true;++changed; }
        if(level==1 && f.number==3 && f.wire==2)descend=true;
        if(level==2 && f.number==4 && f.wire==2)descend=true;
        if(level==3) {
            if((f.number==3||f.number==4)&&f.wire==0)zero(p,f);
            if(f.number==11&&f.wire==2)descend=true;
        }
        if(level==4 && f.number==1&&f.wire==2)descend=true;
        if(level==5&&f.number<=4&&f.wire==0)zero(p,f);
        if(descend&&!rewrite_level(p+f.value,f.end-f.value,level+1,changed))return false;
    }
    return true;
}
inline bool rewrite_manifest(const uint8_t* src,size_t n,uint8_t* dst,size_t capacity) {
    if(n<2||n>capacity||src[0]!=0||src[1]!=6)return false;
    memcpy(dst,src,n);unsigned changed=0;
    return rewrite_level(dst+2,n-2,0,changed)&&changed==1;
}
}
