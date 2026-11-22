// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Committed-asset checks: no generator, git history or network is needed to run this test.
#include "cluster/mapbox_hud.hpp"
#include <algorithm>
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

using namespace nbt::hudterm;
#include "mapbox_wing_fixtures.hpp"
extern const mbx::Piece* mapboxOtherTuGlyph(int family);
extern const mbx::CurveBody* mapboxOtherTuBody(int family);
extern bool mapboxOtherTuBounds(int family, Rect& bounds);
static const char* stage = "raw";
static unsigned caseNo = 0;

static long cross(const Point& a, const Point& b, const Point& c) {
    return (long)(b.x-a.x)*(c.y-a.y) - (long)(b.y-a.y)*(c.x-a.x);
}

static void simple(const std::vector<Point>& p) {
    assert(p.size() >= 3);
    long area = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        const size_t next = i+1 < p.size() ? i+1 : 0;
        area += (long)p[i].x*p[next].y - (long)p[next].x*p[i].y;
        for (size_t j = i+1; j < p.size(); ++j) {
            const size_t jnext = j+1 < p.size() ? j+1 : 0;
            if (next == j || jnext == i) continue;
            const long a = cross(p[i],p[next],p[j]), b = cross(p[i],p[next],p[jnext]);
            const long c = cross(p[j],p[jnext],p[i]), d = cross(p[j],p[jnext],p[next]);
            if (((a<0 && b>0)||(a>0 && b<0)) && ((c<0 && d>0)||(c>0 && d<0))) {
                fprintf(stderr,"%s case %u: crossing edges %lu/%lu\n",stage,caseNo,(unsigned long)i,(unsigned long)j);
                for (size_t k=0;k<p.size();++k) fprintf(stderr," (%d,%d)",p[k].x,p[k].y);
                fprintf(stderr,"\n");
                assert(false);
            }
        }
    }
    assert(area != 0);
}

static uint32_t word(uint32_t hash, int n) { return (hash ^ (n & 0xffff))*16777619U; }
static uint32_t hashPiece(uint32_t hash, const mbx::Piece& p) {
    hash = word(word(hash,p.tone),p.n);
    for (unsigned j = 0; j < p.n; ++j) hash = word(word(hash,p.p[j][0]),p.p[j][1]);
    return hash;
}

static bool contains(const WingCheck& g, int x, int y) {
    for (unsigned i = 0; i < g.n; ++i)
        for (unsigned j = 0; j < g.glyph[i].n; ++j)
            if (g.glyph[i].p[j][0] == x && g.glyph[i].p[j][1] == y) return true;
    const mbx::CurveBody* body = mbxCurveBody(g.family);
    if (body) for (unsigned j = 0; j < body->n; ++j)
        if (body->p[j][0] == x*10 && body->p[j][1] == y*10) return true;
    return false;
}

static double along(const WingCheck& g, double x, double y) { return (x-g.tx)*g.ux+(y-g.ty)*g.uy; }
static double across(const WingCheck& g, double x, double y) { return (x-g.tx)*g.vx+(y-g.ty)*g.vy; }

