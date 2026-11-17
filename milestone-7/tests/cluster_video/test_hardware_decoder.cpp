#include "hardware_peer.hpp"
#include <limits.h>
using aa_cluster::hardware::Session;
static const uint8_t access_unit[]={0,0,0,1,0x67,1,2,0,0,1,0x68,3,4,0,0,0,1,0x65,5,6};
int main() {
    typedef HardwarePeer::P P;
    std::vector<uint8_t> output(P::ContentWidth*P::ContentHeight*4);
    std::vector<uint8_t> reference;
    {
        HardwarePeer peer; Session<HardwarePeer> decoder(peer);
        assert(decoder.open());
        assert(peer.context_name=="kombi_hw_4242");
        assert(decoder.decode(access_unit,sizeof(access_unit),999999,&output[0],true)==0);
        const unsigned expected[]={5,2,4,3};
        assert(peer.packets==std::vector<unsigned>(expected,expected+4));
        // No next packet is needed to obtain asynchronous output.
        peer.clock+=100000;assert(decoder.poll(&output[0])==1);
        assert(output[3]==255 && output[0]==0);
        assert(output[P::PaddingX*4+2]==1 && output[(P::ContentWidth-1)*4]==0);
        reference=output;
        peer.clock+=60000000;assert(decoder.poll(&output[0])==0); // quiet map is valid
        assert(decoder.unchanged()==1);
        assert(decoder.decode(access_unit,sizeof(access_unit),123,&output[0],false)==0);
        peer.clock+=100000;assert(decoder.poll(&output[0])==1);
        assert(decoder.coalesced()==1);
        peer.send_error=-110;
        assert(decoder.decode(access_unit,sizeof(access_unit),123,&output[0],true)==-1);
        decoder.close();decoder.close();assert(peer.objects.empty());
    }
    // Exact car allocation/crop and a second valid offset must produce the
    // same entire fitted image as an unpadded 800x480 source, with no borders.
    for(unsigned mode=0;mode<3;++mode) {
        HardwarePeer peer;Session<HardwarePeer> decoder(peer);
        peer.allocation[0]=896;peer.allocation[1]=576;
        peer.origin[0]=mode==1?96:32;peer.origin[1]=mode==1?96:24;
        peer.source_format=mode==2?8:12;
        assert(decoder.open());
        assert(decoder.decode(access_unit,sizeof(access_unit),0,&output[0],true)==0);
        // The renderer initially publishes its full allocation before setting
        // the crop. Grace must wait, without copying the uninitialized border.
        peer.source[0]=896;peer.source[1]=576;
        peer.clock+=100000;assert(decoder.poll(&output[0])==0 && peer.reads==0);
        peer.source[0]=800;peer.source[1]=480;
        peer.clock+=100000;assert(decoder.poll(&output[0])==1);
        assert(output==reference && peer.reads==1);
        // Also check every byte of the copied picture before fitting, including
        // the final row/column and stride padding; there must be no second resize.
        for(unsigned y=0;y<P::ContentHeight;++y) {
            for(unsigned x=0;x<P::FittedWidth;++x) {
                const uint8_t* pixel=&peer.pixels[y*peer.capture_stride+x*4];
                const unsigned sx=x*(800-1)/(522-1),sy=y*(480-1)/(313-1);
                assert(pixel[0]==sx%256 && pixel[1]==sy%256 && pixel[2]==1 && pixel[3]==255);
                const uint8_t* packed=&output[(y*P::ContentWidth+P::PaddingX+x)*4];
                assert(!memcmp(packed,pixel,4));
            }
            for(unsigned x=P::FittedWidth*4;x<unsigned(peer.capture_stride);++x)
                assert(peer.pixels[y*peer.capture_stride+x]==0xe7);
        }
        decoder.close();assert(peer.objects.empty());
    }
    // Invalid viewports must fail before any readback, even at overflow edges.
    for(unsigned mode=0;mode<10;++mode) {
        HardwarePeer peer;Session<HardwarePeer> decoder(peer);assert(decoder.open());
        assert(decoder.decode(access_unit,sizeof(access_unit),0,&output[0],true)==0);
        switch(mode) {
            case 0:peer.origin[0]=-1;break;
            case 1:peer.origin[1]=-1;break;
            case 2:peer.origin[0]=1;break;
            case 3:peer.origin[1]=1;break;
            case 4:peer.source[0]=799;break;
            case 5:peer.source[1]=479;break;
            case 6:peer.allocation[0]=0;break;
            case 7:peer.origin[0]=INT_MAX;peer.allocation[0]=INT_MAX;break;
            case 8:peer.window_size[0]=896;break;
            case 9:peer.source_format=0;break;
        }
        peer.clock+=6000000;assert(decoder.poll(&output[0])==-1 && peer.reads==0);
        decoder.close();assert(peer.objects.empty());
    }
    // Every partial-start failure releases exactly the objects acquired so far.
    for(unsigned failure=1;failure<85;++failure) {
        HardwarePeer peer;peer.fail_at=failure;Session<HardwarePeer> decoder(peer);
        decoder.open();decoder.close();assert(peer.objects.empty());
    }
    for(unsigned mode=0;mode<3;++mode) {
        HardwarePeer peer;Session<HardwarePeer> decoder(peer);assert(decoder.open());
        assert(decoder.decode(access_unit,sizeof(access_unit),0,&output[0],true)==0);
        peer.no_pixels=mode==0;peer.read_error=mode==1;
        if(mode==2) {peer.output_event=false;peer.clock+=6000000;}
        else {
            peer.clock+=100000;
            assert(decoder.poll(&output[0])==0); // first-frame grace
            peer.clock+=6000000;
        }
        assert(decoder.poll(&output[0])==-1);
        decoder.close();assert(peer.objects.empty());
    }
    uint8_t config[64]={};
    assert(aa_cluster::hardware_configuration(access_unit,sizeof(access_unit),config,sizeof(config))==13);
    assert(aa_cluster::hardware_configuration(access_unit,sizeof(access_unit),config,8)==0);
    assert(aa_cluster::hardware_configuration(access_unit,2,config,sizeof(config))==0);
    puts("PASS hardware renderer lifecycle, scaled readback, padded source viewports, asynchronous output, pixels and failures");
}
