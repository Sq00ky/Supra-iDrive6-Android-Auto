// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "pcm_utils.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
using namespace nbt;
int main() {
    PcmRing r(8);
    uint8_t a[6]={1,2,3,4,5,6}, o[16];
    assert(r.write(a,6)==0); assert(r.size()==6);
    assert(r.write(a,6)==4);                 // overflow drops oldest 4
    assert(r.size()==8);
    assert(r.read(o,8)==8);
    assert(o[0]==5&&o[1]==6&&o[2]==1&&o[7]==6);
    uint8_t big[20]; for(int i=0;i<20;i++)big[i]=i;
    r.write(big,20); assert(r.read(o,8)==8 && o[0]==12 && o[7]==19);
    // resampler: 16k mono -> 48k stereo gives 3x frames
    PcmResampler rs; rs.configure(16000,1,48000,2);
    int16_t in[160]; for(int i=0;i<160;i++) in[i]=(int16_t)(i*100);
    std::vector<int16_t> out; rs.process(in,160,out);
    printf("16k mono->48k stereo: %u in frames -> %u out frames\n",160u,(unsigned)(out.size()/2));
    assert(out.size()/2>=479 && out.size()/2<=481);
    assert(out[0]==out[1]);
    // monotonic ramp stays monotonic
    for(size_t i=2;i<out.size();i+=2) assert(out[i]>=out[i-2]);
    // passthrough
    PcmResampler p; p.configure(48000,2,48000,2); assert(p.passthrough());
    std::vector<int16_t> o2; int16_t s[4]={1,2,3,4}; p.process(s,2,o2); assert(o2.size()==4&&o2[3]==4);
    // 48k stereo -> 44.1k stereo
    PcmResampler d; d.configure(48000,2,44100,2);
    std::vector<int16_t> big2(4800*2,1000), o3; d.process(&big2[0],4800,o3);
    printf("48k->44.1k: 4800 -> %u frames\n",(unsigned)(o3.size()/2));
    assert(o3.size()/2>=4408 && o3.size()/2<=4412);
    puts("PASS"); return 0;
}