static void checkGeometry(const WingCheck& g) {
    assert(contains(g,g.tx,g.ty));
    assert(contains(g,g.shaft[0],g.shaft[1]) && contains(g,g.shaft[2],g.shaft[3]));
    double minU = 1e9, maxU = -1e9;
    uint32_t unchanged = 2166136261U;
    for (unsigned i = 0; i < g.n; ++i) {
        const mbx::Piece& piece = g.glyph[i];
        assert(piece.n >= 3 && piece.n <= 8);
        std::vector<Point> right, left;
        for (unsigned j = 0; j < piece.n; ++j) {
            const int x = piece.p[j][0], y = piece.p[j][1];
            right.push_back(shapes::pt(x,y));
            left.push_back(shapes::pt(240-x,y));
            const double u = along(g,x,y);
            if (u < minU) minU = u;
            if (u > maxU) maxU = u;
        }
        simple(right); simple(left);
        if (g.unchanged & (1ULL<<i)) unchanged = hashPiece(unchanged,piece);
    }
    assert(unchanged == g.hash);   // every unspecified head/body/context piece remains byte-for-byte
    const mbx::CurveBody* body = mbxCurveBody(g.family);
    if (body) {
        std::vector<Point> right,left;
        for (unsigned j = 0; j < body->n; ++j) {
            const int x=body->p[j][0],y=body->p[j][1];
            right.push_back(shapes::pt(x,y)); left.push_back(shapes::pt(2400-x,y));
            const double u=along(g,x/10.0,y/10.0);
            if(u<minU)minU=u;
            if(u>maxU)maxU=u;
        }
        simple(right); simple(left);
    }
    assert(fabs(minU-g.minU)<1e-7); // original tip remains the frontmost point
    if (!body) assert(fabs(maxU-g.maxU)<1e-7); // intentional curved bodies are checked by their arc equations below
    for (unsigned s = 0; s < 2; ++s) {
        const WingSideCheck& w = g.side[s];
        assert(contains(g,w.sx,w.sy));
        assert(fabs((w.target-w.h)-.7*(w.maximum-w.h))<1e-7);
        double actualMax = -1e9;
        for (unsigned i = 0; i < g.n; ++i) if (!(g.unchanged & (1ULL<<i)))
            for (unsigned j = 0; j < g.glyph[i].n; ++j) {
                const double v = w.sign*across(g,g.glyph[i].p[j][0],g.glyph[i].p[j][1]);
                if (v > actualMax) actualMax = v;
            }
        // Coordinates are quantised once on the original integer grid (maximum projection error sqrt(.5)).
        assert(fabs(actualMax-w.target) <= .708);
        const double oldV = w.sign*across(g,w.bx,w.by);
        const double v = w.b+(oldV-w.b)*(w.target-w.b)/(w.maximum-w.b);
        const double u = w.a+(v-w.b)/tan(80.0*3.14159265358979323846/180.0);
        const double x = g.tx+u*g.ux+w.sign*v*g.vx;
        const double y = g.ty+u*g.uy+w.sign*v*g.vy;
        bool matched = false;
        for (unsigned i = 0; i < g.n; ++i)
            for (unsigned j = 0; j < g.glyph[i].n; ++j)
                if (fabs(g.glyph[i].p[j][0]-x)<=.50001 && fabs(g.glyph[i].p[j][1]-y)<=.50001)
                    matched = true;
        assert(matched); // the real return tangent, rather than a rounded corner's outermost vertex, is at 80 degrees
    }
}

