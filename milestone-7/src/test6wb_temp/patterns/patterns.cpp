// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "patterns/patterns.hpp"
#include "cluster/hud6wb_codec.hpp"
namespace test6wb {
using namespace nbt::hudterm;
bool validate(const Batch& b,std::string& error) {
    if(b.calls.empty() || b.calls.size()>128){error="invalid batch size";return false;}
    for(size_t i=0;i<b.calls.size();++i) {
        const Call& c=b.calls[i];
        if(!compatible(c)){error="method/handle or payload rejected";return false;}
        if(c.fkt==kAddPolygon) {
            const Surface* s=surface(c.handle());
            for(size_t j=6;j<c.payload.size();j+=4) {
                int x=(int16_t)nbt::hudterm::rdBe16(&c.payload[j]);
                int y=(int16_t)nbt::hudterm::rdBe16(&c.payload[j+2]);
                if(x<0 || y<0 || x>=(int)s->width || y>=(int)s->height) {
                    error="vector coordinate outside nominal surface";return false;
                }
            }
        }
    }
    return true;
}
Batch clearBatch() {
    Batch b("clear",true);b.add(kSetVisibility,setVisibility(0x17,false));
    for(size_t i=0;i<surfaceCount;++i) {
        const Surface& s=surfaces[i];
        if(s.kind==Vector)b.add(kRemoveAllItems,removeAllItems(s.handle));
        if(s.kind!=Container)b.add(kSetVisibility,setVisibility(s.handle,false));
        if(s.kind==Text)b.add(kTextareaSetText,latinText(s.handle,"",255));
    }
    return b;
}
bool textPattern(unsigned h,const std::string& text,Batch& b,std::string& error) {
    const Surface* s=surface(h);
    if(!s || s->kind!=Text){error="text requires 0x1A, 0x1B or 0x1C";return false;}
    // Existing configurable source bounds; do not silently truncate diagnostic input.
    size_t cap=h==0x1a?40:h==0x1b?8:3;
    std::string encoded=toLatin15(text,256);
    if(encoded.size()>cap){error="encoded text exceeds source profile bound (street 40, distance 8, unit 3)";return false;}
    b=Batch("text",true);b.add(kTextareaSetText,latinText(h,text,cap));
    b.add(kSetVisibility,setVisibility(h,true));b.add(kSetVisibility,setVisibility(0x17,true));
    return validate(b,error);
}
bool barPattern(unsigned value,Batch& b,std::string& error) {
    if(value>255){error="bar value must be 0..255";return false;}
    b=Batch("bar",true);b.add(0x227,distanceBar(0x35,(int)value));
    b.add(kSetVisibility,setVisibility(0x35,true));b.add(kSetVisibility,setVisibility(0x17,true));
    return validate(b,error);
}
Batch frameBatch(bool on) {
    Batch b(on?"frame-on":"frame-off");b.add(kSetVisibility,setVisibility(0x17,on));return b;
}
bool vectorFill(const Surface& s,Batch& b,std::string& error) {
    if(s.kind!=Vector){error="solid vector fill requires a vector surface";return false;}
    b=Batch("fill",true);b.add(kSetVisibility,setVisibility(s.handle,false));
    b.add(kRemoveAllItems,removeAllItems(s.handle));b.add(kSetFillColor,setFillColor(s.handle,2));
    b.add(kSetLineWidth,setLineWidth(s.handle,1));
    std::vector<Point> p;Point a={0,0},z={(int16_t)(s.width-1),0};
    Point v={(int16_t)(s.width-1),(int16_t)(s.height-1)},q={0,(int16_t)(s.height-1)};
    p.push_back(a);p.push_back(z);p.push_back(v);p.push_back(q);p.push_back(a);
    b.add(kAddPolygon,addPolygon(s.handle,0x80,kPolygon,p));
    b.add(kSwitchVisibility,switchVisibility(s.partner,s.handle));
    b.add(kSetVisibility,setVisibility(s.handle,true));b.add(kSetVisibility,setVisibility(0x17,true));
    return validate(b,error);
}
}
