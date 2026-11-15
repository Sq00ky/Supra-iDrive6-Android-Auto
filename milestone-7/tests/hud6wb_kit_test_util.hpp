#ifndef NBT_HUD6WB_KIT_TEST_UTIL_HPP
#define NBT_HUD6WB_KIT_TEST_UTIL_HPP
#include "cluster/hud6wb_kit.hpp"
inline nbt::hudterm::GrafiteKit testKit() {
    nbt::sixwbkit::HudTerminalPalette p;p.graphite=0;p.gold=1;p.white=2;p.gray=3;p.black=4;
    return nbt::hudterm::GrafiteKit(p,p);
}
#endif