static void smoothBodies() {
    const mbx::CurveBody* sharp=mbxCurveBody(5);
    const mbx::CurveBody* turn=mbxCurveBody(9);
    assert(sharp && sharp->n==30 && turn && turn->n==38);
    assert(!mbxCurveBody(29) && !mbxCurveBody(7));
    const double radius=28/sqrt(2.0),cx=85.5+radius,cy=43.5+radius;
    for(unsigned j=1;j<=28;++j) {
        if(j==14 || j==15)continue; // preserved neck joins, not arc points
        const double dx=sharp->p[j][0]/10.0-cx,dy=sharp->p[j][1]/10.0-cy;
        const double expected=radius+(j<=13?6.5:-6.5);
        assert(fabs(sqrt(dx*dx+dy*dy)-expected)<=.072);
    }
    for(unsigned j=1;j<=36;++j) {
        if(j==18 || j==19)continue; // exact original outgoing shaft ends
        const double dx=turn->p[j][0]/10.0-(j<=17?116.5:117),dy=turn->p[j][1]/10.0-55;
        assert(fabs(sqrt(dx*dx+dy*dy)-(j<=17?26.5:14))<=.072);
    }
    // Original entry shafts and necks are exact anchors; only the body between them is reprofiled.
    assert(sharp->p[0][0]==790 && sharp->p[29][0]==920 && sharp->p[0][1]==1200 && sharp->p[29][1]==1200);
    assert(turn->p[0][0]==900 && turn->p[37][0]==1030 && turn->p[0][1]==1200 && turn->p[37][1]==1200);
    assert(sharp->p[14][0]==1400 && sharp->p[14][1]==610 && sharp->p[15][0]==1310 && sharp->p[15][1]==700);
    assert(turn->p[18][0]==1430 && turn->p[18][1]==860 && turn->p[19][0]==1310 && turn->p[19][1]==860);
    const Rect boxes[]={{70,20,170,120},{36,16,89,69},{37,23,82,68}};
    for(unsigned k=0;k<sizeof(boxes)/sizeof(boxes[0]);++k) {
        HudProfile profile=k6wa; profile.glyph=boxes[k];
        const mbx::CurveBody* bodies[]={sharp,turn};
        for(unsigned b=0;b<2;++b) for(int mirror=0;mirror<2;++mirror) {
            Picture pic;
            const mbx::Piece* glyph; unsigned count; bool selectedMirror;
            assert(mbxGlyphForFamily(bodies[b]->family,glyph,count,selectedMirror));
            mbxGlyph(glyph,count,mirror,profile,pic,bodies[b]);
            const std::vector<Point>& p=pic.back().poly.pts;
            simple(p);
            for(size_t j=0;j<p.size();++j) {
                assert(p[j].x>=boxes[k].x0 && p[j].x<=boxes[k].x1);
                assert(p[j].y>=boxes[k].y0 && p[j].y<=boxes[k].y1);
            }
        }
    }
}

// Measure the visible union, including central head pieces, rather than the width of a hidden body quad.
static double sectionWidth(const Picture& pic, int tx, int ty, double ux, double uy, double distance) {
    std::vector<std::pair<double,double> > intervals;
    for(size_t i=0;i<pic.size();++i) {
        if(pic[i].colour!=k6wa.fill)continue;
        const std::vector<Point>& p=pic[i].poly.pts;
        std::vector<double> cuts;
        for(size_t j=0;j<p.size();++j) {
            const Point& a=p[j]; const Point& b=p[j+1<p.size()?j+1:0];
            const double au=(a.x-tx)*ux+(a.y-ty)*uy,bu=(b.x-tx)*ux+(b.y-ty)*uy;
            if(std::min(au,bu)<=distance && distance<std::max(au,bu)) {
                const double av=(a.x-tx)*uy-(a.y-ty)*ux,bv=(b.x-tx)*uy-(b.y-ty)*ux;
                cuts.push_back(av+(distance-au)/(bu-au)*(bv-av));
            }
        }
        std::sort(cuts.begin(),cuts.end());
        assert(cuts.size()%2==0);
        for(size_t j=0;j<cuts.size();j+=2)intervals.push_back(std::make_pair(cuts[j],cuts[j+1]));
    }
    assert(!intervals.empty());
    std::sort(intervals.begin(),intervals.end());
    const double first=intervals[0].first;
    double last=intervals[0].second;
    for(size_t i=1;i<intervals.size();++i) {
        assert(intervals[i].first<=last+1e-7);
        last=std::max(last,intervals[i].second);
    }
    return last-first;
}

static void offrampNeck() {
    const double length=sqrt(49.0*49+42.0*42);
    for(int mirror=0;mirror<2;++mirror) {
        Picture pic;
        mbxGlyph(mbx::kOfframp,mbx::kOfframpN,mirror,k6wa,pic,mbxCurveBody(29)); // identity mapping: 6WA source grid
        for(int u=35;u<=43;++u) {
            const double width=sectionWidth(pic,mirror?84:156,32,(mirror?49.0:-49.0)/length,42.0/length,u);
            assert(width>=12.7 && width<=13.3); // old central head narrowed below12 before meeting the shaft
        }
    }
}

