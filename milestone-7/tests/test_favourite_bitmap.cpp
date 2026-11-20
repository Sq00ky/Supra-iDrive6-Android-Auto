#include "favourite_bitmap.hpp"
#include <assert.h>
#include <vector>
static void put(std::vector<unsigned char>& b,int p,unsigned n){for(int i=0;i<4;i++)b[p+i]=(n>>(i*8))&255;}
static void check(int bits,bool down) {
 int w=5,h=4,bar=2,row=(w*(bits/8)+3)&~3;
 std::vector<unsigned char> b(54+row*h,0);b[0]='B';b[1]='M';put(b,10,54);put(b,14,40);put(b,18,w);put(b,22,down?-h:h);b[26]=1;b[28]=bits;
 for(int y=0;y<h;y++)for(int x=0;x<w;x++) {
  int p=54+(down?y:h-1-y)*row+x*(bits/8);b[p]=30+y;b[p+1]=20+x;b[p+2]=10;
 }
 FILE* f=tmpfile();assert(f);fwrite(&b[0],1,b.size(),f);rewind(f);
 uint32_t pixels[28];for(int i=0;i<28;i++)pixels[i]=0x12345678;
 assert(nbt::favouriteBitmap(f,pixels,w,h,bar,7));
 rewind(f);uint32_t small[18];for(int i=0;i<18;++i)small[i]=0x12345678;
 assert(nbt::favouriteBitmap(f,small,w,h,bar,7,bar));
 for(int y=0;y<bar;++y)for(int x=0;x<w;++x)assert(small[y*7+x]==pixels[y*7+x]);
 for(int i=14;i<18;++i)assert(small[i]==0x12345678); // no write beyond strip allocation
 rewind(f);assert(!nbt::favouriteBitmap(f,small,w,h,bar,7,bar-1));fclose(f);
 for(int y=0;y<h;y++)for(int x=0;x<7;x++) {
  uint32_t expected=x>=w?0x12345678:y>=bar?0:0xff000000u|((30+y)<<16)|((20+x)<<8)|10;
  assert(pixels[y*7+x]==expected);
 }
 b[30]=1;f=tmpfile();fwrite(&b[0],1,b.size(),f);rewind(f);assert(!nbt::favouriteBitmap(f,pixels,w,h,bar,7));fclose(f);
}
int main(){check(24,false);check(24,true);check(32,false);check(32,true);puts("test_favourite_bitmap OK");}
