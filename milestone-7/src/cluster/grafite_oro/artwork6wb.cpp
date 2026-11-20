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
}
bool finite(double v) {return v==v&&v<=std::numeric_limits<double>::max()&&v>=-std::numeric_limits<double>::max();}
const ManeuverInfo& describe(Maneuver::Value m) {return entry(m).info;}
const char* rgb(Color::Value c) {
    switch(c) {case Color::Graphite:return "#303030";case Color::Gold:return "#858585";
    case Color::ExitWhite:case Color::White:return "#ffffff";case Color::Gray:return "#858d98";case Color::Black:return "#303030";case Color::Red:return "#ff0000";default:return "none";}
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
    static const int segments[7][4]={{0,0,74,0},{74,0,74,75},{74,75,74,150},
        {74,150,0,150},{0,150,0,75},{0,75,0,0},{0,75,74,75}};
    unsigned digits=number>9?2:1;
    for(unsigned layer=0;layer<2;++layer) {
        for(unsigned d=0;d<digits;++d) {
            unsigned n=digits==1?number:d?number%10:number/10;
            double x=(digits==1?463:411+d*104)-(n==1?37:0);
            for(unsigned j=0;j<7;++j) if(mask[n]&(1<<j)) {
                std::vector<Point> ps;
                ps.push_back(xy(x+segments[j][0],400+segments[j][1]));
                ps.push_back(xy(x+segments[j][2],400+segments[j][3]));
                scene.push_back(contour(ps,false,Color::None,layer?Color::ExitWhite:Color::Gold,layer?24:34));
            }
        }
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
void roadLayer(Scene& scene,const std::vector<Point>& path,double half,Color::Value color) {
    scene.push_back(contour(silhouette(path,half,false),true,color,Color::None,0));
}
void roads(Scene& scene,const std::vector<std::vector<Point> >& paths,double half) {
    for(unsigned layer=0;layer<2;++layer)
        for(size_t i=0;i<paths.size();++i)
            roadLayer(scene,paths[i],half+(layer?0:13),layer?Color::Graphite:Color::Gold);
}
Shape guidance(const std::vector<Point>& path,double half,bool head=true) {
    return contour(silhouette(path,half,head),true,Color::White,Color::Red,13);
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
        Point end=xy(500+sin(a)*170,560-cos(a)*170-90);
        quadratic(p,xy(500,470),end);
        p.push_back(xy(end.x+sin(a)*360,end.y-cos(a)*360));
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
    const double half=uturn?95:145;
    std::vector<std::vector<Point> > paths(1,route);
    if(branch)paths.push_back(roadPath(direction<0?35:-35,false));
    if(merge) {
        for(int side=-1;side<=1;side+=2) {
            if(m!=Maneuver::Merge && side!=(m==Maneuver::MergeRight?1:-1))continue;
            std::vector<Point> incoming;
            incoming.push_back(xy(500+side*480,900));
            incoming.push_back(xy(500+side*220,720));
            quadratic(incoming,xy(500,540),xy(500,410));
            incoming.push_back(xy(500,100));
            paths.push_back(incoming);
        }
    }
    roads(scene,paths,half);
    // Shorten the body centreline by the head length so its tip meets the road
    // endpoint. Fit the whole composition without cropping or axis stretching.
    std::vector<Point> arrow=route;
    Point& end=arrow.back();Point previous=arrow[arrow.size()-2];double dx=end.x-previous.x,dy=end.y-previous.y,n=sqrt(dx*dx+dy*dy);
    const double arrowHalf=uturn?45:60;
    end.x-=dx/n*arrowHalf*2.5;end.y-=dy/n*arrowHalf*2.5;
    scene.push_back(guidance(arrow,uturn?45:60));return scene;
}
Shape panel(double left,double top,double right,double bottom,Color::Value color) {
    const Point corners[]={xy(left,top),xy(right,top),xy(right,bottom),xy(left,bottom)};
    return contour(std::vector<Point>(corners,corners+4),true,color,Color::None,0);
}

Scene destinationArtwork(Maneuver::Value maneuver) {
    Scene scene;
    // 221 x 158 surface: flag at the left, full-height road on the right.
    scene.push_back(panel(0,32,98,108,Color::Gray));
    const double cell=22;
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col) {
        const double x=5+col*cell,y=37+row*cell;
        scene.push_back(panel(x,y,x+cell,y+cell,(row+col)%2?Color::Black:Color::ExitWhite));
    }
    scene.push_back(panel(111,0,220,157,Color::Gray));
    scene.push_back(panel(115,0,216,157,Color::Graphite));

    std::vector<Point> path;
    path.push_back(xy(165,145));
    if(maneuver==Maneuver::DestinationLeft || maneuver==Maneuver::DestinationRight) {
        const double direction=maneuver==Maneuver::DestinationLeft?-1:1;
        const Point bend=xy(165,95);
        path.push_back(bend);
        quadratic(path,xy(165,61),xy(165+direction*21,61));
        path.push_back(xy(165+direction*29,61));
    } else {
        path.push_back(xy(165,27));
    }
    Shape arrow=guidance(path,6);
    arrow.strokeWidth=3.5;
    scene.push_back(arrow);
    return scene;
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
    double sign=cw?1:-1;
    const bool enter=e.maneuver==Maneuver::RoundaboutEnterCW||e.maneuver==Maneuver::RoundaboutEnterCCW;
    const unsigned number=e.hasExitNumber&&e.exitNumber<=99?e.exitNumber:0;
    const bool known=e.hasExitAngle&&finite(e.exitAngle)&&e.exitAngle>=1&&e.exitAngle<=360;
    const bool hasExit=!enter&&(known||number);
    // When only an ordinal is present, reserve at least one entry gap.
    // Intermediate roads are schematic: AA supplies a target angle and ordinal.
    double sweep=known?e.exitAngle:std::min(300.0,90.0*number);
    if(!hasExit)sweep=55;
    else sweep=std::min(360.0,sweep);
    const unsigned exits=hasExit?(number?number:1):0;
    const double start=180, end=start+sign*sweep;
    std::vector<std::vector<Point> > branches(1);
    branches[0].push_back(polar(start,238));branches[0].push_back(polar(start,418));
    for(unsigned i=1;i<=exits;++i) {
        const double angle=start+sign*sweep*i/exits;
        std::vector<Point> branch;
        branch.push_back(polar(angle,238));branch.push_back(polar(angle,418));
        branches.push_back(branch);
    }
    double interval=exits?std::min(sweep/exits,360-sweep):360;
    if(interval<1 && sweep==360) interval=sweep/exits;
    const double branchHalf=std::max(8.0,std::min(43.0,interval*1.5));
    for(unsigned layer=0;layer<2;++layer) {
        Shape ring;
        ring.fill=layer?Color::Graphite:Color::Gold;
        Contour outer,inner;outer.closed=inner.closed=true;
        arc(outer.points,0,352.5,layer?289:301);
        arc(inner.points,0,352.5,layer?122:110);
        ring.contours.push_back(outer);ring.contours.push_back(inner);
        scene.push_back(ring);
        for(size_t i=0;i<branches.size();++i)
            roadLayer(scene,branches[i],branchHalf+(layer?0:12),ring.fill);
    }
    std::vector<Point> path;
    if(sweep<355)path.push_back(polar(start,407));
    path.push_back(polar(start,320));
    const double bend=std::min(20.0,sweep/3);
    quadratic(path,polar(start,216),polar(start+sign*bend,207));
    arc(path,start+sign*bend,hasExit?end-sign*bend:end,207);
    if(hasExit)quadratic(path,polar(end,207),polar(end,362));
    scene.push_back(guidance(path,18));
    if(number)exitNumber(scene,number);
    return scene;
}

void fit(Scene& scene,Rect area,double margin,bool topLeft) {
    double l=std::numeric_limits<double>::max(),t=l,r=-l,b=-l;
    for(size_t i=0;i<scene.size();++i)for(size_t c=0;c<scene[i].contours.size();++c)for(size_t j=0;j<scene[i].contours[c].points.size();++j) {
        const Point& p=scene[i].contours[c].points[j];double radius=scene[i].strokeWidth/2;
        l=std::min(l,p.x-radius);r=std::max(r,p.x+radius);t=std::min(t,p.y-radius);b=std::max(b,p.y+radius);
    }
    if(l>=r||t>=b)return;
    double scale=std::min((area.width-2*margin)/(r-l),(area.height-2*margin)/(b-t));
    const double x=topLeft?area.x-l*scale:area.x+(area.width-(l+r)*scale)/2;
    const double y=topLeft?area.y-t*scale:area.y+(area.height-(t+b)*scale)/2;
    for(size_t i=0;i<scene.size();++i)transform(scene[i],scale,x,y);
}
Shape laneTip(int shape,bool highlighted,Rect area,double outline) {
    // Left-facing templates. Right turns mirror them inside the same slot.
    static const Point tips[][3]={
        {{0,1},{.5,0},{1,1}},      // straight
        {{0,0},{1,.47},{.45,1}},   // slight
        {{0,.5},{1,0},{1,1}},      // normal
        {{0,.73},{.9,0},{1,1}},    // sharp
        {{0,1},{.26,0},{1,.33}}    // U-turn
    };
    const int kind=shape/2;
    const bool mirror=shape>1&&shape%2!=0;
    std::vector<Point> points;
    for(unsigned i=0;i<3;++i) {
        const Point& p=tips[kind][i];
        points.push_back(xy(area.x+area.width*(mirror?1-p.x:p.x),area.y+area.height*p.y));
    }
    return contour(points,true,highlighted?Color::White:Color::Graphite,
        highlighted?Color::Red:Color::Gold,outline);
}
void laneMark(Scene& scene,double x,double y0,double y1) {
    const Point mark[]={xy(x,y0),xy(x+1.6,y0),xy(x+1.6,y1),xy(x,y1)};
    scene.push_back(contour(std::vector<Point>(mark,mark+4),true,Color::Gold,Color::None,0));
}
Scene maneuverArtwork(const NavigationEvent& e,Rect area,double margin,bool miniature) {
    Scene scene;const Entry& a=entry(e.maneuver);
    if(!valid(area)||!finite(margin)||margin<0||2*margin>=std::min(area.width,area.height))return scene;
    bool round=(e.maneuver>=Maneuver::RoundaboutCompatA&&e.maneuver<=Maneuver::RoundaboutCCWAngle)||
        (e.maneuver>=Maneuver::RoundaboutEnterCW&&e.maneuver<=Maneuver::RoundaboutExitCCW);
    if(e.maneuver>=Maneuver::RoundaboutCCWExample1&&e.maneuver<=Maneuver::RoundaboutCWExample4) {
        NavigationEvent sample=e;
        const bool cw=e.maneuver>=Maneuver::RoundaboutCWExample1;
        sample.maneuver=cw?Maneuver::RoundaboutCW:Maneuver::RoundaboutCCW;
        sample.hasExitNumber=true;
        sample.exitNumber=1+e.maneuver-(cw?Maneuver::RoundaboutCWExample1:Maneuver::RoundaboutCCWExample1);
        scene=roundabout(sample);
        round=true;
    } else if(round)scene=roundabout(e);
    else if(!miniature&&e.maneuver>=Maneuver::Destination&&e.maneuver<=Maneuver::DestinationRight)
        scene=destinationArtwork(e.maneuver);
    else if((e.maneuver>=Maneuver::Start&&e.maneuver<=Maneuver::Merge)||e.maneuver==Maneuver::Straight)scene=flatRoad(e.maneuver);
    else {for(unsigned i=0;i<a.count;++i)scene.push_back(load(a.offset+i));symbolStyle(scene);}
    fit(scene,area,margin,miniature);
    if(round&&!miniature) {
        const double scale=0.96;
        const double cx=area.x+area.width/2,cy=area.y+area.height/2;
        for(size_t i=0;i<scene.size();++i)
            transform(scene[i],scale,cx*(1-scale),cy*(1-scale));
    }
    return scene;
}
}
Scene renderManeuver(const NavigationEvent& e,Rect area,double margin) {
    return maneuverArtwork(e,area,margin,false);
}
Scene renderMiniature(const NavigationEvent& e,Rect area) {
    Scene scene=maneuverArtwork(e,area,0,true);
    for(size_t i=0;i<scene.size();++i) {
        if(scene[i].fill!=Color::None) scene[i].fill=Color::White;
        if(scene[i].stroke!=Color::None) scene[i].stroke=Color::White;
    }
    return scene;
}
Scene renderLanes(const std::vector<Lane>& lanes,Rect area) {
    Scene scene;
    const double top=area.y+4,bottom=area.y+area.height;
    if(!valid(area)||lanes.empty()||lanes.size()>8||area.height<6)return scene;
    const double cell=area.width/lanes.size();
    if(cell<5)return scene;
    laneMark(scene,area.x,top,bottom);
    laneMark(scene,area.x+area.width-1.6,top,bottom);
    for(size_t i=1;i<lanes.size();++i) {
        const double x=area.x+i*cell-.8;
        for(double end=bottom;end>top;end-=9)
            laneMark(scene,x,std::max(top,end-5),end);
    }
    for(size_t i=0;i<lanes.size();++i) {
        const std::vector<Direction>& directions=lanes[i].directions;
        // Sort by direction so combined arrows retain a left-to-right order.
        static const int order[]={8,6,4,2,1,3,5,7,9};
        bool present[10]={false};
        unsigned count=0;
        bool highlighted=false;
        for(size_t j=0;j<directions.size();++j) {
            const int shape=directions[j].shape;
            if(shape<1||shape>9)continue;
            highlighted=highlighted||directions[j].highlighted;
            if(!present[shape]) {
                present[shape]=true;
                ++count;
            }
        }
        if(!count)continue;
        const double slot=(cell-4)/count;
        if(slot<=3)continue;
        unsigned index=0;
        for(size_t j=0;j<9;++j) {
            const int shape=order[j];
            if(!present[shape])continue;
            const double outline=1.5;
            const double width=std::min(30.0,slot-outline-1);
            const double center=area.x+i*cell+2+(index+.5)*slot;
            const Rect tipArea(center-width/2,top+outline/2,width,bottom-top-outline);
            scene.push_back(laneTip(shape,highlighted,tipArea,outline));
            ++index;
        }
    }
    return scene;
}
} }