static HudProfile canonicalProfile() {
    HudProfile p = k6wa;
    p.glyph = Rect{0,0,1000,1000};
    return p;
}

static void mirrorPicture(const Picture& right, const Picture& left, int sumX, int tolerance) {
    assert(right.size() == left.size());
    for (size_t i = 0; i < right.size(); ++i) {
        assert(right[i].colour == left[i].colour);
        const std::vector<Point>& a = right[i].poly.pts;
        const std::vector<Point>& b = left[i].poly.pts;
        assert(a.size() == b.size());
        for (size_t j = 0; j < a.size(); ++j) {
            assert(abs(a[j].x+b[j].x-sumX)<=tolerance);
            assert(a[j].y == b[j].y);
        }
    }
}

static void multiTuGlyphs() {
    const int families[]={4,5,8,9};
    for(unsigned i=0;i<sizeof(families)/sizeof(families[0]);++i) {
        const int family=families[i],key=mbxCanonicalFamily(family);
        const mbx::Piece* selected; unsigned count; bool mirror;
        assert(mbxGlyphForFamily(family,selected,count,mirror));
        // An inline selector may itself be linked from the other TU; take this TU's arrays directly.
        const mbx::Piece* local = key==5 ? mbx::kArrowsharpright : mbx::kArrow180right;
        const mbx::Piece* foreign=mapboxOtherTuGlyph(key);
        assert(local!=foreign); // prove this test crosses the namespace-const array copies
        Picture reference,foreignPieces,foreignBody;
        mbxGlyph(local,count,mirror,k6wa,reference,mbxCurveBody(family));
        mbxGlyph(foreign,count,mirror,k6wa,foreignPieces,mbxCurveBody(family));
        mbxGlyph(local,count,mirror,k6wa,foreignBody,mapboxOtherTuBody(key));
        assert(reference.size()==7 && reference.back().poly.pts.size()==(key==5?30U:38U));
        for(size_t p=0;p<reference.size();++p) {
            const std::vector<Point>& expected=reference[p].poly.pts;
            assert(foreignPieces[p].poly.pts.size()==expected.size() && foreignBody[p].poly.pts.size()==expected.size());
            for(size_t q=0;q<expected.size();++q) {
                assert(expected[q].x==foreignPieces[p].poly.pts[q].x && expected[q].y==foreignPieces[p].poly.pts[q].y);
                assert(expected[q].x==foreignBody[p].poly.pts[q].x && expected[q].y==foreignBody[p].poly.pts[q].y);
            }
        }
        Rect a,b;
        assert(mbxOriginalBounds(family,a) && mapboxOtherTuBounds(family,b));
        assert(a.x0==b.x0 && a.y0==b.y0 && a.x1==b.x1 && a.y1==b.y1);
    }
}

static void renderedFamilies() {
    stage = "static render";
    const int families[] = {1,3,7,5,9,26,38,29};
    const int lefts[] = {1,2,6,4,8,25,37,28};
    const Rect original[] = {{92,26,148,120},{91,33,154,120},{76,26,164,120},{79,37,161,120},
                             {90,29,165,120},{88,22,152,120},{84,28,162,120},{92,29,156,120}};
    const HudProfile p = canonicalProfile();
    for (unsigned i = 0; i < sizeof(families)/sizeof(families[0]); ++i) {
        caseNo = i;
        const mbx::Piece* g; unsigned n; bool mirror;
        assert(mbxGlyphForFamily(families[i],g,n,mirror) && !mirror);
        Picture a,b;
        mbxGlyph(g,n,false,p,a,mbxCurveBody(families[i])); mbxGlyph(g,n,true,p,b,mbxCurveBody(lefts[i]));
        mirrorPicture(a,b,1000,0);
        for (size_t k = 0; k < a.size(); ++k) { simple(a[k].poly.pts); simple(b[k].poly.pts); }
        Rect r,l;
        assert(mbxOriginalBounds(families[i],r) && mbxOriginalBounds(lefts[i],l));
        assert(r.x0==original[i].x0 && r.y0==original[i].y0 && r.x1==original[i].x1 && r.y1==original[i].y1);
        // The straight glyph is symmetric and has no separate mirrored family.
        if (i) assert(r.x0+l.x1==240 && r.x1+l.x0==240 && r.y0==l.y0 && r.y1==l.y1);
    }
    Rect bounds;
    assert(!mbxOriginalBounds(10,bounds) && !mbxOriginalBounds(14,bounds));
    assert(!mbxOriginalBounds(15,bounds) && !mbxOriginalBounds(-1,bounds));
    // The same merge geometry also serves the explicit merge families.
    Rect onramp,merge;
    assert(mbxOriginalBounds(26,onramp) && mbxOriginalBounds(33,merge));
    assert(onramp.x0==merge.x0 && onramp.x1==merge.x1 && onramp.y0==merge.y0 && onramp.y1==merge.y1);
}

