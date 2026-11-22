// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// The compact font remains readable and valid after rounding onto small caption canvases.
#include "cluster/hudterm_digits.hpp"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
using namespace nbt::hudterm;

static long cross(const Point& a, const Point& b, const Point& c) {
    return (long)(b.x - a.x) * (c.y - a.y) - (long)(b.y - a.y) * (c.x - a.x);
}

static bool onSegment(const Point& a,const Point& b,const Point& p) {
    return cross(a,b,p)==0&&p.x>=std::min(a.x,b.x)&&p.x<=std::max(a.x,b.x)
           &&p.y>=std::min(a.y,b.y)&&p.y<=std::max(a.y,b.y);
}

static void valid(const Poly& p,bool simple=true) {
    const size_t n = p.pts.size();
    assert(n >= 3 && n <= 23);
    long area = 0;
    for (size_t i = 0; i < n; ++i) {
        const Point& a = p.pts[i]; const Point& b = p.pts[(i + 1) % n];
        assert(a.x != b.x || a.y != b.y);
        const Point& c = p.pts[(i + 2) % n];
        long turn = cross(a,b,c);
        long dot = (long)(b.x-a.x)*(c.x-b.x)+(long)(b.y-a.y)*(c.y-b.y);
        assert(turn || dot >= 0); // a collapsed corner must not leave a zero-width spur
        area += (long)a.x * b.y - (long)b.x * a.y;
        for (size_t j = i + 2; j < n; ++j) {
            if (i == 0 && j + 1 == n) continue;
            const Point& c = p.pts[j]; const Point& d = p.pts[(j + 1) % n];
            long ac = cross(a, b, c), ad = cross(a, b, d), ca = cross(c, d, a), cb = cross(c, d, b);
            if (simple) assert(!onSegment(a,b,c)&&!onSegment(a,b,d)&&!onSegment(c,d,a)&&!onSegment(c,d,b));
            assert(!(((ac < 0 && ad > 0) || (ac > 0 && ad < 0))
                   && ((ca < 0 && cb > 0) || (ca > 0 && cb < 0))));
        }
    }
    assert(area != 0);
}

static bool painted(const Shape& shape, double x, double y) {
    for (size_t p = 0; p < shape.size(); ++p) {
        bool inside = false;
        const std::vector<Point>& v = shape[p].pts;
        for (size_t i = 0, j = v.size() - 1; i < v.size(); j = i++)
            if ((v[i].y > y) != (v[j].y > y)
                && x < (v[j].x - v[i].x) * (y - v[i].y) / (v[j].y - v[i].y) + v[i].x) inside = !inside;
        if (inside) return true;
    }
    return false;
}

// Keep the selected source silhouette stable, including its hand-adjusted chamfers.
static void sourceInvariants() {
    const uint32_t expected[] = {0x4a31c0c5U,0x71cd15eaU,0x0eac5812U,0x68a8f06aU,0x287ef366U,
                                 0x70d1048bU,0xe6dad667U,0xaaff3b5aU,0x10f36489U,0x8426e42fU};
    for (int d=0;d<10;++d) {
        const digits::Glyph& gl=digits::glyph(d);
        uint32_t hash=2166136261U;
        for (int p=0;p<2;++p) hash=(hash^(uint32_t)gl.n[p])*16777619U;
        for (int p=0;p<2;++p) for (int i=0;i<gl.n[p]*2;++i)
            hash=(hash^(uint32_t)gl.xy[p][i])*16777619U;
        assert(hash==expected[d]);
        assert(gl.n[0]>0&&gl.n[0]<=23&&gl.n[1]<=23);
        Shape source;digits::drawNumber(d,50,0,180,source);
        for (size_t p=0;p<source.size();++p) valid(source[p]);
    }
    const digits::Glyph& one=digits::glyph(1);
    assert(one.xy[0][0]==16&&one.xy[0][1]==28&&one.xy[0][2]==44&&one.xy[0][3]==0); // original 45-degree hook
    assert(one.xy[0][4]-one.xy[0][16]==28); // stem width
    assert(one.xy[0][8]-one.xy[0][12]==92); // foot width
    // The owner's new free-end bevels are true 45-degree edges in the logical cell.
    const int chamfers[][6]={{0,0,0,160,20,180},{1,0,16,28,44,0},{2,0,80,152,100,172},{3,0,20,180,0,160},{5,0,0,172,20,152},
                             {6,0,80,0,100,20},{8,0,0,160,20,180},{9,1,0,160,20,180}};
    for (size_t k=0;k<sizeof(chamfers)/sizeof(chamfers[0]);++k) {
        const int* edge=chamfers[k];const digits::Glyph& gl=digits::glyph(edge[0]);bool found=false;
        for (int i=0;i<gl.n[edge[1]];++i) {
            int j=(i+1)%gl.n[edge[1]];
            if (gl.xy[edge[1]][2*i]==edge[2]&&gl.xy[edge[1]][2*i+1]==edge[3]
                &&gl.xy[edge[1]][2*j]==edge[4]&&gl.xy[edge[1]][2*j+1]==edge[5]) found=true;
        }
        assert(found&&abs(edge[4]-edge[2])==abs(edge[5]-edge[3]));
    }
}

