// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Adapted algorithms: JayJay3071/nbt-android-auto 0fb9fd2 (see BMW_6WB.md).
#include "cluster/hud6wb_codec.hpp"
namespace nbt { namespace hudterm {
std::string toLatin15(const std::string& s, size_t maxChars) {
    std::string o;
    for (size_t i=0;i<s.size() && o.size()<maxChars;) {
        unsigned c=(unsigned char)s[i++], cp=c, more=0;
        if (c>=0xc2 && c<=0xdf) { cp=c&31; more=1; }
        else if (c>=0xe0 && c<=0xef) { cp=c&15; more=2; }
        else if (c>=0xf0 && c<=0xf4) { cp=c&7; more=3; }
        else if (c>=128) { o+='?'; continue; }
        if(i+more>s.size()) {o+='?';break;}
        bool valid=true;
        for (unsigned j=0;j<more && valid;++j) {
            unsigned b=(unsigned char)s[i];
            if ((b&0xc0)!=0x80) { valid=false; break; }
            ++i; cp=(cp<<6)|(b&63);
        }
        if (!valid || (more==1 && cp<0x80) || (more==2 && cp<0x800) ||
            (more==3 && cp<0x10000) || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) { o+='?'; continue; }
        unsigned b='?';
        if (cp>=32 && cp<127) b=cp;
        else if (cp>=160 && cp<=255 && cp!=164 && cp!=166 && cp!=168 && cp!=180 &&
                 cp!=184 && cp!=188 && cp!=189 && cp!=190) b=cp;
        else {
            static const unsigned unicode[]={0x20ac,0x160,0x161,0x17d,0x17e,0x152,0x153,0x178};
            static const unsigned latin[]={164,166,168,180,184,188,189,190};
            for (unsigned j=0;j<8;++j) if(cp==unicode[j]) b=latin[j];
        }
        o+=(char)b;
    }
    return o;
}
std::vector<uint8_t> latinText(uint16_t h,const std::string& s,size_t maxChars) {
    std::string t=toLatin15(s,maxChars>kMaxLatinTextBytes?kMaxLatinTextBytes:maxChars);
    std::vector<uint8_t> p;putBe16(p,h);p.push_back(1);
    p.insert(p.end(),t.begin(),t.end());p.push_back(0);return p;
}
std::vector<uint8_t> distanceBar(uint16_t h,int v) {
    std::vector<uint8_t> p;putBe16(p,h);putBe32(p,v<0?0:v>255?255:v);p.push_back(2);return p;
}
static void pixel(std::vector<uint8_t>& p,uint32_t c) {
    p.push_back((uint8_t)(c>>24));p.push_back((uint8_t)c);
    p.push_back((uint8_t)(c>>8));p.push_back((uint8_t)(c>>16));
}
std::vector<uint8_t> rle32(const std::vector<uint32_t>& px) {
    std::vector<uint8_t> p;size_t i=0;
    while(i<px.size()) {
        size_t n=1;while(i+n<px.size() && n<127 && px[i+n]==px[i]) ++n;
        if(n>=2) {p.push_back((uint8_t)(0x80|n));pixel(p,px[i]);i+=n;continue;}
        size_t start=i++;
        while(i<px.size() && i-start<127) {
            if(i+1<px.size() && px[i]==px[i+1]) break;
            ++i;
        }
        p.push_back((uint8_t)(i-start));for(size_t k=start;k<i;++k)pixel(p,px[k]);
    }
    // Original compressor always appends padding, including for an aligned stream.
    size_t pad=8-p.size()%8;p.insert(p.end(),pad,0);return p;
}
std::vector<uint8_t> bitmap(uint16_t h,uint8_t id,const std::vector<uint32_t>& px,
                            unsigned w,unsigned height,size_t cap) {
    std::vector<uint8_t> p;
    if(!w || !height || w>1024 || height>1024 || px.size()!=(size_t)w*height) return p;
    std::vector<uint8_t> r=rle32(px);
    if(r.size()+10>cap) return p;
    putBe16(p,h);putBe16(p,0);putBe16(p,0);
    p.push_back(1);p.push_back(1);p.push_back(0);p.push_back(id);
    p.insert(p.end(),r.begin(),r.end());return p;
}
} }