static void renderedRoundabouts() {
    stage = "roundabout render";
    const HudProfile p = canonicalProfile();
    for (int angle = 1; angle <= 360; ++angle) {
        caseNo = angle;
        Picture a,b;
        mbxRoundabout(angle,false,p,a); mbxRoundabout(angle,true,p,b);
        assert(!a.empty());
        mirrorPicture(a,b,1000,0);
        // Check the changed exit head in the actual picture; existing ring arcs are outside the wing contract.
        Picture head;
        std::vector<bool> changed;
        const rb::P16 centre = rb::centre(angle);
        const int co = icos(-(angle+90)), si = isin(-(angle+90));
        for (unsigned i = 0; i < mbx::kRbArrowN; ++i) {
            const mbx::Piece& g = mbx::kRbArrow[i];
            rb::P16 q[8];
            for (unsigned j = 0; j < g.n; ++j) {
                const int x = g.p[j][0], y = g.p[j][1];
                q[j] = rb::P16{centre.x+(x*co-y*si)/4096,centre.y+(x*si+y*co)/4096};
            }
            const size_t before = head.size();
            rb::piece(q,g.n,g.tone?p.road:p.fill,false,p.glyph,head);
            if (head.size() != before) changed.push_back(!(kWingChecks[8].unchanged & (1ULL<<i)));
        }
        assert(head.size() <= a.size() && !head.empty());
        const size_t start = a.size()-head.size();
        for (size_t i = 0; i < head.size(); ++i) {
            const std::vector<Point>& actual = a[start+i].poly.pts;
            assert(actual.size() == head[i].poly.pts.size());
            for (size_t j = 0; j < actual.size(); ++j) {
                assert(actual[j].x==head[i].poly.pts[j].x && actual[j].y==head[i].poly.pts[j].y);
            }
            if (changed[i]) { simple(actual); simple(b[start+i].poly.pts); }
        }
    }
}

int main() {
    for (unsigned i = 0; i < sizeof(kWingChecks)/sizeof(kWingChecks[0]); ++i) { caseNo=i; checkGeometry(kWingChecks[i]); }
    for (unsigned i = 0; i < sizeof(kPreservedGlyphChecks)/sizeof(kPreservedGlyphChecks[0]); ++i) {
        const PreservedGlyphCheck& g = kPreservedGlyphChecks[i];
        uint32_t hash = 2166136261U;
        for (unsigned j = 0; j < g.n; ++j) hash = hashPiece(hash,g.glyph[j]);
        assert(hash == g.hash); // destination and unused lane/start/arrival assets are outside this change
    }
    renderedFamilies();
    multiTuGlyphs();
    smoothBodies();
    offrampNeck();
    renderedRoundabouts();
    puts("Mapbox wings: all 9 assets, mirrors, 80-degree return tangents, 70% overhang, fixed anchors and 360 roundabout angles OK");
}
