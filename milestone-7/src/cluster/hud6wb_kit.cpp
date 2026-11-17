#include "cluster/hud6wb_kit.hpp"
#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace nbt { namespace hudterm {
namespace {
using sixwbkit::Point;
struct Edge {Point a,b;};
struct Crossing {double x;size_t edge;bool operator<(const Crossing& o)const{return x<o.x;}};
double at(const Edge& e,double y) {return e.a.x+(y-e.a.y)*(e.b.x-e.a.x)/(e.b.y-e.a.y);}
Point point(double x,double y) {Point p={x,y};return p;}
// Atomic temporary batch: no partial picture escapes on any unsupported shape,
// missing colour, quantization failure or pipeline budget failure.
bool polygon(Batch& b,uint16_t h,const std::vector<Point>& p,const Rect& bounds,unsigned& item) {
    if(p.size()<3||p.size()>99||b.calls.size()+1>2048||item>65535)return false;
    std::vector<hudterm::Point> q;
    for(size_t i=0;i<p.size();++i) {
        if(!sixwbkit::finite(p[i].x)||!sixwbkit::finite(p[i].y)||p[i].x<bounds.x0-.001||p[i].x>bounds.x1+.001||
           p[i].y<bounds.y0-.001||p[i].y>bounds.y1+.001)return false;
        hudterm::Point t;t.x=(int16_t)floor(p[i].x+.5);t.y=(int16_t)floor(p[i].y+.5);
        if(q.empty()||q.back().x!=t.x||q.back().y!=t.y)q.push_back(t);
    }
    if(q.size()>1&&q.front().x==q.back().x&&q.front().y==q.back().y)q.pop_back();
    // Subpixel filled cells may vanish after quantization. Never turn them into
    // an unrelated line; empty quantized cells simply cover no pixels.
    if(q.size()<3)return true;
    long area=0;for(size_t i=0;i<q.size();++i)area+=(long)q[i].x*q[(i+1)%q.size()].y-(long)q[i].y*q[(i+1)%q.size()].x;
    if(!area) {
        int x0=q[0].x,x1=x0,y0=q[0].y,y1=y0;
        for(size_t i=1;i<q.size();++i){x0=std::min(x0,(int)q[i].x);x1=std::max(x1,(int)q[i].x);
            y0=std::min(y0,(int)q[i].y);y1=std::max(y1,(int)q[i].y);}
        return x1-x0<=1||y1-y0<=1; // reject a nontrivial zero-area/self-crossed figure
    }
    if(area<0)std::reverse(q.begin(),q.end()); // consistent winding for filled geometry
    b.add(kAddPolygon,addPolygon(h,(uint16_t)item++,kPolygon,q));return true;
}
// Even-odd scan decomposition at all vertices and edge crossings. It preserves
// holes instead of painting inner contours with a guessed background colour.
bool fill(Batch& b,uint16_t h,const std::vector<sixwbkit::Contour>& cs,const Rect& bounds,unsigned& item) {
    if(cs.size()==1 && cs[0].points.size()<=99)return polygon(b,h,cs[0].points,bounds,item);
    if(cs.size()==2 && cs[0].points.size()==cs[1].points.size() && cs[0].points.size()>=8) {
        size_t n=cs[0].points.size();Point center={0,0},inner={0,0};
        for(size_t i=0;i<n;++i){center.x+=cs[0].points[i].x/n;center.y+=cs[0].points[i].y/n;
            inner.x+=cs[1].points[i].x/n;inner.y+=cs[1].points[i].y/n;}
        bool radial=fabs(center.x-inner.x)<1e-6&&fabs(center.y-inner.y)<1e-6;
        for(size_t i=0;i<n&&radial;++i) {
            Point a=point(cs[0].points[i].x-center.x,cs[0].points[i].y-center.y);
            Point z=point(cs[1].points[i].x-center.x,cs[1].points[i].y-center.y);
            radial=fabs(a.x*z.y-a.y*z.x)<1e-5 && a.x*z.x+a.y*z.y>0 && a.x*a.x+a.y*a.y>z.x*z.x+z.y*z.y;
        }
        if(radial){for(size_t i=0;i<n;++i){size_t j=(i+1)%n;std::vector<Point> p;
            p.push_back(cs[0].points[i]);p.push_back(cs[0].points[j]);p.push_back(cs[1].points[j]);p.push_back(cs[1].points[i]);
            if(!polygon(b,h,p,bounds,item))return false;}return true;}
    }

    std::vector<Edge> edges;std::vector<double> ys;
    for(size_t c=0;c<cs.size();++c) {
        const std::vector<Point>& ps=cs[c].points;
        for(size_t i=0;i<ps.size();++i) {
            const Point& a=ps[i];const Point& z=ps[(i+1)%ps.size()];
            if(!sixwbkit::finite(a.x)||!sixwbkit::finite(a.y))return false;
            ys.push_back(a.y);if(a.y!=z.y){Edge e;e.a=a;e.b=z;edges.push_back(e);}
        }
    }
    if(edges.size()>512)return false;
    for(size_t i=0;i<edges.size();++i)for(size_t j=i+1;j<edges.size();++j) {
        const Edge& a=edges[i];const Edge& z=edges[j];
        double da=(a.b.x-a.a.x)/(a.b.y-a.a.y),db=(z.b.x-z.a.x)/(z.b.y-z.a.y);
        if(fabs(da-db)<1e-12)continue;
        double y=(z.a.x-db*z.a.y-a.a.x+da*a.a.y)/(da-db);
        if(y>std::max(std::min(a.a.y,a.b.y),std::min(z.a.y,z.b.y))+1e-8&&
           y<std::min(std::max(a.a.y,a.b.y),std::max(z.a.y,z.b.y))-1e-8)ys.push_back(y);
    }
    std::sort(ys.begin(),ys.end());
    for(size_t k=1;k<ys.size();++k) {
        double y0=ys[k-1],y1=ys[k];if(y1-y0<1e-8)continue;double mid=(y0+y1)/2;
        std::vector<Crossing> xs;
        for(size_t i=0;i<edges.size();++i)if(mid>std::min(edges[i].a.y,edges[i].b.y)&&mid<std::max(edges[i].a.y,edges[i].b.y)) {
            Crossing x;x.x=at(edges[i],mid);x.edge=i;xs.push_back(x);
        }
        std::sort(xs.begin(),xs.end());if(xs.size()%2)return false;
        for(size_t j=0;j<xs.size();j+=2) {
            const Edge& l=edges[xs[j].edge];const Edge& r=edges[xs[j+1].edge];
            if(xs[j+1].x-xs[j].x<1e-8)continue;
            std::vector<Point> p;p.push_back(point(at(l,y0),y0));p.push_back(point(at(r,y0),y0));
            p.push_back(point(at(r,y1),y1));p.push_back(point(at(l,y1),y1));if(!polygon(b,h,p,bounds,item))return false;
        }
    }return true;
}
bool stroke(Batch& b,uint16_t h,const sixwbkit::Contour& c,double width,const Rect& bounds,unsigned& item) {
    if(width<=0||!sixwbkit::finite(width))return false;
    double radius=width/2;size_t n=c.points.size();if(!n)return true;
    size_t segments=c.closed?n:n-1;
    for(size_t i=0;i<segments;++i) {
        Point a=c.points[i],z=c.points[(i+1)%n];double dx=z.x-a.x,dy=z.y-a.y,len=sqrt(dx*dx+dy*dy);if(len<1e-9)continue;
        double x=-dy/len*radius,y=dx/len*radius;std::vector<Point> p;
        p.push_back(point(a.x+x,a.y+y));p.push_back(point(z.x+x,z.y+y));p.push_back(point(z.x-x,z.y-y));p.push_back(point(a.x-x,a.y-y));
        if(!polygon(b,h,p,bounds,item))return false;
    }
    // Round joins/caps approximated at <=1 pixel chord error at these scales.
    for(size_t i=0;i<n;++i) {std::vector<Point> p;for(unsigned j=0;j<16;++j) {
        double a=6.283185307179586*j/16;p.push_back(point(c.points[i].x+cos(a)*radius,c.points[i].y+sin(a)*radius));}
        if(!polygon(b,h,p,bounds,item))return false;
    }return true;
}
}
namespace {
static const uint8_t COLOR_ROAD_FILL = 3;
static const uint8_t COLOR_ROAD_EDGE = 2;
static const uint8_t COLOR_GUIDANCE_FILL = 6;
static const uint8_t COLOR_GUIDANCE_EDGE = 5;
}

