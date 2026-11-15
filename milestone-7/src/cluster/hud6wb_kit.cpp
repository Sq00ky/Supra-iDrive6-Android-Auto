#include "cluster/hud6wb_kit.hpp"
#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <errno.h>
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
typedef std::map<std::string,long> Configuration;
Configuration configuration() {
    Configuration values;FILE* f=fopen("/opt/aa/6wb-kit.palette","r");if(!f)return values;
    char line[128];unsigned count=0;bool ok=true;
    while(fgets(line,sizeof(line),f)) {
        if(++count>48||(!strchr(line,'\n')&&!feof(f))){ok=false;break;}
        if(line[0]=='#'||line[0]=='\n'||line[0]=='\r')continue;
        char* eq=strchr(line,'=');if(!eq){ok=false;break;}*eq++=0;char* end=NULL;errno=0;long v=strtol(eq,&end,0);
        while(end&&(*end=='\r'||*end=='\n'))++end;
        if(errno||end==eq||!end||*end||values.count(line)){ok=false;break;}values[line]=v;
    }
    if(ferror(f))ok=false;fclose(f);if(!ok)values.clear();return values;
}
long setting(const Configuration& c,const char* name,long fallback) {Configuration::const_iterator i=c.find(name);return i==c.end()?fallback:i->second;}
sixwbkit::HudTerminalPalette palette(const Configuration& c,const char* prefix) {
    sixwbkit::HudTerminalPalette p;const char* names[]={"graphite","gold","white","gray","black"};int* fields[]={&p.graphite,&p.gold,&p.white,&p.gray,&p.black};
    for(unsigned i=0;i<5;++i){std::string key=std::string(prefix)+names[i];long v=setting(c,key.c_str(),-1);if(v>=0&&v<20)*fields[i]=(int)v;}
    return p;
}
}
GrafiteKit::GrafiteKit() {
    readProfile();
    printf("[6wb kit] profile /opt/aa/6wb-kit.palette; indices graphite/gold/white/gray/black: perspective %d/%d/%d/%d/%d, flat %d/%d/%d/%d/%d, lanes %d/%d/%d/%d/%d (-1=unmapped)\n",
        main_.graphite,main_.gold,main_.white,main_.gray,main_.black,flat_.graphite,flat_.gold,flat_.white,flat_.gray,flat_.black,
        lanes_.graphite,lanes_.gold,lanes_.white,lanes_.gray,lanes_.black);
}
void GrafiteKit::readProfile() {
    Configuration c=configuration();main_=palette(c,"main.");flat_=palette(c,"flat.");lanes_=palette(c,"lanes.");
    // Explicit configurable indices, never RGB-as-index or guessed defaults.
}
void configureKitPlanes(Bmw6wbProfile& p) {
    Configuration c=configuration();long fa=setting(c,"flat.a",0),fb=setting(c,"flat.b",0);
    long pa=setting(c,"perspective.a",p.main.a),pb=setting(c,"perspective.b",p.main.b);
    if(fa>0&&fa<=255&&fb>0&&fb<=255){p.flat.a=(uint16_t)fa;p.flat.b=(uint16_t)fb;}
    if(pa>0&&pa<=255&&pb>0&&pb<=255){p.main.a=(uint16_t)pa;p.main.b=(uint16_t)pb;p.base.bufA=p.main.a;p.base.bufB=p.main.b;}
    // Dimensions remain the documented 221x158; actual C04 must match. The
    // unidentified flat pair has NO default. No scanning or temporary test backend dependency.
}
KitBarState kitBarState(const kombinav::NavView& v,unsigned range) {
    KitBarState s;double metres=0;s.visible=kombinav::distanceMetres(v,metres);
    s.value=metres<=0?0:metres>=range?255:(int)(metres*255/range);return s;
}
KitPlane kitPlane(sixwbkit::PlanePolicy::Value policy,bool visible) {
    return policy==sixwbkit::PlanePolicy::DistanceBased&&visible?KitPerspective:KitFlat;
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
            unsigned width=(unsigned)floor(s.strokeWidth+.5);if(!width)width=1;
            b.add(kSetLineColor,setLineColor(h,color));b.add(kSetLineWidth,setLineWidth(h,(uint8_t)width));
            for(size_t c=0;c<s.contours.size();++c) {
                const sixwbkit::Contour& contour=s.contours[c];std::vector<hudterm::Point> ps;
                for(size_t j=0;j<contour.points.size();++j) {
                    const sixwbkit::Point& p=contour.points[j];
                    if(!sixwbkit::finite(p.x)||!sixwbkit::finite(p.y)||p.x<bounds.x0||p.x>bounds.x1||p.y<bounds.y0||p.y>bounds.y1)return false;
                    hudterm::Point q;q.x=(int16_t)floor(p.x+.5);q.y=(int16_t)floor(p.y+.5);
                    if(ps.empty()||ps.back().x!=q.x||ps.back().y!=q.y)ps.push_back(q);
                }
                if(contour.closed && !ps.empty() && (ps.front().x!=ps.back().x||ps.front().y!=ps.back().y))ps.push_back(ps.front());
                for(size_t first=0;first<ps.size();) {
                    size_t end=std::min(first+99,ps.size());
                    std::vector<hudterm::Point> part(ps.begin()+first,ps.begin()+end);
                    b.add(kAddPolygon,addPolygon(h,(uint16_t)item++,kPolyline,part));
                    if(b.calls.size()>2048)return false;
                    if(end==ps.size())break;first=end-1;
                }
            }
        }
        if(b.calls.size()>2048)return false;
    }
    out.calls.insert(out.calls.end(),b.calls.begin(),b.calls.end());return true;
}
bool GrafiteKit::central(const kombinav::NavView& v,uint16_t h,const Rect& r,bool perspective,Batch& out)const {
    if(v.aa_type<0||v.aa_type>46)return false; // report an unsupported code, never substitute straight-on
    sixwbkit::NavigationEvent e;e.maneuver=maneuver(v.aa_type);
    e.hasExitAngle=v.has_exit_angle;e.exitAngle=(double)v.exit_angle;e.hasExitNumber=v.has_exit_number;e.exitNumber=(unsigned)v.exit_number;
    if(e.hasExitAngle&&(!sixwbkit::finite(e.exitAngle)||e.exitAngle<1||e.exitAngle>360))return false;
    sixwbkit::Rect area(r.x0,r.y0,r.x1-r.x0,r.y1-r.y0);
    return encode(sixwbkit::renderManeuver(e,area),perspective?main_:flat_,h,r,out);
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
