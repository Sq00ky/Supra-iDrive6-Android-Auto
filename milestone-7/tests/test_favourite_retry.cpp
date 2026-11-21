#include "favourite_retry.hpp"
#include <assert.h>
int main(){
 nbt::FavouriteRetry r;assert(r.ready(1000));
 assert(r.failed(1000)==1);assert(!r.ready(1099));assert(r.ready(1100));
 assert(r.failed(1100)==2);assert(r.ready(1200));
 assert(r.failed(1200)==3);assert(!r.ready(1699));assert(r.ready(1700));
 for(unsigned i=4;i<100;++i){unsigned t=1700+(i-4)*500;assert(r.ready(t));assert(r.failed(t)==i);}
 r.reset();assert(r.ready(1));assert(r.failed(0xfffffff0u)==1);
 assert(!r.ready(0x20));assert(r.ready(0x54));
 r.reset();assert(r.ready(0));
}