// Count bounded black components in a quarter-pixel mask. A single sampling cell can be isolated in a narrow diagonal gap.
static int counters(const Shape& shape,int h) {
    const int scale=4,w=(digits::width(h)+2)*scale,height=(h+2)*scale;
    std::vector<unsigned char> mask(w*height,0);
    for (int y=0;y<height;++y) for (int x=0;x<w;++x)
        mask[y*w+x]=painted(shape,(x+0.5)/scale-1,(y+0.5)/scale-1)?1:0;
    int components=0;
    for (int start=0;start<w*height;++start) if (!mask[start]) {
        int samples=0;std::vector<int> todo(1,start);mask[start]=2;
        while (!todo.empty()) {
            int current=todo.back();todo.pop_back();++samples;
            int neighbours[]={current-1,current+1,current-w,current+w};
            for (int i=0;i<4;++i) {
                int n=neighbours[i];
                if (n<0||n>=w*height||(i<2&&n/w!=current/w)||mask[n]) continue;
                mask[n]=2;todo.push_back(n);
            }
        }
        if (samples>1) ++components;
    }
    return components-1;
}

int main() {
    sourceInvariants();
    const int counterDigits[]={0,4,6,8,9},heights[]={18,22,30};
    for (size_t h=0;h<3;++h) for (size_t d=0;d<5;++d) {
        Shape shape;digits::drawNumber(counterDigits[d],digits::width(heights[h])/2,0,heights[h],shape);
        int expected=counterDigits[d]==8?2:1;
        assert(shape.size()==2&&counters(shape,heights[h])==expected);
    }
    Shape diagonal;digits::drawNumber(7,50,0,180,diagonal);
    assert(painted(diagonal,84.5,40.5)&&painted(diagonal,35.5,150.5));
    assert(!painted(diagonal,84.5,150.5));

    unsigned checked = 0;
    for (int h = 6; h <= 40; ++h)
        for (int n = 0; n <= 999; ++n) {
            Shape shape; digits::drawNumber(n, 100, 10, h, shape);
            int count = n > 99 ? 3 : n > 9 ? 2 : 1;
            int width = count * digits::width(h) + (count - 1) * digits::gap(h);
            assert(!shape.empty() && shape.size() <= (unsigned)(2 * count));
            for (size_t p = 0; p < shape.size(); ++p) {
                valid(shape[p],h>=8); // at 6–7 pixels some counters/stems collapse to shared boundary points
                for (size_t i = 0; i < shape[p].pts.size(); ++i) {
                    const Point& point = shape[p].pts[i];
                    assert(point.x >= 100 - width / 2 && point.x <= 100 - width / 2 + width);
                    assert(point.y >= 10 && point.y <= 10 + h);
                }
            }
            ++checked;
        }

    Shape sentinel; digits::drawNumber(8, 10, 10, 22, sentinel);
    size_t original = sentinel.size();
    digits::drawNumber(1, 0, 0, 0, sentinel);
    digits::drawNumber(1, 0, 0, -1, sentinel);
    assert(sentinel.size() == original);
    printf("geometric HUD digits: %u number/height combinations; simple outlines at 8..40px; counters at 18/22/30px; source and pixel fitting passed\n", checked);
}
