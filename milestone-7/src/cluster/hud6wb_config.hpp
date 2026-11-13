// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUD6WB_CONFIG_HPP
#define CLUSTER_HUD6WB_CONFIG_HPP
#include "cluster/hud6wb_profile.hpp"
#include <stdlib.h>
#include <errno.h>
namespace nbt { namespace hudterm {
inline int sixwbNumber(const char* name,int fallback,int low,int high) {
    const char* value=getenv(name);if(!value || !*value)return fallback;
    char* end=NULL;errno=0;long n=strtol(value,&end,10);
    if(errno || end==value || *end || n<low || n>high)return fallback;
    return (int)n;
}
inline void configure6wb(Bmw6wbProfile& p) {
    p.base.streetChars=sixwbNumber("NBT_6WB_STREET_CHARS",p.base.streetChars,1,40);
    p.base.distanceChars=sixwbNumber("NBT_6WB_DISTANCE_CHARS",p.base.distanceChars,1,8);
    p.lineWidth=sixwbNumber("NBT_6WB_LINE_WIDTH",p.lineWidth,1,22);
    p.laneLineWidth=sixwbNumber("NBT_6WB_LANE_LINE_WIDTH",p.laneLineWidth,1,10);
    int x=sixwbNumber("NBT_6WB_ARROW_X",p.base.glyph.x0,0,p.main.width-2);
    int y=sixwbNumber("NBT_6WB_ARROW_Y",p.base.glyph.y0,0,p.main.height-2);
    int w=sixwbNumber("NBT_6WB_ARROW_WIDTH",p.base.glyph.x1-p.base.glyph.x0+1,32,p.main.width);
    int h=sixwbNumber("NBT_6WB_ARROW_HEIGHT",p.base.glyph.y1-p.base.glyph.y0+1,32,p.main.height);
    // Reject the rectangle as a unit; never silently clip configured geometry.
    if(x+w<=p.main.width && y+h<=p.main.height) {
        Rect r={(int16_t)x,(int16_t)y,(int16_t)(x+w-1),(int16_t)(y+h-1)};p.base.glyph=r;
    }
    p.roundaboutSegments=sixwbNumber("NBT_6WB_ROUNDABOUT_SEGMENTS",p.roundaboutSegments,24,72);
    p.iconAntialias=sixwbNumber("NBT_6WB_ICON_AA",0,0,1)==1;
}
} }
#endif
