#include "favourite_colour.hpp"
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
int main(){
 unsigned char blue[12]={0,232,8,2,255,255,254,1,0,255,255,255};
 nbt::AmbientColour a;assert(nbt::parseAmbientColour(0xc0,0xa,0xcd0,0xc,blue,12,a));
 assert(a.valid&&a.hue==232&&a.scheme==8);
 uint32_t rgb=nbt::favouriteColourRGB(nbt::kFavouriteAmbient,a);assert(rgb==0x0020fe);
 a.hue=120;a.scheme=9;assert(nbt::favouriteColourRGB(nbt::kFavouriteAmbient,a)==rgb); // White + Blue follows Blue.
 a.scheme=6;a.hue=120;assert(nbt::favouriteColourRGB(nbt::kFavouriteAmbient,a)==0x931a01);
 a.scheme=7;a.hue=120;assert(nbt::favouriteColourRGB(nbt::kFavouriteAmbient,a)==0xffffff);
 assert(!nbt::parseAmbientColour(0xc0,0xa,0xcd0,0,blue,12,a)); // Never consume a lighting setter as confirmation.
 assert(!nbt::parseAmbientColour(0xc0,3,0xcd0,0xc,blue,12,a));
 assert(!nbt::parseAmbientColour(0xc0,0xa,0xcd0,0xc,blue,11,a));
 blue[2]=16;assert(!nbt::parseAmbientColour(0xc0,0xa,0xcd0,0xc,blue,12,a));
 assert(nbt::favouriteColourRGB(nbt::kFavouriteBMW,a)==0);
 assert(nbt::favouriteColourRGB(nbt::kFavouriteWhite,a)==0xffffff);
 uint32_t source[]={0xff0030ffu,0xffffffffu,0xff999999u,0},out[4];
 nbt::renderFavouriteColour(out,source,4,0);
 assert(out[0]==0xffff3000u&&out[1]==source[1]&&out[2]==source[2]&&out[3]==0); // Fix red/blue swap.
 nbt::renderFavouriteColour(out,source,4,0x0000ff);
 assert(out[0]==0xff0000ffu&&out[1]==source[1]&&out[2]==source[2]);
 nbt::renderFavouriteColour(out,source,4,0xffffff);
 assert(out[0]==0xffffffffu&&out[1]==0xffffffffu);
 uint32_t edge[]={0xff00e8ffu,0xff020618u,0xffeeeeeeu},edgeOut[3];
 nbt::renderFavouriteColour(edgeOut,edge,3,0x0020fe);
 assert((edgeOut[0]&0xff)>200 && ((edgeOut[0]>>16)&255)==0);
 assert(edgeOut[1]!=0xff180602u && edgeOut[2]==edge[2]);
 const unsigned schemes[]={4,6,9,11,13};
 const uint32_t expected[]={0xd76e14,0x931a01,0x0020fe,0x10fe32,0x783cfe};
 for(unsigned i=0;i<5;++i){a.scheme=schemes[i];a.hue=120;assert(nbt::favouriteColourRGB(nbt::kFavouriteAmbient,a)==expected[i]);}
 char path[]="/tmp/favourite-colour-XXXXXX";int fd=mkstemp(path);assert(fd>=0);close(fd);
 for(int mode=0;mode<nbt::kFavouriteColourCount;++mode){FILE* f=fopen(path,"w");assert(f);fprintf(f,"%d\n",mode);fclose(f);assert(nbt::loadFavouriteColour(path)==mode);}
 const char* bad[]={"-1","9","garbage","5 trailing",""};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i){FILE* f=fopen(path,"w");fputs(bad[i],f);fclose(f);assert(nbt::loadFavouriteColour(path)==nbt::kFavouriteBMW);}
 unlink(path);assert(nbt::loadFavouriteColour(path)==nbt::kFavouriteBMW);
}
