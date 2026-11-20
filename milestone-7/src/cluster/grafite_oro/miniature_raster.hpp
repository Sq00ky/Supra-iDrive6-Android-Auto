#ifndef NBT_6WB_MINIATURE_RASTER_HPP
#define NBT_6WB_MINIATURE_RASTER_HPP
#include "artwork6wb.hpp"
#include <stddef.h>
#include <stdint.h>
namespace nbt { namespace sixwbkit {
inline bool inside(const sixwbkit::Shape& s,double x,double y) {
    bool hit=false;for(size_t c=0;c<s.contours.size();++c){const std::vector<sixwbkit::Point>& ps=s.contours[c].points;
        for(size_t i=0,j=ps.size()?ps.size()-1:0;i<ps.size();j=i++)if((ps[i].y>y)!=(ps[j].y>y)&&
            x<(ps[j].x-ps[i].x)*(y-ps[i].y)/(ps[j].y-ps[i].y)+ps[i].x)hit=!hit;}
    return hit;
}
inline bool onStroke(const sixwbkit::Shape& s,double x,double y) {
    double radius=s.strokeWidth/2;
    for(size_t c=0;c<s.contours.size();++c){const sixwbkit::Contour& ct=s.contours[c];size_t n=ct.points.size();
        size_t count=ct.closed?n:n?n-1:0;
        for(size_t i=0;i<count;++i){const sixwbkit::Point& a=ct.points[i];const sixwbkit::Point& b=ct.points[(i+1)%n];
            double dx=b.x-a.x,dy=b.y-a.y,d=dx*dx+dy*dy,t=d?((x-a.x)*dx+(y-a.y)*dy)/d:0;
            if(t<0)t=0;if(t>1)t=1;double xx=x-a.x-t*dx,yy=y-a.y-t*dy;
            if(xx*xx+yy*yy<=radius*radius)return true;}
    }return false;
}
inline void rasterKit(std::vector<uint32_t>& pixels,unsigned w,unsigned h,const sixwbkit::Scene& scene) {
    for(size_t i=0;i<scene.size();++i)for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        const sixwbkit::Shape& s=scene[i];
        if(s.fill!=sixwbkit::Color::None&&inside(s,x+.5,y+.5))pixels[(size_t)y*w+x]=0xffffffff;
        if(s.stroke!=sixwbkit::Color::None&&onStroke(s,x+.5,y+.5))pixels[(size_t)y*w+x]=0xffffffff;
    }
}
} }
#endif
