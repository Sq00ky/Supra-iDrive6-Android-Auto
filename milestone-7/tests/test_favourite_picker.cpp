#include "favourite_picker.hpp"
#include <assert.h>
int main(){
 nbt::FavouritePicker p;
 assert(nbt::pickerHSV(0,255,255)==0xff0000);
 assert(nbt::pickerHSV(120,255,255)==0x00ff00);
 assert(nbt::pickerHSV(240,255,255)==0x0000ff);
 assert(nbt::pickerHSV(100,0,255)==0xffffff);
 assert(nbt::pickerHSV(100,255,0)==0);
 p.touch(230,105,1);assert(p.rgb()==0xffffff);
 p.touch(1030,465,2);assert(p.rgb()==0);
 p.touch(1030,465,3);assert(p.drag==0);
 p.touch(1100,105,1);assert(p.hue==0);
 p.touch(1100,900,2);assert(p.hue==359);
 p.touch(1100,0,2);assert(p.hue==0);
 p.touch(1100,100,3);
 p.fromRGB(0x0020fe);assert(p.value==254&&p.saturation==255&&p.hue>=230&&p.hue<=234);
 assert(p.touch(1260,330,1)==1);
 assert(p.touch(1260,420,1)==2);
 assert(p.touch(0,0,1)==0);
}
