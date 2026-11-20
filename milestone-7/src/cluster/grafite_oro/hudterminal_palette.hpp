#ifndef NBT_GRAFITE_PALETTE_HPP
#define NBT_GRAFITE_PALETTE_HPP
#include "artwork6wb.hpp"
#include <stdint.h>
namespace nbt { namespace sixwbkit {
struct HudTerminalPalette {int graphite,gold,white,gray,black,red,exitWhite;
    HudTerminalPalette():graphite(-1),gold(-1),white(-1),gray(-1),black(-1),red(-1),exitWhite(4){} };
struct HudPaint {enum Value {Skip,Mapped,Unmapped};};
enum {HudColorCount=20};
HudPaint::Value hudColorIndex(const HudTerminalPalette& p,Color::Value c,uint8_t& out);
struct HudPaletteSwatch {uint8_t index;Contour fill,line;};
std::vector<HudPaletteSwatch> hudPaletteSwatches(Rect area,unsigned columns=5);
} }
#endif
