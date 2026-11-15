#pragma once
#include "wels/codec_api.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
namespace aa_cluster {
// One decoder instance exclusively for display_id=2; the dashboard decoder is
// never touched. Only complete Annex-B access units enter this interface.
class Decoder {
    ISVCDecoder* decoder_;
    uint8_t* previous_;
    bool have_previous_;
    unsigned unchanged_,coalesced_;
    static uint8_t clamp(int v) { return v<0?0:v>255?255:uint8_t(v); }
public:
    Decoder():decoder_(0),previous_(0),have_previous_(false),unchanged_(0),coalesced_(0) {}
    ~Decoder() { close(); }
    void close() { if(decoder_) { decoder_->Uninitialize();WelsDestroyDecoder(decoder_);decoder_=0; } free(previous_);previous_=0;have_previous_=false; }
    unsigned unchanged() const {return unchanged_;}
    unsigned coalesced() const {return coalesced_;}
    bool open() {
        close();if(WelsCreateDecoder(&decoder_)||!decoder_)return false;
        int threads=0;
        if(decoder_->SetOption(DECODER_OPTION_NUM_OF_THREADS,&threads)) { close();return false; }
        SDecodingParam param={};param.eEcActiveIdc=ERROR_CON_DISABLE;
        param.sVideoProperty.eVideoBsType=VIDEO_BITSTREAM_AVC;
        if(decoder_->Initialize(&param)) { close();return false; }
        previous_=(uint8_t*)malloc(800*480*3/2);
        if(!previous_) {close();return false;}
        return true;
    }
    // Returns 1 for a changed picture, 0 for config/no change, -1 on error.
    int decode(const uint8_t* bytes,unsigned n,uint64_t pts,uint8_t* bgra,bool present=true) {
        if(!decoder_||!n||n>512*1024)return -1;
        uint8_t* plane[3]={};SBufferInfo info={};info.uiInBsTimeStamp=pts;
        DECODING_STATE status=decoder_->DecodeFrameNoDelay(bytes,int(n),plane,&info);
        if(status!=dsErrorFree)return -1;
        if(!info.iBufferStatus)return 0;
        const SSysMEMBuffer& b=info.UsrData.sSystemBuffer;
        // The negotiated stream is 800x480. Refuse other dimensions before
        // indexing its planes; renegotiation requires a new tested profile.
        if(b.iWidth!=800||b.iHeight!=480||b.iStride[0]<800||b.iStride[1]<400||!plane[0]||!plane[1]||!plane[2])return -1;
        // Preserve every predictive reference, but let a slow display catch up.
        // The comparison cache remains the last PRESENTED image, not this one.
        if(!present) {++coalesced_;return 0;}
        // AA can encode identical pictures at 30 Hz. Decode every access unit
        // to preserve references, but avoid expensive Screen RPCs and colour
        // conversion when all three visible YUV planes are byte-identical.
        bool same=have_previous_;unsigned offset=0;
        for(unsigned c=0;c<3;++c) {
            unsigned w=c?400:800,h=c?240:480,stride=b.iStride[c?1:0];
            for(unsigned y=0;y<h;++y) {
                if(same && memcmp(previous_+offset,plane[c]+y*stride,w))same=false;
                offset+=w;
            }
        }
        if(same) {++unchanged_;return 0;}
        offset=0;
        for(unsigned c=0;c<3;++c) {
            unsigned w=c?400:800,h=c?240:480,stride=b.iStride[c?1:0];
            for(unsigned y=0;y<h;++y) {memcpy(previous_+offset,plane[c]+y*stride,w);offset+=w;}
        }
        have_previous_=true;
        // Fit 800x480 into the car's 620x313 content rectangle, preserving
        // aspect ratio (522x313, 49px black each side). Never overwrite row 0.
        for(unsigned y=0;y<313;++y) for(unsigned x=0;x<620;++x) {
            uint8_t* out=bgra+(y*620+x)*4;out[0]=out[1]=out[2]=0;out[3]=255;
            if(x<49||x>=571)continue;
            // Fixed-point bilinear sampling before YUV->RGB, including chroma.
            unsigned sx=(x-49)*799u*256/521,sy=y*479u*256/312;
            int yy=sample(plane[0],b.iStride[0],800,480,sx,sy)-16;
            int u=sample(plane[1],b.iStride[1],400,240,sx/2,sy/2)-128;
            int v=sample(plane[2],b.iStride[1],400,240,sx/2,sy/2)-128;
            // Limited-range BT.601, as negotiated SD H.264. BGRA memory is the
            // car-tested byte order despite Screen calling the format RGBA8888.
            out[0]=clamp((298*yy+516*u+128)>>8);
            out[1]=clamp((298*yy-100*u-208*v+128)>>8);
            out[2]=clamp((298*yy+409*v+128)>>8);
        }
        return 1;
    }
    static unsigned sample(const uint8_t* p,int stride,unsigned w,unsigned h,unsigned x,unsigned y) {
        unsigned ix=x>>8,iy=y>>8,fx=x&255,fy=y&255;
        unsigned nx=ix+1<w?ix+1:ix,ny=iy+1<h?iy+1:iy;
        unsigned a=p[iy*stride+ix]*(256-fx)+p[iy*stride+nx]*fx;
        unsigned b=p[ny*stride+ix]*(256-fx)+p[ny*stride+nx]*fx;
        return (a*(256-fy)+b*fy+32768)>>16;
    }
};
}
