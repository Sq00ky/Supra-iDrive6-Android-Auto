#include "favourite_release.hpp"
#include <assert.h>
int main(){
 nbt::FavouriteRelease r;
 assert(r.update(42,true,4,100)==4);assert(r.contacts()==1);
 assert(r.update(42,true,0,200)==4);assert(r.pending()&&!r.due(399)&&r.due(400));
 r.consume();assert(!r.pending());r.update(42,true,0,450);assert(!r.pending()); // one final attempt
 assert(r.update(42,true,4,500)==4);assert(r.contacts()==2); // fresh touch cancels old release
 r.update(42,true,0,550);assert(r.pending());r.update(42,false,0,551);assert(!r.pending());
 assert(r.update(42,true,0,552)==0); // no key retained across a car overlay
 r.update(42,true,8,600);assert(r.update(43,true,0,601)==0); // no cache from old daemon
 r.update(43,true,4,700);r.update(43,true,0,800);r.update(43,true,8,850);assert(!r.pending());
 r.update(43,true,3,900);assert(r.update(43,true,0,901)==0); // reject multi-key ambiguity
 r.update(43,true,4,0xffffff00u);r.update(43,true,0,0xfffffff0u);assert(r.due(0x100u));
}
