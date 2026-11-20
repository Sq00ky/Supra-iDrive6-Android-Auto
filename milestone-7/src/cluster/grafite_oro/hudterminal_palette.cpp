#include "hudterminal_palette.hpp"
namespace nbt { namespace sixwbkit {
HudPaint::Value hudColorIndex(const HudTerminalPalette& p,Color::Value c,uint8_t& out) {
    int index=-1;switch(c) {
    case Color::None:return HudPaint::Skip;case Color::Graphite:index=p.graphite;break;
    case Color::Gold:index=p.gold;break;case Color::White:index=p.white;break;
    case Color::Gray:index=p.gray;break;case Color::Black:index=p.black;break;case Color::Red:index=p.red;break;case Color::ExitWhite:index=p.exitWhite;break;
    }
    if(index<0||index>=HudColorCount)return HudPaint::Unmapped;
    out=(uint8_t)index;return HudPaint::Mapped;
}
std::vector<HudPaletteSwatch> hudPaletteSwatches(Rect a,unsigned columns) {
    std::vector<HudPaletteSwatch> out;
    if(!columns||columns>HudColorCount||!finite(a.x)||!finite(a.y)||!finite(a.width)||!finite(a.height)||
       !finite(a.x+a.width)||!finite(a.y+a.height))return out;
    unsigned rows=(HudColorCount+columns-1)/columns;double cw=a.width/columns,ch=a.height/rows;
    if(cw<10||ch<10)return out;
    for(unsigned i=0;i<HudColorCount;++i) {
        double x=a.x+(i%columns)*cw,y=a.y+(i/columns)*ch;
        HudPaletteSwatch s;s.index=(uint8_t)i;s.fill.closed=true;
        Point p[]={{x+.2*cw,y+.15*ch},{x+.8*cw,y+.15*ch},{x+.8*cw,y+.55*ch},{x+.2*cw,y+.55*ch}};
        s.fill.points.assign(p,p+4);Point l[]={{x+.2*cw,y+.78*ch},{x+.8*cw,y+.78*ch}};
        s.line.points.assign(l,l+2);out.push_back(s);
    }return out;
}
} }
