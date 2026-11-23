// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Sharp-turn and ramp lead geometry and its head clearance on the measured 126x77 HUD. No car or transport is needed.
#include "cluster/hudterm_junction.hpp"
#include "cluster/hud6wa_profile.hpp"
#include <algorithm>
#include <assert.h>
#include <math.h>
#include <stdio.h>

using namespace nbt::hudterm;

static bool samePoint(const Point& a, const Point& b) { return a.x==b.x && a.y==b.y; }
static bool samePath(const Path& a, const Path& b) {
    if(a.heads!=b.heads || a.pts.size()!=b.pts.size())return false;
    for(size_t i=0;i<a.pts.size();++i)if(!samePoint(a.pts[i],b.pts[i]))return false;
    return true;
}

static void preserved(const Layout& old, const Layout& now) {
    assert(old.roads.size()==now.roads.size());
    for(size_t i=0;i<old.roads.size();++i)assert(samePath(old.roads[i],now.roads[i]));
    assert(old.ring==now.ring && old.cx==now.cx && old.cy==now.cy && old.r==now.r);
    assert(old.ex==now.ex && old.ey==now.ey && old.maxTail==now.maxTail && old.exitNumber==now.exitNumber);
    assert(old.route.heads==now.route.heads && old.route.pts.size()==now.route.pts.size());
    for(size_t i=0;i+1<old.route.pts.size();++i)assert(samePoint(old.route.pts[i],now.route.pts[i]));
}

static long cross(const Point& a,const Point& b,const Point& c) {
    return(long)(b.x-a.x)*(c.y-a.y)-(long)(b.y-a.y)*(c.x-a.x);
}
static void simple(const Poly& p) {
    assert(p.pts.size()>=3);
    long area=0;
    const size_t n=p.pts.size();
    for(size_t i=0;i<n;++i) {
        const size_t next=i+1<n?i+1:0;
        area+=(long)p.pts[i].x*p.pts[next].y-(long)p.pts[next].x*p.pts[i].y;
        for(size_t j=i+2;j<n;++j) {
            if(i==0 && j+1==n)continue;
            const Point& a=p.pts[i];const Point& b=p.pts[next];
            const Point& c=p.pts[j];const Point& d=p.pts[j+1<n?j+1:0];
            const long x=cross(a,b,c),y=cross(a,b,d),u=cross(c,d,a),v=cross(c,d,b);
            assert(!(((x<0 && y>0)||(x>0 && y<0)) && ((u<0 && v>0)||(u>0 && v<0))));
        }
    }
    assert(area!=0);
}

static Rect bounds(const Poly& p) {
    Rect r={32767,32767,-32768,-32768};
    for(size_t i=0;i<p.pts.size();++i) {
        r.x0=std::min(r.x0,p.pts[i].x);r.y0=std::min(r.y0,p.pts[i].y);
        r.x1=std::max(r.x1,p.pts[i].x);r.y1=std::max(r.y1,p.pts[i].y);
    }
    return r;
}

static void checkFamilies() {
    unsigned changed=0;
    for(int type=0;type<=60;++type) {
        Layout old;
        if(!junctionLayout(type,true,90,true,5,k6wa.glyph,k6wa.canvas.y1,old))continue;
        Layout now=old;lengthenHudRoute(type,now);preserved(old,now);
        const int family=nbt::kombinav::kombiDirection(type);
        const bool sharp=family==4 || family==5;
        const bool ramp=family==25 || family==26 || family==28 || family==29;
        if(sharp || ramp) {
            assert(!samePoint(old.route.pts.back(),now.route.pts.back()));
            const Point& bend=now.route.pts[now.route.pts.size()-2];
            assert(hypot(now.route.pts.back().x-bend.x,now.route.pts.back().y-bend.y)>=(sharp?63:53));
            ++changed;
        } else assert(samePath(old.route,now.route));
        Layout again=now;lengthenHudRoute(type,again);
        assert(samePath(now.route,again.route)); // safe to apply again when the view is rebuilt
        for(int stage=1;stage<=4;++stage)assert(tailFor(old,stage,4)==tailFor(now,stage,4));
    }
    assert(changed==14); // sharp9/10, on-ramp aliases13..20 and exit aliases21..24
}

static void checkHeads() {
    HudProfile profile=k6wa;fitMeasured(126,77,profile);
    const JunctionColours colours={profile.edge,profile.road,profile.route};
    unsigned checked=0;
    for(int mode=0;mode<2;++mode)for(int type=9;type<=24;++type) {
        if(type==11 || type==12)continue;
        Layout layout;
        assert(junctionLayout(type,false,0,false,0,k6wa.glyph,k6wa.canvas.y1,layout));
        lengthenHudRoute(type,layout);
        assert(junctionFits(layout,4,profile.canvas,k6wa.glyph,k6wa.canvas.y1+1,profile.glyph,77));
        Picture roads;drawRoads(layout,colours,kJunctionStyle,roads);
        fitPicture(roads,k6wa.glyph,k6wa.canvas.y1+1,profile.glyph,77);
        JunctionStyle style=kJunctionStyle;
        if(mode) {style.headL=34;style.headW=27;
            if(type==9 || type==10){style.routeW=20;style.routeCore=16;}
        }
        else {style.routeW=24;style.routeCore=20;style.headL=28;style.headW=23;}
        std::vector<Point> firstHead;
        for(int stage=1;stage<=4;++stage) {
            Picture route;drawRoute(layout,tailFor(layout,stage,4),colours,style,route,19,mode!=0);
            fitPicture(route,k6wa.glyph,k6wa.canvas.y1+1,profile.glyph,77);
            assert(route.size()==4);
            for(size_t i=0;i<route.size();++i)simple(route[i].poly);
            for(int i=1;i<=3;i+=2) {
                Rect r=bounds(route[i].poly);
                assert(r.x0>=0 && r.y0>=0 && r.x1<=125 && r.y1<=76); // includes the dark border
            }
            const Poly& head=route[3].poly;
            Rect r=bounds(head);
            if(type>=21) {
                const Rect main=bounds(roads[layout.roads.size()].poly);
                // The full head clears the separate straight highway, including its nearest wing.
                assert(type%2 ? main.x0-r.x1>=1 : r.x0-main.x1>=1);
            } else if(type>=13) {
                Picture bend(1);bend[0].poly.pts.push_back(layout.route.pts.front());
                fitPicture(bend,k6wa.glyph,k6wa.canvas.y1+1,profile.glyph,77);
                assert(bend[0].poly.pts[0].y-r.y1>=4); // no wing crowds the junction's incoming corner
            }
            if(type==9 || type==10) {
                const Poly& stem=route[0].poly;
                const Rect edge=bounds(route[1].poly);
                int left=std::min(stem.pts.front().x,stem.pts.back().x);
                int right=std::max(stem.pts.front().x,stem.pts.back().x);
                assert((type==9?left-edge.x1:edge.x0-right)>=3); // full head and border clear the incoming stem
            }
            if(stage==1)firstHead=head.pts;
            else {
                assert(firstHead.size()==head.pts.size());
                for(size_t i=0;i<head.pts.size();++i)assert(samePoint(firstHead[i],head.pts[i]));
            }
            ++checked;
        }
    }
    assert(checked==112);
}

int main() {
    unsetenv("NBT_HUD_CANVAS");unsetenv("NBT_HUD_GLYPH");
    checkFamilies();checkHeads();
    puts("HUD sharp turns and ramps: all aliases, both styles, 112 stages; unchanged roads/tails and unclipped separated heads OK");
}
