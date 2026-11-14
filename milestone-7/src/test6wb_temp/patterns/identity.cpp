// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Diagnostic geometry; bitmap encoding and vector methods remain the existing protocol.
#include "patterns/patterns.hpp"
#include "cluster/hud6wb_codec.hpp"
#include <stdio.h>
#include <stdlib.h>
namespace test6wb {
using namespace nbt::hudterm;
namespace {
struct Segment {int x,y,xx,yy;Segment(int a,int b,int c,int d):x(a),y(b),xx(c),yy(d){}};
typedef std::vector<Segment> Lines;
void add(Lines& lines,int x,int y,int xx,int yy) {lines.push_back(Segment(x,y,xx,yy));}
// Seven-segment hexadecimal digits, plus x/T. No native text method on images.
unsigned mask(char c) {
    static const unsigned masks[]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f,
                                   0x77,0x7c,0x39,0x5e,0x79,0x71};
    if(c>='0' && c<='9')return masks[c-'0'];
    if(c>='A' && c<='F')return masks[c-'A'+10];
    return 0;
}
int glyphY(int y,bool bitmap) {return bitmap?y*3/4:y*2;}
void label(Lines& lines,const char* text,int x,int y,bool bitmap) {
    static const int strokes[7][4]={{0,0,4,0},{4,0,4,4},{4,4,4,8},{0,8,4,8},
                                    {0,4,0,8},{0,0,0,4},{0,4,4,4}};
    for(const char* p=text;*p;++p,x+=12) {
        unsigned m=mask(*p);
        for(unsigned k=0;k<7;++k)if(m&(1u<<k))
            add(lines,x+strokes[k][0]*2,y+glyphY(strokes[k][1],bitmap),
                      x+strokes[k][2]*2,y+glyphY(strokes[k][3],bitmap));
        if(*p=='x'){add(lines,x,y+glyphY(2,bitmap),x+8,y+glyphY(6,bitmap));add(lines,x+8,y+glyphY(2,bitmap),x,y+glyphY(6,bitmap));}
        if(*p=='T'){add(lines,x,y,x+8,y);add(lines,x+4,y,x+4,y+glyphY(8,bitmap));}
    }
}
void guide(Lines& lines,int x,int h,bool bitmap) {
    if(!bitmap){add(lines,x,1,x,h-2);return;}
    // Sparse guides preserve the original RLE cap without discarding labels.
    for(int y=1;y<=h-2;y+=8)add(lines,x,y,x,y+1>h-2?h-2:y+1);
}
void outsideLabels(Lines& out,const Segment& s,int w,int h) {
    // Structure is axis-aligned. Split it instead of drawing through vector glyphs.
    int dx=s.xx==s.x?0:s.xx>s.x?1:-1,dy=s.yy==s.y?0:s.yy>s.y?1:-1;
    bool active=false;int beginX=0,beginY=0,lastX=0,lastY=0;
    for(int x=s.x,y=s.y;;x+=dx,y+=dy) {
        bool first=x>=17 && x<=123 && y>=6 && y<=24;
        bool second=x>=w-123 && x<=w-17 && y>=h-22 && y<=h-4;
        if(!first && !second) {
            if(!active){beginX=x;beginY=y;active=true;}lastX=x;lastY=y;
        } else if(active){add(out,beginX,beginY,lastX,lastY);active=false;}
        if(x==s.xx && y==s.yy)break;
    }
    if(active)add(out,beginX,beginY,lastX,lastY);
}
void raster(std::vector<uint32_t>& px,int w,int h,const Segment& l,uint32_t colour) {
    int x=l.x,y=l.y,dx=abs(l.xx-x),dy=-abs(l.yy-y),sx=x<l.xx?1:-1,sy=y<l.yy?1:-1,e=dx+dy;
    for(;;) {
        if(x>=0 && y>=0 && x<w && y<h)px[(size_t)y*w+x]=colour;
        if(x==l.xx && y==l.yy)break;
        int ee=2*e;if(ee>=dy){e+=dy;x+=sx;}if(ee<=dx){e+=dx;y+=sy;}
    }
}
bool colourValue(const std::string& name,uint32_t& colour) {
    if(name=="red")colour=0xffff0000;else if(name=="blue")colour=0xff0000ff;
    else if(name=="green")colour=0xff00ff00;else if(name=="white")colour=0xffffffff;
    else if(name=="black")colour=0xff000000;else return false;
    return true;
}
}
bool identifiedPattern(const Surface& s,unsigned trial,const std::string& variant,
                       const std::string& colour,Batch& b,std::string& error,bool border) {
    if((s.kind!=Bitmap && s.kind!=Vector) || trial<1 || trial>320) {
        error="identified pattern requires bitmap/vector handle and trial 1..320";return false;
    }
    if(variant!="identity" && variant!="bands"){error="pattern must be identity or bands";return false;}
    if(s.kind==Vector && colour!="theme"){error="vector colour is theme index 2; RGB unknown";return false;}
    const int w=s.width,h=s.height;Lines structure,glyphs;
    // Frame and centre cross; grid every 40 logical pixels, no fabricated screen coordinates.
    add(structure,0,0,w-1,0);add(structure,w-1,0,w-1,h-1);
    add(structure,w-1,h-1,0,h-1);add(structure,0,h-1,0,0);
    add(structure,w/2,0,w/2,h-1);add(structure,0,h/2,w-1,h/2);
    if(variant=="identity") {
        for(int x=40;x<w-1;x+=40)guide(structure,x,h,s.kind==Bitmap);
        for(int y=40;y<h-1;y+=40)add(structure,1,y,w-2,y);
    } else {
        // Handle-specific repetition survives partial clipping; wider spacing than the grid.
        unsigned count=s.handle==0x18?1:s.handle==0x19?2:s.handle==0x1f?3:
                       s.handle==0x20?4:s.handle==0x23?5:6;
        for(unsigned k=0;k<count;++k) {
            int x=20+(int)k*10;guide(structure,x,h,s.kind==Bitmap);
            guide(structure,w-1-x,h,s.kind==Bitmap);
        }
    }
    // Origin: double L at (0,0); other corners carry 2, 3 and 4 inward ticks.
    add(structure,1,10,1,1);add(structure,1,1,12,1);
    add(structure,3,8,3,3);add(structure,3,3,10,3);
    for(int k=0;k<2;++k)add(structure,w-4-k*3,2,w-4-k*3,6);
    for(int k=0;k<3;++k)add(structure,w-4-k*3,h-3,w-4-k*3,h-7);
    for(int k=0;k<4;++k)add(structure,3+k*3,h-3,3+k*3,h-7);
    char text[20];snprintf(text,sizeof(text),"0x%02X T%03u",s.handle,trial);
    // Two copies in opposite regions; no reliance on external text resources.
    const bool isBitmap=s.kind==Bitmap;
    int labelWidth=104,labelHeight=glyphY(8,isBitmap);
    label(glyphs,text,18,7,isBitmap);
    label(glyphs,text,w-labelWidth-18,h-labelHeight-5,isBitmap);
    b=Batch(variant.c_str(),true);b.add(kSetVisibility,setVisibility(s.handle,false));
    if(s.kind==Bitmap) {
        uint32_t background;
        if(!colourValue(colour,background)){error="unknown bitmap colour";return false;}
        uint32_t ink=background==0xffffffff || background==0xff00ff00?0xff000000:0xffffffff;
        uint32_t edge=border?background:ink;
        if(border){background=0xff000000;ink=0xffffffff;}
        std::vector<uint32_t> px((size_t)w*h,background);
        for(size_t i=0;i<structure.size();++i)raster(px,w,h,structure[i],edge);
        // Clear each label box so grid lines cannot obscure the identifier.
        const int xs[]={18,w-labelWidth-18},ys[]={7,h-labelHeight-5};
        for(int k=0;k<2;++k)for(int y=ys[k]-1;y<=ys[k]+labelHeight+1;++y)
            for(int x=xs[k]-1;x<=xs[k]+labelWidth+1;++x)px[(size_t)y*w+x]=background;
        for(size_t i=0;i<glyphs.size();++i)raster(px,w,h,glyphs[i],ink);
        std::vector<uint8_t> payload=bitmap(s.handle,0,px,w,h,4096);
        if(payload.empty()){error="identified bitmap exceeds 4096-byte cap; no fallback without labels";return false;}
        Call c(0xc05,payload);c.txType=0;b.calls.push_back(c);
    } else {
        b.add(kRemoveAllItems,removeAllItems(s.handle));b.add(kSetFillColor,setFillColor(s.handle,2));
        b.add(kSetLineWidth,setLineWidth(s.handle,1));
        Lines all;
        for(size_t i=0;i<structure.size();++i)outsideLabels(all,structure[i],w,h);
        all.insert(all.end(),glyphs.begin(),glyphs.end());
        unsigned item=0x80;
        for(size_t i=0;i<all.size();) {
            std::vector<Point> p;Point a={(int16_t)all[i].x,(int16_t)all[i].y};
            Point z={(int16_t)all[i].xx,(int16_t)all[i].yy};p.push_back(a);p.push_back(z);
            // The outer frame is one closed five-point polyline, not four calls.
            // Its coordinates never intersect either label box.
            if(i==0) {
                for(size_t j=1;j<4;++j){Point q={(int16_t)all[j].xx,(int16_t)all[j].yy};p.push_back(q);}
                i=4;
            } else ++i;
            b.add(kAddPolygon,addPolygon(s.handle,(uint16_t)item++,kPolyline,p));
        }
    }
    b.add(kSwitchVisibility,switchVisibility(s.partner,s.handle));
    b.add(kSetVisibility,setVisibility(s.handle,true));b.add(kSetVisibility,setVisibility(0x17,true));
    return validate(b,error);
}
}