sixwbkit::HudTerminalPalette provisionalKitPalette() {
    sixwbkit::HudTerminalPalette p;
    p.graphite=COLOR_ROAD_FILL;p.black=COLOR_ROAD_FILL;
    p.gold=COLOR_ROAD_EDGE;p.gray=COLOR_ROAD_EDGE;
    p.white=COLOR_GUIDANCE_FILL;p.red=COLOR_GUIDANCE_EDGE;return p;
}
GrafiteKit::GrafiteKit():main_(provisionalKitPalette()),lanes_(main_) {
    printf("[6wb kit] experimental flat colours: road=3, road edge=2, guidance=6, outline=5; physical colours unconfirmed\n");
}
KitBarState kitBarState(const kombinav::NavView& v,unsigned range) {
    KitBarState s;double metres=0;s.visible=kombinav::distanceMetres(v,metres);
    s.value=metres<=0?0:metres>=range?255:(int)(metres*255/range);return s;
}
sixwbkit::Maneuver::Value GrafiteKit::maneuver(int aaType) {
    // Explicit AA mapping: 0..46 are the model families already used by nav_msg.hpp.
    // Do not assign the extra ferry variants 47..50 without a protocol capture.
    switch(aaType) {
    case 0:return sixwbkit::Maneuver::Unknown;
    case 1:return sixwbkit::Maneuver::Start;
    case 2:return sixwbkit::Maneuver::RoadNameChange;
    case 3:return sixwbkit::Maneuver::KeepLeft;
    case 4:return sixwbkit::Maneuver::KeepRight;
    case 5:return sixwbkit::Maneuver::SlightLeft;
    case 6:return sixwbkit::Maneuver::SlightRight;
    case 7:return sixwbkit::Maneuver::Left;
    case 8:return sixwbkit::Maneuver::Right;
    case 9:return sixwbkit::Maneuver::SharpLeft;
    case 10:return sixwbkit::Maneuver::SharpRight;
    case 11:return sixwbkit::Maneuver::UTurnLeft;
    case 12:return sixwbkit::Maneuver::UTurnRight;
    case 13:return sixwbkit::Maneuver::RampSlightLeft;
    case 14:return sixwbkit::Maneuver::RampSlightRight;
    case 15:return sixwbkit::Maneuver::RampLeft;
    case 16:return sixwbkit::Maneuver::RampRight;
    case 17:return sixwbkit::Maneuver::RampSharpLeft;
    case 18:return sixwbkit::Maneuver::RampSharpRight;
    case 19:return sixwbkit::Maneuver::RampUTurnLeft;
    case 20:return sixwbkit::Maneuver::RampUTurnRight;
    case 21:return sixwbkit::Maneuver::ExitSlightLeft;
    case 22:return sixwbkit::Maneuver::ExitSlightRight;
    case 23:return sixwbkit::Maneuver::ExitLeft;
    case 24:return sixwbkit::Maneuver::ExitRight;
    case 25:return sixwbkit::Maneuver::ForkLeft;
    case 26:return sixwbkit::Maneuver::ForkRight;
    case 27:return sixwbkit::Maneuver::MergeLeft;
    case 28:return sixwbkit::Maneuver::MergeRight;
    case 29:return sixwbkit::Maneuver::Merge;
    case 30:return sixwbkit::Maneuver::RoundaboutCompatA;
    case 31:return sixwbkit::Maneuver::RoundaboutCompatB;
    case 32:return sixwbkit::Maneuver::RoundaboutCW;
    case 33:return sixwbkit::Maneuver::RoundaboutCWAngle;
    case 34:return sixwbkit::Maneuver::RoundaboutCCW;
    case 35:return sixwbkit::Maneuver::RoundaboutCCWAngle;
    case 36:return sixwbkit::Maneuver::Straight;
    case 37:return sixwbkit::Maneuver::Ferry;
    case 38:return sixwbkit::Maneuver::TrainFerry;
    case 39:return sixwbkit::Maneuver::Destination;
    case 40:return sixwbkit::Maneuver::DestinationStraight;
    case 41:return sixwbkit::Maneuver::DestinationLeft;
    case 42:return sixwbkit::Maneuver::DestinationRight;
    case 43:return sixwbkit::Maneuver::RoundaboutEnterCW;
    case 44:return sixwbkit::Maneuver::RoundaboutExitCW;
    case 45:return sixwbkit::Maneuver::RoundaboutEnterCCW;
    case 46:return sixwbkit::Maneuver::RoundaboutExitCCW;
    default:return sixwbkit::Maneuver::Unknown;
    }
}
bool GrafiteKit::laneEvent(const kombinav::NavView& v,sixwbkit::NavigationEvent& e) {
    e=sixwbkit::NavigationEvent();if(v.lanes.size()>8)return false;
    for(size_t i=0;i<v.lanes.size();++i) {
        sixwbkit::Lane lane;
        for(size_t j=0;j<v.lanes[i].size();++j) {
            const kombinav::LaneDirection& d=v.lanes[i][j];
            if(d.shape<0||d.shape>9)return false;
            lane.directions.push_back(sixwbkit::Direction(d.shape,d.highlighted));
        }
        e.lanes.push_back(lane);
    }return true;
}
bool GrafiteKit::encode(const sixwbkit::Scene& scene,const sixwbkit::HudTerminalPalette& palette,uint16_t h,const Rect& bounds,Batch& out) {
    // Resolve every role before producing even the first command.
    for(size_t i=0;i<scene.size();++i) {uint8_t color=0;
        if(sixwbkit::hudColorIndex(palette,scene[i].fill,color)==sixwbkit::HudPaint::Unmapped||
           sixwbkit::hudColorIndex(palette,scene[i].stroke,color)==sixwbkit::HudPaint::Unmapped)return false;
    }
    Batch b;unsigned item=0x80;
    for(size_t i=0;i<scene.size();++i) {
        const sixwbkit::Shape& s=scene[i];uint8_t color=0;
        if(sixwbkit::hudColorIndex(palette,s.fill,color)==sixwbkit::HudPaint::Mapped) {
            b.add(kSetFillColor,setFillColor(h,color));if(!fill(b,h,s.contours,bounds,item))return false;
        }
        if(sixwbkit::hudColorIndex(palette,s.stroke,color)==sixwbkit::HudPaint::Mapped) {
            if(!sixwbkit::finite(s.strokeWidth)||s.strokeWidth<=0||s.strokeWidth>255)return false;
            // Never treat 218 as the 21B selector. Stroke geometry is expanded
            // to filled polygons, using the SAME verified colour path as fills.
            b.add(kSetFillColor,setFillColor(h,color));
            for(size_t c=0;c<s.contours.size();++c)
                if(!stroke(b,h,s.contours[c],s.strokeWidth,bounds,item))return false;
        }
        if(b.calls.size()>2048)return false;
    }
    out.calls.insert(out.calls.end(),b.calls.begin(),b.calls.end());return true;
}
bool GrafiteKit::central(const kombinav::NavView& v,uint16_t h,const Rect& r,Batch& out)const {
    if(h!=0x1f && h!=0x20)return false;
    if(v.aa_type<0||v.aa_type>46)return false; // report an unsupported code, never substitute straight-on
    sixwbkit::NavigationEvent e;e.maneuver=maneuver(v.aa_type);
    e.hasExitAngle=v.has_exit_angle && v.exit_angle>=1 && v.exit_angle<=360;
    e.exitAngle=(double)v.exit_angle;
    e.hasExitNumber=v.has_exit_number && v.exit_number>=1 && v.exit_number<=99;
    e.exitNumber=(unsigned)v.exit_number;
    sixwbkit::Rect area(r.x0,r.y0,r.x1-r.x0,r.y1-r.y0);
    return encode(sixwbkit::renderManeuver(e,area,0.0),main_,h,r,out);
}
bool GrafiteKit::lanes(const kombinav::NavView& v,uint16_t h,unsigned w,unsigned height,Batch& out)const {
    sixwbkit::NavigationEvent e;if(!laneEvent(v,e))return false;
    if(e.lanes.empty())return true; // empty means clear, not stale lanes
    Rect bounds={0,0,(int16_t)(w-1),(int16_t)(height-1)};
    return encode(sixwbkit::renderLanes(e.lanes,sixwbkit::Rect(0,0,w-1,height-1)),lanes_,h,bounds,out);
}
bool GrafiteKit::swatches(uint16_t h,const Rect& useful,unsigned columns,Batch& out)const {
    sixwbkit::Rect area(useful.x0,useful.y0,useful.x1-useful.x0,useful.y1-useful.y0);
    std::vector<sixwbkit::HudPaletteSwatch> cells=sixwbkit::hudPaletteSwatches(area,columns);
    if(cells.size()!=20)return false;Batch b;unsigned item=0x80;
    for(size_t i=0;i<cells.size();++i) {
        b.add(kSetFillColor,setFillColor(h,cells[i].index));
        if(!polygon(b,h,cells[i].fill.points,useful,item))return false;
        // A 2px round-cap line expanded to filled contours uses the SAME 21B
        // index and leaves no persistent line-colour changes in the terminal.
        if(!stroke(b,h,cells[i].line,2,useful,item))return false;
    }
    out.calls.insert(out.calls.end(),b.calls.begin(),b.calls.end());return true;
}
} }
