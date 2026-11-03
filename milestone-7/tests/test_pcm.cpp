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
    // device rate choice: the car's browser device reports 22050..44100
    assert(pickDeviceRate(48000, 0, 22050, 44100) == 44100);    // default: lowered to the reported maximum
    assert(pickDeviceRate(32000, 0, 22050, 44100) == 32000);    // inside the range: kept
    assert(pickDeviceRate(16000, 0, 22050, 44100) == 44100);    // below the range: the maximum
    assert(pickDeviceRate(48000, 0, 0, 0) == 48000);            // range unknown: as configured
    assert(pickDeviceRate(48000, 48000, 22050, 44100) == 48000); // NBT_BROWSER_FORCE_RATE: asked for as is
    assert(pickDeviceRate(44100, 48000, 0, 0) == 48000);
    // with a forced 48 kHz device, guidance (48 kHz mono) is only copied to both channels: every sample exact, one
    // frame late (the interpolator holds the previous frame; 21 us), across process() calls
    PcmResampler g; g.configure(48000,1,48000,2); assert(!g.passthrough());
    int16_t gm[64]; for(int i=0;i<64;i++) gm[i]=(int16_t)(i*311-9000);
    std::vector<int16_t> go; g.process(gm,32,go); g.process(gm+32,32,go);
    assert(go.size()==128);
    assert(go[0]==gm[0] && go[1]==gm[0]);
    for(int i=1;i<64;i++) assert(go[2*i]==gm[i-1] && go[2*i+1]==gm[i-1]);
    // drift trim: +500 ppm consumes 0.05 % more input per output frame (plays faster), -500 ppm less
    {
        std::vector<int16_t> src(48000*2, 1000);
        PcmResampler t0; t0.configure(48000,2,44100,2);
        std::vector<int16_t> a; t0.process(&src[0],48000,a);
        PcmResampler tp; tp.configure(48000,2,44100,2); tp.setTrimPpm(500); assert(tp.trimPpm()==500);
        std::vector<int16_t> b2; tp.process(&src[0],48000,b2);
        PcmResampler tn; tn.configure(48000,2,44100,2); tn.setTrimPpm(-500);
        std::vector<int16_t> c2; tn.process(&src[0],48000,c2);
        long na=(long)a.size()/2, nb=(long)b2.size()/2, nc=(long)c2.size()/2;
        printf("48k->44.1k 1 s: %ld frames, +500 ppm %ld, -500 ppm %ld\n", na, nb, nc);
        assert(na>=44099 && na<=44101);
        assert(nb>=na-24 && nb<=na-20);              // 44100 / 1.0005 = 44078
        assert(nc>=na+20 && nc<=na+24);
        // the trim survives configure() / reset()
        tp.configure(48000,2,44100,2); tp.reset(); assert(tp.trimPpm()==500);
        // same rate and channels: a trim leaves passthrough, removing it goes back
        PcmResampler q; q.configure(48000,2,48000,2); assert(q.passthrough());
        q.setTrimPpm(300); assert(!q.passthrough());
        std::vector<int16_t> d2; q.process(&src[0],48000,d2);
        long nd=(long)d2.size()/2;
        assert(nd>=48000-16 && nd<=48000-13);        // 48000 / 1.0003 = 47985.6
        q.setTrimPpm(0); assert(q.passthrough());
        q.setTrimPpm(50000); assert(q.trimPpm()==20000);   // clamped
        q.setTrimPpm(-50000); assert(q.trimPpm()==-20000);
        // a constant signal stays exactly constant through a trimmed resampler (no interpolation error)
        for(size_t i=0;i<b2.size();i++) assert(b2[i]==1000);
    }
    // read chunk for the mixer's fixed 1920-byte buffer (10 ms of 48 kHz stereo)
    const size_t cap = 48000/100*2*2;
    assert(pcmChunkBytes(48000,2,10,cap) == 1920);               // media: exactly the buffer
    assert(pcmChunkBytes(48000,1,10,cap) == 960);                // guidance at 48 kHz
    assert(pcmChunkBytes(16000,1,10,cap) == 320);                // system / guidance at 16 kHz
    assert(pcmChunkBytes(44100,2,10,cap) == 441*4);              // whole frames
    assert(pcmChunkBytes(96000,2,10,cap) == 1920);               // too high a rate: capped, no overflow
    assert(pcmChunkBytes(192000,2,10,cap) == 1920);
    assert(pcmChunkBytes(0,2,10,cap) == 4);                      // rate 0: one frame, never 0 (no stall)
    assert(pcmChunkBytes(50,1,10,cap) == 2);
    assert(pcmChunkBytes(48000,2,10,1922) == 1920);              // cap rounded down to whole frames
    assert(pcmChunkBytes(48000,2,10,3) == 0);                    // cap below one frame
    assert(pcmChunkBytes(48000,0,10,cap) == 960);                // channels 0 counts as mono
    puts("PASS"); return 0;
}
