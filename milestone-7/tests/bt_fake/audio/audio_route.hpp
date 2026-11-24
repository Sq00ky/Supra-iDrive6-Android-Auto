// Test double: observe routing requests without executing audioconn or contacting a car.
#pragma once
#include <stdint.h>
namespace nbt {
extern int btBuilds, btRemoves, btCancels;
class AudioRoute {
public:
    typedef void (*GrantedFn)(void*);
    bool configure() { return true; }
    bool build(uint32_t, GrantedFn cb, void* user, int) { ++btBuilds; cb(user); return true; }
    bool buildFocus(uint32_t, uint32_t, GrantedFn cb, void* user, int) { ++btBuilds; cb(user); return true; }
    bool remove(uint32_t) { ++btRemoves; return true; }
    bool removeFocus(uint32_t, uint32_t) { ++btRemoves; return true; }
    bool listConnections(const char*, int) { return true; }
    void cancelPending() { ++btCancels; }
};
}
