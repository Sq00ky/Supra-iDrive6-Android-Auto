#pragma once
#include <stdint.h>
#include <stddef.h>
namespace aa_cluster {
enum { Port=34980, HeaderSize=24, MaxPayload=512*1024, MaxConfig=64*1024, QueueSize=256, QueueBytes=4*1024*1024 };
enum Kind { Reset=1, Video=2, Stop=3 };
inline uint32_t be32(const uint8_t* p) { return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
inline void put32(uint8_t* p,uint32_t n) { p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n; }
inline uint64_t be64(const uint8_t* p) { return uint64_t(be32(p))<<32|be32(p+4); }
inline void header(uint8_t* p,unsigned kind,unsigned epoch,unsigned len,uint64_t pts) {
    put32(p,0x41435031);put32(p+4,kind);put32(p+8,epoch);put32(p+12,len);
    put32(p+16,pts>>32);put32(p+20,uint32_t(pts));
}
inline bool valid_header(const uint8_t* p) {
    unsigned k=be32(p+4),n=be32(p+12);
    return be32(p)==0x41435031 && k>=Reset && k<=Stop &&
        (k==Video?(n>0&&n<=MaxPayload):n==0);
}
inline bool annexb_idr(const uint8_t* p,size_t n) {
    for(size_t i=2;i+1<n;++i) if(p[i]==1 && p[i-1]==0 && p[i-2]==0 && (p[i+1]&31)==5) return true;
    return false;
}
// Bounded protobuf varint reader for AA control fields (not media bytes).
inline unsigned field(const uint8_t* p,size_t n,unsigned wanted,unsigned fallback) {
    size_t i=0;
    while(i<n) {
        uint64_t tag=0,val=0; unsigned shift=0;
        do { if(i==n||shift>63) return fallback; uint8_t b=p[i++]; tag|=uint64_t(b&127)<<shift;shift+=7;if(!(b&128))break; } while(true);
        unsigned wire=unsigned(tag)&7;
        if(wire==0||wire==2) {
            shift=0;
            do { if(i==n||shift>63) return fallback; uint8_t b=p[i++];val|=uint64_t(b&127)<<shift;shift+=7;if(!(b&128))break; } while(true);
            if(wire==0 && tag>>3==wanted) return val>0xffffffffu?fallback:unsigned(val);
            if(wire==2) { if(val>n-i)return fallback;i+=size_t(val); }
        } else if(wire==1||wire==5) { size_t skip=wire==1?8:4;if(skip>n-i)return fallback;i+=skip; }
        else return fallback;
    }
    return fallback;
}
}
