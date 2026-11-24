#pragma once
namespace nbt {
extern int btPlays, btStops;
class BtPlay {
public:
    void init() {}
    void play(const char*) { ++btPlays; }
    void stop(const char*) { ++btStops; }
};
}
