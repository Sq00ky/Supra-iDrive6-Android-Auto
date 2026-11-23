// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Native sharp/ramp head clearance; no legacy centreline extrapolation or image fitting.
#include "cluster/hud_unified.hpp"
#include <algorithm>
#include <assert.h>
#include <stdio.h>
using namespace nbt::hudterm;

static Rect bounds(const Poly& p) {
    Rect r={32767,32767,-32768,-32768};
    for(size_t i=0;i<p.pts.size();++i) {
        r.x0=std::min(r.x0,p.pts[i].x);r.y0=std::min(r.y0,p.pts[i].y);
        r.x1=std::max(r.x1,p.pts[i].x);r.y1=std::max(r.y1,p.pts[i].y);
    }
    return r;
}
int main() {
    unsigned checked=0;
    for(int type=9;type<=24;++type) {
        if(type==11 || type==12)continue;
        bool mirror;const unified::Glyph* g=unified::glyph(type,mirror);
        assert(g);
        Picture far;unified::route(type,k6wa,0,far);
        assert(far.size()==4);
        for(int stage=1;stage<=4;++stage) {
            Picture route;unified::route(type,k6wa,stage,route);
            assert(route.size()==4);
            assert(addPolygon(5,0,kPolygon,route[3].poly.pts)==addPolygon(5,0,kPolygon,far[3].poly.pts));
            Rect h=bounds(route[3].poly),edge=bounds(route[1].poly);
            assert(edge.x0>=0 && edge.y0>=0 && edge.x1<=125 && edge.y1<=76);
            if(type==9 || type==10) {
                // The complete head (including its border) is clear of the incoming ten-pixel stem.
                assert(type==9 ? 69-edge.x1>=3 : edge.x0-55>=3);
            } else if(type>=21) {
                Picture roads;unified::roads(type,k6wa,true,roads);
                // White exit head clears the separate vertical highway (first road fill).
                Rect main=bounds(roads[roads.size()/2].poly);
                assert(type%2 ? main.x0-h.x1>=1 : h.x0-main.x1>=1);
            }
            ++checked;
        }
    }
    assert(checked==56);
    puts("HUD sharp turns and ramps: all 14 aliases / four stages, fixed unclipped heads and native clearance OK");
}
