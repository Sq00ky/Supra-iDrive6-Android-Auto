#include "favourite_ready.hpp"
#include <vector>
#include <assert.h>
int main() {
    const int w=1440,bar=64,tile=108;
    std::vector<uint32_t> p(w*bar,0xff999999u);
    assert(!nbt::favouriteLabelsReady(&p[0],w,bar,w));
    assert(nbt::favouriteSelectedButton(&p[0],w,bar,w)==0);
    for(int button=1;button<=8;++button) {
        std::fill(p.begin(),p.end(),0xff000000u);
        for(int y=0;y<56;++y)
            for(int x=(button-1)*tile+12;x<(button-1)*tile+300 && x<w;++x)
                p[y*w+x]=0xff001080u;
        assert(nbt::favouriteLabelsReady(&p[0],w,bar,w));
        assert(nbt::favouriteSelectedButton(&p[0],w,bar,w)==button);
        assert(nbt::favouriteButtonFromMask(1u<<(button-1))==button);
    }
    assert(nbt::favouriteButtonFromMask(0)==0);
    assert(nbt::favouriteButtonFromMask(3)==0);
    assert(nbt::favouriteButtonFromMask(256)==0);
    assert(!nbt::favouriteLabelsReady(0,w,bar,w));
    assert(!nbt::favouriteLabelsReady(&p[0],w,bar,100));
}
