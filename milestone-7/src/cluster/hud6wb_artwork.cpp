// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Offset-path/bevel algorithm adapted from JayJay3071/nbt-android-auto 0fb9fd2.
#include "cluster/hud6wb_artwork.hpp"
#include <math.h>
namespace nbt { namespace hudterm {
namespace {
struct V { double x,y; V(double a=0,double b=0):x(a),y(b){} };
V norm(V p) {double n=sqrt(p.x*p.x+p.y*p.y);return n>0?V(p.x/n,p.y/n):V();}
Point point(V p) {Point q;q.x=(int16_t)floor(p.x+.5);q.y=(int16_t)floor(p.y+.5);return q;}
V heading(V p,double deg,double length) {
    double r=deg*3.141592653589793/180;return V(p.x+sin(r)*length,p.y-cos(r)*length);
}
void arrow(const std::vector<V>& path, Shape& out) {
    if(path.size()<2)return;
    const double half=65,head=150,headHalf=155;
    std::vector<V> a,b;
    for(size_t i=0;i<path.size();++i) {
        V d1=i?norm(V(path[i].x-path[i-1].x,path[i].y-path[i-1].y)):
            norm(V(path[1].x-path[0].x,path[1].y-path[0].y));
        V d2=i+1<path.size()?norm(V(path[i+1].x-path[i].x,path[i+1].y-path[i].y)):d1;
        V n1(-d1.y,d1.x),n2(-d2.y,d2.x),m=norm(V(n1.x+n2.x,n1.y+n2.y));
        double c=m.x*n1.x+m.y*n1.y;if(c<.2)c=.2;
        V ma(path[i].x+m.x*half/c,path[i].y+m.y*half/c),mb(path[i].x-m.x*half/c,path[i].y-m.y*half/c);
        double dot=d1.x*d2.x+d1.y*d2.y,cross=d1.x*d2.y-d1.y*d2.x;
        if(!i || i+1==path.size() || dot>.5) {a.push_back(ma);b.push_back(mb);}
        else if(cross>0) {a.push_back(ma);b.push_back(V(path[i].x-n1.x*half,path[i].y-n1.y*half));b.push_back(V(path[i].x-n2.x*half,path[i].y-n2.y*half));}
        else {a.push_back(V(path[i].x+n1.x*half,path[i].y+n1.y*half));a.push_back(V(path[i].x+n2.x*half,path[i].y+n2.y*half));b.push_back(mb);}
    }
    size_t n=path.size();V end=path[n-1],d=norm(V(end.x-path[n-2].x,end.y-path[n-2].y)),nn(-d.y,d.x);
    Poly p;p.type=kPolyline;
    for(size_t i=0;i<a.size();++i)p.pts.push_back(point(a[i]));
    p.pts.push_back(point(V(end.x+nn.x*headHalf,end.y+nn.y*headHalf)));
    p.pts.push_back(point(V(end.x+d.x*head,end.y+d.y*head)));
    p.pts.push_back(point(V(end.x-nn.x*headHalf,end.y-nn.y*headHalf)));
    for(size_t i=b.size();i>0;--i)p.pts.push_back(point(b[i-1]));
    p.pts.push_back(p.pts[0]);out.push_back(p);
}
}
Shape bmwManeuver(const ShapeSpec& s,const Rect& rect,bool knownExit) {
    Shape design,out;std::vector<V> path;
    if(s.kind==kShapeRoundabout) {
        Poly ring;ring.type=kPolyline;
        for(unsigned i=0;i<=24;++i)ring.pts.push_back(point(heading(V(500,430),i*15,210)));
        design.push_back(ring);
        Poly entry;entry.type=kPolyline;entry.pts.push_back(point(V(500,950)));entry.pts.push_back(point(V(500,640)));design.push_back(entry);
        // Entry is south; sweep follows the AA direction of circulation.
        double a=180+(s.clockwise?s.exit:-s.exit);
        if(knownExit) {path.push_back(heading(V(500,430),a,210));path.push_back(heading(V(500,430),a,330));arrow(path,design);}
    } else if(s.kind==kShapeUTurn) {
        int side=s.angle<0?-1:1;
        path.push_back(V(500-side*140,930));path.push_back(V(500-side*140,300));
        path.push_back(V(500+side*140,300));path.push_back(V(500+side*140,580));arrow(path,design);
    } else if(s.kind==kShapeFlag) {
        drawShape(s,design);
    } else {
        path.push_back(V(500,940));V knee(500,s.angle?570:300);path.push_back(knee);
        if(s.angle)path.push_back(heading(knee,s.angle,280));
        arrow(path,design);
        if(s.stub!=kNoStub) {Poly stub;stub.type=kPolyline;stub.pts.push_back(point(knee));stub.pts.push_back(point(heading(knee,s.stub,230)));design.push_back(stub);}
    }
    // Clamp design-space extremes before fitting to the surface; no off-surface writes.
    fitShape(design,rect,out);return out;
}
Shape bmwExitNumber(unsigned number,const Rect& rect) {
    Shape design,out;if(number<1 || number>99)return out;
    static const unsigned masks[]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
    static const int segments[7][4]={{0,0,400,0},{400,0,400,500},{400,500,400,1000},
        {400,1000,0,1000},{0,1000,0,500},{0,500,0,0},{0,500,400,500}};
    unsigned count=number>9?2:1;
    for(unsigned digit=0;digit<count;++digit) {
        unsigned n=count==1?number:digit==0?number/10:number%10;
        for(unsigned j=0;j<7;++j)if(masks[n]&(1<<j)) {
            Poly p;p.type=kPolyline;
            int offset=count==1?300:(int)digit*550;
            p.pts.push_back(point(V(offset+segments[j][0],segments[j][1])));
            p.pts.push_back(point(V(offset+segments[j][2],segments[j][3])));design.push_back(p);
        }
    }
    fitShape(design,rect,out);return out;
}
Shape bmwLane(int shape,bool highlighted,const Rect& rect) {
    Shape out;if(shape<1 || shape>9)return out;
    ShapeSpec spec;
    static const int angles[8]={0,0,-45,45,-90,90,-135,135};
    if(shape>=8) {spec.kind=kShapeUTurn;spec.angle=shape==8?-1:1;}
    else spec.angle=angles[shape];
    out=bmwManeuver(spec,rect);
    if(highlighted)for(size_t i=0;i<out.size();++i)out[i].type=kPolygon;
    return out;
}
} }
