#include "artwork6wb.hpp"
#include <algorithm>
#include <limits>
#include <math.h>
#include <cstddef>
using std::size_t;
namespace nbt { namespace sixwbkit {
namespace {
struct RawContour {bool closed;unsigned first,count;};
struct RawShape {Color::Value fill,stroke;double width;unsigned first,count;};
struct Entry {ManeuverInfo info;unsigned offset,count;};
#include "artwork_data.inc"
typedef char CatalogSizeCheck[(sizeof(entries)/sizeof(*entries)==Maneuver::Count)?1:-1];
const Entry& entry(Maneuver::Value m) {unsigned i=(unsigned)m;return entries[i<Maneuver::Count?i:0];}
bool valid(Rect r) {return finite(r.x)&&finite(r.y)&&finite(r.width)&&finite(r.height)&&
    finite(r.x+r.width)&&finite(r.y+r.height)&&r.width>0&&r.height>0;}
Shape load(unsigned index) {
    const RawShape& a=shapes[index];Shape s;s.fill=a.fill;s.stroke=a.stroke;s.strokeWidth=a.width;
    for(unsigned j=0;j<a.count;++j) {const RawContour& r=contours[a.first+j];Contour c;c.closed=r.closed;
        c.points.assign(points+r.first,points+r.first+r.count);s.contours.push_back(c);}return s;
}
void transform(Shape& s,double scale,double x,double y) {
    s.strokeWidth*=scale;
    for(size_t c=0;c<s.contours.size();++c)for(size_t i=0;i<s.contours[c].points.size();++i) {
        Point& p=s.contours[c].points[i];p.x=p.x*scale+x;p.y=p.y*scale+y;
    }
}
Shape line(double x,double top,double bottom,Color::Value color,double width) {
    Shape s;s.stroke=color;s.strokeWidth=width;Contour c;Point a={x,top},b={x,bottom};
    c.points.push_back(a);c.points.push_back(b);s.contours.push_back(c);return s;
}
}
bool finite(double v) {return v==v&&v<=std::numeric_limits<double>::max()&&v>=-std::numeric_limits<double>::max();}
const ManeuverInfo& describe(Maneuver::Value m) {return entry(m).info;}
const char* rgb(Color::Value c) {
    switch(c) {case Color::Graphite:return "#303030";case Color::Gold:return "#858585";
    case Color::White:return "#ffffff";case Color::Gray:return "#858d98";case Color::Black:return "#303030";case Color::Red:return "#ff0000";default:return "none";}
}
// Dynamic primitives share the kit's exact semantic colours and design units.
namespace {
Point xy(double x,double y) {Point p={x,y};return p;}
Shape contour(const std::vector<Point>& ps,bool closed,Color::Value fill,Color::Value stroke,double width) {
    Shape s;s.fill=fill;s.stroke=stroke;s.strokeWidth=width;Contour c;c.closed=closed;c.points=ps;s.contours.push_back(c);return s;
}
Point polar(double angle,double radius) {double a=angle*3.141592653589793/180;return xy(500+sin(a)*radius,475-cos(a)*radius);}
void arc(std::vector<Point>& ps,double first,double last,double radius) {
    unsigned n=(unsigned)ceil(fabs(last-first)/7.5);if(!n)n=1;
    for(unsigned i=0;i<=n;++i)ps.push_back(polar(first+(last-first)*i/n,radius));
}
void quadratic(std::vector<Point>& ps,Point control,Point end) {
    Point begin=ps.back();for(unsigned i=1;i<=8;++i) {double t=i/8.0,u=1-t;
        ps.push_back(xy(u*u*begin.x+2*u*t*control.x+t*t*end.x,u*u*begin.y+2*u*t*control.y+t*t*end.y));}
}
void exitNumber(Scene& scene,unsigned number) {
    if(!number||number>99)return;
    static const unsigned mask[]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
    static const int seg[7][4]={{0,0,60,0},{60,0,60,70},{60,70,60,140},{60,140,0,140},{0,140,0,70},{0,70,0,0},{0,70,60,70}};
    unsigned digits=number>9?2:1;
    for(unsigned d=0;d<digits;++d) {unsigned n=digits==1?number:d?number%10:number/10;double x=digits==1?470:430+d*80;
        for(unsigned j=0;j<7;++j)if(mask[n]&(1<<j)) {std::vector<Point> ps;
            ps.push_back(xy(x+seg[j][0],405+seg[j][1]));ps.push_back(xy(x+seg[j][2],405+seg[j][3]));
            scene.push_back(contour(ps,false,Color::None,Color::White,17));}
    }
}
// Flat composition in design units. Area fitting is separate and always uniform.
// A single closed silhouette contains the shaft AND head; no fat native line.
std::vector<Point> silhouette(const std::vector<Point>& input,double half,bool head) {
    // Adjacent arc/quadratic primitives share endpoints. Remove only those
    // duplicates before computing normals; never change the maneuver angle.
    std::vector<Point> path;
    for(size_t i=0;i<input.size();++i)
        if(path.empty()||fabs(input[i].x-path.back().x)>1e-9||fabs(input[i].y-path.back().y)>1e-9)path.push_back(input[i]);
    std::vector<Point> left,right,out;if(path.size()<2)return out;
    for(size_t i=0;i<path.size();++i) {
        Point before=i?xy(path[i].x-path[i-1].x,path[i].y-path[i-1].y):xy(path[1].x-path[0].x,path[1].y-path[0].y);
        Point after=i+1<path.size()?xy(path[i+1].x-path[i].x,path[i+1].y-path[i].y):before;
        double a=sqrt(before.x*before.x+before.y*before.y),b=sqrt(after.x*after.x+after.y*after.y);
        if(a<1e-9||b<1e-9)return out;
        before.x/=a;before.y/=a;after.x/=b;after.y/=b;
        Point normal=xy(-before.y-after.y,before.x+after.x);double n=sqrt(normal.x*normal.x+normal.y*normal.y);
        if(n<1e-9)normal=xy(-after.y,after.x);else {normal.x/=n;normal.y/=n;}
        double cosine=normal.x*(-after.y)+normal.y*after.x;
        double distance=half/std::max(0.5,cosine); // bounded miter, no perspective
        left.push_back(xy(path[i].x+normal.x*distance,path[i].y+normal.y*distance));
        right.push_back(xy(path[i].x-normal.x*distance,path[i].y-normal.y*distance));
    }
    out=left;
    if(head) {
        Point end=path.back(),prev=path[path.size()-2];double dx=end.x-prev.x,dy=end.y-prev.y,n=sqrt(dx*dx+dy*dy);dx/=n;dy/=n;
        out.push_back(xy(end.x-dy*half*2.3,end.y+dx*half*2.3));
        out.push_back(xy(end.x+dx*half*2.5,end.y+dy*half*2.5));
        out.push_back(xy(end.x+dy*half*2.3,end.y-dx*half*2.3));
    }
    for(size_t i=right.size();i>0;--i)out.push_back(right[i-1]);return out;
}
Shape road(const std::vector<Point>& path,double half) {
    return contour(silhouette(path,half,false),true,Color::Graphite,Color::Gold,6);
}
Shape guidance(const std::vector<Point>& path,double half,bool head=true) {
    return contour(silhouette(path,half,head),true,Color::White,Color::Red,6);
}
std::vector<Point> roadPath(int direction,bool uturn) {
    std::vector<Point> p;
    if(uturn) {
        const int side=direction<0?-1:1;
        p.push_back(xy(500-side*250,900));p.push_back(xy(500-side*250,350));
        for(unsigned i=1;i<=16;++i){double a=3.141592653589793*i/16;
            p.push_back(xy(500-side*250*cos(a),350-250*sin(a)));}
        p.push_back(xy(500+side*250,650));return p;
    }
    p.push_back(xy(500,900));
    if(!direction){p.push_back(xy(500,100));return p;}
    p.push_back(xy(500,560));
    if(direction==90||direction==-90||direction==135||direction==-135) {
        int side=direction<0?-1:1;const unsigned steps=(direction==90||direction==-90)?8:12;
        double turn=(direction<0?-direction:direction)*3.141592653589793/180;
        for(unsigned i=1;i<=steps;++i){double a=turn*i/steps;
            p.push_back(xy(500+side*220*(1-cos(a)),560-220*sin(a)));}
        Point end=p.back();p.push_back(xy(end.x+side*sin(turn)*330,end.y-cos(turn)*330));
    } else {
        double a=direction*3.141592653589793/180;
        p.push_back(xy(500+sin(a)*500,560-cos(a)*500));
    }
    return p;
}
Scene flatRoad(Maneuver::Value m) {
    int direction=0;bool uturn=false,branch=false,merge=false;
    switch(m) {
    case Maneuver::KeepLeft:case Maneuver::ForkLeft:direction=-35;branch=true;break;
    case Maneuver::KeepRight:case Maneuver::ForkRight:direction=35;branch=true;break;
    case Maneuver::SlightLeft:direction=-45;break;case Maneuver::SlightRight:direction=45;break;
    case Maneuver::Left:direction=-90;break;case Maneuver::Right:direction=90;break;
    case Maneuver::SharpLeft:direction=-135;break;case Maneuver::SharpRight:direction=135;break;
    case Maneuver::UTurnLeft:direction=-1;uturn=true;break;case Maneuver::UTurnRight:direction=1;uturn=true;break;
    case Maneuver::RampSlightLeft:case Maneuver::ExitSlightLeft:direction=-45;branch=true;break;
    case Maneuver::RampSlightRight:case Maneuver::ExitSlightRight:direction=45;branch=true;break;
    case Maneuver::RampLeft:case Maneuver::ExitLeft:direction=-90;branch=true;break;
    case Maneuver::RampRight:case Maneuver::ExitRight:direction=90;branch=true;break;
    case Maneuver::RampSharpLeft:direction=-135;branch=true;break;
    case Maneuver::RampSharpRight:direction=135;branch=true;break;
    case Maneuver::RampUTurnLeft:direction=-1;uturn=true;break;
    case Maneuver::RampUTurnRight:direction=1;uturn=true;break;
    case Maneuver::MergeLeft:case Maneuver::MergeRight:case Maneuver::Merge:merge=true;break;
    default:break;
    }
    Scene scene;std::vector<Point> route=roadPath(direction,uturn);
    const double half=uturn?120:direction?180:310;
    // Junctions use full-size roads meeting the shared carriageway, not two
    // reduced copies of a single arrow. The recommended route stays continuous.
    if(branch)scene.push_back(road(roadPath(direction<0?35:-35,false),180));
    if(merge) {
        int side=m==Maneuver::MergeRight?1:-1;
        std::vector<Point> incoming;incoming.push_back(xy(500+side*520,900));incoming.push_back(xy(500,500));incoming.push_back(xy(500,100));
        scene.push_back(road(incoming,180));
        if(m==Maneuver::Merge){incoming[0].x=1020;scene.push_back(road(incoming,180));}
    }
    scene.push_back(road(route,half));
    // Shorten the body centreline by the head length so its tip meets the road
    // endpoint. Fit the whole composition without cropping or axis stretching.
    std::vector<Point> arrow=route;
    Point& end=arrow.back();Point previous=arrow[arrow.size()-2];double dx=end.x-previous.x,dy=end.y-previous.y,n=sqrt(dx*dx+dy*dy);
    const double arrowHalf=uturn?45:60;
    end.x-=dx/n*arrowHalf*2.5;end.y-=dy/n*arrowHalf*2.5;
    scene.push_back(guidance(arrow,uturn?45:60));return scene;
}
void symbolStyle(Scene& scene) {
    for(size_t i=0;i<scene.size();++i) {
        Shape& s=scene[i];
        if(s.stroke==Color::White&&!s.contours.empty()&&!s.contours[0].closed) {
            s=guidance(s.contours[0].points,s.strokeWidth/2,false);
        } else {
            if(s.stroke!=Color::None){s.stroke=Color::Red;s.strokeWidth=6;}
            if(s.fill==Color::White&&s.stroke==Color::None){s.stroke=Color::Red;s.strokeWidth=6;}
        }
    }
}

Scene roundabout(const NavigationEvent& e) {
    Scene scene;bool cw=e.maneuver==Maneuver::RoundaboutCW||e.maneuver==Maneuver::RoundaboutCWAngle||
        e.maneuver==Maneuver::RoundaboutEnterCW||e.maneuver==Maneuver::RoundaboutExitCW;
    double sign=cw?1:-1;bool enter=e.maneuver==Maneuver::RoundaboutEnterCW||e.maneuver==Maneuver::RoundaboutEnterCCW;
    bool known=e.hasExitAngle&&finite(e.exitAngle)&&e.exitAngle>=1&&e.exitAngle<=360&&!enter;
    double exit=180+sign*e.exitAngle;
    // Kit ring: centre (500,475), outer radius 360, inner radius 190,
    // wide carriageway, 6-unit curbs, filled white/red guidance.
    Shape ring;ring.fill=Color::Graphite;Contour outer,inner;outer.closed=inner.closed=true;
    arc(outer.points,0,352.5,360);arc(inner.points,0,352.5,190);
    ring.contours.push_back(outer);ring.contours.push_back(inner);scene.push_back(ring);
    scene.push_back(contour(outer.points,true,Color::None,Color::Gold,6));
    scene.push_back(contour(inner.points,true,Color::None,Color::Gold,6));
    // Draw branch roads over the ring's exterior curb, retaining the inner hole.
    std::vector<Point> entry;entry.push_back(xy(414,700));entry.push_back(xy(586,700));entry.push_back(xy(586,900));entry.push_back(xy(414,900));
    scene.push_back(contour(entry,true,Color::Graphite,Color::None,0));
    scene.push_back(line(414,700,900,Color::Gold,6));scene.push_back(line(586,700,900,Color::Gold,6));
    if(known) {
        double a=exit*3.141592653589793/180,dx=sin(a),dy=-cos(a),nx=-dy*86,ny=dx*86;
        Point begin=polar(exit,275),end=polar(exit,425);std::vector<Point> road;
        road.push_back(xy(begin.x+nx,begin.y+ny));road.push_back(xy(end.x+nx,end.y+ny));
        road.push_back(xy(end.x-nx,end.y-ny));road.push_back(xy(begin.x-nx,begin.y-ny));
        scene.push_back(contour(road,true,Color::Graphite,Color::None,0));
        for(int side=-1;side<=1;side+=2) {std::vector<Point> curb;curb.push_back(xy(begin.x+nx*side,begin.y+ny*side));curb.push_back(xy(end.x+nx*side,end.y+ny*side));
            scene.push_back(contour(curb,false,Color::None,Color::Gold,6));}
    }
    std::vector<Point> path;path.push_back(xy(500,897));path.push_back(xy(500,782));
    double sweep=known?e.exitAngle:50;double bend=std::min(20.0,sweep/3);
    quadratic(path,xy(500,702),polar(180+sign*bend,275));
    if(known) {
        if(sweep>2*bend)arc(path,180+sign*bend,exit-sign*bend,275);
        quadratic(path,polar(exit,275),polar(exit,350));
        scene.push_back(guidance(path,28));
    } else {
        arc(path,180+sign*bend,180+sign*50,275);
        scene.push_back(guidance(path,28,false));
        // Generic circulation only: no invented exit branch or radial arrow.
    }
    if(e.hasExitNumber){Scene digits;exitNumber(digits,e.exitNumber);symbolStyle(digits);scene.insert(scene.end(),digits.begin(),digits.end());}return scene;
}
void fit(Scene& scene,Rect area,double margin) {
    double l=std::numeric_limits<double>::max(),t=l,r=-l,b=-l;
    for(size_t i=0;i<scene.size();++i)for(size_t c=0;c<scene[i].contours.size();++c)for(size_t j=0;j<scene[i].contours[c].points.size();++j) {
        const Point& p=scene[i].contours[c].points[j];double radius=scene[i].strokeWidth/2;
        l=std::min(l,p.x-radius);r=std::max(r,p.x+radius);t=std::min(t,p.y-radius);b=std::max(b,p.y+radius);
    }
    if(l>=r||t>=b)return;
    double scale=std::min((area.width-2*margin)/(r-l),(area.height-2*margin)/(b-t));
    for(size_t i=0;i<scene.size();++i)transform(scene[i],scale,area.x+(area.width-(l+r)*scale)/2,area.y+(area.height-(t+b)*scale)/2);
}
Shape laneArrow(int shape,bool highlighted) {
    // Same stroke/fill vocabulary as the supplied lane shapes; each direction
    // keeps its exact AA category and its own recommendation.
    static const double angle[]={0,0,-45,45,-90,90,-135,135,-180,180};
    std::vector<Point> path;path.push_back(xy(0,65));path.push_back(xy(0,0));
    double a=angle[shape]*3.141592653589793/180;
    if(shape>=8) {double side=shape==8?-1:1;path.push_back(xy(side*36,-35));path.push_back(xy(side*65,-10));path.push_back(xy(side*65,25));}
    else if(shape!=1)path.push_back(xy(sin(a)*55,-cos(a)*55));
    else path.back()=xy(0,-65);
    Point end=path.back(),prev=path[path.size()-2];double dx=end.x-prev.x,dy=end.y-prev.y,n=sqrt(dx*dx+dy*dy);dx/=n;dy/=n;
    // Offset centre line, with bounded miters. Side-specific points give a
    // single closed silhouette, instead of substituting an unrelated icon.
    std::vector<Point> left,right;
    for(size_t i=0;i<path.size();++i) {
        Point d=i+1<path.size()?xy(path[i+1].x-path[i].x,path[i+1].y-path[i].y):xy(dx,dy);
        double len=sqrt(d.x*d.x+d.y*d.y);d.x/=len;d.y/=len;
        left.push_back(xy(path[i].x-d.y*11,path[i].y+d.x*11));right.push_back(xy(path[i].x+d.y*11,path[i].y-d.x*11));
    }
    std::vector<Point> outline=left;
    outline.push_back(xy(end.x-dy*23,end.y+dx*23));outline.push_back(xy(end.x+dx*30,end.y+dy*30));outline.push_back(xy(end.x+dy*23,end.y-dx*23));
    for(size_t i=right.size();i>0;--i)outline.push_back(right[i-1]);
    return contour(outline,true,highlighted?Color::White:Color::Graphite,highlighted?Color::Red:Color::Gray,3.5);
}
}
Scene renderManeuver(const NavigationEvent& e,Rect area,double margin) {
    Scene scene;const Entry& a=entry(e.maneuver);
    if(!valid(area)||!finite(margin)||margin<0||2*margin>=std::min(area.width,area.height))return scene;
    bool round=(e.maneuver>=Maneuver::RoundaboutCompatA&&e.maneuver<=Maneuver::RoundaboutCCWAngle)||
        (e.maneuver>=Maneuver::RoundaboutEnterCW&&e.maneuver<=Maneuver::RoundaboutExitCCW);
    if(round)scene=roundabout(e);
    else if((e.maneuver>=Maneuver::Start&&e.maneuver<=Maneuver::Merge)||e.maneuver==Maneuver::Straight)scene=flatRoad(e.maneuver);
    else {for(unsigned i=0;i<a.count;++i)scene.push_back(load(a.offset+i));symbolStyle(scene);}
    fit(scene,area,margin);return scene;
}
Scene renderLanes(const std::vector<Lane>& lanes,Rect area) {
    Scene scene;if(!valid(area)||lanes.empty()||lanes.size()>8)return scene;
    double cell=area.width/lanes.size(),width=std::min(1.4,std::min(cell,area.height)/8);
    Shape bg;bg.fill=Color::Graphite;std::vector<Point> bgPoints;
    bgPoints.push_back(xy(area.x,area.y));bgPoints.push_back(xy(area.x+area.width,area.y));
    bgPoints.push_back(xy(area.x+area.width,area.y+area.height));bgPoints.push_back(xy(area.x,area.y+area.height));
    scene.push_back(contour(bgPoints,true,Color::Graphite,Color::None,0));
    scene.push_back(line(area.x+width/2,area.y+width,area.y+area.height-width,Color::Gold,width));
    scene.push_back(line(area.x+area.width-width/2,area.y+width,area.y+area.height-width,Color::Gold,width));
    for(size_t i=0;i<lanes.size();++i) {
        if(i)for(int dash=0;dash<4;++dash) {double y=area.y+(dash+.24)*area.height/4;
            scene.push_back(line(area.x+i*cell,y,y+area.height/8,Color::Gray,width));}
        // The kit's approved composite silhouettes remain exact for the cases
        // they describe. Rich per-branch data selects the parametric path below,
        // never a different renderer or a lossy three-bit conversion.
        unsigned mask=0;bool uniform=true,have=false,highlighted=false;
        for(size_t j=0;j<lanes[i].directions.size();++j) {
            const Direction& d=lanes[i].directions[j];
            if(d.shape==0){uniform=false;continue;}
            if(d.shape!=1&&d.shape!=4&&d.shape!=5){uniform=false;continue;}
            if(have&&highlighted!=d.highlighted)uniform=false;
            have=true;highlighted=d.highlighted;mask|=d.shape==1?LaneStraight:d.shape==4?LaneLeft:LaneRight;
        }
        if(uniform&&mask) {
            Shape arrow=load(laneOffsets[mask-1]);arrow.fill=highlighted?Color::White:Color::None;
            arrow.stroke=highlighted?Color::Red:Color::Gray;arrow.strokeWidth=3.5;
            double l=std::numeric_limits<double>::max(),t=l,r=-l,b=-l;
            for(size_t j=0;j<arrow.contours[0].points.size();++j){const Point& p=arrow.contours[0].points[j];
                l=std::min(l,p.x);r=std::max(r,p.x);t=std::min(t,p.y);b=std::max(b,p.y);}
            double scale=std::min((cell-4*width)/187.5,(area.height-4*width)/142.5);
            transform(arrow,scale,area.x+(i+.5)*cell-(l+r)*scale/2,area.y+(area.height-(t+b)*scale)/2);
            scene.push_back(arrow);continue;
        }
        for(unsigned pass=0;pass<2;++pass)for(size_t j=0;j<lanes[i].directions.size();++j) {
            const Direction& d=lanes[i].directions[j];if((d.highlighted?1u:0u)!=pass||d.shape<1||d.shape>9)continue; // position still reserved
            Shape arrow=laneArrow(d.shape,d.highlighted);Scene one;one.push_back(arrow);
            Rect box(area.x+i*cell+2*width,area.y+2*width,cell-4*width,area.height-4*width);
            // Constant design envelope across recommendation and direction: no
            // resizing individual branches as their highlighting changes.
            double scale=std::min(box.width/210.0,box.height/210.0);
            transform(one[0],scale,box.x+box.width/2,box.y+box.height/2);
            scene.push_back(one[0]);
        }
    }return scene;
}
} }
