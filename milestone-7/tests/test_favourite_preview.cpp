#include "car/favourite_preview.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
using nbt::car::FavouritePreview;
static void touch(FavouritePreview& p, int button, uint32_t now) {
    uint8_t data[8] = {0}; if (button) data[button - 1] = 1;
    assert(p.feed(0x15, 0x0c, 0xc02, 0xc, data, 8, now));
}
int main() {
    FavouritePreview p;
    // Synthetic key sequences in both views. Only AA's foreground view may peek/return.
    const int order[] = {1,2,3,4,5,6,7,8,1,8};
    for (int native = 0; native < 2; ++native) {
        for (unsigned i = 0; i < sizeof(order)/sizeof(order[0]); ++i) {
            uint32_t at = 1000 + native * 60000 + i * 5000;
            touch(p, order[i], at);
            assert(p.step(at + 250, !native, !native, true) == !native);
            touch(p, 0, at + 2000);
            assert(p.step(at + 2000, !native, false, true)==!native); // brief grace only for an already visible preview
            assert(!p.step(at + 2000+FavouritePreview::kReleaseGraceMs, !native, false, true));
            assert(!p.step(at + 2750, !native, false, true));
        }
    }
    // Wrong address, command operation, invalid values or truncated payload never open a preview.
    uint8_t data[8] = {1};
    assert(!p.feed(0x15, 2, 0xc02, 0xc, data, 8, 200000));
    assert(!p.feed(0x15, 0x0c, 0xc02, 0, data, 8, 200000));
    assert(!p.feed(0x15, 0x0c, 0xc02, 0xc, data, 7, 200000));
    data[3] = 3; assert(!p.feed(0x15, 0x0c, 0xc02, 0xc, data, 8, 200000));
    assert(!p.step(200000, true, true, true));
    // A completed tap between polls must not replay a stale label after release.
    touch(p, 2, 201000); touch(p, 0, 201100);
    assert(!p.step(201250, true, true, true));
    assert(!p.step(201850, true, false, true));
    // Explicit user exit/camera/reverse suppresses automatic return, even if the finger is still held.
    touch(p, 3, 202000); assert(p.step(202100, true, true, true));
    assert(!p.step(202200, false, false, true));
    assert(!p.step(202300, true, true, true));
    touch(p, 0, 202400);
    touch(p, 4, 203000); assert(p.step(203100, true, true, true));
    p.cancel(); assert(!p.step(203200, true, true, true));
    touch(p, 0, 203300);
    // Lost release / repeated copies cannot strand the picture in iDrive indefinitely.
    touch(p, 5, 204000); assert(p.step(204100, true, true, true));
    touch(p, 5, 213000);
    assert(!p.step(214000, true, false, true));
    assert(!p.step(214500, true, true, true));
    touch(p, 0, 215000);
    touch(p, 6, 216000); assert(p.step(216100, true, true, true));
    assert(!p.step(216200, true, false, false));
    assert(!p.step(216300, true, true, true));
    // A fresh touch during release grace extends the preview without requiring AA to flash up first.
    touch(p, 7, 217000); assert(p.step(217100, true, true, true));
    touch(p, 0, 217200); touch(p, 8, 217400);
    assert(p.step(217500, true, false, true));
    touch(p, 0, 217600); assert(!p.step(218350, true, false, true));
    // Unsigned monotonic-clock wrap.
    touch(p, 1, 0xffffff00u); assert(p.step(0xffffff10u, true, true, true));
    touch(p, 0, 0x10u); assert(p.step(0x100u, true, false, true));
    assert(!p.step(0x10u + FavouritePreview::kReleaseGraceMs, true, false, true));
    FavouritePreview repeated;uint8_t key[8]={0,0,1};
    assert(repeated.feed(0x15,0xc,0xc02,0xc,key,8,100));assert(repeated.step(100,true,true,true));
    for(unsigned i=1;i<=12;++i){
        key[2]=2;assert(repeated.feed(0x15,0xc,0xc02,0xc,key,8,i*2000));
        assert(repeated.step(i*2000,true,true,true));
        key[2]=1;assert(repeated.feed(0x15,0xc,0xc02,0xc,key,8,i*2000+50));
        assert(repeated.step(i*2000+50,true,true,true));
    }
    key[2]=0;repeated.feed(0x15,0xc,0xc02,0xc,key,8,25000);assert(repeated.step(25000,true,true,true));
    assert(!repeated.step(25000+FavouritePreview::kReleaseGraceMs,true,true,true));
    touch(repeated,3,27000);assert(repeated.step(27000,true,true,true));touch(repeated,0,27100);
    assert(!repeated.step(27101,false,false,true)); // camera/user exit cancels grace immediately
    puts("test_favourite_preview OK");
}
