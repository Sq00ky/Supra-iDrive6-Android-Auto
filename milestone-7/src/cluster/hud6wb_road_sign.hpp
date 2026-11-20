#ifndef NBT_HUD6WB_ROAD_SIGN_HPP
#define NBT_HUD6WB_ROAD_SIGN_HPP

#include <stdint.h>
#include <string>
#include <vector>

namespace nbt { namespace hudterm {
namespace road_sign {

struct Layout {
    unsigned x, y, width, height, spaces;
    Layout() : x(0), y(9), width(62), height(26), spaces(15) {}
};

inline bool digit(char c) { return c>='0' && c<='9'; }
inline char upper(char c) { return c>='a' && c<='z' ? c-'a'+'A' : c; }
inline bool blank(char c) { return c==' ' || c=='\t'; }

inline std::string route(const std::string& road) {
    size_t i=road.find_first_not_of(" \t");
    if(i==std::string::npos)return std::string();
    std::string code(1,upper(road[i++]));
    if(code[0]=='S' && i<road.size())code+=upper(road[i++]);
    if(code!="A" && code!="SS" && code!="SP")return std::string();
    while(i<road.size() && blank(road[i]))++i;
    const size_t start=i;
    while(i<road.size() && digit(road[i]))++i;
    if(i==start || i-start>4)return std::string();
    code+=road.substr(start,i-start);
    if(i==road.size() || blank(road[i]))return code;
    // A suffix such as SS1bis is a different route. Do not misidentify it.
    if(std::string("/-,;(").find(road[i])!=std::string::npos ||
       road.compare(i,3,"–")==0 || road.compare(i,3,"—")==0 || road.compare(i,2,"·")==0)return code;
    return std::string();
}

inline uint32_t background(const std::string& code) {
    return code[0]=='A'?0xff006b3c:code[1]=='S'?0xff1565a8:0xffffd534;
}
inline uint32_t ink(const std::string& code) {
    return code[0]=='S' && code[1]=='P'?0xff000000:0xffffffff;
}

inline const uint8_t* glyph(char c) {
    static const uint8_t rows[][5]={
        {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
        {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},
        {2,5,7,5,5},{7,4,7,1,7},{6,5,6,4,4}
    };
    return rows[digit(c)?c-'0':c=='A'?10:c=='S'?11:12];
}

inline bool draw(const std::string& code,unsigned w,unsigned h,const Layout& p,
                 std::vector<uint32_t>& pixels) {
    pixels.clear();
    if(code.empty() || route(code)!=code || !w || !h || w>1024 || h>1024 ||
       p.x>=w || p.y>=h || p.width>w-p.x || p.height>h-p.y ||
       p.width<5 || p.height<7)return false;
    const unsigned units=4*(unsigned)code.size()-1;
    const unsigned sx=(p.width-4)/units,sy=(p.height-4)/5;
    const unsigned scale=sx<sy?sx:sy;
    if(!scale)return false;
    pixels.assign((size_t)w*h,0);
    const uint32_t bg=background(code), fg=ink(code);
    for(unsigned y=0;y<p.height;++y)
        for(unsigned x=0;x<p.width;++x) pixels[(p.y+y)*w+p.x+x]=bg;
    const unsigned tx=p.x+(p.width-units*scale)/2,ty=p.y+(p.height-5*scale)/2;
    for(size_t i=0;i<code.size();++i) {
        const uint8_t* rows=glyph(code[i]);
        for(unsigned yy=0;yy<5*scale;++yy)
            for(unsigned xx=0;xx<3*scale;++xx)
                if(rows[yy/scale]&(4>>(xx/scale)))pixels[(ty+yy)*w+tx+i*4*scale+xx]=fg;
    }
    return true;
}

inline std::string street(const std::string& road,bool visible,const Layout& p,size_t cap) {
    if(!visible || cap<2)return road;
    const size_t spaces=p.spaces<cap?p.spaces:cap-1;
    return std::string(spaces,' ')+road;
}

} } }
#endif
