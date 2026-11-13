// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// BMW 6WB experimental profile, from JayJay3071/nbt-android-auto 0fb9fd2,
// docs/CLUSTER-HUDTERMINAL.md. These are observations on NBT x86, NOT F31 validation.
#ifndef CLUSTER_HUD6WB_PROFILE_HPP
#define CLUSTER_HUD6WB_PROFILE_HPP
#include "cluster/hud6wa_profile.hpp"
namespace nbt { namespace hudterm {
struct Surface {
    uint16_t a, b, width, height;
    Surface(uint16_t aa, uint16_t bb, uint16_t w, uint16_t h) : a(aa), b(bb), width(w), height(h) {}
};
struct Bmw6wbProfile {
    HudProfile base;
    Surface main, icons, lanes;
    uint16_t bar, frameWidth, frameHeight, barWidth, barHeight;
    uint8_t lineWidth, laneLineWidth, textEncoding;
    uint32_t vectorTx, headerTag, barRangeMetres;
    size_t maxBitmapBytes;
    bool roadShields,iconAntialias;
    unsigned roundaboutSegments; // default preserves the hardware-tested geometry
    Rect nextGlyph;
    Bmw6wbProfile() : main(0x1f,0x20,221,158), icons(0x18,0x19,305,44), lanes(0x23,0x24,283,40),
        bar(0x35), frameWidth(309), frameHeight(330), barWidth(28), barHeight(113),
        lineWidth(6), laneLineWidth(3), textEncoding(1), vectorTx(4), headerTag(0xffff),
        barRangeMetres(1000), maxBitmapBytes(4096), roadShields(false), iconAntialias(false), roundaboutSegments(24) {
        Rect next={90,3,126,39};nextGlyph=next;
        base.name="6wb"; base.frame=0x17; base.street=0x1a; base.distance=0x1b; base.unit=0x1c;
        base.bufA=main.a; base.bufB=main.b; base.fill=2;
        Rect c={0,0,220,157}; base.canvas=c;
        // Experimental placement below the perspective horizon; keep inside the documented image.
        Rect g={20,74,200,151}; base.glyph=g;
        base.streetChars=15; base.distanceChars=5; base.streetPunct="-./";
    }
};
inline bool validProfile(const Bmw6wbProfile& p) {
    const uint16_t h[]={p.base.frame,p.base.street,p.base.distance,p.base.unit,p.main.a,p.main.b,
        p.icons.a,p.icons.b,p.lanes.a,p.lanes.b,p.bar};
    for(size_t i=0;i<sizeof(h)/sizeof(h[0]);++i) {
        if(!h[i] || h[i]>255)return false;
        for(size_t j=0;j<i;++j)if(h[i]==h[j])return false;
    }
    const Surface s[]={p.main,p.icons,p.lanes};
    for(size_t i=0;i<3;++i)if(!s[i].width||!s[i].height||s[i].width>1024||s[i].height>1024)return false;
    return p.base.bufA==p.main.a && p.base.bufB==p.main.b && p.textEncoding==1 &&
        p.base.glyph.x0>=0 && p.base.glyph.y0>=0 && p.base.glyph.x1>p.base.glyph.x0 && p.base.glyph.y1>p.base.glyph.y0 &&
        p.base.glyph.x1<p.main.width && p.base.glyph.y1<p.main.height && p.lanes.width>=80 && p.lanes.height>=16 &&
        p.icons.width>=130 && p.icons.height>=44 && p.frameWidth && p.frameHeight && p.barWidth && p.barHeight &&
        p.roundaboutSegments>=24 && p.roundaboutSegments<=72 &&
        p.nextGlyph.x0>=0 && p.nextGlyph.y0>=0 && p.nextGlyph.x1>p.nextGlyph.x0 && p.nextGlyph.y1>p.nextGlyph.y0 &&
        p.nextGlyph.x1<p.icons.width && p.nextGlyph.y1<p.icons.height &&
        p.lineWidth>0 && p.laneLineWidth>0 && p.barRangeMetres>0 && p.maxBitmapBytes>=18 && p.maxBitmapBytes<=4096 &&
        p.base.streetChars>0 && p.base.streetChars<=255 && p.base.distanceChars>0 && p.base.distanceChars<=255;
}
inline HandleSet handlesOf(const Bmw6wbProfile& p) {
    HandleSet h=handlesOf(p.base);
    h.allow(p.icons.a); h.allow(p.icons.b); h.allow(p.lanes.a); h.allow(p.lanes.b); h.allow(p.bar);
    return h;
}

} }
#endif
